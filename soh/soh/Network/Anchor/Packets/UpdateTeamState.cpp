#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include "soh/OTRGlobals.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#ifdef COMBO_BUILD
#include "rando/ComboAnchorToast.h"                    // shared cross-game resync-toast debounce
#include "soh/Enhancements/randomizer/hook_handlers.h" // OOT_LookupForeign (foreign backfill)
// Launcher cross-deliver seam for foreign-check backfill.
extern "C" void (*gComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" void (*gComboTriforceProgress)(int game, int fileNum);
#endif

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * UPDATE_TEAM_STATE
 *
 * Pushes the current save state to the server for other teammates to use.
 *
 * Fires when the server passes on a REQUEST_TEAM_STATE packet, or when this client saves the game
 *
 * When sending this packet we will assume that the team queue has been emptied for this client, so the queue
 * stored in the server will be cleared.
 *
 * When receiving this packet, if there is items in the team queue, we will play them back in order.
 */

void Anchor::SendPacket_UpdateTeamState() {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");

    // Assume the team queue has been emptied, so clear it
    payload["queue"] = json::array();

    payload["state"] = gSaveContext;
    // manually update current scene flags (skip while dormant/no play state — the save copy is current)
    if (gPlayState != NULL) {
        payload["state"]["sceneFlags"][gPlayState->sceneNum * 4] = gPlayState->actorCtx.flags.chest;
        payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 1] = gPlayState->actorCtx.flags.swch;
        payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 2] = gPlayState->actorCtx.flags.clear;
        payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 3] = gPlayState->actorCtx.flags.collect;
    }

    // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
    // doesn't have to generate the seed themselves Currently it doesn't work :)
    if (IS_RANDO) {
        auto randoContext = Rando::Context::GetInstance();

        payload["state"]["rando"] = json::object();
        payload["state"]["rando"]["itemLocations"] = json::array();
        for (int i = 0; i < RC_MAX; i++) {
            payload["state"]["rando"]["itemLocations"][i] = json::array();
            // payload["state"]["rando"]["itemLocations"][i]["rgID"] =
            // randoContext->GetItemLocation(i)->GetPlacedRandomizerGet();
            payload["state"]["rando"]["itemLocations"][i][0] = randoContext->GetItemLocation(i)->GetCheckStatus();
            payload["state"]["rando"]["itemLocations"][i][1] = (u8)randoContext->GetItemLocation(i)->GetIsSkipped();

            // if (randoContext->GetItemLocation(i)->GetPlacedRandomizerGet() == RG_ICE_TRAP) {
            //     payload["state"]["rando"]["itemLocations"][i]["fakeRgID"] =
            //     randoContext->GetItemOverride(i).LooksLike();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"] = json::object();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["english"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetEnglish();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["french"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetFrench();
            // }
            // if (randoContext->GetItemLocation(i)->HasCustomPrice()) {
            //     payload["state"]["rando"]["itemLocations"][i]["price"] =
            //     randoContext->GetItemLocation(i)->GetPrice();
            // }
        }

        // auto entranceCtx = randoContext->GetEntranceShuffler();
        // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
        //     payload["state"]["rando"]["entrances"][i] = json::object();
        //     payload["state"]["rando"]["entrances"][i]["type"] = entranceCtx->entranceOverrides[i].type;
        //     payload["state"]["rando"]["entrances"][i]["index"] = entranceCtx->entranceOverrides[i].index;
        //     payload["state"]["rando"]["entrances"][i]["destination"] = entranceCtx->entranceOverrides[i].destination;
        //     payload["state"]["rando"]["entrances"][i]["override"] = entranceCtx->entranceOverrides[i].override;
        //     payload["state"]["rando"]["entrances"][i]["overrideDestination"] =
        //     entranceCtx->entranceOverrides[i].overrideDestination;
        // }

        // payload["state"]["rando"]["seed"] = json::array();
        // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
        //     payload["state"]["rando"]["seed"][i] = randoContext->hashIconIndexes[i];
        // }
        // payload["state"]["rando"]["inputSeed"] = randoContext->GetSeedString();
        // payload["state"]["rando"]["finalSeed"] = randoContext->GetSeed();

        // payload["state"]["rando"]["randoSettings"] = json::array();
        // for (int i = 0; i < RSK_MAX; i++) {
        //     payload["state"]["rando"]["randoSettings"][i] =
        //     randoContext->GetOption((RandomizerSettingKey(i))).GetSelectedOptionIndex();
        // }

        // payload["state"]["rando"]["masterQuestDungeonCount"] = randoContext->GetDungeons()->CountMQ();
        // payload["state"]["rando"]["masterQuestDungeons"] = json::array();
        // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
        //     payload["state"]["rando"]["masterQuestDungeons"][i] = randoContext->GetDungeon(i)->IsMQ();
        // }
        // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
        //     payload["state"]["rando"]["requiredTrials"][i] = randoContext->GetTrial(i)->IsRequired();
        // }
    }

    SendJsonToRemote(payload);
}

