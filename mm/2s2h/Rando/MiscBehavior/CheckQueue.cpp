#include "MiscBehavior.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/CustomItem/CustomItem.h"
#include "2s2h/CustomMessage/CustomMessage.h"
#include "2s2h/BenGui/Notification.h"
#include "2s2h/Rando/StaticData/StaticData.h"
#include "2s2h/ShipUtils.h"
#include "Traps.h"
#ifdef COMBO_BUILD
#include "rando/CrossForeign.h"           // ComboShip: cross-world foreign-item marker map
#include "2s2h/SaveManager/SaveManager.h" // ComboShip: persist delivered cross items into the MM save
// ComboShip: Anchor shared-progression co-op — broadcast a locally-obtained check to teammates.
extern "C" void MMAnchor_BroadcastCheckItem(int randoCheckId, int randoItemId);
// ComboShip (issue #3): immediate cross-game delivery. gMMComboCrossDeliver (defined in BenPort.cpp)
// routes a foreign item to the OTHER game's resident save NOW; MMAnchor_BroadcastCrossItem shares it
// with networked teammates (no-op when Anchor is inactive).
extern "C" void (*gMMComboCrossDeliver)(int targetGame, const char* itemName, const char* srcCheckName);
extern "C" int gMMComboGoalRequired;
extern "C" int (*gMMComboOtherTriforceCount)(void);
extern "C" void MMAnchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);
#endif

extern "C" {
#include "variables.h"
#include <functions.h>
extern TexturePtr gItemIcons[131];
extern s16 D_801CFF94[250];
}

#ifdef COMBO_BUILD

// ComboShip: cache of MM checks that hold a foreign (OOT-bound) item, built from the pushed
// consolidated spoiler blob. Rebuilt when MM_LoadComboRando bumps the generation counter.
static uint64_t g_comboRandoGen = 0;
static uint64_t g_mmForeignGen = (uint64_t)-1;
static std::unordered_map<std::string, ComboRando::ForeignItem> g_mmForeignMap;

uint64_t Rando::MiscBehavior::ComboRandoGen() {
    return g_comboRandoGen;
}
void Rando::MiscBehavior::InvalidateComboForeignCache() {
    ++g_comboRandoGen; // all foreign/hint caches observe this and rebuild lazily
}

// ComboShip: lookup for UI surfaces (tracker/shops) that want the real OOT item name behind
// RI_COMBO_FOREIGN. Returns nullptr when the check isn't foreign. Keyed by Checks[].name (RC_*).
const ComboRando::ForeignItem* Rando::MiscBehavior::MM_LookupForeign(RandoCheckId rc) {
    int slot = gSaveContext.fileNum;
    if (slot == 0xFF)
        return nullptr; // no real save loaded (title screen sentinel)
    if (g_mmForeignGen != g_comboRandoGen) {
        g_mmForeignMap = ComboRando::LoadForeignForGame(slot, ComboRando::GAME_MM);
        g_mmForeignGen = g_comboRandoGen;
    }
    auto it = g_mmForeignMap.find(Rando::StaticData::GetCheckDisplayName(rc)); // ComboShip: friendly key
    return it != g_mmForeignMap.end() ? &it->second : nullptr;
}

// ComboShip: deliver a foreign-marked check's real item to its home game, toast, and persist the
// save (caller sets the obtained flags). Exposed so the shop buy path can reuse it.
void Rando::MiscBehavior::SendForeignCheck(RandoCheckId rc) {
    int slot = gSaveContext.fileNum;
    if (g_mmForeignGen != g_comboRandoGen) {
        g_mmForeignMap = ComboRando::LoadForeignForGame(slot, ComboRando::GAME_MM);
        g_mmForeignGen = g_comboRandoGen;
    }
    // ComboShip: key by the friendly combo-spoiler name (GetCheckDisplayName) — the SAME name the MM
    // dump emits and the fill/foreign array carry. Must match foreign[].checkName exactly.
    const std::string checkName = Rando::StaticData::GetCheckDisplayName(rc);
    auto it = g_mmForeignMap.find(checkName);
    if (it != g_mmForeignMap.end()) {
        // Grant straight into the dormant target game's resident save (and persist it there), then
        // share with networked teammates. Replaces the old JSON mailbox + per-frame drain.
        if (gMMComboCrossDeliver)
            gMMComboCrossDeliver((int)it->second.itemGame, it->second.itemName.c_str(), checkName.c_str());
        MMAnchor_BroadcastCrossItem((int)it->second.itemGame, it->second.itemName.c_str(), checkName.c_str());
        Notification::Emit({ .message = "Sent to Hyrule:", .suffix = it->second.displayName });
        SPDLOG_INFO("[ComboShip] MM delivered foreign item '{}' to OOT (from check '{}')", it->second.itemName,
                    checkName);
    } else {
        SPDLOG_WARN("[ComboShip] MM foreign sentinel at '{}' but no foreign-map entry; dropping", checkName);
    }
    // Persist the obtained flags (set by the caller) straight into the save so the collected state
    // survives a transition to OOT and back.
    SaveManager_SaveCurrentForCombo();
}

