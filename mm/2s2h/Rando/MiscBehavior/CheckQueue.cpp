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
extern "C" void (*gMMComboCrossDeliver)(int targetGame, const char* itemName);
extern "C" void MMAnchor_BroadcastCrossItem(int targetGame, const char* itemName, const char* srcCheckName);
#endif

extern "C" {
#include "variables.h"
#include <functions.h>
extern TexturePtr gItemIcons[131];
extern s16 D_801CFF94[250];
}

#ifdef COMBO_BUILD

// ComboShip: per-slot cache of MM checks that hold a foreign (OOT-bound) item. Reloaded when the
// active slot changes.
static int g_mmForeignSlot = -1;
static std::unordered_map<std::string, ComboRando::ForeignItem> g_mmForeignMap;

// ComboShip: lookup for UI surfaces (tracker/shops) that want the real OOT item name behind
// RI_COMBO_FOREIGN. Returns nullptr when the check isn't foreign. Keyed by Checks[].name (RC_*).
const ComboRando::ForeignItem* Rando::MiscBehavior::MM_LookupForeign(RandoCheckId rc) {
    int slot = gSaveContext.fileNum;
    if (slot == 0xFF)
        return nullptr; // no real save loaded (title screen sentinel)
    if (slot != g_mmForeignSlot) {
        g_mmForeignMap = ComboRando::LoadForeignForGame(slot, ComboRando::GAME_MM);
        g_mmForeignSlot = slot;
    }
    auto it = g_mmForeignMap.find(Rando::StaticData::Checks[rc].name);
    return it != g_mmForeignMap.end() ? &it->second : nullptr;
}

// ComboShip: divert a foreign-marked MM check into the cross-world mailbox instead of granting
// locally. Enqueues the real item for its home game, shows a "Sent to Hyrule" toast, and persists
// the save (the caller has already marked the check obtained).
static void Rando_SendForeignCheck(RandoCheckId rc) {
    int slot = gSaveContext.fileNum;
    if (slot != g_mmForeignSlot) {
        g_mmForeignMap = ComboRando::LoadForeignForGame(slot, ComboRando::GAME_MM);
        g_mmForeignSlot = slot;
    }
    // ComboShip: key by StaticData::Checks[].name (the "RC_*" identifier) — the SAME name the MM
    // dump emits and the fill writes into the foreign map. CheckNames[rc] is the human-readable
    // display name ("Termina Field Grass 160") and never matches the map keys.
    const std::string checkName = Rando::StaticData::Checks[rc].name;
    auto it = g_mmForeignMap.find(checkName);
    if (it != g_mmForeignMap.end()) {
        // Grant straight into the dormant target game's resident save (and persist it there), then
        // share with networked teammates. Replaces the old JSON mailbox + per-frame drain.
        if (gMMComboCrossDeliver)
            gMMComboCrossDeliver((int)it->second.itemGame, it->second.itemName.c_str());
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
                    Rando::StaticData::ShouldShowGetItemCutscene(ConvertItem(randoSaveCheck.randoItemId, randoCheckId)),
                .param = (int16_t)randoCheckId,
                .giveItem =
                    [](Actor* actor, PlayState* play) {
                        auto& randoSaveCheck = RANDO_SAVE_CHECKS[CUSTOM_ITEM_PARAM];
#ifdef COMBO_BUILD
                        // ComboShip: this MM check holds an item belonging to OOT. Divert it to the
                        // mailbox instead of granting locally. Branch on the raw stored id (before
                        // ConvertItem) so the sentinel is matched directly.
                        if (randoSaveCheck.randoItemId == RI_COMBO_FOREIGN) {
                            randoSaveCheck.cycleObtained = true;
                            randoSaveCheck.obtained = true;
                            randoSaveCheck.eligible = false;
                            Rando_SendForeignCheck((RandoCheckId)CUSTOM_ITEM_PARAM);
                            queued = false;
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
                            if (gSaveContext.save.shipSaveInfo.rando.foundTriforcePieces + 1 >=
                                RANDO_SAVE_OPTIONS[RO_TRIFORCE_PIECES_REQUIRED]) {
                                prefix = "You";
                                message = "completed the Triforce";
                            }
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
                        Rando::GiveItem(randoItemId);
                        randoSaveCheck.cycleObtained = true;
                        randoSaveCheck.obtained = true;
                        randoSaveCheck.eligible = false;
                        queued = false;
#ifdef COMBO_BUILD
                        // ComboShip: shared-progression co-op — broadcast this obtained check's RAW
                        // item to Anchor teammates (CUSTOM_ITEM_PARAM is still the checkId here; it is
                        // overwritten with the item id on the next line). No-op if Anchor is inactive.
                        MMAnchor_BroadcastCheckItem((int)CUSTOM_ITEM_PARAM, (int)randoSaveCheck.randoItemId);
#endif
                        CUSTOM_ITEM_PARAM = randoItemId;
                    },
                .drawItem =
                    [](Actor* actor, PlayState* play) {
                        RandoItemId randoItemId;

                        // If the item has been given, the CUSTOM_ITEM_PARAM is set to the RI, prior to that it's the RC
                        if (CUSTOM_ITEM_FLAGS & CustomItem::CALLED_ACTION) {
                            if ((RandoItemId)CUSTOM_ITEM_PARAM == RI_TRAP) {
                                randoItemId = RI_MAX_TRAP;
                            } else {
                                randoItemId = (RandoItemId)CUSTOM_ITEM_PARAM;
                            }
                        } else {
                            auto& randoSaveCheck = RANDO_SAVE_CHECKS[CUSTOM_ITEM_PARAM];
                            randoItemId =
                                Rando::ConvertItem(randoSaveCheck.randoItemId, (RandoCheckId)CUSTOM_ITEM_PARAM);
                        }

                        Matrix_Scale(30.0f, 30.0f, 30.0f, MTXMODE_APPLY);
                        Rando::DrawItem(randoItemId, (RandoCheckId)CUSTOM_ITEM_PARAM, actor);
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
