#include "ShuffleTradeItems.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"

#ifdef COMBO_BUILD
// ComboShip (cross-game items): the child trade cycle also holds MM's Blast/Stone/Kamaro Masks (worn in OOT),
// after the vanilla Weird Egg .. Mask of Truth run. Index space: 0..10 vanilla, 11..13 combo masks.
#define CHILD_TRADE_VANILLA_COUNT (ITEM_MASK_TRUTH - ITEM_WEIRD_EGG + 1)
#define CHILD_TRADE_COUNT (CHILD_TRADE_VANILLA_COUNT + 3)

static u8 ChildTradeItemAt(int index) {
    return index < CHILD_TRADE_VANILLA_COUNT ? (u8)(ITEM_WEIRD_EGG + index)
                                             : (u8)(ITEM_COMBO_MASK_BLAST + (index - CHILD_TRADE_VANILLA_COUNT));
}

static int ChildTradeOwnedAt(int index) {
    return index < CHILD_TRADE_VANILLA_COUNT
               ? Flags_GetRandomizerInf(RAND_INF_CHILD_TRADES_HAS_WEIRD_EGG + index)
               : Flags_GetRandomizerInf(RAND_INF_OBTAINED_COMBO_MASK_BLAST + (index - CHILD_TRADE_VANILLA_COUNT));
}

static int ChildTradeIndexOf(u8 item) {
    if (item >= ITEM_COMBO_MASK_BLAST && item <= ITEM_COMBO_MASK_KAMARO) {
        return CHILD_TRADE_VANILLA_COUNT + (item - ITEM_COMBO_MASK_BLAST);
    }
    // Vanilla behavior for everything else (ITEM_NONE wraps like before).
    return (u8)(item - ITEM_WEIRD_EGG);
}

u8 Randomizer_GetNextChildTradeItem() {
    int current = ChildTradeIndexOf(INV_CONTENT(ITEM_TRADE_CHILD));
    for (int i = 0; i < CHILD_TRADE_COUNT; i++) {
        int index = (current + i + 1) % CHILD_TRADE_COUNT;
        if (ChildTradeOwnedAt(index)) {
            return ChildTradeItemAt(index);
        }
    }
    return ITEM_NONE;
}

u8 Randomizer_GetPrevChildTradeItem() {
    int current = ChildTradeIndexOf(INV_CONTENT(ITEM_TRADE_CHILD)) % CHILD_TRADE_COUNT;
    for (int i = 0; i < CHILD_TRADE_COUNT; i++) {
        int index = (current - i - 1 + CHILD_TRADE_COUNT) % CHILD_TRADE_COUNT;
        if (ChildTradeOwnedAt(index)) {
            return ChildTradeItemAt(index);
        }
    }
    return ITEM_NONE;
}
#else
u8 Randomizer_GetNextChildTradeItem() {
    const u8 numTradeItems = ITEM_MASK_TRUTH - ITEM_WEIRD_EGG + 1;
    u8 currentTradeItemIndex = INV_CONTENT(ITEM_TRADE_CHILD) - ITEM_WEIRD_EGG;
    for (int i = 0; i < numTradeItems; i++) {
        u8 tradeIndex = (currentTradeItemIndex + i + 1) % numTradeItems;
        if (Flags_GetRandomizerInf(tradeIndex + RAND_INF_CHILD_TRADES_HAS_WEIRD_EGG)) {
            return ITEM_WEIRD_EGG + tradeIndex;
        }
    }
    return ITEM_NONE;
}

u8 Randomizer_GetPrevChildTradeItem() {
    const u8 numTradeItems = ITEM_MASK_TRUTH - ITEM_WEIRD_EGG + 1;
    u8 currentTradeItemIndex = INV_CONTENT(ITEM_TRADE_CHILD) - ITEM_WEIRD_EGG;
    for (int i = 0; i < numTradeItems; i++) {
        u8 tradeIndex = (currentTradeItemIndex - i - 1 + numTradeItems) % numTradeItems;
        if (Flags_GetRandomizerInf(tradeIndex + RAND_INF_CHILD_TRADES_HAS_WEIRD_EGG)) {
            return ITEM_WEIRD_EGG + tradeIndex;
        }
    }
    return ITEM_NONE;
}

#endif

u8 Randomizer_GetNextAdultTradeItem() {
    const u8 numTradeItems = ITEM_CLAIM_CHECK - ITEM_POCKET_EGG + 1;
    u8 currentTradeItemIndex = INV_CONTENT(ITEM_TRADE_ADULT) - ITEM_POCKET_EGG;
    for (int i = 0; i < numTradeItems; i++) {
        u8 tradeIndex = (currentTradeItemIndex + i + 1) % numTradeItems;
        if (Flags_GetRandomizerInf(tradeIndex + RAND_INF_ADULT_TRADES_HAS_POCKET_EGG)) {
            return ITEM_POCKET_EGG + tradeIndex;
        }
    }
    return ITEM_NONE;
}

u8 Randomizer_GetPrevAdultTradeItem() {
    const u8 numTradeItems = ITEM_CLAIM_CHECK - ITEM_POCKET_EGG + 1;
    u8 currentTradeItemIndex = INV_CONTENT(ITEM_TRADE_ADULT) - ITEM_POCKET_EGG;
    for (int i = 0; i < numTradeItems; i++) {
        u8 tradeIndex = (currentTradeItemIndex - i - 1 + numTradeItems) % numTradeItems;
        if (Flags_GetRandomizerInf(tradeIndex + RAND_INF_ADULT_TRADES_HAS_POCKET_EGG)) {
            return ITEM_POCKET_EGG + tradeIndex;
        }
    }
    return ITEM_NONE;
}