// ComboShip (bug 1): shared co-op broadcast seam, called by any grant path (CheckQueue below, MM
// shop buys in EnGirlA.cpp). No-op if wasObtained (see MiscBehavior.h).
void Rando::MiscBehavior::BroadcastCheckObtainedIfFirst(RandoCheckId rc, RandoItemId rawItemId, bool wasObtained) {
    if (!wasObtained) {
        MMAnchor_BroadcastCheckItem((int)rc, (int)rawItemId);
    }
}

// ComboShip: mirror ShouldShowGetItemCutscene, but for a foreign check use the foreign item's
// home-game importance (advancement) against the skip-get-item-cutscene setting.
bool Rando::MiscBehavior::ShouldShowForeignCutscene(RandoCheckId rc) {
    const ComboRando::ForeignItem* fi = MM_LookupForeign(rc);
    // A disguised trap counts as the progression it pretends to be, or skipping junk cutscenes would
    // identify every trap on sight.
    const bool advancement = fi != nullptr && (fi->advancement || fi->HasDisguise());
    const int lvl = CVarGetInteger("gEnhancements.Cutscenes.SkipGetItemCutscenes", 0);
    if (lvl == 0) {
        return true;
    }
    return advancement ? (lvl < 3) : (lvl < 1);
}
#endif

static bool queued = false;

