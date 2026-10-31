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
    // Cross-game teleport songs (see "Cross-game teleport songs" in docs/deviations/rando.md).
    SF_SONG_OF_SOARING,
    SF_MINUET_OF_FOREST, // the six warp songs stay contiguous and in OOT warp-index order
    SF_BOLERO_OF_FIRE,
    SF_SERENADE_OF_WATER,
    SF_REQUIEM_OF_SPIRIT,
    SF_NOCTURNE_OF_SHADOW,
    SF_PRELUDE_OF_LIGHT,
    // Cross-game items: OOT gear usable in MM (MM's "Iron Boots (MM)" etc., mm/2s2h/Rando/MiscBehavior/CrossGear.cpp).
    SF_IRON_BOOTS,
    SF_HOVER_BOOTS,
    SF_GORON_TUNIC,
    SF_ZORA_TUNIC,
    SF_SCALES,
    SF_STRENGTH,
    SF_GERUDO_MASK,
    SF_SKULL_MASK,
    SF_SPOOKY_MASK,
    SF_SUNS_SONG,
    SF_SARIAS_SONG,
    // MM masks worn in OOT (OOT's copy exists with the matching OOT option on).
    SF_BLAST_MASK,
    SF_STONE_MASK,
    SF_KAMARO_MASK,
    // Songs playable in the other game (OOT's copy of an MM song needs OOT's "MM Songs in OoT"; MM's Zelda's
    // Lullaby needs MM's matching option).
    SF_ELEGY,
    SF_SONG_OF_HEALING,
    SF_SONATA,
    SF_BOSSA_NOVA,
    SF_OATH,
    SF_ZELDAS_LULLABY,
    // OOT spells cast in MM (MM's copy needs MM's matching option).
    SF_DINS_FIRE,
    SF_FARORES_WIND,
    SF_NAYRUS_LOVE,
    // OOT tools used in MM (MM's copy needs MM's matching option). No Deku Shield family: OOT's burns and is
    // re-bought, and a shared one would keep coming back.
    SF_HAMMER,
    SF_BOOMERANG,
    SF_SLINGSHOT,
    // OOT items in MM (MM's copy needs MM's matching option; mm/2s2h/Rando/MiscBehavior/CrossUpgrades.cpp).
    SF_STONE_OF_AGONY,
    SF_STICK_UPGRADES,
    SF_NUT_UPGRADES,
    // OoTMM's "Shared Child Swords": OOT's Kokiri Sword + Extra Child Swords and MM's Progressive Sword.
    SF_CHILD_SWORDS,
    SF_COUNT
};
// Family bitmask (bit i = SharedFamily i). 64 bits since the cross-game item families (was uint32_t);
// spoilers store family KEYS, never the integer, so the width is not a save/seed format.
using SharedMask = uint64_t;
inline constexpr SharedMask SharedBit(int family) {
    return SharedMask{ 1 } << family;
}
static_assert(SF_COUNT <= 64, "Shared Items mask is 64 bits wide");

inline bool IsTeleportSong(SharedFamily f) {
    return f >= SF_SONG_OF_SOARING && f <= SF_PRELUDE_OF_LIGHT;
}
// Guest = the game that only has the song through ComboShip. Soaring is MM's; the warp songs are OOT's.
inline bool TeleportSongGuestIsOot(SharedFamily f) {
    return f == SF_SONG_OF_SOARING;
}

struct SharedFamilyDef {
    SharedFamily family;
    const char* key;     // spoiler/CLI key
    const char* cvar;    // menu CVar
    const char* label;   // menu checkbox label
    const char* tooltip; // OoTMM-style tooltip
    const char* ootName; // OOT pool item name
    const char* mmName;  // MM pool item name (trimmed / oracle-credited)
    int mmTierCap;       // max tiers MM can hold
    bool isMask;         // requires OOT Shuffle Masks
    bool mmHasItem;      // false = MM has no pool copy of this item (no trim, no oracle mirror)
    bool ootToMmOnly;    // true = one-way OOT->MM; MM's tier signal isn't a reliable OOT source
    // A second OOT pool name counted as the same family (OOT's Kokiri Sword beside its Progressive Kokiri Swords).
    // The family is only effective when OOT's pool has the main ootName: the alt name alone never shares it.
    const char* ootAltName = nullptr;
};

