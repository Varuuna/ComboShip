// combo/rando/SharedItems.h
// ComboShip: OoTMM-style Shared Items — one item counts for both games. Header-only table + mask
// helpers, compiled into exe, soh.dll, 2ship.dll, comborando. See docs/deviations/rando.md.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace ComboRando {

enum SharedFamily {
    SF_BOW = 0,
    SF_BOMB_BAG,
    SF_BOMBCHU_BAG,
    SF_MAGIC,
    SF_WALLET,
    SF_HOOKSHOT,
    SF_FIRE_ARROWS,
    SF_ICE_ARROWS,
    SF_LIGHT_ARROWS,
    SF_LENS,
    SF_EPONAS_SONG,
    SF_SONG_OF_STORMS,
    SF_GORON_MASK,
    SF_ZORA_MASK,
    SF_KEATON_MASK,
    SF_BUNNY_HOOD,
    SF_MASK_OF_TRUTH,
    SF_COUNT
};

struct SharedFamilyDef {
    SharedFamily family;
    const char* key;     // spoiler/CLI key
    const char* cvar;    // menu CVar
    const char* label;   // menu checkbox label
    const char* tooltip; // OoTMM-style tooltip
    const char* ootName; // OOT pool item name
    const char* mmName;  // MM pool item name (trimmed / oracle-credited)
    int mmTierCap;       // max tiers MM can hold
    bool isMask;         // requires OOT Mask Quest = Shuffle
    bool mmHasItem;      // false = MM has no pool copy of this item (no trim, no oracle mirror)
    bool ootToMmOnly;    // true = one-way OOT->MM; MM's tier signal isn't a reliable OOT source
};

