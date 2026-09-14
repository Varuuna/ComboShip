// combo/rando/CrossShared.h
// ComboShip: shared cross-game items (OoTMM's "sharedX" model). Header-only; compiled into soh.dll,
// 2ship.dll, comboui.dll, ComboShip.exe and comborando. Design: docs/CROSS_ITEMS_PLAN.md.
//
// A shared pair merges an item both games already have (OOT "Lens of Truth" + MM "Lens of Truth")
// into one logical item: the fill keeps one set of copies, logic credits a pickup to both games, and
// a local pickup grants the other half into the dormant game's resident save.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "CrossForeign.h" // GameId, g_comboForeignJson

namespace ComboRando {

// One shared pair. key is the spoiler key ("sharedItems": { key: true }) and the CVar suffix
// (gCombo.Rando.Shared.<key>). The merged pool keeps max(OOT copies, MM copies) of the pair, so it
// follows each game's own pool settings (OOT's pool size and Infinite Upgrades tier) without a count here.
struct CwSharedPair {
    const char* key;
    const char* label;   // menu text
    const char* desc;    // menu tooltip
    const char* ootName; // OOT English item name (Rando::StaticData::itemNameToEnum key)
    const char* mmName;  // MM friendly item name (GetItemDisplayName)
    int mmMax;           // highest level MM can hold; OOT's Infinite Upgrades tier sits one above it
};

// Row order is the settings bit order: append only. Progressive pairs share the upgrade level (quiver,
// bag, meter); ammo and magic counts stay per game (docs/CROSS_ITEMS_PLAN.md, "Counts").
inline constexpr CwSharedPair kSharedPairs[] = {
    { "lensOfTruth", "Lens of Truth", "One Lens of Truth for both games.", "Lens of Truth", "Lens of Truth", 1 },
    { "bows", "Bows", "Progressive Bow upgrades count for both games. Arrows stay per game.", "Progressive Bow",
      "Progressive Bow", 3 },
    { "bombBags", "Bomb Bags", "Progressive Bomb Bag upgrades count for both games. Bombs stay per game.",
      "Progressive Bomb Bag", "Progressive Bomb Bag", 3 },
    { "magic", "Magic", "Progressive Magic Meter upgrades count for both games. Magic stays per game.",
      "Progressive Magic Meter", "Progressive Magic", 2 },
};
inline constexpr int kSharedPairCount = static_cast<int>(sizeof(kSharedPairs) / sizeof(kSharedPairs[0]));

// Which pairs are enabled for a seed. Bit i <=> kSharedPairs[i].
struct CwSharedSettings {
    uint32_t mask = 0;

    bool Any() const {
        return mask != 0;
    }
    bool Enabled(int idx) const {
        return idx >= 0 && idx < kSharedPairCount && (mask & (1u << idx)) != 0;
    }
    void Set(int idx, bool on) {
        if (idx < 0 || idx >= kSharedPairCount)
            return;
        if (on)
            mask |= (1u << idx);
        else
            mask &= ~(1u << idx);
    }
    // Spoiler form: { "lensOfTruth": true, ... } — absent keys are off, so old seeds read as none.
    nlohmann::json ToJson() const {
        nlohmann::json j = nlohmann::json::object();
        for (int i = 0; i < kSharedPairCount; ++i)
            j[kSharedPairs[i].key] = Enabled(i);
        return j;
    }
    static CwSharedSettings FromJson(const nlohmann::json& j) {
        CwSharedSettings s;
        if (!j.is_object())
            return s;
        for (int i = 0; i < kSharedPairCount; ++i)
            s.Set(i, j.value(kSharedPairs[i].key, false));
        return s;
    }
    static CwSharedSettings FromMask(uint32_t m) {
        CwSharedSettings s;
        s.mask = m;
        return s;
    }
};

// The enabled pair whose name in `game` is `name`, else nullptr. Name-based so an unshuffled copy
// counts too.
inline const CwSharedPair* CwSharedPairForItem(const CwSharedSettings& s, GameId game, const std::string& name) {
    for (int i = 0; i < kSharedPairCount; ++i) {
        if (!s.Enabled(i))
            continue;
        const CwSharedPair& p = kSharedPairs[i];
        if (name == (game == GAME_OOT ? p.ootName : p.mmName))
            return &p;
    }
    return nullptr;
}

inline const char* CwSharedName(const CwSharedPair& p, GameId game) {
    return game == GAME_OOT ? p.ootName : p.mmName;
}

// Credit one collected/assumed item to the owned sets, mirroring an enabled pair into the other game.
// This is OoTMM's `has(X) || has(SHARED_X)` macro applied at the owned-set level, so the native logic
// graphs stay untouched. Used by the fill and the playthrough validator.
inline void CreditOwnedShared(const CwSharedSettings& s, GameId game, const std::string& name,
                              std::vector<std::string>& ootOwned, std::vector<std::string>& mmOwned) {
    (game == GAME_OOT ? ootOwned : mmOwned).push_back(name);
    if (const CwSharedPair* p = CwSharedPairForItem(s, game, name))
        (game == GAME_OOT ? mmOwned : ootOwned).push_back(CwSharedName(*p, game == GAME_OOT ? GAME_MM : GAME_OOT));
}

// Both games' names of every enabled pair (what SuffixCrossGameItems must leave untagged).
inline std::set<std::string> CwSharedNames(const CwSharedSettings& s) {
    std::set<std::string> out;
    for (int i = 0; i < kSharedPairCount; ++i) {
        if (!s.Enabled(i))
            continue;
        out.insert(kSharedPairs[i].ootName);
        out.insert(kSharedPairs[i].mmName);
    }
    return out;
}

// Spoiler key holding the settings (top-level, beside "goal"/"startingGame").
inline constexpr const char* kSharedItemsKey = "sharedItems";

inline CwSharedSettings SharedSettingsFromSpoiler(const nlohmann::json& spoiler) {
    return CwSharedSettings::FromJson(spoiler.value(kSharedItemsKey, nlohmann::json::object()));
}
inline CwSharedSettings SharedSettingsFromSpoiler(const std::string& spoilerJson) {
    try {
        return SharedSettingsFromSpoiler(nlohmann::json::parse(spoilerJson));
    } catch (...) { return {}; }
}

// DLL-side: the settings of the seed pushed via SOH_/MM_LoadComboRando. Parses the whole blob, so
// callers cache the result on their generation counter.
inline CwSharedSettings LoadSharedSettingsFromBlob() {
    if (g_comboForeignJson.empty())
        return {};
    return SharedSettingsFromSpoiler(g_comboForeignJson);
}

} // namespace ComboRando
