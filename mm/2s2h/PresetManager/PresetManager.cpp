#include "PresetManager.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <filesystem>
#include <fstream>
#include <set>
#include "2s2h/BenPort.h"
#include "2s2h/BenGui/UIWidgets.hpp"
#include "2s2h/BenGui/Notification.h"
#include <ship/window/FileDropMgr.h>

std::unordered_map<std::string, std::string> tagMap = {
    { "gEventLog", "Developer Tools" },
    { "gDeveloperTools", "Developer Tools" },
    { "gCollisionViewer", "Developer Tools" },
    { "gCheats", "Enhancements" },
    { "gEnhancements", "Enhancements" },
    { "gFixes", "Enhancements" },
    { "gModes", "Enhancements" },
    { "gHudEditor", "HUD" },
    { "ItemTracker", "HUD" },
    { "gRando", "Rando" },
};

nlohmann::json defaultsPresetJ = R"(
{
    "ClearCVars": [
        "gCheats",
        "gCollisionViewer",
        "gDeveloperTools",
        "gEnhancements",
        "gEventLog",
        "gFixes",
        "gHudEditor",
        "gModes",
        "gNetwork",
        "gNotifications",
        "gRando",
        "gWindows",
        "ItemTracker",
        "gAudioEditor"
    ],
    "CVars": {},
    "type": "2S2H_PRESET",
    "version": 1
}
)"_json;

