// mm/2s2h/Rando/Logic/ShopPrices.cpp
// ComboShip: Ship of Harkinian's shop price settings (soh's 3drando/shops.cpp GetRandomPrice), for Majora's Mask's
// shops and Tingle's maps. Random is 2Ship's own roll (0-199 for every check) and stays the default.
//
// - Vanilla: MM's own prices (the Goron Shop's winter ones).
// - Cheap Balanced / Balanced: Ship of Harkinian's weighted rolls (average ~31 / ~126 rupees).
// - Fixed / Range: a set price, or a range, in steps of 5.
// - Set By Wallet: weights for free, the starting wallet (1-99), the Adult's (100-200), the Giant's (201-500) and
//   the Tycoon's (501-999) Wallets.
// - Affordable (any but Fixed): the cheapest price of the wallet a roll needs.
//
// Prices are capped at the biggest wallet the seed has (999 with the Tycoon's Wallet shuffled, else 500), and logic
// asks for the wallet a price needs (Logic.h CAN_AFFORD).
#ifdef COMBO_BUILD

#include "Logic.h"
#include <array>
#include <unordered_map>

extern "C" {
#include "ShipUtils.h"
}

namespace Rando {

namespace Logic {

namespace {

// MM's prices, read from its shop messages.
const std::unordered_map<RandoCheckId, uint16_t> kVanillaPrices = {
    { RC_BOMB_SHOP_ITEM_01, 30 },
    { RC_BOMB_SHOP_ITEM_02, 40 },
    { RC_BOMB_SHOP_ITEM_03, 50 },
    { RC_BOMB_SHOP_ITEM_04_OR_CURIOSITY_SHOP_ITEM, 90 },
    { RC_CURIOSITY_SHOP_SPECIAL_ITEM, 500 },
    { RC_TRADING_POST_SHOP_ITEM_01, 30 },
    { RC_TRADING_POST_SHOP_ITEM_02, 30 },
    { RC_TRADING_POST_SHOP_ITEM_03, 80 },
    { RC_TRADING_POST_SHOP_ITEM_04, 50 },
    { RC_TRADING_POST_SHOP_ITEM_05, 10 },
    { RC_TRADING_POST_SHOP_ITEM_06, 30 },
    { RC_TRADING_POST_SHOP_ITEM_07, 30 },
    { RC_TRADING_POST_SHOP_ITEM_08, 40 },
    { RC_HAGS_POTION_SHOP_ITEM_01, 60 },
    { RC_HAGS_POTION_SHOP_ITEM_02, 10 },
    { RC_HAGS_POTION_SHOP_ITEM_03, 20 },
    { RC_GORON_SHOP_ITEM_01, 40 },
    { RC_GORON_SHOP_ITEM_02, 40 },
    { RC_GORON_SHOP_ITEM_03, 80 },
    { RC_ZORA_SHOP_ITEM_01, 90 },
    { RC_ZORA_SHOP_ITEM_02, 20 },
    { RC_ZORA_SHOP_ITEM_03, 60 },
    { RC_MILK_BAR_PURCHASE_MILK, 20 },
    { RC_MILK_BAR_PURCHASE_CHATEAU, 200 },
    { RC_GORMAN_MILK_PURCHASE, 50 },
    // Tingle: the map of where he floats is 20 (Clock Town's 5), the other one 40.
    { RC_CLOCK_TOWN_NORTH_TINGLE_MAP_01, 5 },
    { RC_CLOCK_TOWN_NORTH_TINGLE_MAP_02, 40 },
    { RC_ROAD_TO_SOUTHERN_SWAMP_TINGLE_MAP_01, 20 },
    { RC_ROAD_TO_SOUTHERN_SWAMP_TINGLE_MAP_02, 40 },
    { RC_TWIN_ISLANDS_TINGLE_MAP_01, 20 },
    { RC_TWIN_ISLANDS_TINGLE_MAP_02, 40 },
    { RC_MILK_ROAD_TINGLE_MAP_01, 20 },
    { RC_MILK_ROAD_TINGLE_MAP_02, 40 },
    { RC_GREAT_BAY_COAST_TINGLE_MAP_01, 20 },
    { RC_GREAT_BAY_COAST_TINGLE_MAP_02, 40 },
    { RC_IKANA_CANYON_TINGLE_MAP_01, 20 },
    { RC_IKANA_CANYON_TINGLE_MAP_02, 40 },
};

// Ship of Harkinian's tables: a beta distribution over 0-295 (Balanced) and 0-95 (Cheap Balanced) in steps of 5.
constexpr std::array<double, 60> kBalanced = {
    0.005326994, 0.014908518, 0.027114719, 0.041315285, 0.057136304, 0.074325887, 0.092667151, 0.112002061, 0.132198214,
    0.153125390, 0.174696150, 0.196810540, 0.219388148, 0.242361379, 0.265657012, 0.289205134, 0.312970402, 0.336877590,
    0.360881110, 0.384932772, 0.408976198, 0.432982176, 0.456902494, 0.480686053, 0.504313389, 0.527746488, 0.550938554,
    0.573856910, 0.596465330, 0.618736235, 0.640646600, 0.662162782, 0.683240432, 0.703859801, 0.724001242, 0.743631336,
    0.762722631, 0.781259986, 0.799198449, 0.816521905, 0.833208595, 0.849243398, 0.864579161, 0.879211177, 0.893112051,
    0.906263928, 0.918639420, 0.930222611, 0.940985829, 0.950914731, 0.959992180, 0.968187000, 0.975495390, 0.981884488,
    0.987344345, 0.991851853, 0.995389113, 0.997937921, 0.999481947, 1.000000000,
};
constexpr std::array<double, 20> kCheapBalanced = {
    0.097500187, 0.190002748, 0.277509301, 0.360018376, 0.437522571, 0.510021715, 0.577520272,
    0.640029304, 0.697527584, 0.750024535, 0.797518749, 0.840011707, 0.877508776, 0.910010904,
    0.937504342, 0.960004661, 0.977502132, 0.989998967, 0.997500116, 1.000000000,
};

template <size_t N> uint16_t RollTable(const std::array<double, N>& table) {
    double roll = Ship_Random(0, 1000000) / 1000000.0;
    for (size_t i = 0; i < N; i++) {
        if (roll < table[i]) {
            return static_cast<uint16_t>(i * 5);
        }
    }
    return static_cast<uint16_t>((N - 1) * 5);
}

// Inclusive range.
uint16_t RollBetween(uint16_t low, uint16_t high) {
    if (low > high) {
        std::swap(low, high);
    }
    return static_cast<uint16_t>(Ship_Random(low, high + 1));
}

uint16_t RollByWallet(const RandoSaveInfo& saveInfo, bool tycoon) {
    const uint32_t* o = saveInfo.randoSaveOptions;
    uint32_t weights[] = {
        o[RO_SHOP_PRICES_WEIGHT_FREE],
        o[RO_SHOP_PRICES_WEIGHT_STARTING_WALLET],
        o[RO_SHOP_PRICES_WEIGHT_ADULT_WALLET],
        o[RO_SHOP_PRICES_WEIGHT_GIANT_WALLET],
        tycoon ? o[RO_SHOP_PRICES_WEIGHT_TYCOON_WALLET] : 0,
    };
    uint32_t total = 0;
    for (uint32_t w : weights) {
        total += w;
    }
    if (total == 0) {
        return RollBetween(0, tycoon ? 999 : 500); // no weights: any price a wallet holds
    }
    int32_t pick = Ship_Random(0, (s32)total);
    size_t tier = 0;
    while (tier < 4 && pick >= (int32_t)weights[tier]) {
        pick -= weights[tier];
        tier++;
    }
    switch (tier) {
        case 0:
            return 0;
        case 1:
            return RollBetween(1, 99);
        case 2:
            return RollBetween(100, 200);
        case 3:
            return RollBetween(201, 500);
        default:
            return RollBetween(501, 999);
    }
}

} // namespace

uint16_t RollCheckPrice(const RandoSaveInfo& saveInfo, RandoCheckId randoCheckId) {
    RandoCheckType type = Rando::StaticData::Checks[randoCheckId].randoCheckType;
    uint32_t mode = saveInfo.randoSaveOptions[RO_SHOP_PRICES];
    // 2Ship rolls a price for every check it visits; only shops and Tingle read it. Keep that roll (and so the RNG
    // stream) for everything but the purchases a non-default setting prices.
    if ((type != RCTYPE_SHOP && type != RCTYPE_TINGLE_SHOP) || mode == RO_SHOP_PRICES_RANDOM) {
        return static_cast<uint16_t>(Ship_Random(0, 200));
    }

    bool tycoon = saveInfo.randoSaveOptions[RO_SHUFFLE_TYCOON_WALLET] == RO_GENERIC_YES;
    uint16_t price;
    switch (mode) {
        case RO_SHOP_PRICES_VANILLA: {
            auto it = kVanillaPrices.find(randoCheckId);
            price = (it != kVanillaPrices.end()) ? it->second : 20;
            break;
        }
        case RO_SHOP_PRICES_CHEAP_BALANCED:
            price = RollTable(kCheapBalanced);
            break;
        case RO_SHOP_PRICES_BALANCED:
            price = RollTable(kBalanced);
            break;
        case RO_SHOP_PRICES_FIXED_PRICE:
            price = static_cast<uint16_t>(saveInfo.randoSaveOptions[RO_SHOP_PRICES_FIXED] * 5);
            break;
        case RO_SHOP_PRICES_RANGE:
            price = RollBetween(static_cast<uint16_t>(saveInfo.randoSaveOptions[RO_SHOP_PRICES_RANGE_1] * 5),
                                static_cast<uint16_t>(saveInfo.randoSaveOptions[RO_SHOP_PRICES_RANGE_2] * 5));
            break;
        case RO_SHOP_PRICES_SET_BY_WALLET:
            price = RollByWallet(saveInfo, tycoon);
            break;
        default:
            price = static_cast<uint16_t>(Ship_Random(0, 200));
            break;
    }

    if (saveInfo.randoSaveOptions[RO_SHOP_PRICES_AFFORDABLE] == RO_GENERIC_ON && mode != RO_SHOP_PRICES_FIXED_PRICE) {
        price = (price > 500) ? 505 : (price > 200) ? 205 : (price > 99) ? 100 : (price > 0) ? 5 : 0;
    }

    // Never more than the seed's biggest wallet holds.
    uint16_t cap = tycoon ? 999 : 500;
    return (price > cap) ? cap : price;
}

} // namespace Logic

} // namespace Rando

#endif
