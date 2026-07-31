// combo/rando/CrossForeign.h
// ComboShip: cross-world foreign-item lookup — shared by soh.dll, 2ship.dll, ComboShip.exe.
// The consolidated combo spoiler's "foreign" array records which checks (in each game) hold an item
// belonging to the OTHER game. At pickup the check's own game places a sentinel; the pickup code
// consults this array for the real foreign item + destination game, then delivers it immediately.
// The spoiler is pushed once per save-load into an in-memory blob (Combo_SetForeignJson, driven by
// SOH_/MM_LoadComboRando) — no runtime file read; it lives baked in the slot's .combosav container.
//
// "foreign" array element:
//   { "checkGame":"oot|mm", "checkName":"<friendly check name>", "itemGame":"oot|mm",
//     "itemName":"<friendly item name>", "displayName":"<human name + (MM)/(OOT)>",
//     optional trap disguise: "fakeItemName", "fakeDisplayName", "fakeTrickName" }
//
// Note: checkName/itemName are the friendly combo-spoiler names (bare, no suffix) — the home game
// resolves itemName to grant it, so both must match that game's friendly name maps exactly.
#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <nlohmann/json.hpp>

namespace ComboRando {

// Game identity used across the cross-world rando layer (soh.dll, 2ship.dll, ComboShip.exe).
enum GameId : uint8_t { GAME_OOT = 0, GAME_MM = 1 };

// ComboShip merged-save IO callbacks: launcher-provided, pushed into each DLL at boot via
// SOH_SetComboSaveIO / MM_SetComboSaveIO. game: 0=OOT,1=MM (GameId); fileNum 0-based (MM maps its
// 1-based mmFileNum via fileNum = mmFileNum - 1). Read returns the section JSON ("" if absent).
typedef const char* (*FnComboReadSave)(int game, int fileNum);
typedef void (*FnComboWriteSave)(int game, int fileNum, const char* json);

// Sentinel item names written into each game's own APPLY payload (not the persisted spoiler) for a
// foreign check; each game resolves its own sentinel to the RG_/RI_COMBO_FOREIGN item.
inline constexpr const char* kForeignSentinelNameOOT = "Combo Foreign Item"; // OOT English name
inline constexpr const char* kForeignSentinelNameMM = "RI_COMBO_FOREIGN";    // MM RI_ spoilerName

// Rebuild one game's apply payload from a consolidated seed: stored placements name foreign items
// for real, so the sentinel goes back at every foreign check (MM's apply THROWS on a real one).
inline nlohmann::json ApplyPayloadFromConsolidated(const nlohmann::json& consolidated, GameId game) {
    const char* gameKey = (game == GAME_OOT) ? "oot" : "mm";
    const char* sentinel = (game == GAME_OOT) ? kForeignSentinelNameOOT : kForeignSentinelNameMM;
    nlohmann::json apply =
        consolidated.value(gameKey, nlohmann::json::object()).value("placements", nlohmann::json::object());
    for (const auto& fm : consolidated.value("foreign", nlohmann::json::array())) {
        std::string checkName = fm.value("checkName", "");
        if (!checkName.empty() && fm.value("checkGame", "") == gameKey) {
            apply[checkName] = sentinel;
        }
    }
    return apply;
}

struct ForeignItem {
    GameId itemGame;          // the game the item belongs to / must be delivered to
    std::string itemName;     // friendly item name in itemGame (bare; resolved by that game's map)
    std::string displayName;  // human string for the "sent"/"received" text
    bool advancement = false; // progression in its home game -> drives the held-up pickup animation
    // Trap disguise (empty = none). The name/model shown until the check is collected; the GRANT
    // always uses itemName. fakeItemName lives in itemGame's namespace (feeds the draw producers).
    std::string fakeItemName;
    std::string fakeDisplayName; // disguise human name (suffixed like displayName)
    std::string fakeTrickName;   // typo'd disguise name for shop/merchant/hint text
    bool HasDisguise() const {
        return !fakeItemName.empty();
    }
};

inline std::string GameIdToKey(GameId g) {
    return g == GAME_OOT ? "oot" : "mm";
}
inline GameId KeyToGameId(const std::string& s) {
    return s == "mm" ? GAME_MM : GAME_OOT;
}

inline std::filesystem::path ConsolidatedDir() {
    return std::filesystem::path("Randomizer");
}

// Per-seed spoiler named from the 5 hash-icon indexes, like SoH's own (spoiler_log.cpp). The newest
// is remembered in CVAR_GENERAL("ComboSpoiler"); see docs/deviations/rando.md.
inline std::filesystem::path ComboSpoilerPath(const nlohmann::json& fileHash, const char* stem = "Combo") {
    std::string name = stem;
    for (const auto& idx : fileHash) {
        char pair[8];
        snprintf(pair, sizeof(pair), "-%02d", idx.get<int>());
        name += pair;
    }
    return ConsolidatedDir() / (name + ".json");
}

// The consolidated combo spoiler, pushed once per save-load by SOH_/MM_LoadComboRando. The three
// loaders below parse it in place of the retired per-slot file. C++17 inline var: one per DLL.
inline std::string g_comboForeignJson;

// Store the pushed spoiler blob (called by each DLL's LoadComboRando export). Null clears it.
inline void Combo_SetForeignJson(const char* json) {
    g_comboForeignJson = json ? json : "";
}

// Drop a trailing " (OOT)"/" (MM)" tag so re-tagging an already-tagged name (plandomizer round-trip)
// can't double it.
inline std::string StripGameSuffix(std::string s) {
    for (const char* suf : { " (OOT)", " (MM)" }) {
        const size_t n = std::strlen(suf);
        if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) {
            s.resize(s.size() - n);
            break;
        }
    }
    return s;
}