nlohmann::json curatedPresetJ = R"(
{
    "CVars": {
        "gAudioEditor": {
            "ChildGoronCry": 1,
            "LowHpAlarm": 1,
            "MuteCarpenterSfx": 1
        },
        "gCheats": {
            "EasyFrameAdvance": 1
        },
        "gDeveloperTools": {
            "BetterMapSelect": {
                "Enabled": 1
            },
            "DebugEnabled": 1,
            "LogLevel": 2
        },
        "gEnhancements": {
            "Camera": {
                "FixTargettingCameraSnap": 1
            },
            "Cutscenes": {
                "HideTitleCards": 1,
                "SkipEnemyCutscenes": 1,
                "SkipEntranceCutscenes": 1,
                "SkipFirstCycle": 1,
                "SkipGetItemCutscenes": 2,
                "SkipIntroSequence": 1,
                "SkipMiscInteractions": 1,
                "SkipOnePointCutscenes": 1,
                "SkipStoryCutscenes": 1,
                "SkipToFileSelect": 1
            },
            "Cycle": {
                "DoNotResetBottleContent": 1,
                "DoNotResetChateau": 1,
                "DoNotResetConsumables": 1,
                "DoNotResetRazorSword": 1,
                "DoNotResetRupees": 1,
                "DoNotResetScarecrowSong": 1,
                "DoNotResetTimeSpeed": 1,
                "KeepExpressMail": 1,
                "OceansideWalletAnyDay": 1,
                "SaveOnMoonCrash": 1,
                "StopOceansideSpiderHouseSquatter": 1
            },
            "Dialogue": {
                "FastBankSelection": 1,
                "FastText": 1,
                "SkipBottlePickupMessages": 1
            },
            "DifficultyOptions": {
                "GoronRace": 1,
                "JinxedTimer": 20,
                "LowerBankRewardThresholds": 1
            },
            "Dpad": {
                "DpadEquips": 1
            },
            "Equipment": {
                "BetterPictoMessage": 1,
                "ChuDrops": 1,
                "MagicArrowEquipSpeed": 1,
                "TwoHandedSwordSpinAttack": 1
            },
            "Fixes": {
                "CompletedHeartContainerAudio": 1,
                "ControlCharacters": 1,
                "DekuButlerFixShockLoopAnimation": 1,
                "FierceDeityZTargetMovement": 1
            },
            "Graphics": {
                "3DItemDrops": 1,
                "ActorCullingAccountsForWidescreen": 1,
                "AuthenticLogo": 1,
                "BowReticle": 1,
                "ClockType": 1,
                "DisableSceneGeometryDistanceCheck": 1,
                "FixSceneGeometrySeams": 1,
                "IncreaseActorDrawDistance": 5
            },
            "Items": {
                "ColorPictograph": 1,
                "RemoveExplosiveLimit": 1
            },
            "Masks": {
                "3DSMaskEquip": 1,
                "FastTransformation": 1,
                "FierceDeitysAnywhere": 1,
                "BlastMaskCooldown": 0.0,
                "PersistentBunnyHood": {
                    "Enabled": 1
                }
            },
            "Minigames": {
                "AlwaysWinDoggyRace": 1,
                "BeaverRaceRingsCollected": 2,
                "BombersHideAndSeek": 2,
                "CuccoShackCuccoCount": 2,
                "HoneyAndDarlingDay1": 4,
                "HoneyAndDarlingDay2": 4,
                "HoneyAndDarlingDay3": 8,
                "PowderKegCertification": 1,
                "RomaniTargetPractice": 5,
                "SkipBalladOfWindfish": 1,
                "SkipHorseRace": 1,
                "SkipLittleBeaver": 1,
                "SwampArcheryScore": 1580,
                "SwordsmanSchoolScore": 6,
                "TownArcheryScore": 25
            },
            "Mods": {
                "AlternateAssetsHotkey": 0
            },
            "Playback": {
                "DpadOcarina": 1,
                "NoDropOcarinaInput": 1,
                "SkipScarecrowSong": 1
            },
            "Player": {
                "ClimbSpeed": 5,
                "FastFlowerLaunch": 1,
                "FasterPushAndPull": 1,
                "FierceDeityPutaway": 1,
                "InfiniteDekuHopping": 1,
                "InstantPutaway": 1,
                "PreventDiveOverWater": 1,
                "UnderwaterOcarina": 1
            },
            "PlayerActions": {
                "ArrowCycle": 1,
                "InstantRecall": 1
            },
            "Restorations": {
                "BonkCollision": 1,
                "ConstantFlipsHops": 1,
                "OoTFasterSwim": 1,
                "PowerCrouchStab": 1,
                "SideRoll": 1,
                "SoilPatch": 1,
                "TatlISG": 1,
                "WoodfallMountainAppearance": 1
            },
            "Saving": {
                "Autosave": 1,
                "DisableSaveDelay": 1,
                "PauseSave": 1,
                "PersistentOwlSaves": 1
            },
            "Songs": {
                "BetterOwlWarpMenu": 1,
                "BetterSongOfDoubleTime": 1,
                "EnableSunsSong": 1,
                "FasterSongPlayback": 1,
                "PauseOwlWarp": 1,
                "SkipSoTCutscenes": 1,
                "SkipSoaringCutscene": 1,
                "ZoraEggCount": 1
            },
            "Timesavers": {
                "AlwaysShowShrineFeathers": 1,
                "DampeDiggingSkip": 1,
                "FastChests": 1,
                "FasterBottles": 1,
                "FasterSceneTransitions": 1,
                "GalleryTwofer": 1,
                "MarineLabHP": 1,
                "SkipBalladOfWindfish": 1,
                "SwampBoatSpeed": 1
            }
        },
        "gFixes": {
            "FixButtonEnvColor": 1,
            "FixEponaStealingSword": 1,
            "FixIkanaGreatFairyFountainColor": 1
        },
        "gHudEditor": {
            "A": {
                "Mode": 4
            },
            "B": {
                "Mode": 4
            },
            "CDown": {
                "Mode": 4
            },
            "CLeft": {
                "Mode": 4
            },
            "CRight": {
                "Mode": 4
            },
            "CUp": {
                "Mode": 4
            },
            "Carrots": {
                "Mode": 2
            },
            "Clock": {
                "Mode": 2
            },
            "DPad": {
                "Mode": 4
            },
            "Hearts": {
                "Mode": 3
            },
            "Keys": {
                "Mode": 3
            },
            "Magic": {
                "Mode": 3
            },
            "Minigames": {
                "Mode": 3
            },
            "Minimap": {
                "Mode": 4
            },
            "Rupees": {
                "Mode": 3
            },
            "SkullKidTimer": {
                "Mode": 2
            },
            "Skulltulas": {
                "Mode": 3
            },
            "Start": {
                "Mode": 4
            },
            "Timers": {
                "Mode": 3
            }
        }
    },
    "type": "2S2H_PRESET",
    "version": 1
}
)"_json;