// The six warp-song rows share these (one menu toggle for the group).
inline constexpr const char* kSharedWarpSongsCVar = "gCombo.Rando.Shared.WarpSongs";
inline constexpr const char* kSharedWarpSongsLabel = "Shared Warp Songs";
inline constexpr const char* kSharedWarpSongsTooltip =
    "One copy of each Ocarina of Time warp song counts for both games. Requires MM's OoT Warp Songs.";

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
        // Two MM tiers only with MM's Short Hookshot (Short Hookshot, Hookshot); otherwise MM's Hookshot comes with the
        // first copy and the second is ignored there (MM_GetSharedTier / MM_RaiseSharedTier).
        { SF_HOOKSHOT, "hookshot", "gCombo.Rando.Shared.Hookshot", "Shared Hookshot",
          "One Progressive Hookshot counts for both games. MM gets its Hookshot with the first one, or with MM's Short "
          "Hookshot on, the Short Hookshot and then the Hookshot.",
          "Progressive Hookshot", "Hookshot", 2, false, true, false },
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
          "One Goron Mask counts for both games. Requires OOT Shuffle Masks.", "Goron Mask", "Goron Mask", 1, true,
          true, false },
        { SF_ZORA_MASK, "zoraMask", "gCombo.Rando.Shared.ZoraMask", "Shared Zora Mask",
          "One Zora Mask counts for both games. Requires OOT Shuffle Masks.", "Zora Mask", "Zora Mask", 1, true, true,
          false },
        { SF_KEATON_MASK, "keatonMask", "gCombo.Rando.Shared.KeatonMask", "Shared Keaton Mask",
          "One Keaton Mask counts for both games. Requires OOT Shuffle Masks.", "Keaton Mask", "Keaton Mask", 1, true,
          true, false },
        { SF_BUNNY_HOOD, "bunnyHood", "gCombo.Rando.Shared.BunnyHood", "Shared Bunny Hood",
          "One Bunny Hood counts for both games. Requires OOT Shuffle Masks.", "Bunny Hood", "Bunny Hood", 1, true,
          true, false },
        { SF_MASK_OF_TRUTH, "maskOfTruth", "gCombo.Rando.Shared.MaskOfTruth", "Shared Mask of Truth",
          "One Mask of Truth counts for both games. Requires OOT Shuffle Masks.", "Mask of Truth", "Mask of Truth", 1,
          true, true, false },
        // Teleport songs need their feature on in both games; the six warp rows share one CVar.
        { SF_SONG_OF_SOARING, "songOfSoaring", "gCombo.Rando.Shared.SongOfSoaring", "Shared Song of Soaring",
          "One Song of Soaring counts for both games. Requires OOT's Song of Soaring (soar to Termina).",
          "Song of Soaring", "Song of Soaring", 1, false, true, false },
        { SF_MINUET_OF_FOREST, "minuetOfForest", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Minuet of Forest", "Minuet of Forest", 1, false, true, false },
        { SF_BOLERO_OF_FIRE, "boleroOfFire", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Bolero of Fire", "Bolero of Fire", 1, false, true, false },
        { SF_SERENADE_OF_WATER, "serenadeOfWater", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Serenade of Water", "Serenade of Water", 1, false, true, false },
        { SF_REQUIEM_OF_SPIRIT, "requiemOfSpirit", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Requiem of Spirit", "Requiem of Spirit", 1, false, true, false },
        { SF_NOCTURNE_OF_SHADOW, "nocturneOfShadow", kSharedWarpSongsCVar, kSharedWarpSongsLabel,
          kSharedWarpSongsTooltip, "Nocturne of Shadow", "Nocturne of Shadow", 1, false, true, false },
        { SF_PRELUDE_OF_LIGHT, "preludeOfLight", kSharedWarpSongsCVar, kSharedWarpSongsLabel, kSharedWarpSongsTooltip,
          "Prelude of Light", "Prelude of Light", 1, false, true, false },
        // Cross-game items: MM's copy only exists with MM's matching Ocarina of Time Gear option on (otherwise
        // the family logs "MM pool has none" and stays unshared).
        { SF_IRON_BOOTS, "bootsIron", "gCombo.Rando.Shared.BootsIron", "Shared Iron Boots",
          "One Iron Boots counts for both games. Requires MM's Iron Boots (Ocarina of Time Gear).", "Iron Boots",
          "Iron Boots", 1, false, true, false },
        { SF_HOVER_BOOTS, "bootsHover", "gCombo.Rando.Shared.BootsHover", "Shared Hover Boots",
          "One Hover Boots counts for both games. Requires MM's Hover Boots (Ocarina of Time Gear).", "Hover Boots",
          "Hover Boots", 1, false, true, false },
        { SF_GORON_TUNIC, "tunicGoron", "gCombo.Rando.Shared.TunicGoron", "Shared Goron Tunic",
          "One Goron Tunic counts for both games. Requires MM's Goron Tunic (Ocarina of Time Gear).", "Goron Tunic",
          "Goron Tunic", 1, false, true, false },
        { SF_ZORA_TUNIC, "tunicZora", "gCombo.Rando.Shared.TunicZora", "Shared Zora Tunic",
          "One Zora Tunic counts for both games. Requires MM's Zora Tunic (Ocarina of Time Gear).", "Zora Tunic",
          "Zora Tunic", 1, false, true, false },
        { SF_SCALES, "scales", "gCombo.Rando.Shared.Scales", "Shared Scales",
          "One Progressive Scale counts for both games. Requires MM's Scales (Ocarina of Time Gear).",
          "Progressive Scale", "Progressive Scale", 2, false, true, false },
        { SF_STRENGTH, "strength", "gCombo.Rando.Shared.Strength", "Shared Strength",
          "One Strength Upgrade counts for both games. Requires MM's Strength (Ocarina of Time Gear).",
          "Strength Upgrade", "Strength Upgrade", 3, false, true, false },
        { SF_GERUDO_MASK, "maskGerudo", "gCombo.Rando.Shared.MaskGerudo", "Shared Gerudo Mask",
          "One Gerudo Mask counts for both games. Requires OOT Shuffle Masks and MM's Gerudo Mask (Ocarina of "
          "Time Gear).",
          "Gerudo Mask", "Gerudo Mask", 1, true, true, false },
        { SF_SKULL_MASK, "maskSkull", "gCombo.Rando.Shared.MaskSkull", "Shared Skull Mask",
          "One Skull Mask counts for both games. Requires OOT Shuffle Masks and MM's Skull Mask (Ocarina of "
          "Time Gear).",
          "Skull Mask", "Skull Mask", 1, true, true, false },
        { SF_SPOOKY_MASK, "maskSpooky", "gCombo.Rando.Shared.MaskSpooky", "Shared Spooky Mask",
          "One Spooky Mask counts for both games. Requires OOT Shuffle Masks and MM's Spooky Mask (Ocarina of "
          "Time Gear).",
          "Spooky Mask", "Spooky Mask", 1, true, true, false },
        { SF_SUNS_SONG, "songSun", "gCombo.Rando.Shared.SongSun", "Shared Sun's Song",
          "One Sun's Song counts for both games. Requires MM's Sun's Song shuffle.", "Sun's Song", "Sun's Song", 1,
          false, true, false },
        { SF_SARIAS_SONG, "songSaria", "gCombo.Rando.Shared.SongSaria", "Shared Saria's Song",
          "One Saria's Song counts for both games. Requires MM's Saria's Song shuffle (its copies are replaced "
          "by the shared one).",
          "Saria's Song", "Saria's Song", 1, false, true, false },
        // MM masks in OOT: OOT's copy only exists with OOT's matching "MM Masks in OoT" option on (otherwise the
        // family logs "OOT pool has none" and stays unshared).
        { SF_BLAST_MASK, "maskBlast", "gCombo.Rando.Shared.MaskBlast", "Shared Blast Mask",
          "One Blast Mask counts for both games. Requires OOT's Blast Mask (MM Masks in OoT).", "Blast Mask",
          "Blast Mask", 1, false, true, false },
        { SF_STONE_MASK, "maskStone", "gCombo.Rando.Shared.MaskStone", "Shared Stone Mask",
          "One Stone Mask counts for both games. Requires OOT's Stone Mask (MM Masks in OoT).", "Stone Mask",
          "Stone Mask", 1, false, true, false },
        { SF_KAMARO_MASK, "maskKamaro", "gCombo.Rando.Shared.MaskKamaro", "Shared Kamaro's Mask",
          "One Kamaro's Mask counts for both games. Requires OOT's Kamaro's Mask (MM Masks in OoT).", "Kamaro's Mask",
          "Kamaro's Mask", 1, false, true, false },
        { SF_ELEGY, "songElegy", "gCombo.Rando.Shared.SongElegy", "Shared Elegy of Emptiness",
          "One Elegy of Emptiness counts for both games. Requires OOT's MM Songs in OoT.", "Elegy of Emptiness",
          "Elegy of Emptiness", 1, false, true, false },
        { SF_SONG_OF_HEALING, "songHealing", "gCombo.Rando.Shared.SongHealing", "Shared Song of Healing",
          "One Song of Healing counts for both games. Requires OOT's MM Songs in OoT.", "Song of Healing",
          "Song of Healing", 1, false, true, false },
        { SF_SONATA, "songAwakening", "gCombo.Rando.Shared.SongAwakening", "Shared Sonata of Awakening",
          "One Sonata of Awakening counts for both games. Requires OOT's MM Songs in OoT.", "Sonata of Awakening",
          "Sonata of Awakening", 1, false, true, false },
        { SF_BOSSA_NOVA, "songZora", "gCombo.Rando.Shared.SongZora", "Shared New Wave Bossa Nova",
          "One New Wave Bossa Nova counts for both games. Requires OOT's MM Songs in OoT.", "New Wave Bossa Nova",
          "New Wave Bossa Nova", 1, false, true, false },
        { SF_OATH, "songOrder", "gCombo.Rando.Shared.SongOrder", "Shared Oath to Order",
          "One Oath to Order counts for both games. Requires OOT's MM Songs in OoT.", "Oath to Order", "Oath to Order",
          1, false, true, false },
        { SF_ZELDAS_LULLABY, "songZeldaLullaby", "gCombo.Rando.Shared.SongZeldaLullaby", "Shared Zelda's Lullaby",
          "One Zelda's Lullaby counts for both games. Requires MM's Zelda's Lullaby (Ocarina of Time Gear).",
          "Zelda's Lullaby", "Zelda's Lullaby", 1, false, true, false },
        { SF_DINS_FIRE, "spellFire", "gCombo.Rando.Shared.SpellFire", "Shared Din's Fire",
          "One Din's Fire counts for both games. Requires MM's Din's Fire (Ocarina of Time Gear).", "Din's Fire",
          "Din's Fire", 1, false, true, false },
        { SF_FARORES_WIND, "spellWind", "gCombo.Rando.Shared.SpellWind", "Shared Farore's Wind",
          "One Farore's Wind counts for both games. Requires MM's Farore's Wind (Ocarina of Time Gear).",
          "Farore's Wind", "Farore's Wind", 1, false, true, false },
        { SF_NAYRUS_LOVE, "spellLove", "gCombo.Rando.Shared.SpellLove", "Shared Nayru's Love",
          "One Nayru's Love counts for both games. Requires MM's Nayru's Love (Ocarina of Time Gear).", "Nayru's Love",
          "Nayru's Love", 1, false, true, false },
        { SF_HAMMER, "hammer", "gCombo.Rando.Shared.Hammer", "Shared Megaton Hammer",
          "One Megaton Hammer counts for both games. Requires MM's Megaton Hammer (Ocarina of Time Gear).",
          "Megaton Hammer", "Megaton Hammer", 1, false, true, false },
        { SF_BOOMERANG, "boomerang", "gCombo.Rando.Shared.Boomerang", "Shared Boomerang",
          "One Boomerang counts for both games. Requires MM's Boomerang (Ocarina of Time Gear).", "Boomerang",
          "Boomerang", 1, false, true, false },
        { SF_SLINGSHOT, "slingshot", "gCombo.Rando.Shared.Slingshot", "Shared Slingshot",
          "One Progressive Slingshot counts for both games (MM only needs the Fairy Slingshot). Requires MM's Fairy "
          "Slingshot (Ocarina of Time Gear).",
          "Progressive Slingshot", "Fairy Slingshot", 1, false, true, false },
        { SF_STONE_OF_AGONY, "stoneAgony", "gCombo.Rando.Shared.StoneAgony", "Shared Stone of Agony",
          "One Stone of Agony counts for both games. Requires MM's Stone of Agony (Ocarina of Time Gear).",
          "Stone of Agony", "Stone of Agony", 1, false, true, false },
        // MM's stick/nut capacity starts one tier up (10 sticks, 20 nuts): tier n is OOT's n-th Progressive upgrade
        // past the starting capacity (20/30 sticks, 30/40 nuts) in both games.
        { SF_STICK_UPGRADES, "stickUpgrades", "gCombo.Rando.Shared.NutsSticks", "Shared Deku Sticks & Nuts",
          "One Deku Stick or Deku Nut capacity upgrade counts for both games. Requires MM's Deku Stick & Nut "
          "Upgrades (Ocarina of Time Gear).",
          "Progressive Stick Capacity", "Progressive Stick Capacity", 2, false, true, false },
        { SF_NUT_UPGRADES, "nutUpgrades", "gCombo.Rando.Shared.NutsSticks", "Shared Deku Sticks & Nuts",
          "One Deku Stick or Deku Nut capacity upgrade counts for both games. Requires MM's Deku Stick & Nut "
          "Upgrades (Ocarina of Time Gear).",
          "Progressive Nut Capacity", "Progressive Nut Capacity", 2, false, true, false },
        // OOT's Kokiri Sword and its two Progressive Kokiri Swords (Extra Child Swords) are the three tiers; MM's three
        // Progressive Swords are trimmed. Without Extra Child Swords OOT has a single sword tier and MM's Razor and
        // Gilded Swords would be lost, so like OoTMM it needs that option (OOT's pool has no Progressive Kokiri Sword
        // without it, and the family stays unshared).
        { SF_CHILD_SWORDS, "childSwords", "gCombo.Rando.Shared.ChildSwords", "Shared Kokiri Sword",
          "One Kokiri Sword, Razor Sword and Gilded Sword count for both games: three progressive child swords. "
          "Requires Ocarina of Time's Extra Child Swords.",
          "Progressive Kokiri Sword", "Progressive Sword", 3, false, true, false, "Kokiri Sword" },
    };
    return table;
}

inline const SharedFamilyDef& SharedFamilyByIndex(int i) {
    return SharedFamilyTable()[i];
}

// True for any OOT pool name of a family (its ootName or ootAltName).
inline bool SharedFamilyOotNameMatches(const SharedFamilyDef& def, const std::string& name) {
    return name == def.ootName || (def.ootAltName != nullptr && name == def.ootAltName);
}

// How many of `names` are the family's OOT items (either name).
inline size_t SharedFamilyOotCount(const SharedFamilyDef& def, const std::vector<std::string>& names) {
    size_t n = 0;
    for (const auto& name : names)
        if (SharedFamilyOotNameMatches(def, name))
            ++n;
    return n;
}

inline SharedMask SharedMaskFromKeys(const nlohmann::json& keys) {
    SharedMask mask = 0;
    if (!keys.is_array())
        return mask;
    for (const auto& k : keys) {
        if (!k.is_string())
            continue;
        const std::string s = k.get<std::string>();
        for (int i = 0; i < SF_COUNT; ++i)
            if (s == SharedFamilyTable()[i].key)
                mask |= SharedBit(i);
    }
    return mask;
}

inline SharedMask SharedMaskAll() {
    return (SF_COUNT >= 64) ? ~SharedMask{ 0 } : (SharedBit(SF_COUNT) - 1);
}

// Names to leave untagged by SuffixCrossGameItems: both games' pool name for every effective family.
inline std::set<std::string> SharedUntaggedNames(SharedMask mask) {
    std::set<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i) {
        if (!(mask & SharedBit(i)))
            continue;
        out.insert(SharedFamilyTable()[i].ootName);
        if (SharedFamilyTable()[i].ootAltName != nullptr)
            out.insert(SharedFamilyTable()[i].ootAltName);
        if (SharedFamilyTable()[i].mmHasItem)
            out.insert(SharedFamilyTable()[i].mmName);
    }
    return out;
}

inline std::vector<std::string> SharedKeysFromMask(SharedMask mask) {
    std::vector<std::string> out;
    for (int i = 0; i < SF_COUNT; ++i)
        if (mask & SharedBit(i))
            out.push_back(SharedFamilyTable()[i].key);
    return out;
}

} // namespace ComboRando