// Mirror MM's trap-name trick: double one letter of the disguise's name so it reads as a near-miss.
inline std::string MakeTrickName(const std::string& name, uint32_t r) {
    if (name.empty())
        return name;
    std::string out = name;
    const size_t i = r % out.size();
    out.insert(i, 1, out[i]);
    return out;
}

// One game's item-table metadata from its dump's "items" array, keyed by the friendly grant name.
struct ForeignItemMeta {
    std::string displayName;
    bool advancement = false;
    bool trap = false;
};

inline std::unordered_map<std::string, ForeignItemMeta> ParseItemMeta(const std::string& dump) {
    std::unordered_map<std::string, ForeignItemMeta> m;
    try {
        auto d = nlohmann::json::parse(dump);
        for (const auto& it : d.value("items", nlohmann::json::array())) {
            std::string n = it.value("name", "");
            if (n.empty())
                continue;
            ForeignItemMeta meta;
            meta.displayName = it.value("displayName", n);
            meta.advancement = it.value("advancement", false);
            meta.trap = it.value("trap", false);
            m.emplace(std::move(n), std::move(meta));
        }
    } catch (...) {}
    return m;
}

// Give every foreign TRAP a disguise: a plausible progression item OF THE TRAP'S OWN GAME that this
// seed actually placed (mirrors OOT's possibleIceTrapModels), plus a typo'd name for shop/hint text.
// Deterministic: a dedicated LCG stream off masterSeed, sorted candidate sets, array order.
// ootPlacements/mmPlacements are the fill's raw check->item maps (real names, pre-sentinel).
inline void AssignTrapDisguises(nlohmann::json& foreignArr, const nlohmann::json& ootPlacements,
                                const nlohmann::json& mmPlacements, const std::string& sohDump,
                                const std::string& mmDump, uint32_t masterSeed) {
    const auto ootMeta = ParseItemMeta(sohDump), mmMeta = ParseItemMeta(mmDump);
    if (ootMeta.empty() && mmMeta.empty())
        return;
    std::set<std::string> ootCand, mmCand;
    auto scan = [&](const nlohmann::json& pl) {
        if (!pl.is_object())
            return;
        for (auto it = pl.begin(); it != pl.end(); ++it) {
            if (!it.value().is_string())
                continue;
            const std::string n = it.value().get<std::string>();
            auto o = ootMeta.find(n);
            if (o != ootMeta.end() && o->second.advancement && !o->second.trap)
                ootCand.insert(n);
            auto m = mmMeta.find(n);
            if (m != mmMeta.end() && m->second.advancement && !m->second.trap)
                mmCand.insert(n);
        }
    };
    scan(ootPlacements);
    scan(mmPlacements);

    uint64_t s = static_cast<uint64_t>(masterSeed) ^ 0x7A9F3C1D5E2B4867ULL;
    auto next = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<uint32_t>(s >> 33);
    };
    for (auto& fm : foreignArr) {
        const std::string itemGame = fm.value("itemGame", "");
        if (itemGame != "oot" && itemGame != "mm")
            continue;
        const auto& meta = (itemGame == "mm") ? mmMeta : ootMeta;
        auto mit = meta.find(fm.value("itemName", ""));
        if (mit == meta.end() || !mit->second.trap)
            continue;
        const auto& cand = (itemGame == "mm") ? mmCand : ootCand;
        if (cand.empty())
            continue; // no plausible model this seed -> stays undisguised rather than lying badly
        auto cit = cand.begin();
        std::advance(cit, next() % cand.size());
        const std::string& fake = *cit;
        auto fmeta = meta.find(fake);
        const std::string dn =
            (fmeta != meta.end() && !fmeta->second.displayName.empty()) ? fmeta->second.displayName : fake;
        fm["fakeItemName"] = fake;
        fm["fakeDisplayName"] = dn;
        fm["fakeTrickName"] = MakeTrickName(dn, next());
    }
}