nlohmann::json voyage3PresetJ = R"(
{
    "ClearCVars": [
        "gCheats",
        "gCollisionViewer",
        "gDeveloperTools",
        "gEventLog",
        "gFixes",
        "gNetwork",
        "gNotifications",
        "gRando"
    ],
    "CVars": {
        "gCheats": {
            "EasyFrameAdvance": 1,
            "SpeedModifier": {
                "Mode": 0
            }
        },
        "gEnhancements": {
            "Cutscenes": {
                "HideTitleCards": 1,
                "SkipEnemyCutscenes": 1,
                "SkipEntranceCutscenes": 1,
                "SkipFirstCycle": 1,
                "SkipGetItemCutscenes": 2,
                "SkipIntroSequence": 1,
                "SkipMiscInteractions": 1,
                "SkipOnePointCutscenes": 1,
                "SkipStoryCutscenes": 1,
                "SkipToFileSelect": 1
            },
            "Cycle": {
                "DoNotResetChateau": 0,
                "DoNotResetConsumables": 0,
                "DoNotResetRazorSword": 0,
                "DoNotResetRupees": 0,
                "DoNotResetTimeSpeed": 0,
                "SaveOnMoonCrash": 0,
                "TingleAlwaysInClockTown": 0
            },
            "Dialogue": {
                "AutoBombersCode": 1,
                "FastBankSelection": 1,
                "FastText": 1,
                "SkipBottlePickupMessages": 1
            },
            "DifficultyOptions": {
                "DamageMultiplier": 0,
                "DekuGuardSearchBalls": 0,
                "DeleteFileOnDeath": 0,
                "DisableTakkuriSteal": 1,
                "FrogChoirCount": 5,
                "GibdoTradeSequence": 1,
                "GoronRace": 1,
                "HiddenGrottosVisibility": 0,
                "HyperEnemies": 0,
                "JinxedTimer": 60,
                "LowerBankRewardThresholds": 1,
				"NoHeartDrops": 0,
                "NoRandomDrops": 0,
                "PermanentHeartLoss": 0
            },
            "Dpad": {
                "DpadEquips": 1
            },
            "Equipment": {
                "BetterPictoMessage": 1,
                "BombArrows": 0,
                "ChuDrops": 1,
                "GreatFairySwordBButton": 0,
                "ItemUnequip": 0,
                "MagicArrowEquipSpeed": 1,
                "TwoHandedSwordSpinAttack": 0
            },
            "Fixes": {
                "CompletedHeartContainerAudio": 1,
                "ConsoleCrashes": 1,
                "ControlCharacters": 1,
                "FierceDeityZTargetMovement": 1,
                "FixTexturesOOB": 1
            },
            "Gameplay": {
                "ExtendedProjectileInteractionDistance": 0
            },
            "Graphics": {
                "3DItemDrops": 1,
                "AuthenticLogo": 1,
                "DisableBlackBars": 1,
                "FixSceneGeometrySeams": 0,
                "IncreaseActorDrawDistance": 3,
                "MotionBlur": {
                    "Mode": 1
                }
            },
            "Items": {
                "AmmoBuyback": 0,
                "ColorPictograph": 1,
                "ExtraPowderKegs": 0,
                "PictoBoxOnCUp": 0,
                "RemoveExplosiveLimit": 0
            },
            "Masks": {
                "3DSMaskEquip": 1,
                "BlastMaskCooldown": 3.0,
                "BlastMaskKeg": 1,
                "FastTransformation": 1,
                "FierceDeitysAnywhere": 1,
                "GoronRollingFastSpikes": 0,
                "GoronRollingIgnoresMagic": 0,
                "NoBlastMaskCooldown": 1,
                "PersistentBunnyHood": {
                    "Enabled": 1
                }
            },
            "Minigames": {
                "AlwaysWinDoggyRace": 1,
                "BeaverRaceRingsCollected": 20,
                "BoatArcheryHealth": 30,
                "BoatArcheryInvincible": 1,
                "BoatArcheryScore": 10,
                "BombersHideAndSeek": 5,
                "CremiaHugs": 0,
                "CuccoShackCuccoCount": 1,
                "HoneyAndDarlingDay1": 8,
                "HoneyAndDarlingDay2": 8,
                "HoneyAndDarlingDay3": 16,
                "RomaniTargetPractice": 10,
                "SkipLittleBeaver": 1,
                "SwampArcheryScore": 2120,
                "SwordsmanSchoolScore": 30,
                "TownArcheryScore": 50,
                "TreasureChestShopShowFullMaze": 0
            },
            "Mods": {
                "AlternateAssets": 1
            },
            "Playback": {
                "DpadOcarina": 1,
                "NoDropOcarinaInput": 1,
                "SkipScarecrowSong": 1
            },
            "Player": {
                "ClimbSpeed": 4,
                "FastFlowerLaunch": 1,
                "FasterPushAndPull": 1,
                "FierceDeityPutaway": 1,
                "InfiniteDekuHopping": 0,
                "InstantPutaway": 1,
                "ManualJump": 0,
                "PreventDiveOverWater": 0,
                "UnderwaterOcarina": 1,
                "UnsheatheWithoutSlashing": 0
            },
            "PlayerActions": {
                "ArrowCycle": 1,
                "InstantRecall": 1,
                "RemoteBombchu": 0
            },
            "Restorations": {
                "BonkCollision": 0,
                "ConstantFlipsHops": 0,
                "JPGrottos": 0,
                "OoTFasterSwim": 0,
                "PowerCrouchStab": 2,
                "SideRoll": 0,
                "TatlISG": 1,
                "WoodfallMountainAppearance": 1
            },
            "Saving": {
                "Autosave": 0,
                "FileSlot3": 1,
                "PauseSave": 1,
                "PersistentOwlSaves": 0,
                "RememberSaveLocation": 0
            },
            "Shops": {
                "CuriosityShopRefills": 0
            },
            "Songs": {
                "BetterSongOfDoubleTime": 1,
                "FasterSongPlayback": 1,
                "SkipSoTCutscenes": 1,
                "SkipSoaringCutscene": 1,
                "ZoraEggCount": 1
            },
            "Timesavers": {
                "AlwaysShowShrineFeathers": 0,
                "AutoBankDeposit": 0,
                "DampeDiggingSkip": 1,
                "FastChests": 1,
                "FasterRupeeAccumulator": 1,
                "FasterSceneTransitions": 1,
                "GalleryTwofer": 1,
                "MarineLabHP": 1,
                "PowderKegCertification": 0,
                "SkipBalladOfWindfish": 1,
                "SwampBoatSpeed": 1
            }
        },
        "gFixes": {
            "FixAmmoCountEnvColor": 1,
            "FixButtonEnvColor": 1,
            "FixEponaStealingSword": 1,
            "FixIkanaGreatFairyFountainColor": 1
        },
        "gForceCursorVisibility": 1,
        "gMatchRefreshRate": 0,
		"gModes": {
			"MirroredWorld": {
                "Mode": 0,
                "StoneTowerTempleFix": 0
            },
			"TimeMovesWhenYouMove": 0
		},
        "gRando": {
            "CSMC": 1,
            "Enabled": 1,
            "ExcludedChecks": "6,20,24,55,57,76,98,99,100,101,102,103,104,105,106,138,139,140,141,142,143,144,146,242,243,244,245,246,247,248,249,258,374,375,376,377,378,379,380,381,382,383,384,385,1195,",
            "GenerateSpoiler": 0,
            "JunkItems": 0,
            "Options": {
                "RO_ACCESS_MAJORA_MASKS_COUNT": 0,
                "RO_ACCESS_MAJORA_REMAINS_COUNT": 4,
                "RO_ACCESS_MOON_REMAINS_COUNT": 4,
                "RO_HINTS_BANK_SIGN": 1,
                "RO_HINTS_BOSS_REMAINS": 1,
                "RO_HINTS_GOSSIP_STONES": 1,
                "RO_HINTS_GOSSIP_STONE_STRENGTH": 100,
                "RO_HINTS_HOOKSHOT": 1,
                "RO_HINTS_OATH_TO_ORDER": 1,
                "RO_HINTS_PURCHASEABLE": 0,
                "RO_HINTS_SONG_OF_SOARING": 1,
                "RO_HINTS_SPIDER_HOUSES": 1,
                "RO_HINTS_TRANSFORMATIONS": 1,
                "RO_MINIMUM_STRAY_FAIRIES": 5,
                "RO_PLENTIFUL_ITEMS": 0,
				"RO_PLACEMENT_BOSS_KEYS": 1,
                "RO_PLACEMENT_SMALL_KEYS": 0,
                "RO_PLACEMENT_STRAY_FAIRIES": 0,
                "RO_SHUFFLE_BOSS_REMAINS": 1,
                "RO_SHUFFLE_COWS": 1,
                "RO_SHUFFLE_FREESTANDING_ITEMS": 0,
                "RO_SHUFFLE_GOLD_SKULLTULAS": 0,
                "RO_SHUFFLE_GRASS_DROPS": 0,
                "RO_SHUFFLE_OWL_STATUES": 0,
                "RO_SHUFFLE_SHOPS": 1,
                "RO_SHUFFLE_SONG_SARIA": 1,
                "RO_SHUFFLE_TINGLE_SHOPS": 1,
                "RO_SHUFFLE_TRAPS": 1,
                "RO_SHUFFLE_TYCOON_WALLET": 0,
                "RO_STARTING_MAPS_AND_COMPASSES": 1,
                "RO_STARTING_RUPEES": 1,
                "RO_STRAY_FAIRIES_MAX": 15,
                "RO_STRAY_FAIRIES_REQUIRED": 5,
                "RO_TRAP_AMOUNT": 6
            },
            "StartingItems": [
                "RI_PROGRESSIVE_SWORD",
                "RI_SHIELD_HERO",
                "RI_OCARINA",
                "RI_SONG_TIME",
                "RI_MASK_BUNNY",
                "RI_SONG_EPONA"
            ],
            "SariaPriorityItems": [
                "RI_BOW",
                "RI_MASK_BLAST",
                "RI_BOMB_BAG_20",
                "RI_SINGLE_MAGIC",
                "RI_ARROW_FIRE",
                "RI_ARROW_ICE",
                "RI_ARROW_LIGHT",
                "RI_SONG_SONATA",
                "RI_SONG_LULLABY",
                "RI_SONG_NOVA",
                "RI_SONG_ELEGY",
                "RI_MASK_FIERCE_DEITY"
            ],
            "Traps": {
                "Freeze": 1,
                "Shock": 1
            }
        }
    },
    "type": "2S2H_PRESET",
    "version": 1
}
)"_json;

