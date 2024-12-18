

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

                //if (a_AE->effect->baseEffect->HasArchetype(RE::EffectArchetypes::ArchetypeID::kSummonCreature)) {
                //    summonedeffect = reinterpret_cast<RE::SummonCreatureEffect*>(a_AE);
                //    if (summonedeffect) {
                //        soughtKeywordEditorIdPrefix = "MagicSummon";

                //        summonedactor = summonedeffect->commandedActor.get().get();
                //    }
                //} else if (a_AE->effect->baseEffect->HasArchetype(RE::EffectArchetypes::ArchetypeID::kReanimate)) {
                //    reanimatedeffect = reinterpret_cast<RE::ReanimateEffect*>(a_AE);
                //    if (reanimatedeffect) {
                //        soughtKeywordEditorIdPrefix = "MagicSummon";
                //        //reanimated = 1;
                //        summonedactor = reanimatedeffect->commandedActor.get().get();
                //    }
                //} else if (a_AE->effect->baseEffect->HasArchetype(
                //               RE::EffectArchetypes::ArchetypeID::kCommandSummoned)) {
                //    commandedeffect = reinterpret_cast<RE::CommandEffect*>(a_AE);
                //    if (commandedeffect) {
                //        soughtKeywordEditorIdPrefix = "MagicCommand";

                //        summonedactor = commandedeffect->commandedActor.get().get();
                //    }
                //}

                //std::unordered_map<std::string, float> keywordmap;

                
                //float perkfactorcapped = 0.0f;
                //RE::HandleEntryPoint(RE::PerkEntryPoint::kModSpellCost, summoner, &perkfactorcapped,
                //                     "VanillaCapped", 3,
                //                     {akCastedMagic});
                //if (perkfactor >= 1.0f && perkfactor + perkfactorcapped < 1.0f) {
                //
                //    perkfactor = 1.0f;
                //}
                // 
                //keywordmap["untyped"] = perkfactor;
                
                // ---------------------------------------------------
                
                // ---------------------------------------------------

                // ---------------------------------------------------
                // accounting to determine if someone should get dispelled
                //for (std::uint32_t widx = 0; widx < a_activeSummonEffectsIndexSorted.size(); ++widx) {
                //        auto element = commandedActorsEffectsArray[a_activeSummonEffectsIndexSorted[widx]];
                //        accountedfor = 0;

                //  
                //        
                //        // iterating over keywords in search of special "Magic(Summon/Command)" keyword to determine the typed pool
                //        // by default vanilla summon spells have such keywords, but without PEPE perks their typed pools are 0
                //        for (std::uint32_t idx = 0; idx < element.activeEffect->effect->baseEffect->numKeywords;
                //             ++idx) {
                //            if (element.activeEffect->effect->baseEffect->keywords[idx]->formEditorID.contains(
                //                    soughtKeywordEditorIdPrefix)) {
                //                auto testkey =
                //                    element.activeEffect->effect->baseEffect->keywords[idx]->formEditorID.c_str();

                //                // initializing pool limit for this specific keyword through PEPE perks
                //                // by default for vanilla summon spell this typed pool will be 0 because if no pepe perk present for summoner
                //                if (!keywordmap.contains(testkey)) {

                //                    perkfactor = 0.0f;
                //                    RE::HandleEntryPoint(RE::PerkEntryPoint::kModSpellCost, summoner, &perkfactor, element.activeEffect->effect->baseEffect->keywords[idx]->formEditorID.c_str(), 3, {element.activeEffect->spell});
                //                        keywordmap[testkey] = perkfactor;
                //                }

                //                // if not accountedfor and pool still has space then subtract 1 from pool and mark effect as accounted
                //                if (keywordmap[testkey] >= 1.0f && accountedfor == 0) {

                //                        keywordmap[testkey] -= 1.0f;
                //                        accountedfor = 1;
                //                }

                //            }
                //        }

                //        // if effect has no "MagicSpecialConjuration" keyword and was not accounted for with typed keyword look for space in "vanilla"? pool
                //        if (!element.activeEffect->effect->baseEffect->HasKeywordString("MagicSpecialConjuration")) {
                //            if (keywordmap["untyped"] > 0.0f && accountedfor == 0) {
                //                keywordmap["untyped"] -= 1.0f;
                //                accountedfor = 1;
                //            }

                //        }
                //        
                //        // if effect didnt get accounted for then push it to dispel list
                //        if (accountedfor == 0) {
                //            a_effectsToDeleteIndex.push_back(a_activeSummonEffectsIndexSorted[widx]);
                //            // seems to be irrelevant as (n == 0) always + logic is complete without it 
                //            //if (widx == 0) n = 0;
                //        }

                //}

                // dispel effects liable for dispel
                //if (a_effectsToDeleteIndex.size() > 0) {
                //        for (std::uint32_t widx = 0; widx < a_effectsToDeleteIndex.size(); ++widx) {

                //            commandedActorsEffectsArray[a_effectsToDeleteIndex[widx]].activeEffect->Dispel(true);
                //        }
                //}

                // ???
                //if (n == 0 && reanimated == 0) {
                //    std::vector<RE::SpellItem*> reanimateSpells;

                //    //RE::BGSEntryPoint::HandleEntryPoint(RE::PerkEntryPoint::kApplyReanimateSpell, summoner,
                //    //                                    akCastedMagic, summonedactor, &reanimateSpells); //This would be to use vanilla apply reanimate spell entry point

                //     RE::HandleEntryPoint(RE::PerkEntryPoint::kApplyReanimateSpell, summoner,
                //                          &reanimateSpells, "SummonSpell", 3, {akCastedMagic, summonedactor});
                //    for (auto* reanimateSpell : reanimateSpells) {
                //        if (reanimateSpell->IsPermanent()) {
                //            summonedactor->AddSpell(reanimateSpell);
                //        } else {
                //            summoner->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)
                //                ->CastSpellImmediate(reanimateSpell, false, summonedactor, 1.0F, false, 0.0F, nullptr);
                //        }
                //    }
                //    std::vector<RE::SpellItem*> selfreanimateSpells;
                //    RE::HandleEntryPoint(RE::PerkEntryPoint::kApplyReanimateSpell, summoner, &reanimateSpells,
                //                         "SummonSpell", 4, {akCastedMagic, summonedactor});
                //    for (auto* reanimateSpell : selfreanimateSpells) {
                //        if (reanimateSpell->IsPermanent()) {
                //            summoner->AddSpell(reanimateSpell);
                //        } else {
                //            summoner->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)
                //                ->CastSpellImmediate(reanimateSpell, false, summoner, 1.0F, false, 0.0F, nullptr);
                //        }
                //    }



                //}
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


SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SetupLog();
    //SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
    //    if (message->type == SKSE::MessagingInterface::kDataLoaded)
    //        
    //});

    logger::info("SummonActorSeparateLimit is initialized!");
   
    Hooks::Install();
    return true;
}