// Tag a spoiler "foreign" array's displayNames with their home-game suffix for the consolidated file.
// Every display surface (shops, hints, trackers, toasts) reads displayName, so tag once here.
// ootCheckAreas (checkName -> OOT area name, from SOH_DumpRandoHintData's "checks" list) is optional;
// when given, oot-side entries get a "checkArea" field for the combo hint layer's foolish-area logic.
// MM-side entries omit it — MM's own dump carries its per-check "locationHints" (region names) instead.
inline nlohmann::json BuildForeignArray(const nlohmann::json& foreignArray,
                                        const std::unordered_map<std::string, std::string>& ootCheckAreas = {}) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& fm : foreignArray) {
        std::string checkGame = fm.value("checkGame", "");
        std::string checkName = fm.value("checkName", "");
        if (checkGame.empty() || checkName.empty())
            continue;
        std::string itemGame = fm.value("itemGame", "");
        std::string itemName = fm.value("itemName", "");
        const bool tagged = (itemGame == "mm" || itemGame == "oot");
        const char* suffix = (itemGame == "mm") ? " (MM)" : " (OOT)";
        auto tag = [&](std::string s) {
            s = StripGameSuffix(std::move(s));
            if (!s.empty() && tagged)
                s += suffix;
            return s;
        };
        std::string displayName = tag(fm.value("displayName", itemName));
        nlohmann::json entry = { { "checkGame", checkGame },     { "checkName", checkName },
                                 { "itemGame", itemGame },       { "itemName", itemName },
                                 { "displayName", displayName }, { "advancement", fm.value("advancement", false) } };
        // Trap disguise: fakeItemName stays bare (it's a grant-namespace key); the shown names get tagged.
        std::string fakeItemName = fm.value("fakeItemName", "");
        if (!fakeItemName.empty()) {
            entry["fakeItemName"] = fakeItemName;
            entry["fakeDisplayName"] = tag(fm.value("fakeDisplayName", fakeItemName));
            entry["fakeTrickName"] = tag(fm.value("fakeTrickName", fm.value("fakeDisplayName", fakeItemName)));
        }
        if (checkGame == "oot") {
            auto it = ootCheckAreas.find(checkName);
            if (it != ootCheckAreas.end())
                entry["checkArea"] = it->second;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

// Suffix cross-game ITEM-name collisions in the consolidated placements so a name like "Mirror Shield"
// (in both games) reads unambiguously in the file / plandomizer. Only NATIVE placements are suffixed
// (item's game == check's game), with that game's "(OOT)"/"(MM)" tag; each game strips its own suffix
// on apply. Foreign checks are skipped (their real cross-game item is carried by the foreign[] array,
// whose displayName already carries the tag). Check names are never suffixed: they live in per-game
// objects (oot/mm) and a game DLL can't reproduce a cross-game-aware suffix at runtime.
inline void SuffixCrossGameItems(nlohmann::json& ootPlacements, nlohmann::json& mmPlacements,
                                 const nlohmann::json& foreignArray, const std::string& sohDump,
                                 const std::string& mmDump) {
    auto itemNames = [](const std::string& dump) {
        std::set<std::string> s;
        try {
            auto d = nlohmann::json::parse(dump);
            for (auto& it : d.value("pool", nlohmann::json::array()))
                if (auto n = it.value("name", std::string{}); !n.empty())
                    s.insert(std::move(n));
            for (auto& it : d.value("items", nlohmann::json::array()))
                if (auto n = it.value("name", std::string{}); !n.empty())
                    s.insert(std::move(n));
            for (auto& f : d.value("fixed", nlohmann::json::array()))
                if (auto n = f.value("item", std::string{}); !n.empty())
                    s.insert(std::move(n));
        } catch (...) {}
        return s;
    };
    std::set<std::string> ootSet = itemNames(sohDump), mmSet = itemNames(mmDump), shared;
    for (const auto& n : ootSet)
        if (mmSet.count(n))
            shared.insert(n);
    if (shared.empty())
        return;
    std::set<std::string> ootForeign, mmForeign;
    for (const auto& fm : foreignArray) {
        std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
        if (cn.empty())
            continue;
        (cg == "oot" ? ootForeign : mmForeign).insert(cn);
    }
    auto process = [&](nlohmann::json& pl, const std::set<std::string>& foreign, const char* suf) {
        for (auto it = pl.begin(); it != pl.end(); ++it) {
            if (!it.value().is_string() || foreign.count(it.key()))
                continue;
            std::string v = it.value().get<std::string>();
            if (shared.count(v))
                it.value() = v + suf;
        }
    };
    process(ootPlacements, ootForeign, " (OOT)");
    process(mmPlacements, mmForeign, " (MM)");
}

// Load one game's foreign-check section from the pushed spoiler blob, keyed by check name.
// Returns empty on missing/corrupt data (never throws across the channel). slot is unused now that
// the source is the in-memory blob (kept for call-site compatibility).
inline std::unordered_map<std::string, ForeignItem> LoadForeignForGame(int slot, GameId checkGame) {
    (void)slot;
    std::unordered_map<std::string, ForeignItem> map;
    if (g_comboForeignJson.empty())
        return map;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        const std::string key = GameIdToKey(checkGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("checkGame", "") != key)
                continue;
            ForeignItem fi;
            fi.itemGame = KeyToGameId(fm.value("itemGame", ""));
            fi.itemName = fm.value("itemName", "");
            fi.displayName = fm.value("displayName", fi.itemName);
            fi.advancement = fm.value("advancement", false);
            // Absent in pre-disguise saves -> empty -> every consumer falls back to the true name.
            fi.fakeItemName = fm.value("fakeItemName", "");
            fi.fakeDisplayName = fm.value("fakeDisplayName", "");
            fi.fakeTrickName = fm.value("fakeTrickName", "");
            map.emplace(fm.value("checkName", ""), std::move(fi));
        }
    } catch (...) { /* corrupt -> treat as empty */
    }
    return map;
}