void Anchor::SendPacket_ClearTeamState(std::string teamId) {
    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = teamId;
    payload["queue"] = json::array();
    payload["state"] = json::object();
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateTeamState(nlohmann::json payload) {
    if (!roomState.syncItemsAndFlags) {
        return;
    }

#ifdef COMBO_BUILD
    // ComboShip: MM's UPDATE_TEAM_STATE has a different state shape; from_json would throw
    // mid-handler and leave isHandlingUpdateTeamState stuck true (muting check-status sync).
    if (payload.contains("state") && !payload["state"].contains("healthCapacity")) {
        return;
    }
#endif

    isHandlingUpdateTeamState = true;
    // This can happen in between file select and the game starting, so we can't use this check, but we need to ensure
    // we be careful to wrap PlayState usage in this check
    //
    // if (!IsSaveLoaded()) {
    //     return;
    // }

    if (payload.contains("state")) {
        SaveContext loadedData = payload["state"].get<SaveContext>();

        gSaveContext.healthCapacity = loadedData.healthCapacity;
        gSaveContext.magicLevel = loadedData.magicLevel;
        gSaveContext.magicCapacity = loadedData.magicCapacity;
        gSaveContext.magic = static_cast<s8>(loadedData.magicCapacity);
        gSaveContext.isMagicAcquired = loadedData.isMagicAcquired;
        gSaveContext.isDoubleMagicAcquired = loadedData.isDoubleMagicAcquired;
        gSaveContext.isDoubleDefenseAcquired = loadedData.isDoubleDefenseAcquired;
        gSaveContext.bgsFlag = loadedData.bgsFlag;
        gSaveContext.swordHealth = loadedData.swordHealth;
#ifdef COMBO_BUILD
        // ComboShip (#136): the wholesale ship.quest replace would regress the Triforce count against a
        // stale peer; a resync may only ever add.
        const uint8_t localTriforcePieces = gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected;
#endif
        gSaveContext.ship.quest = loadedData.ship.quest;
#ifdef COMBO_BUILD
        if (localTriforcePieces > gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected) {
            gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected = localTriforcePieces;
        }
#endif

        for (int i = 0; i < 124; i++) {
            // ComboShip (bug 5): union scene flags — a stale peer must never clear a flag we've set.
            u32 chest = gSaveContext.sceneFlags[i].chest | loadedData.sceneFlags[i].chest;
            u32 clear = gSaveContext.sceneFlags[i].clear | loadedData.sceneFlags[i].clear;
            u32 collect = gSaveContext.sceneFlags[i].collect | loadedData.sceneFlags[i].collect;
            u32 swch = gSaveContext.sceneFlags[i].swch | loadedData.sceneFlags[i].swch;

            // These swch bits are level/timer state (can legitimately go down) — keep LOCAL, don't union.
            u32 keepMask = 0;
            if (i == SCENE_WATER_TEMPLE) {
                keepMask = (1 << 0x1C) | (1 << 0x1D) | (1 << 0x1E); // water level
            } else if (i == SCENE_FOREST_TEMPLE) {
                keepMask = (1 << 0x1B); // elevator
            } else if (i == SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR) {
                keepMask = (1 << 0x17); // collapse timer
            }
            if (keepMask) {
                swch = (swch & ~keepMask) | (gSaveContext.sceneFlags[i].swch & keepMask);
            }

            gSaveContext.sceneFlags[i].chest = chest;
            gSaveContext.sceneFlags[i].swch = swch;
            gSaveContext.sceneFlags[i].clear = clear;
            gSaveContext.sceneFlags[i].collect = collect;
            // ComboShip: dormant apply's IsSaveLoaded() is true from fileNum alone; gPlayState is null.
            if (IsSaveLoaded() && gPlayState != NULL && gPlayState->sceneNum == i) {
                gPlayState->actorCtx.flags.chest = chest;
                gPlayState->actorCtx.flags.swch = swch;
                gPlayState->actorCtx.flags.clear = clear;
                gPlayState->actorCtx.flags.collect = collect;
            }
        }

        for (int i = 0; i < 14; i++) {
            gSaveContext.eventChkInf[i] |= loadedData.eventChkInf[i];
        }

        for (int i = 0; i < 4; i++) {
            gSaveContext.itemGetInf[i] |= loadedData.itemGetInf[i];
        }

        // Skip last row of infTable, don't want to sync swordless flag
        for (int i = 0; i < 29; i++) {
            gSaveContext.infTable[i] |= loadedData.infTable[i];
        }

        for (int i = 0; i < ceil((RAND_INF_MAX + 15) / 16); i++) {
            gSaveContext.ship.randomizerInf[i] |= loadedData.ship.randomizerInf[i];
        }

        for (int i = 0; i < 6; i++) {
            gSaveContext.gsFlags[i] |= loadedData.gsFlags[i];
        }

        gSaveContext.ship.stats.firstInput = loadedData.ship.stats.firstInput;
        gSaveContext.ship.stats.fileCreatedAt = loadedData.ship.stats.fileCreatedAt;

        // Ensure ganon barrier state matches trials
        if (gSaveContext.eventChkInf[10] & 0x2000 && gSaveContext.eventChkInf[11] & 0xFC00) {
            gSaveContext.eventChkInf[12] |= 0x8;
        }

        // Restore master sword state
        // Disabling this for now, not really sure I understand why I did this in the past
        // u8 hasMasterSword = CHECK_OWNED_EQUIP(EQUIP_TYPE_SWORD, 1);
        // if (hasMasterSword) {
        //     loadedData.inventory.equipment |= 0x2;
        // } else {
        //     loadedData.inventory.equipment &= ~0x2;
        // }

        // Restore bottle contents (unless it's ruto's letter)
        for (int i = 0; i < 4; i++) {
            if (gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_NONE &&
                gSaveContext.inventory.items[SLOT_BOTTLE_1 + i] != ITEM_LETTER_RUTO) {
                loadedData.inventory.items[SLOT_BOTTLE_1 + i] = gSaveContext.inventory.items[SLOT_BOTTLE_1 + i];
            }
        }

        // Take the higher ammo count (union), unless it's beans (planting is authoritative).
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
            if (i != SLOT(ITEM_BEAN) && i != SLOT(ITEM_BEAN + 1) &&
                gSaveContext.inventory.ammo[i] > loadedData.inventory.ammo[i]) {
                loadedData.inventory.ammo[i] = gSaveContext.inventory.ammo[i];
            }
        }

        // ComboShip (bug 4/5): union the inventory — a resync must never regress owned progress.
        loadedData.inventory.questItems |= gSaveContext.inventory.questItems;
        loadedData.inventory.equipment |= gSaveContext.inventory.equipment; // owned-piece bits
        for (int i = 0; i < 8; i++) {
            u32 mask = gUpgradeMasks[i];
            u8 shift = gUpgradeShifts[i];
            u32 localVal = (gSaveContext.inventory.upgrades & mask) >> shift;
            u32 inVal = (loadedData.inventory.upgrades & mask) >> shift;
            if (localVal > inVal) {
                loadedData.inventory.upgrades = (loadedData.inventory.upgrades & ~mask) | (localVal << shift);
            }
        }
        // Keep a locally-owned item slot (bottles already resolved above with their Ruto-letter nuance).
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.items); i++) {
            if (i >= SLOT_BOTTLE_1 && i < SLOT_BOTTLE_1 + 4) {
                continue;
            }
            if (gSaveContext.inventory.items[i] != ITEM_NONE) {
                loadedData.inventory.items[i] = gSaveContext.inventory.items[i];
            }
        }
        // Small-key counts (max) and dungeon items (map/compass/boss-key bits) aren't covered above.
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.dungeonKeys); i++) {
            if (gSaveContext.inventory.dungeonKeys[i] > loadedData.inventory.dungeonKeys[i]) {
                loadedData.inventory.dungeonKeys[i] = gSaveContext.inventory.dungeonKeys[i];
            }
        }
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.dungeonItems); i++) {
            loadedData.inventory.dungeonItems[i] |= gSaveContext.inventory.dungeonItems[i];
        }

        gSaveContext.inventory = loadedData.inventory;

        // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
        // doesn't have to generate the seed themselves Currently it doesn't work :)
        if (IS_RANDO && payload["state"].contains("rando")) {
            auto randoContext = Rando::Context::GetInstance();

            for (int i = 0; i < RC_MAX; i++) {
                auto itemLocation = payload["state"]["rando"].at("itemLocations").at(i);
                // randoContext->GetItemLocation(i)->RefPlacedItem() =
                // itemLocation.at("rgID").get<RandomizerGet>();
                auto* loc = OTRGlobals::Instance->gRandoContext->GetItemLocation(i);
                // ComboShip (bug 3): union not replace — the enum is progressive (unchecked < ...
                // < collected < saved), so only advance status; a stale/incomplete peer must not
                // un-collect a check we already have.
                RandomizerCheckStatus incoming = itemLocation.at(0).get<RandomizerCheckStatus>();
                RandomizerCheckStatus localStatus = loc->GetCheckStatus();
#ifdef COMBO_BUILD
                // ComboShip (bug 4): backfill a check the team obtained but we missed. Capture LOCAL
                // before the advance below; obtained/granted are one and the same on the live path.
                // Foreign checks hold the OTHER game's item, absent from this inventory snapshot, so
                // deliver them cross-game (dedup-guarded). Native items are already recovered by the
                // inventory union above — granting here too would double-credit additive items.
                if (incoming >= RCSHOW_COLLECTED && localStatus < RCSHOW_COLLECTED &&
                    loc->GetPlacedRandomizerGet() == RG_COMBO_FOREIGN) {
                    const std::string checkName = Rando::StaticData::GetLocation((RandomizerCheck)i)->GetName();
                    const ComboRando::ForeignItem* fi = OOT_LookupForeign(gSaveContext.fileNum, checkName);
                    if (fi && gComboCrossDeliver) {
                        gComboCrossDeliver((int)fi->itemGame, fi->itemName.c_str(), checkName.c_str());
                    }
                }
#endif
                if (incoming > localStatus) {
                    loc->SetCheckStatus(incoming);
                }
                // ComboShip (finding 5): only advance skip, a stale peer must not un-skip.
                u8 incomingSkipped = itemLocation.at(1).get<u8>();
                if (incomingSkipped > (u8)loc->GetIsSkipped()) {
                    loc->SetIsSkipped(incomingSkipped);
                }

                // if (itemLocation.contains("fakeRgID")) {
                //     randoContext->overrides.emplace(static_cast<RandomizerCheck>(i),
                //     Rando::ItemOverride(static_cast<RandomizerCheck>(i),
                //     itemLocation.at("fakeRgID").get<RandomizerGet>()));
                //     randoContext->GetItemOverride(i).GetTrickName().english =
                //     itemLocation.at("trickName").at("english").get<std::string>();
                //     randoContext->GetItemOverride(i).GetTrickName().french =
                //     itemLocation.at("trickName").at("french").get<std::string>();
                // }
                // if (itemLocation.contains("price")) {
                //     u16 price = itemLocation.at("price"].get<u16>();
                //     if (price > 0) {
                //         randoContext->GetItemLocation(i)->SetCustomPrice(price);
                //     }
                // }
            }

            // auto entranceCtx = randoContext->GetEntranceShuffler();
            // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
            //     entranceCtx->entranceOverrides[i].type =
            //     payload["state"]["rando"]["entrances"][i]["type"].get<u16>(); entranceCtx->entranceOverrides[i].index
            //     = payload["state"]["rando"]["entrances"][i]["index"].get<s16>();
            //     entranceCtx->entranceOverrides[i].destination =
            //     payload["state"]["rando"]["entrances"][i]["destination"].get<s16>();
            //     entranceCtx->entranceOverrides[i].override =
            //     payload["state"]["rando"]["entrances"][i]["override"].get<s16>();
            //     entranceCtx->entranceOverrides[i].overrideDestination =
            //     payload["state"]["rando"]["entrances"][i]["overrideDestination"].get<s16>();
            // }

            // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
            //     randoContext->hashIconIndexes[i] = payload["state"]["rando"]["seed"][i].get<u8>();
            // }
            // randoContext->GetSettings()->SetSeedString(payload["state"]["rando"]["inputSeed"].get<std::string>());
            // randoContext->GetSettings()->SetSeed(payload["state"]["rando"]["finalSeed"].get<u32>());

            // for (int i = 0; i < RSK_MAX; i++) {
            //     randoContext->GetOption(RandomizerSettingKey(i)).SetSelectedIndex(payload["state"]["rando"]["randoSettings"][i].get<u8>());
            // }

            // randoContext->GetDungeons()->ClearAllMQ();
            // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
            //     if (payload["state"]["rando"]["masterQuestDungeons"][i].get<bool>()) {
            //         randoContext->GetDungeon(i)->SetMQ();
            //     }
            // }

            // randoContext->GetTrials()->SkipAll();
            // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
            //     if (payload["state"]["rando"]["requiredTrials"][i].get<bool>()) {
            //         randoContext->GetTrial(i)->SetAsRequired();
            //     }
            // }
        }

#ifdef COMBO_BUILD
        // ComboShip: OOT and MM each request a resync (connect/manual), and the server answers each, so
        // collapse the burst to ONE toast across BOTH games via a shared-CVar timestamp (per-DLL statics
        // can't dedup a cross-game OOT+MM pair).
        if (isDormantApply) {
            dormantDidApply = true; // let PumpDormant persist; no toast for a backgrounded apply
        } else if (ComboAnchor_ShouldToastResync()) {
            Notification::Emit({ .message = "Save updated from team" });
        }
        // ComboShip (#136): a teammate's pieces can cross the combined goal for us too — re-evaluate.
        if (gComboTriforceProgress != NULL) {
            gComboTriforceProgress(0, gSaveContext.fileNum);
        }
#else
        Notification::Emit({
            .message = "Save updated from team",
        });
#endif
    }

    if (payload.contains("queue")) {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        for (auto& item : payload["queue"]) {
            nlohmann::json itemPayload = nlohmann::json::parse(item.get<std::string>());
            incomingPacketQueue.push(itemPayload);
        }
    }
    isHandlingUpdateTeamState = false;
}