std::unordered_map<std::string, std::pair<nlohmann::json, std::set<std::string>>> presets = {};
const std::filesystem::path presetsFolderPath(Ship::Context::GetPathRelativeToAppDirectory("presets", appShortName));

void PresetManager_RefreshPresets() {
    presets.clear();
    presets.insert(
        { "Defaults (Everything Off)", { defaultsPresetJ, { "Developer Tools", "Enhancements", "HUD", "Rando" } } });
    presets.insert({ "Curated", { curatedPresetJ, { "Developer Tools", "Enhancements", "HUD" } } });
    presets.insert({ "Voyage 3", { voyage3PresetJ, { "Developer Tools", "Enhancements", "Rando" } } });

    // ensure the presets folder exists
    if (!std::filesystem::exists(presetsFolderPath)) {
        std::filesystem::create_directory(presetsFolderPath);
    }

    // Add all files in the presets folder to the list of presets
    for (const auto& entry : std::filesystem::directory_iterator(presetsFolderPath)) {
        if (entry.is_regular_file()) {
            std::string fileName = entry.path().filename().string();
            fileName.erase(fileName.find_last_of('.'));

            try {
                // Read the file
                nlohmann::json j;
                std::ifstream file(entry.path());
                file >> j;

                // Ensure the file is a valid preset
                if (!j.contains("type") || j["type"] != "2S2H_PRESET") {
                    continue;
                }

                // TODO: Migrate preset if it's an old version

                presets[fileName] = { j, {} };

                if (j.contains("ClearCVars")) {
                    std::vector<std::string> clearCVars = j["ClearCVars"].get<std::vector<std::string>>();
                    for (const auto& cvar : clearCVars) {
                        if (tagMap.contains(cvar)) {
                            presets[fileName].second.insert(tagMap[cvar]);
                        }
                    }
                }

                if (j.contains("CVars")) {
                    for (const auto& [key, value] : j["CVars"].items()) {
                        if (tagMap.contains(key)) {
                            presets[fileName].second.insert(tagMap[key]);
                        }
                    }
                }
                // Add the file to the list of presets
            } catch (...) {}
        }
    }
}

