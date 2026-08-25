#include "Spoiler.h"
#include "Rando/Rando.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include "ShipUtils.h"

extern "C" {
#include "overlays/actors/ovl_En_Sth/z_en_sth.h"
}

namespace Rando {

namespace Spoiler {

void ApplyToSaveContext(nlohmann::json spoiler) {
    gSaveContext.save.shipSaveInfo.rando.finalSeed = spoiler["finalSeed"].get<uint32_t>();

    for (auto& [randoOptionId, randoStaticOption] : Rando::StaticData::Options) {
        RANDO_SAVE_OPTIONS[randoOptionId] =
            spoiler["options"].value(randoStaticOption.name, (uint32_t)randoStaticOption.defaultValue);
    }

    if (!RANDO_SAVE_OPTIONS[RO_SHUFFLE_GOLD_SKULLTULAS]) {
        RANDO_SAVE_OPTIONS[RO_SKULLTULA_TOKENS_REQUIRED] = SPIDER_HOUSE_TOKENS_REQUIRED;
    }

    auto startingItems = Rando::GetStartingItemsFromSpoiler(spoiler);
    Rando::SetStartingItemsInSave(gSaveContext.save.shipSaveInfo.rando, startingItems);

    auto priorityItems = Rando::GetSariaPriorityItemsFromSpoiler(spoiler);
    Rando::SetSariaPriorityItemsInSave(gSaveContext.save.shipSaveInfo.rando, priorityItems);

    for (auto& [randoCheckId, randoStaticCheck] : Rando::StaticData::Checks) {
        if (randoStaticCheck.randoCheckId == RC_UNKNOWN) {
            continue;
        }

        if (!spoiler["checks"].contains(randoStaticCheck.name)) {
#ifdef COMBO_BUILD
            // ComboShip: never let a small-key check revert to vanilla delivery — that bumps only
            // inventory.dungeonKeys and desyncs the rando mirror. Junk it like GeneratePools does an
            // excluded check (shuffled + skipped), which is also how a player exclusion lands here.
            auto iit = Rando::StaticData::Items.find(randoStaticCheck.randoItemId); // find, not [] — std::map
            if (iit != Rando::StaticData::Items.end() && iit->second.randoItemType == RITYPE_SMALL_KEY) {
                SPDLOG_WARN("[ComboShip] small key check not in the placement payload (excluded or broken), "
                            "forcing shuffled junk: {}",
                            randoStaticCheck.name);
                RANDO_SAVE_CHECKS[randoCheckId].randoItemId = RI_JUNK;
                RANDO_SAVE_CHECKS[randoCheckId].shuffled = true;
                RANDO_SAVE_CHECKS[randoCheckId].skipped = true;
                continue;
            }
#endif
            RANDO_SAVE_CHECKS[randoCheckId].randoItemId = randoStaticCheck.randoItemId;
            RANDO_SAVE_CHECKS[randoCheckId].shuffled = false;
            continue;
        }

        // Check if it's an object or a string
        if (spoiler["checks"][randoStaticCheck.name].is_object()) {
            std::string itemName = spoiler["checks"][randoStaticCheck.name]["randoItemId"].get<std::string>();
            RandoItemId randoItemId = Rando::StaticData::GetItemIdFromName(itemName.c_str());

            if (randoItemId == RI_UNKNOWN) {
                SPDLOG_ERROR("Unknown item in spoiler for check {}: {}", randoStaticCheck.name, itemName);
                throw std::runtime_error("Unknown item in spoiler: " + itemName);
            }

            RANDO_SAVE_CHECKS[randoCheckId].randoItemId = randoItemId;
            RANDO_SAVE_CHECKS[randoCheckId].shuffled = true;

            // If it has a price, set it
            if (spoiler["checks"][randoStaticCheck.name].contains("price")) {
                RANDO_SAVE_CHECKS[randoCheckId].price =
                    spoiler["checks"][randoStaticCheck.name]["price"].get<uint16_t>();
            }
        } else {
            std::string itemName = spoiler["checks"][randoStaticCheck.name].get<std::string>();
            RandoItemId randoItemId = Rando::StaticData::GetItemIdFromName(itemName.c_str());

            if (randoItemId == RI_UNKNOWN) {
                SPDLOG_ERROR("Unknown item in spoiler for check {}: {}", randoStaticCheck.name, itemName);
                throw std::runtime_error("Unknown item in spoiler: " + itemName);
            }

            RANDO_SAVE_CHECKS[randoCheckId].randoItemId = randoItemId;
            RANDO_SAVE_CHECKS[randoCheckId].shuffled = true;
        }
    }
}

} // namespace Spoiler

} // namespace Rando