// A foreign check's location, keyed the other way round (by itemName) for a game that wants to know
// where ITS OWN item ended up when placed at a check in the other game (family-B: MM item -> OOT check).
struct ForeignPlacement {
    GameId checkGame;
    std::string checkName;
    std::string displayName;
    bool advancement = false;
};

// Load itemGame's cross-placed items, keyed by itemName (the item's own namespace). Used when a
// display routine's local check scan fails and it needs to know which OTHER game's check holds it.
inline std::unordered_map<std::string, ForeignPlacement> LoadForeignByItem(int slot, GameId itemGame) {
    (void)slot;
    std::unordered_map<std::string, ForeignPlacement> map;
    if (g_comboForeignJson.empty())
        return map;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        const std::string key = GameIdToKey(itemGame);
        for (const auto& fm : j.value("foreign", nlohmann::json::array())) {
            if (fm.value("itemGame", "") != key)
                continue;
            ForeignPlacement fp;
            fp.checkGame = KeyToGameId(fm.value("checkGame", ""));
            fp.checkName = fm.value("checkName", "");
            fp.displayName = fm.value("displayName", fp.checkName);
            fp.advancement = fm.value("advancement", false);
            map.emplace(fm.value("itemName", ""), std::move(fp));
        }
    } catch (...) { /* corrupt -> treat as empty */
    }
    return map;
}

// Phase 4: MM's cross-game hint consumption. Mirrors the "hints.mm" object CrossHints.h::Generate
// writes (gossipPool for gossip-stone draws, itemLocations for family-B GetItemLocationHintName).
struct HintGossipEntry {
    uint32_t weight = 1;
    std::string text;
};
struct MmHints {
    std::vector<HintGossipEntry> gossipPool;
    std::unordered_map<std::string, std::string> itemLocations; // itemName -> "in <area> (OOT)"
};

// Load the hints.mm object from the pushed spoiler blob. Empty (never throws) on missing/corrupt
// data. slot unused (source is the in-memory blob; kept for call-site compatibility).
inline MmHints LoadHintsMM(int slot) {
    (void)slot;
    MmHints out;
    if (g_comboForeignJson.empty())
        return out;
    try {
        nlohmann::json j = nlohmann::json::parse(g_comboForeignJson);
        auto mm = j.value("hints", nlohmann::json::object()).value("mm", nlohmann::json::object());
        for (auto& g : mm.value("gossipPool", nlohmann::json::array()))
            out.gossipPool.push_back({ g.value("weight", 1u), g.value("text", "") });
        const auto itemLocs = mm.value("itemLocations", nlohmann::json::object());
        for (auto& [k, v] : itemLocs.items())
            out.itemLocations.emplace(k, v.get<std::string>());
    } catch (...) { /* corrupt -> treat as empty */
    }
    return out;
}

} // namespace ComboRando
