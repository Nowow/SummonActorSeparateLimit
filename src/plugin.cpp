

#include "include\PerkEntryPointExtenderAPI.h"
#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder) SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    //RE::ConsoleLog::GetSingleton()->Print(logFilePath.string().c_str());
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
}


struct Hooks {
    struct CommandedActorLimitHook {
        static void thunk(RE::PerkEntryPoint entry_point, RE::Actor* target, RE::MagicItem* a_spell, void* out) {
            //logger::info("We in CommandedActorLimitHook func body");
            float* floatPtr = static_cast<float*>(out);
            *floatPtr = 999.0f;  // If you need more than 999 summons, I think you've got a problem

        }
        static inline REL::Relocation<decltype(thunk)> func;
    };

    struct CommandedActorHook {
        static void thunk(RE::AIProcess* test, RE::ActiveEffectReferenceEffectController* target2, void* target3) {
            func(test, target2, target3);
            auto a_AE = target2->effect;

            logger::info("Commanded Actor effect was cast!");

            bool casterIsPlayer = test->GetUserData()->IsPlayerRef();
            logger::info("Caster was player: {}", casterIsPlayer ? "true" : "false");
            
            if (casterIsPlayer && a_AE->effect->baseEffect->HasKeywordString("MagicSummon_ED_Uncapped")) {
                logger::info("Summon spell was cast by player and it had effect with MagicSummon_ED_Uncapped keyword, so doing nothing further");
                return;
            }

            std::vector<int> a_effectsToDeleteIndex;
            std::vector<int> a_activeSummonEffectsIndex;
            std::vector<int> a_activeSummonEffectsIndexSorted;
            std::vector<float> a_activeSummonEffectsDuration;

            //logger::info("We in CommandedActorHook func body");

            float perkfactor = 0.0f;
            
            int j = 0;

            if (a_AE && test->middleHigh->perkData) {  

                auto akCastedMagic = a_AE->spell;
                auto commandedActorsEffectsArray = test->middleHigh->commandedActors;
                auto summoner = test->GetUserData();
                
                // getting current command limit with respect to the relevant entry point
                perkfactor = 1.0f;
                RE::BGSEntryPoint::HandleEntryPoint(RE::PerkEntryPoint::kModCommandedActorLimit, summoner,
                                                    akCastedMagic, &perkfactor);

                logger::info("Current command limit: {}", perkfactor);

                // sorting active summon effects from newest to oldest
                for (auto& elements : commandedActorsEffectsArray) {
                    if (commandedActorsEffectsArray[j].activeEffect) {
                        a_activeSummonEffectsIndex.push_back(j);
                        a_activeSummonEffectsDuration.push_back(
                            commandedActorsEffectsArray[j].activeEffect->elapsedSeconds);
                    }
                    j += 1;
                }
                for (std::uint32_t widx = 0; widx < a_activeSummonEffectsIndex.size(); ++widx) {
                    auto maxtime =
                        std::max_element(a_activeSummonEffectsDuration.begin(), a_activeSummonEffectsDuration.end());
                    float maxvalue = *maxtime;
                    auto iter = (std::find(a_activeSummonEffectsDuration.begin(), a_activeSummonEffectsDuration.end(),
                                           maxvalue));
                    auto index = std::distance(a_activeSummonEffectsDuration.begin(), iter);
                    if (a_activeSummonEffectsIndexSorted.empty()) {
                        a_activeSummonEffectsIndexSorted.push_back(a_activeSummonEffectsIndex[index]);
                    } else {
                        a_activeSummonEffectsIndexSorted.insert(a_activeSummonEffectsIndexSorted.begin(),
                                                                a_activeSummonEffectsIndex[index]);
                    }
                    a_activeSummonEffectsDuration[index] = 0.0f;
                }

                // iterate over sorted active summon effects to determine which to dispel
                for (std::uint32_t widx = 0; widx < a_activeSummonEffectsIndexSorted.size(); ++widx) {
                    auto element = commandedActorsEffectsArray[a_activeSummonEffectsIndexSorted[widx]];

                    if (!casterIsPlayer  || !element.activeEffect->effect->baseEffect->HasKeywordString(
                            "MagicSummon_ED_Uncapped")) {
                        if (perkfactor >= 1.0f) {
                            // summon limit has space for this effect

                            logger::info("Checking capped command spell, not reached limit yet");

                            perkfactor -= 1.0f;
                        } else {
                            // summon limit is full, this spell and other

                            logger::info("Checking capped command spell, limit has already been reached, will be dispelled");
                            a_effectsToDeleteIndex.push_back(a_activeSummonEffectsIndexSorted[widx]);
                        }
                    }   
                }

                // dispel effects liable for dispel
                if (a_effectsToDeleteIndex.size() > 0) {
                    for (std::uint32_t widx = 0; widx < a_effectsToDeleteIndex.size(); ++widx) {
                        commandedActorsEffectsArray[a_effectsToDeleteIndex[widx]].activeEffect->Dispel(true);
                    }
                }
            }
        }
        static inline REL::Relocation<decltype(thunk)> func;
    };
    static void Install() {
        REL::Relocation<std::uintptr_t> functionCommandedActorLimitHook{RELOCATION_ID(38993, 40056),
                                                                        REL::Relocate(0xA1, 0xEC)};
        stl::write_thunk_call<CommandedActorLimitHook>(functionCommandedActorLimitHook.address());

        REL::Relocation<std::uintptr_t> functionCommandedActorHook{RELOCATION_ID(38904, 39950),
                                                                   REL::Relocate(0x14B, 0x12B)};
        stl::write_thunk_call<CommandedActorHook>(functionCommandedActorHook.address());
    };
};


