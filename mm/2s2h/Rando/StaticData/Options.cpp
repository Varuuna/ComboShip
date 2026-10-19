#include "StaticData.h"

extern "C" {
#include "overlays/actors/ovl_En_Sth/z_en_sth.h"
}

namespace Rando {

namespace StaticData {

#define RO(id, defaultValue)                             \
    {                                                    \
        id, {                                            \
            id, #id, "gRando.Options." #id, defaultValue \
        }                                                \
    }

// clang-format off
std::map<RandoOptionId, RandoStaticOption> Options = {
    RO(RO_ACCESS_DUNGEONS,             RO_ACCESS_DUNGEONS_FORM_AND_SONG),
    RO(RO_ACCESS_MAJORA_MASKS_COUNT,   0),
    RO(RO_ACCESS_MAJORA_REMAINS_COUNT, 0),
    RO(RO_ACCESS_MOON_MASKS_COUNT,     0),
    RO(RO_ACCESS_MOON_REMAINS_COUNT,   4),
    RO(RO_ACCESS_TRIALS,               RO_ACCESS_TRIALS_VANILLA),
    RO(RO_CLOCK_SHUFFLE_PROGRESSIVE,   RO_CLOCK_SHUFFLE_RANDOM),
    RO(RO_CLOCK_SHUFFLE,               RO_GENERIC_OFF),
    RO(RO_CLOCK_TERMINAL_TIME,         350), // Default: 05:50 (10 "minutes" before crash)
    RO(RO_HINTS_BANK_SIGN,             RO_GENERIC_OFF),
    RO(RO_HINTS_BOSS_REMAINS,          RO_GENERIC_OFF),
    RO(RO_HINTS_GOSSIP_STONE_STRENGTH, 50),
    RO(RO_HINTS_GOSSIP_STONES,         RO_GENERIC_OFF),
    RO(RO_HINTS_HOOKSHOT,              RO_GENERIC_OFF),
    RO(RO_HINTS_MOON_GOSSIP_STONES,    RO_GENERIC_OFF),
    RO(RO_HINTS_OATH_TO_ORDER,         RO_GENERIC_OFF),
    RO(RO_HINTS_PURCHASEABLE,          RO_GENERIC_OFF),
    RO(RO_HINTS_SONG_OF_SOARING,       RO_GENERIC_OFF),
    RO(RO_HINTS_SPIDER_HOUSES,         RO_GENERIC_OFF),
    RO(RO_HINTS_TRANSFORMATIONS,       RO_GENERIC_OFF),
    RO(RO_LOGIC,                       RO_LOGIC_GLITCHLESS),
    RO(RO_PLACEMENT_BOSS_KEYS,         RO_DUNGEON_ITEM_ANYWHERE),
    RO(RO_PLACEMENT_SMALL_KEYS,        RO_DUNGEON_ITEM_ANYWHERE),
    RO(RO_PLACEMENT_STRAY_FAIRIES,     RO_DUNGEON_ITEM_ANYWHERE),
    RO(RO_PLENTIFUL_ITEMS,             RO_GENERIC_OFF),
    RO(RO_PURCHASE_INFINITE_RUPEES,    RO_GENERIC_OFF),
    RO(RO_SHUFFLE_BARREL_DROPS,        RO_GENERIC_OFF),
    RO(RO_SHUFFLE_BOSS_REMAINS,        RO_REMAINS_SHUFFLE_VANILLA),
    RO(RO_SHUFFLE_BOSS_SOULS,          RO_GENERIC_OFF),
    RO(RO_SHUFFLE_BUTTERFLIES,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_COWS,                RO_GENERIC_OFF),
    RO(RO_SHUFFLE_CRATE_DROPS,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_ENEMY_DROPS,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_ENEMY_SOULS,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_FREESTANDING_ITEMS,  RO_GENERIC_OFF),
    RO(RO_SHUFFLE_FROGS,               RO_GENERIC_OFF),
    RO(RO_SHUFFLE_GOLD_SKULLTULAS,     RO_GENERIC_OFF),
    RO(RO_SHUFFLE_GRASS_DROPS,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_HIVE_DROPS,          RO_GENERIC_OFF),
    RO(RO_SHUFFLE_OCARINA_BUTTONS,     RO_GENERIC_OFF),
    RO(RO_SHUFFLE_OWL_STATUES,         RO_GENERIC_OFF),
    RO(RO_SHUFFLE_POT_DROPS,           RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SHOPS,               RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SKELETON_KEY,        RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SNOWBALL_DROPS,      RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SONGS,               RO_SONG_SHUFFLE_ANYWHERE),
    RO(RO_SHUFFLE_SONG_DOUBLE_TIME,    RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SONG_INVERTED_TIME,  RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SONG_SARIA,          RO_GENERIC_OFF),
    RO(RO_SHUFFLE_SONG_SUN,            RO_GENERIC_OFF),
#ifdef COMBO_BUILD
    RO(RO_SHUFFLE_SONG_WARP_SONGS,     RO_GENERIC_OFF), // ComboShip (teleport songs)
#endif
    RO(RO_SHUFFLE_SWIM,                RO_GENERIC_OFF),
    RO(RO_SHUFFLE_TINGLE_SHOPS,        RO_GENERIC_OFF),
    RO(RO_SHUFFLE_TRAPS,               RO_GENERIC_OFF),
    RO(RO_SHUFFLE_TREE_DROPS,          RO_GENERIC_OFF),
    RO(RO_SHUFFLE_TRIFORCE_PIECES,     RO_GENERIC_OFF),
    RO(RO_SHUFFLE_TYCOON_WALLET,       RO_GENERIC_OFF),
    RO(RO_SHUFFLE_WONDER_ITEMS,        RO_GENERIC_OFF),
    RO(RO_SKULLTULA_SHUFFLED,          SPIDER_HOUSE_TOKENS_REQUIRED),
    RO(RO_SKULLTULA_TOKENS_REQUIRED,   SPIDER_HOUSE_TOKENS_REQUIRED),
    RO(RO_STARTING_CONSUMABLES,        RO_GENERIC_OFF),
    RO(RO_STARTING_HEALTH,             3),
    RO(RO_STARTING_MAPS_AND_COMPASSES, RO_GENERIC_OFF),
    RO(RO_STARTING_RUPEES,             RO_GENERIC_OFF),
    RO(RO_STRAY_FAIRIES_MAX,           STRAY_FAIRY_SCATTERED_TOTAL),
    RO(RO_STRAY_FAIRIES_REQUIRED,      STRAY_FAIRY_SCATTERED_TOTAL),
    RO(RO_TRAP_AMOUNT,                 5),
    RO(RO_TRIFORCE_PIECES_MAX,         DEFAULT_TRIFORCE_PIECES_MAX),
    RO(RO_TRIFORCE_PIECES_REQUIRED,    DEFAULT_TRIFORCE_PIECES_MAX),
#ifdef COMBO_BUILD
    // ComboShip (cross-game items): OoTMM's "Iron Boots (MM)" etc.
    RO(RO_CROSS_BOOTS_IRON,            RO_GENERIC_OFF),
    RO(RO_CROSS_BOOTS_HOVER,           RO_GENERIC_OFF),
    RO(RO_CROSS_TUNIC_GORON,           RO_GENERIC_OFF),
    RO(RO_CROSS_TUNIC_ZORA,            RO_GENERIC_OFF),
    RO(RO_CROSS_SCALES,                RO_GENERIC_OFF),
    RO(RO_CROSS_STRENGTH,              RO_GENERIC_OFF),
    RO(RO_CROSS_MASK_GERUDO,           RO_GENERIC_OFF),
    RO(RO_CROSS_MASK_SKULL,            RO_GENERIC_OFF),
    RO(RO_CROSS_MASK_SPOOKY,           RO_GENERIC_OFF),
    RO(RO_CROSS_SONG_ZELDA,            RO_GENERIC_OFF),
    RO(RO_CROSS_SPELL_FIRE,         RO_GENERIC_OFF),
    RO(RO_CROSS_SPELL_WIND,         RO_GENERIC_OFF),
    RO(RO_CROSS_SPELL_LOVE,         RO_GENERIC_OFF),
    RO(RO_CROSS_HAMMER,             RO_GENERIC_OFF),
    RO(RO_CROSS_BOOMERANG,          RO_GENERIC_OFF),
    RO(RO_CROSS_SLINGSHOT,          RO_GENERIC_OFF),
    RO(RO_CROSS_SHIELD_DEKU,        RO_GENERIC_OFF),
    RO(RO_CROSS_STONE_AGONY,        RO_GENERIC_OFF),
    RO(RO_CROSS_OCARINA_FAIRY,      RO_GENERIC_OFF),
    RO(RO_CROSS_HOOKSHOT_SHORT,     RO_GENERIC_OFF),
    RO(RO_CROSS_STICKS_NUTS,        RO_GENERIC_OFF),
    RO(RO_CROSS_GAME_FW,            RO_GENERIC_OFF),
    RO(RO_CROSS_GAME_AGE,           RO_GENERIC_OFF),
#define AGE_REQ(suffix, label, ootmmId, item, poolOption) RO(RO_AGE_REQ_##suffix, RO_AGE_ANY),
#include "2s2h/Rando/MiscBehavior/AgeReqList.h"
#undef AGE_REQ
    // Ship of Harkinian's shop prices, Infinite Upgrades, Bombchu Bag and Key Rings.
    RO(RO_SHOP_PRICES,                      RO_SHOP_PRICES_RANDOM),
    RO(RO_SHOP_PRICES_FIXED,                10), // 50 rupees
    RO(RO_SHOP_PRICES_RANGE_1,              4),  // 20 rupees
    RO(RO_SHOP_PRICES_RANGE_2,              40), // 200 rupees
    RO(RO_SHOP_PRICES_WEIGHT_FREE,          0),
    RO(RO_SHOP_PRICES_WEIGHT_STARTING_WALLET, 1),
    RO(RO_SHOP_PRICES_WEIGHT_ADULT_WALLET,  1),
    RO(RO_SHOP_PRICES_WEIGHT_GIANT_WALLET,  1),
    RO(RO_SHOP_PRICES_WEIGHT_TYCOON_WALLET, 1),
    RO(RO_SHOP_PRICES_AFFORDABLE,           RO_GENERIC_OFF),
    RO(RO_INFINITE_UPGRADES,                RO_INFINITE_UPGRADES_OFF),
    RO(RO_BOMBCHU_BAG,                      RO_BOMBCHU_BAG_NONE),
    RO(RO_KEYRINGS,                         RO_KEYRINGS_MODE_OFF),
    RO(RO_KEYRINGS_COUNT,                   2),
    RO(RO_KEYRINGS_WOODFALL,                RO_GENERIC_OFF),
    RO(RO_KEYRINGS_SNOWHEAD,                RO_GENERIC_OFF),
    RO(RO_KEYRINGS_GREAT_BAY,               RO_GENERIC_OFF),
    RO(RO_KEYRINGS_STONE_TOWER,             RO_GENERIC_OFF),
    RO(RO_CROSS_ROCS_FEATHER,               RO_GENERIC_OFF),
#endif
};
// clang-format on

RandoOptionId GetOptionIdFromName(const char* name) {
    for (auto& [randoOptionId, randoStaticOption] : Options) {
        if (strcmp(name, randoStaticOption.name) == 0) {
            return randoOptionId;
        }
    }
    return RO_MAX;
}

} // namespace StaticData

} // namespace Rando