void PresetManager_ApplyPreset(nlohmann::json j) {
    if (!j.contains("type") || j["type"] != "2S2H_PRESET") {
        throw std::runtime_error("Invalid preset");
    }

    if (j.contains("ClearCVars")) {
        auto clearCVars = j["ClearCVars"].get<std::vector<std::string>>();

        for (const auto& cvar : clearCVars) {
            // Replace slashes with dots in key, and remove leading dot
            std::string path = cvar;
            std::replace(path.begin(), path.end(), '/', '.');
            if (path[0] == '.') {
                path.erase(0, 1);
            }
            CVarClearBlock(path.c_str());
            CVarClear(path.c_str());
        }
    }

    if (j.contains("CVars")) {
        auto cvars = j["CVars"].flatten();

        for (auto& [key, value] : cvars.items()) {
            // Replace slashes with dots in key, and remove leading dot
            std::string path = key;
            std::replace(path.begin(), path.end(), '/', '.');
            if (path[0] == '.') {
                path.erase(0, 1);
            }
            if (value.is_string()) {
                CVarSetString(path.c_str(), value.get<std::string>().c_str());
            } else if (value.is_number_integer()) {
                CVarSetInteger(path.c_str(), value.get<int>());
            } else if (value.is_number_float()) {
                CVarSetFloat(path.c_str(), value.get<float>());
            }
        }
    }

    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    ShipInit::Init("*");
    Notification::Emit({ .message = "Preset Loaded" });
}