void MessageListener(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {

        logger::info("All plugins have loaded, checking if SummonActorLimitOverhaul is present");
        if (GetModuleHandle(L"SummonActorLimitOverhaul.dll") == nullptr) {
            logger::info("No SummonActorLimitOverhaul detected, installing hooks...");
            Hooks::Install();
            logger::info("EverdamnedSupportPlugin was installed!");
        } else {
            logger::info("SummonActorLimitOverhaul detected, plugin hooks were not installed");
        }

    }
}

std::string MyNativeFunction(RE::StaticFunctionTag*, int numba) {
    logger::info("MyNativeFunction in C++ got called!, numba was: {}", numba);
    return "Hello from C++!";
}

std::string GetSpellAndReturnItsName(RE::StaticFunctionTag*, RE::SpellItem* theSpell) {
    logger::info("The spell name: {}", theSpell->GetFullName());
    return theSpell->GetFullName();
}

RE::Actor* GetEffectAndReturnActor(RE::StaticFunctionTag*, RE::ActiveEffect* theEffect) {
    logger::info("The effect name: {}", theEffect->effect->baseEffect->GetFullName());
    logger::info("The effect caster name: {}", theEffect->caster.get().get()->GetName());
    return theEffect->caster.get().get();
}

//RE::Actor* CastCommandSpellFromSourceAndReturnActor(RE::StaticFunctionTag*, RE::ActiveEffect* theEffect) {
//    logger::info("The effect name: {}", theEffect->effect->baseEffect->GetFullName());
//    logger::info("The effect caster name: {}", theEffect->caster.get().get()->GetName());
//    return theEffect->caster.get().get();
//}

bool BindPapyrusFunctions(RE::BSScript::IVirtualMachine* vm) {
    vm->RegisterFunction("PapyrusNativeFunctionBinding", "ED_SKSEnativebindings", MyNativeFunction);
    vm->RegisterFunction("GetProvidedSpellName", "ED_SKSEnativebindings", GetSpellAndReturnItsName);
    vm->RegisterFunction("GetEffectCaster", "ED_SKSEnativebindings", GetEffectAndReturnActor);
    logger::info("Papyrus functions bound!");
    return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();
    //skse->GetPluginInfo("");
    SKSE::GetMessagingInterface()->RegisterListener(MessageListener);
    SKSE::GetPapyrusInterface()->Register(BindPapyrusFunctions);


    //SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
    //    if (message->type == SKSE::MessagingInterface::kDataLoaded)
    //        
    //});
    
    return true;
}