// This function handles queuing up item gives that the player has been marked as eligible for. If you are looking for
// the behavior of the actual giving itself, the heavy lifting is done by the GameInteractor queue. This function is
// currently called every frame, and loops through the entire list of checks, this works for now but as the check list
// grows we should keep an eye on performance.
//
// Though it seems counter-intuitive, we currently only allow one thing from randommizer to be queued at a time,
// primarily because of the way an item can be converted may change as you queue up multiple items. This is actually
// fine for both the giving/drawing, as we can call ConvertItem() inside the Give/Draw lambda, but the message we
// pass to the queue is static and would need to be updated if we allowed multiple items to be queued at once.
void Rando::MiscBehavior::CheckQueue() {
    if (queued) {
        return;
    }

    for (auto& [randoCheckId, randoStaticCheck] : Rando::StaticData::Checks) {
        auto randoSaveCheck = RANDO_SAVE_CHECKS[randoCheckId];

        if (randoSaveCheck.eligible) {
            queued = true;

            GameInteractor::Instance->events.emplace_back(GIEventGiveItem{
                .showGetItemCutscene =
#ifdef COMBO_BUILD
                    // Foreign checks decide by the foreign item's home-game importance.
                (randoSaveCheck.randoItemId == RI_COMBO_FOREIGN)
                    ? Rando::MiscBehavior::ShouldShowForeignCutscene(randoCheckId)
                    :
#endif
                    Rando::StaticData::ShouldShowGetItemCutscene(ConvertItem(randoSaveCheck.randoItemId, randoCheckId)),
                .param = (int16_t)randoCheckId,
                .giveItem =
                    [](Actor* actor, PlayState* play) {
                        auto& randoSaveCheck = RANDO_SAVE_CHECKS[CUSTOM_ITEM_PARAM];
#ifdef COMBO_BUILD
                        // ComboShip: this MM check holds an item belonging to OOT. Present it like a
                        // native item (real name + get-item cutscene when important at home; the
                        // held-up model is drawn by the drawItem lambda -> MM_DrawComboForeign), then
                        // deliver it cross-game instead of granting locally. Branch on the raw stored
                        // id (before ConvertItem) so the sentinel is matched directly.
                        if (randoSaveCheck.randoItemId == RI_COMBO_FOREIGN) {
                            RandoCheckId cid = (RandoCheckId)CUSTOM_ITEM_PARAM;
                            const ComboRando::ForeignItem* fi = Rando::MiscBehavior::MM_LookupForeign(cid);
                            // A foreign trap fires on the FINDER, like a native RI_TRAP: MM's own trap
                            // message + effect, and never cross-delivered (nothing to send).
                            const bool foreignTrap = fi != nullptr && fi->trap;
                            std::string foreignName = Rando::StaticData::GetItemName(RI_COMBO_FOREIGN, false, cid);
                            CustomMessage::Entry entry = {
                                .textboxType = 2,
                                .icon = Rando::StaticData::GetIconForZMessage(RI_COMBO_FOREIGN),
                                .msg = foreignTrap ? GetTrapMessage() : ("You found " + foreignName + "!"),
                            };
                            if (CUSTOM_ITEM_FLAGS & CustomItem::GIVE_ITEM_CUTSCENE) {
                                CustomMessage::SetActiveCustomMessage(entry.msg, entry);
                            } else if (Rando::MiscBehavior::ShouldShowForeignCutscene(cid)) {
                                CustomMessage::StartTextbox(entry.msg + "\x1C\x02\x10", entry);
                            }
                            // ComboShip (bug 3): cycleObtained wipes every Song of Time, so this branch
                            // re-runs on cycle re-collection. Only cross-deliver/broadcast the FIRST
                            // time this check is permanently obtained, or a cycle reset would re-grant
                            // the item into OOT's save on every replay.
                            bool wasObtained = randoSaveCheck.obtained;
                            randoSaveCheck.cycleObtained = true;
                            randoSaveCheck.obtained = true;
                            randoSaveCheck.eligible = false;
                            if (foreignTrap) {
                                Rando::MiscBehavior::OfferTrapItem(); // fires here, not cross-granted
                                if (!wasObtained) {
                                    // Teammates still receive it; Anchor's receive path springs it.
                                    MMAnchor_BroadcastCrossItem((int)fi->itemGame, fi->itemName.c_str(),
                                                                Rando::StaticData::GetCheckDisplayName(cid).c_str());
                                }
                                SaveManager_SaveCurrentForCombo();
                            } else if (!wasObtained) {
                                Rando::MiscBehavior::SendForeignCheck(cid); // cross-deliver + toast + save
                            }
                            queued = false;
                            // ComboShip (#66): keep the check id (the held-up foreign model resolves
                            // by check id) before CUSTOM_ITEM_PARAM is repurposed as the item id.
                            CUSTOM_ITEM_PARAM2 = cid;
                            CUSTOM_ITEM_PARAM = RI_COMBO_FOREIGN;
                            return;
                        }
#endif
                        RandoItemId randoItemId =
                            Rando::ConvertItem(randoSaveCheck.randoItemId, (RandoCheckId)CUSTOM_ITEM_PARAM);
                        std::string prefix = "You found";
                        std::string message =
                            Rando::StaticData::GetItemName(randoItemId, true, (RandoCheckId)CUSTOM_ITEM_PARAM);

                        if (randoItemId == RI_JUNK) {
                            randoItemId = Rando::CurrentJunkItem((RandoCheckId)CUSTOM_ITEM_PARAM);
                        }
                        if (randoItemId == RI_TRIFORCE_PIECE) {
#ifdef COMBO_BUILD
                            // ComboShip (#136): the goal counts BOTH games' pieces.
                            const int comboPieces =
                                gSaveContext.save.shipSaveInfo.rando.foundTriforcePieces + 1 +
                                (gMMComboOtherTriforceCount != NULL ? gMMComboOtherTriforceCount() : 0);
                            if (gMMComboGoalRequired > 0 && comboPieces >= gMMComboGoalRequired) {
                                prefix = "You";
                                message = "completed the Triforce";
                            }
#else
                            if (gSaveContext.save.shipSaveInfo.rando.foundTriforcePieces + 1 >=
                                RANDO_SAVE_OPTIONS[RO_TRIFORCE_PIECES_REQUIRED]) {
                                prefix = "You";
                                message = "completed the Triforce";
                            }
#endif
                            randoItemId = RI_TRIFORCE_PIECE_PREVIOUS;
                        }

                        if (randoItemId == RI_TRAP) {
                            prefix = "";
                            message = GetTrapMessage();
                            // We need to remove the Color Codes if the player is skipping Item Get Cutscenes as the
                            // Notification Emit doesnt support it.
                            if (CVarGetInteger("gEnhancements.Cutscenes.SkipGetItemCutscenes", 0) >= 2) {
                                message = CustomMessage::RemoveColorCodes(message);
                            }
                        }

                        CustomMessage::Entry entry = {
                            .textboxType = 2,
                            .icon = Rando::StaticData::GetIconForZMessage(randoItemId),
                            .msg = (prefix == "" ? "" : prefix + " ") + message + (randoItemId == RI_TRAP ? "" : "!"),
                        };

                        if (CUSTOM_ITEM_FLAGS & CustomItem::GIVE_ITEM_CUTSCENE) {
                            CustomMessage::SetActiveCustomMessage(entry.msg, entry);
                        } else if (Rando::StaticData::ShouldShowGetItemCutscene(randoItemId)) {
                            CustomMessage::StartTextbox(entry.msg + "\x1C\x02\x10", entry);
                        } else {
                            if (Rando::StaticData::Items[randoItemId].randoItemType != RITYPE_JUNK) {
                                Notification::Emit({
                                    .itemIcon = Rando::StaticData::GetIconTexturePath(randoItemId),
                                    .message = prefix,
                                    .suffix = message,
                                });
                            }
                        }
#ifdef COMBO_BUILD
                        // ComboShip (bug 3): capture BEFORE the grant — cycleObtained wipes every Song
                        // of Time, so renewables/consumables re-run this lambda every cycle. Local grant
                        // still happens every time; only the teammate broadcast is gated so a re-collect
                        // doesn't re-share an already-permanent check.
                        bool wasObtained = randoSaveCheck.obtained;
#endif
                        Rando::GiveItem(randoItemId);
                        randoSaveCheck.cycleObtained = true;
                        randoSaveCheck.obtained = true;
                        randoSaveCheck.eligible = false;
                        queued = false;
#ifdef COMBO_BUILD
                        // ComboShip: shared-progression co-op — broadcast this obtained check's RAW
                        // item to Anchor teammates (CUSTOM_ITEM_PARAM is still the checkId here; it is
                        // overwritten with the item id on the next line). No-op if Anchor is inactive.
                        Rando::MiscBehavior::BroadcastCheckObtainedIfFirst((RandoCheckId)CUSTOM_ITEM_PARAM,
                                                                           randoSaveCheck.randoItemId, wasObtained);
#endif
                        CUSTOM_ITEM_PARAM = randoItemId;
                    },
                .drawItem =
                    [](Actor* actor, PlayState* play) {
                        RandoItemId randoItemId;
                        RandoCheckId randoCheckId = (RandoCheckId)CUSTOM_ITEM_PARAM;

                        // If the item has been given, the CUSTOM_ITEM_PARAM is set to the RI, prior to that it's the RC
                        if (CUSTOM_ITEM_FLAGS & CustomItem::CALLED_ACTION) {
                            if ((RandoItemId)CUSTOM_ITEM_PARAM == RI_TRAP) {
                                randoItemId = RI_MAX_TRAP;
                            } else {
                                randoItemId = (RandoItemId)CUSTOM_ITEM_PARAM;
                            }
#ifdef COMBO_BUILD
                            // ComboShip (#66): post-pickup CUSTOM_ITEM_PARAM is the item id, but the
                            // foreign model resolves by check id, stashed in CUSTOM_ITEM_PARAM2.
                            if (randoItemId == RI_COMBO_FOREIGN) {
                                randoCheckId = (RandoCheckId)CUSTOM_ITEM_PARAM2;
                            }
#endif
                        } else {
                            auto& randoSaveCheck = RANDO_SAVE_CHECKS[CUSTOM_ITEM_PARAM];
                            randoItemId = Rando::ConvertItem(randoSaveCheck.randoItemId, randoCheckId);
                        }

                        Matrix_Scale(30.0f, 30.0f, 30.0f, MTXMODE_APPLY);
                        Rando::DrawItem(randoItemId, randoCheckId, actor);
                    } });
            return;
        }
    }
}

void Rando::MiscBehavior::CheckQueueReset() {
    queued = false;
    GameInteractor::Instance->currentEvent = GIEventNone{};
    GameInteractor::Instance->events.clear();
}