inline const SharedFamilyDef* SharedFamilyTable() {
    static const SharedFamilyDef table[SF_COUNT] = {
        { SF_BOW, "bows", "gCombo.Rando.Shared.Bows", "Shared Bows", "One Progressive Bow counts for both games.",
          "Progressive Bow", "Progressive Bow", 3, false, true, false },
        { SF_BOMB_BAG, "bombBags", "gCombo.Rando.Shared.BombBags", "Shared Bomb Bags",
          "One Progressive Bomb Bag counts for both games.", "Progressive Bomb Bag", "Progressive Bomb Bag", 3, false,
          true, false },
        { SF_BOMBCHU_BAG, "bombchuBags", "gCombo.Rando.Shared.BombchuBags", "Shared Bombchu Bags",
          "One Bombchu Bag counts for both games. Requires OOT Bombchu Bag != None.", "Bombchu Bag", "", 1, false,
          false, true },
        { SF_MAGIC, "magic", "gCombo.Rando.Shared.Magic", "Shared Magic",
          "One Progressive Magic Meter counts for both games.", "Progressive Magic Meter", "Progressive Magic", 2,
          false, true, false },
        { SF_WALLET, "wallets", "gCombo.Rando.Shared.Wallets", "Shared Wallets",
          "One Progressive Wallet counts for both games. Forces Shuffle Child Wallet off.", "Progressive Wallet",
          "Progressive Wallet", 3, false, true, false },
        { SF_HOOKSHOT, "hookshot", "gCombo.Rando.Shared.Hookshot", "Shared Hookshot",
          "One Progressive Hookshot counts for both games (MM only needs tier 1).", "Progressive Hookshot", "Hookshot",
          1, false, true, false },
        { SF_FIRE_ARROWS, "fireArrows", "gCombo.Rando.Shared.FireArrows", "Shared Fire Arrows",
          "One Fire Arrows counts for both games.", "Fire Arrows", "Fire Arrows", 1, false, true, false },
        { SF_ICE_ARROWS, "iceArrows", "gCombo.Rando.Shared.IceArrows", "Shared Ice Arrows",
          "One Ice Arrows counts for both games.", "Ice Arrows", "Ice Arrows", 1, false, true, false },
        { SF_LIGHT_ARROWS, "lightArrows", "gCombo.Rando.Shared.LightArrows", "Shared Light Arrows",
          "One Light Arrows counts for both games.", "Light Arrows", "Light Arrows", 1, false, true, false },
        { SF_LENS, "lens", "gCombo.Rando.Shared.Lens", "Shared Lens of Truth",
          "One Lens of Truth counts for both games.", "Lens of Truth", "Lens of Truth", 1, false, true, false },
        { SF_EPONAS_SONG, "eponasSong", "gCombo.Rando.Shared.EponasSong", "Shared Epona's Song",
          "One Epona's Song counts for both games.", "Epona's Song", "Epona's Song", 1, false, true, false },
        { SF_SONG_OF_STORMS, "songOfStorms", "gCombo.Rando.Shared.SongOfStorms", "Shared Song of Storms",
          "One Song of Storms counts for both games.", "Song of Storms", "Song of Storms", 1, false, true, false },
        { SF_GORON_MASK, "goronMask", "gCombo.Rando.Shared.GoronMask", "Shared Goron Mask",
          "One Goron Mask counts for both games. Requires OOT Mask Quest = Shuffle.", "Goron Mask", "Goron Mask", 1,
          true, true, false },
        { SF_ZORA_MASK, "zoraMask", "gCombo.Rando.Shared.ZoraMask", "Shared Zora Mask",
          "One Zora Mask counts for both games. Requires OOT Mask Quest = Shuffle.", "Zora Mask", "Zora Mask", 1, true,
          true, false },
        { SF_KEATON_MASK, "keatonMask", "gCombo.Rando.Shared.KeatonMask", "Shared Keaton Mask",
          "One Keaton Mask counts for both games. Requires OOT Mask Quest = Shuffle.", "Keaton Mask", "Keaton Mask", 1,
          true, true, false },
        { SF_BUNNY_HOOD, "bunnyHood", "gCombo.Rando.Shared.BunnyHood", "Shared Bunny Hood",
          "One Bunny Hood counts for both games. Requires OOT Mask Quest = Shuffle.", "Bunny Hood", "Bunny Hood", 1,
          true, true, false },
        { SF_MASK_OF_TRUTH, "maskOfTruth", "gCombo.Rando.Shared.MaskOfTruth", "Shared Mask of Truth",
          "One Mask of Truth counts for both games. Requires OOT Mask Quest = Shuffle.", "Mask of Truth",
          "Mask of Truth", 1, true, true, false },
    };
    return table;
}

inline const SharedFamilyDef& SharedFamilyByIndex(int i) {
    return SharedFamilyTable()[i];
}

inline uint32_t SharedMaskFromKeys(const nlohmann::json& keys) {
    uint32_t mask = 0;
    if (!keys.is_array())
        return mask;
    for (const auto& k : keys) {
        if (!k.is_string())
            continue;
        const std::string s = k.get<std::string>();
        for (int i = 0; i < SF_COUNT; ++i)
            if (s == SharedFamilyTable()[i].key)
                mask |= (1u << i);
    }
    return mask;
}

inline uint32_t SharedMaskAll() {
    return (SF_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << SF_COUNT) - 1u);
}

// Names to leave untagged by SuffixCrossGameItems: both games' pool name for every effective family.
inline std::set<std::string> SharedUntaggedNames(uint32_t mask) {
    std::set<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i) {
        if (!(mask & (1u << i)))
            continue;
        out.insert(SharedFamilyTable()[i].ootName);
        if (SharedFamilyTable()[i].mmHasItem)
            out.insert(SharedFamilyTable()[i].mmName);
    }
    return out;
}

inline std::vector<std::string> SharedKeysFromMask(uint32_t mask) {
    std::vector<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i)
        if (mask & (1u << i))
            out.push_back(SharedFamilyTable()[i].key);
    return out;
}

} // namespace ComboRando