// Copies 2ship2harkinian.json to the presets folder, then removes everything except the CVars block
void PresetManager_CreatePreset(std::string presetName) {
    try {
        std::ifstream existingFileStream(Ship::Context::GetPathRelativeToAppDirectory("2ship2harkinian.json"));

        nlohmann::json existingJson;
        existingFileStream >> existingJson;

        nlohmann::json newJson;
        newJson["type"] = "2S2H_PRESET";
        newJson["version"] = 1;
        newJson["CVars"] = existingJson["CVars"];
        // We may handle this differently in the future, but for now we're just not going to include gSettings and
        // gWindows. Users can still manually add them if they want.
        newJson["CVars"].erase("gSettings");
        newJson["CVars"].erase("gWindows");

        std::string presetFileName = presetName + ".json";
        const std::filesystem::path newPresetFilePath = presetsFolderPath / presetFileName;
        std::ofstream newFileStream(newPresetFilePath);
        newFileStream << newJson.dump(4);

        newFileStream.close();

        PresetManager_RefreshPresets();
    } catch (...) { Notification::Emit({ .suffix = "Failed to create preset" }); }
}

bool PresetManager_HandleFileDropped(char* filePath) {
    try {
        std::ifstream fileStream(filePath);

        if (!fileStream.is_open()) {
            return false;
        }

        // Check if first byte is "{"
        if (fileStream.peek() != '{') {
            return false;
        }

        nlohmann::json j;

        // Attempt to parse the file
        try {
            fileStream >> j;
        } catch (nlohmann::json::exception& e) { return false; }

        // Check if the file is a spoiler file
        if (!j.contains("type") || j["type"] != "2S2H_PRESET") {
            return false;
        }

        // Save the spoiler file to the presets folder
        std::string presetFileName = std::filesystem::path(filePath).filename().string();
        const std::filesystem::path newPresetFilePath = presetsFolderPath / presetFileName;
        std::filesystem::copy_file(filePath, newPresetFilePath, std::filesystem::copy_options::overwrite_existing);

        PresetManager_RefreshPresets();
        PresetManager_ApplyPreset(j);

        return true;
    } catch (std::exception& e) { return false; } catch (...) {
        return false;
    }
}

void PresetManager_Draw() {
    ImGui::BeginChild("PresetManager", ImVec2(500, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.5f));
    ImGui::TextWrapped("Drag and drop a preset file into the window to load it, or drop it into the presets folder and "
                       "refresh the list.");
    ImGui::PopStyleColor();
    if (UIWidgets::Button("Open Presets Folder", { .size = ImVec2(ImGui::GetContentRegionAvail().x - 42, 0) })) {
        std::string path = "file:///" + std::filesystem::absolute(presetsFolderPath).string();
        SDL_OpenURL(path.c_str());
    }
    ImGui::SameLine();
    if (UIWidgets::Button(ICON_FA_REFRESH)) {
        PresetManager_RefreshPresets();
    }
    ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextPadding, ImVec2(20, 0));
    ImGui::SeparatorText("Available Presets");

    std::string clickedPreset;

    for (const auto& [preset, pair] : presets) {
        const auto& [j, categories] = pair;
        ImGui::PushID(preset.c_str());

        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", preset.c_str());
        int index = 0;
        ImGui::PushStyleColor(ImGuiCol_Button, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, UIWidgets::ColorValues.at(UIWidgets::Colors::DarkGray));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5, 6));
        for (const auto& category : categories) {
            if (index++ > 0) {
                ImGui::SameLine();
            }
            ImGui::Button(category.c_str());
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        ImGui::EndGroup();
        auto lastGroupSize = ImGui::GetItemRectSize();
        auto lastGroupPos = ImGui::GetCursorPosY();
        // Vertically align to center, horizontally align to the right
        ImGui::BeginGroup();
        ImGui::SetCursorPos({ ImGui::GetContentRegionAvail().x - 80, lastGroupPos - (lastGroupSize.y / 2) - 22 });
        if (UIWidgets::Button("Apply", { .color = UIWidgets::Colors::Orange })) {
            clickedPreset = preset;
        }
        ImGui::EndGroup();
        // ImGui::SeparatorText("");
        ImGui::PopID();
    }

    if (!clickedPreset.empty()) {
        PresetManager_ApplyPreset(presets[clickedPreset].first);
    }

    ImGui::PopStyleVar(1);

    static bool showAddPresetForm = false;
    static bool focusPresetName = false;

    if (showAddPresetForm) {
        ImGui::Text("New Preset");

        static char presetName[256] = "";

        if (focusPresetName) {
            ImGui::SetKeyboardFocusHere();
            focusPresetName = false;
        }

        UIWidgets::PushStyleSlider(UIWidgets::Colors::Gray);
        ImGui::InputText("##Name", presetName, sizeof(presetName));
        UIWidgets::PopStyleSlider();

        ImGui::SameLine();

        if (UIWidgets::Button(ICON_FA_FLOPPY_O, { .size = ImVec2(0, 0), .color = UIWidgets::Colors::Green })) {
            PresetManager_CreatePreset(presetName);
            presetName[0] = '\0';
            showAddPresetForm = false;
        }

        ImGui::SameLine();
        if (UIWidgets::Button(ICON_FA_TIMES, { .size = ImVec2(0, 0), .color = UIWidgets::Colors::Red })) {
            presetName[0] = '\0';
            showAddPresetForm = false;
        }
    } else {
        if (UIWidgets::Button("Create Preset from Current Config", { .color = UIWidgets::Colors::Green })) {
            showAddPresetForm = true;
            focusPresetName = true;
        }
    }

    ImGui::EndChild();
}

void PresetManager_RegisterHooks() {
    Ship::Context::GetInstance()->GetFileDropMgr()->RegisterDropHandler(PresetManager_HandleFileDropped);
    PresetManager_RefreshPresets();
}

static RegisterShipInitFunc initFunc(PresetManager_RegisterHooks, {});
