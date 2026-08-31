// combo/rando/CrossHints.h
// ComboShip: cross-game hint generation (Phase 3). Runs once per successful fill; composes the FINAL
// pre-rendered hint text for both games — the games only display it. Mirrors OOT's hintSettingTable/
// DistributeAndPlaceHints weighting, but draws candidates from BOTH games' dumps by importance only,
// with no world bias. All randomness via one seeded RNG (CwRng(masterSeed ^ 0x48494E54)) for
// determinism. Documented v1 simplifications vs native OOT hints: see docs/deviations/rando.md.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#include "ComboPlaythrough.h" // ParseSpoilerPlacements, RequirednessResult
#include "CrossWorldRando.h"  // CwRng

namespace ComboRando {

// Whether any enabled hint surface actually consumes requiredness (WotH/Foolish areas, or MM's cross
// gossip pool weighting below, c.required). If neither side wants it, PareDownPlaythrough can be
// skipped entirely: Generate() still produces every other hint category from an empty RequirednessResult.
// hintDistribution 0 ("Useless" — see HintPresets() below) has no WotH/Foolish category at all, so even
// with gossip stones on, requiredness is never consumed on the OOT side; conservative otherwise (any
// other distribution keeps WotH+Foolish nonzero, so falls through to the existing gossip-stone check).
inline bool NeedsRequirednessPareDown(const std::string& sohHintDumpJson, const std::string& mmDumpJson) {
    try {
        auto hd = nlohmann::json::parse(sohHintDumpJson.empty() ? "{}" : sohHintDumpJson);
        auto opts = hd.value("options", nlohmann::json::object());
        int gossipStoneHints = opts.value("gossipStoneHints", 0);
        int hintDistribution = opts.value("hintDistribution", 1);
        if (gossipStoneHints != 0 && hintDistribution != 0)
            return true;
    } catch (...) {}
    try {
        auto md = nlohmann::json::parse(mmDumpJson.empty() ? "{}" : mmDumpJson);
        auto opts = md.value("options", nlohmann::json::object());
        if (opts.value("RO_HINTS_GOSSIP_STONES", 0) != 0 || opts.value("RO_HINTS_PURCHASEABLE", 0) != 0)
            return true;
    } catch (...) {}
    return false;
}

// A hint fragment in all 3 OOT-displayed languages. MM-sourced content (no translation available)
// duplicates its English text into de/fr, per the design's "English in all 3 slots" convention.
struct Tri {
    std::string en, de, fr;
};
inline Tri EnglishOnly(const std::string& s) {
    return { s, s, s };
}
inline Tri FromJson(const nlohmann::json& j) {
    return { j.value("en", ""), j.value("de", ""), j.value("fr", "") };
}
// Per-language concatenation (mirrors CustomMessage::operator+, used to append altar end clauses).
inline Tri& operator+=(Tri& a, const Tri& b) {
    a.en += b.en;
    a.de += b.de;
    a.fr += b.fr;
    return a;
}

// Picks one clear/ambiguous/obscure variant from a {clear:{en,de,fr}, ambiguous:[...], obscure:[...]}
// entry (the shape SOH_DumpRandoHintData's Combo_HintTextToJson emits), per the player's hintClarity
// option (0=Obscure, 1=Ambiguous, 2=Clear — matches RO_HINT_CLARITY_* ordering). Falls back toward
// clear when the requested tier has no variants recorded.
// `mask` (a trap's disguise entry, null for none): the RNG draw always runs on `entry`, so the stream
// is byte-identical either way; the drawn tier/index is then remapped onto the mask, which is rendered.
inline Tri PickTemplate(const nlohmann::json& entry, int clarity, CwRng& rng, const nlohmann::json* mask = nullptr) {
    const nlohmann::json& src = mask ? *mask : entry;
    auto render = [&](const char* tier, size_t idx) -> Tri {
        if (tier) {
            auto a = src.value(tier, nlohmann::json::array());
            if (!a.empty())
                return FromJson(a[idx % a.size()]);
        }
        return FromJson(src.value("clear", nlohmann::json::object()));
    };
    if (clarity <= 0) {
        auto obs = entry.value("obscure", nlohmann::json::array());
        if (!obs.empty())
            return render("obscure", obs.size() > 1 ? rng.below(static_cast<uint32_t>(obs.size())) : 0);
    }
    if (clarity <= 1) {
        auto amb = entry.value("ambiguous", nlohmann::json::array());
        if (!amb.empty())
            return render("ambiguous", amb.size() > 1 ? rng.below(static_cast<uint32_t>(amb.size())) : 0);
    }
    return render(nullptr, 0);
}

// Null json (no disguise) -> no mask pointer.
inline const nlohmann::json* MaskOrNull(const nlohmann::json& j) {
    return j.is_null() ? nullptr : &j;
}

// Splices [[N]] (1-indexed) placeholders per-language, matching CustomMessage::InsertNames' convention.
inline void ReplacePlaceholder(Tri& text, int n, const Tri& value) {
    const std::string marker = "[[" + std::to_string(n) + "]]";
    auto rep = [&](std::string& s, const std::string& v) {
        size_t p;
        while ((p = s.find(marker)) != std::string::npos)
            s.replace(p, marker.size(), v);
    };
    rep(text.en, value.en);
    rep(text.de, value.de);
    rep(text.fr, value.fr);
}

// One resolved placement, enriched with its home-game hint text fragments and requiredness.
struct HintCandidate {
    GameId checkGame;
    std::string checkName;
    std::string areaKey; // "oot:<area>" / "mm:<region>" (matches RequirednessResult::areaHasRequired)
    bool dungeon = false, overworld = false, song = false;
    // OOT: real clear/ambiguous/obscure; MM: {clear:{en:region,de:region,fr:region}}
    nlohmann::json locationHint = nlohmann::json::object();
    GameId itemGame;
    std::string itemKey;                                // raw item key (RG english name / RI_* spoiler name)
    nlohmann::json itemHint = nlohmann::json::object(); // same shape as locationHint
    nlohmann::json itemHintMask;                        // trap disguise's hint entry (null = none)
    uint32_t weight = 1;
    bool required = false;
    // Native ItemLocation::IsHintable: false for confined/non-shuffled placements, which stones must
    // never target. Candidates stay in the list either way (full views below still need them).
    bool hintable = true;
};

// Weighted category mirror of OOT's hintSettingTable (hints.cpp:153). junkWeight fills any stones
// left once every category's weight/pool is exhausted (native's "no weighted types left" fallback).
struct DistCategory {
    std::string name;
    uint32_t weight;
};
struct Preset {
    uint8_t alwaysCopies;
    uint8_t trialCopies;
    uint32_t junkWeight;
    std::vector<DistCategory> cats; // order: WotH, Foolish, Song, Overworld, Dungeon, NamedItem, Random
};
inline const std::array<Preset, 4>& HintPresets() {
    static const std::array<Preset, 4> kPresets{ {
        { 0, 0, 1, {} }, // Useless: no dedicated categories -> always junk
        { 1,
          1,
          6,
          { { "WotH", 7 },
            { "Foolish", 4 },
            { "Song", 2 },
            { "Overworld", 4 },
            { "Dungeon", 3 },
            { "NamedItem", 10 },
            { "Random", 12 } } }, // Balanced
        { 2,
          1,
          0,
          { { "WotH", 12 },
            { "Foolish", 12 },
            { "Song", 4 },
            { "Overworld", 6 },
            { "Dungeon", 6 },
            { "NamedItem", 8 },
            { "Random", 8 } } }, // Strong
        { 2,
          1,
          0,
          { { "WotH", 15 },
            { "Foolish", 15 },
            { "Song", 2 },
            { "Overworld", 7 },
            { "Dungeon", 7 },
            { "NamedItem", 5 } } }, // Very Strong (no Random, matches native table)
    } };
    return kPresets;
}

// Sentinel checkName SOH_ApplyComboHints recognizes for the Ganondorf hint (RH_GANONDORF_HINT) —
// avoids needing a runtime string->RandomizerHint lookup for this one special-cased slot.
inline constexpr const char* kGanondorfHintKey = "__GANONDORF__";

// masterSeed: same seed the fill used (all randomness here derives from it, seeded independently via
// the XOR tag, so hint generation is deterministic without perturbing the fill's own RNG stream).
// sohDumpJson/mmDumpJson: the STATIC dumps (pool/items/advancement — mmDumpJson also carries
// locationHints/weightClass). sohHintDumpJson: SOH_DumpRandoHintData's schema (options/stones/checks/
// items/hintTextTable/requiredTrials). foreignArray: BuildForeignArray's output (used for the
// hints.mm.itemLocations family-B upgrade). spoilerJson: the raw combined-fill spoiler (own-namespace
// oot/mm placement maps + foreign[] — same shape RunPlaythrough/PareDownPlaythrough consume).
inline nlohmann::json Generate(uint32_t masterSeed, const std::string& sohDumpJson, const std::string& sohHintDumpJson,
                               const std::string& mmDumpJson, const nlohmann::json& foreignArray,
                               const std::string& spoilerJson, const RequirednessResult& pareDown) {
    nlohmann::json out;
    out["version"] = 1;
    nlohmann::json ootHints = nlohmann::json::array();
    nlohmann::json mmGossipPool = nlohmann::json::array();
    nlohmann::json mmItemLocations = nlohmann::json::object();

    CwRng rng(masterSeed ^ 0x48494E54u);

    nlohmann::json hintDump, staticDump, mmDump;
    try {
        hintDump = nlohmann::json::parse(sohHintDumpJson);
    } catch (...) { hintDump = nlohmann::json::object(); }
    try {
        staticDump = nlohmann::json::parse(sohDumpJson);
    } catch (...) { staticDump = nlohmann::json::object(); }
    try {
        mmDump = nlohmann::json::parse(mmDumpJson);
    } catch (...) { mmDump = nlohmann::json::object(); }

    const auto options = hintDump.value("options", nlohmann::json::object());
    const int gossipStoneHints = options.value("gossipStoneHints", 0);
    const int hintClarity = options.value("hintClarity", 2);
    const int hintDistribution = std::clamp(options.value("hintDistribution", 1), 0, 3);
    const bool ganondorfHintOn = options.value("ganondorfHint", 0) != 0;

    // OOT per-check hint info, keyed by check name.
    struct OotCheckInfo {
        std::string area;
        bool dungeon = false, overworld = false, song = false;
        nlohmann::json locationHint;
    };
    std::unordered_map<std::string, OotCheckInfo> ootChecks;
    for (auto& c : hintDump.value("checks", nlohmann::json::array())) {
        OotCheckInfo info;
        info.area = c.value("area", "");
        info.dungeon = c.value("dungeon", false);
        info.overworld = c.value("overworld", false);
        info.song = c.value("song", false);
        info.locationHint = c.value("locationHint", nlohmann::json::object());
        ootChecks.emplace(c.value("name", ""), std::move(info));
    }
    // OOT per-item hint text, keyed by English item name (matches placement values).
    std::unordered_map<std::string, nlohmann::json> ootItemHints;
    for (auto& it : hintDump.value("items", nlohmann::json::array()))
        ootItemHints.emplace(it.value("name", ""), it.value("hint", nlohmann::json::object()));
    const auto hintTextTable = hintDump.value("hintTextTable", nlohmann::json::object());
    auto tmpl = [&](const char* key) { return hintTextTable.value(key, nlohmann::json::object()); };

    // MM per-check region text + per-item weight/displayName.
    std::unordered_map<std::string, std::string> mmLocationHints;
    const auto mmLocHintsJson = mmDump.value("locationHints", nlohmann::json::object());
    for (auto& [chk, region] : mmLocHintsJson.items())
        mmLocationHints.emplace(chk, region.get<std::string>());
    struct MmItemInfo {
        std::string displayName;
        uint32_t weightClass = 1;
    };
    std::unordered_map<std::string, MmItemInfo> mmItems;
    for (auto& it : mmDump.value("items", nlohmann::json::array())) {
        MmItemInfo info;
        info.displayName = it.value("displayName", it.value("name", ""));
        info.weightClass = it.value("weightClass", 1u);
        mmItems.emplace(it.value("name", ""), std::move(info));
    }

    // Item-hint text + weight for either game's item (shared by the fill-time candidate list and
    // the always-hint block below, so both compose text identically).
    auto itemHintAndWeight = [&](GameId itemGame, const std::string& itemKey,
                                 bool required) -> std::pair<nlohmann::json, uint32_t> {
        if (itemGame == GAME_OOT) {
            auto it = ootItemHints.find(itemKey);
            nlohmann::json hint = it != ootItemHints.end() ? it->second : nlohmann::json::object();
            return { hint, required ? 3u : 1u };
        }
        auto it = mmItems.find(itemKey);
        std::string dn = it != mmItems.end() ? it->second.displayName : itemKey;
        nlohmann::json hint = { { "clear", { { "en", dn }, { "de", dn }, { "fr", dn } } } };
        return { hint, it != mmItems.end() ? std::max<uint32_t>(1, it->second.weightClass) : 1u };
    };

    // Trap disguises: hint text must name the disguise, never the true trap. "<checkGame>:<check>" ->
    // fake item name (already in the item's own game namespace, so the hint maps above resolve it).
    std::unordered_map<std::string, std::string> disguiseByCheck;
    for (auto& fm : foreignArray) {
        std::string fake = fm.value("fakeItemName", "");
        if (!fake.empty())
            disguiseByCheck.emplace(fm.value("checkGame", "") + ":" + fm.value("checkName", ""), std::move(fake));
    }
    // Consumes no RNG and never changes weights — purely the text a hint renders for this check.
    auto maskHint = [&](GameId checkGame, const std::string& check, GameId itemGame) -> nlohmann::json {
        auto it = disguiseByCheck.find((checkGame == GAME_OOT ? "oot:" : "mm:") + check);
        if (it == disguiseByCheck.end())
            return nlohmann::json();
        nlohmann::json h = itemHintAndWeight(itemGame, it->second, false).first;
        if (h.value("clear", nlohmann::json::object()).empty())
            h = { { "clear", { { "en", it->second }, { "de", it->second }, { "fr", it->second } } } };
        return h;
    };

    // Family-B upgrade data (Phase 4 consumes this): MM items placed at an OOT check, keyed by the
    // MM item's own friendly name -> "in <area> (OOT)".
    for (auto& fm : foreignArray) {
        if (fm.value("checkGame", "") != "oot" || fm.value("itemGame", "") != "mm")
            continue;
        std::string itemName = fm.value("itemName", "");
        std::string checkName = fm.value("checkName", "");
        // Area from the hint dump's own per-check table (foreign[] no longer persists checkArea).
        auto ca = ootChecks.find(checkName);
        std::string area = (ca != ootChecks.end() && !ca->second.area.empty()) ? ca->second.area : checkName;
        if (!itemName.empty())
            mmItemLocations[itemName] = "in " + area + " (OOT)";
    }

    // Build the candidate list from the same placements the pare-down scored, so requiredness lines
    // up exactly with what gets hinted.
    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);
    // Full OOT check->placement index (unfiltered by advancement) — the always-hint checks below
    // may hold non-advancement items (e.g. a Piece of Heart at the Big Poes reward).
    std::unordered_map<std::string, size_t> ootPlacementIndex;
    for (size_t i = 0; i < placements.size(); ++i)
        if (placements[i].checkGame == GAME_OOT)
            ootPlacementIndex.emplace(placements[i].check, i);
    // Non-hintable checks, from both dumps' fixed[] (confined placements that never went through a
    // shuffle fill). Absent flag = pre-flag DLL -> everything hintable, i.e. the old behavior.
    std::unordered_set<std::string> nonHintable;
    auto loadNonHintable = [&](const char* prefix, const nlohmann::json& dump) {
        const auto fixedArr = dump.value("fixed", nlohmann::json::array());
        bool sawFlag = false;
        for (auto& f : fixedArr) {
            if (f.contains("hintable"))
                sawFlag = true;
            if (!f.value("hintable", true))
                nonHintable.insert(prefix + f.value("check", std::string()));
        }
        if (!fixedArr.empty() && !sawFlag)
            std::cerr << "[HINTS] " << (prefix[0] == 'o' ? "oot" : "mm")
                      << " dump has no 'hintable' flag on fixed placements; hints may "
                         "target non-shuffled checks - rebuild soh.dll/2ship.dll\n";
    };
    loadNonHintable("oot:", staticDump);
    loadNonHintable("mm:", mmDump);
    // Forced placements (spoiler startKnown, e.g. Link's Pocket) are owned at start — never hintable.
    try {
        for (auto& sk : nlohmann::json::parse(spoilerJson).value("startKnown", nlohmann::json::array()))
            nonHintable.insert(sk.value("checkGame", "") + ":" + sk.value("checkName", ""));
    } catch (...) {}

    std::vector<HintCandidate> candidates;
    candidates.reserve(placements.size());
    std::unordered_set<std::string> areaHasMajor; // native barren predicate: barren = no WotH + no major
    for (auto& p : placements) {
        if (!p.advancement)
            continue; // junk is never hinted as WotH/Foolish/item content
        HintCandidate c;
        c.checkGame = p.checkGame;
        c.checkName = p.check;
        c.itemGame = p.itemGame;
        c.itemKey = p.item;
        std::string checkKey = (p.checkGame == GAME_OOT ? "oot:" : "mm:") + p.check;
        auto reqIt = pareDown.requiredByCheck.find(checkKey);
        c.required = reqIt != pareDown.requiredByCheck.end() && reqIt->second;
        c.hintable = !nonHintable.count(checkKey);
        if (p.checkGame == GAME_OOT) {
            auto it = ootChecks.find(p.check);
            if (it != ootChecks.end()) {
                c.areaKey = "oot:" + it->second.area;
                c.dungeon = it->second.dungeon;
                c.overworld = it->second.overworld;
                c.song = it->second.song;
                c.locationHint = it->second.locationHint;
            }
        } else {
            auto it = mmLocationHints.find(p.check);
            std::string region = it != mmLocationHints.end() ? it->second : p.check;
            c.areaKey = "mm:" + region;
            c.overworld = true; // MM checks bucket into "Overworld" (no dungeon/song split exported)
            c.locationHint = { { "clear", { { "en", region }, { "de", region }, { "fr", region } } } };
        }
        std::tie(c.itemHint, c.weight) = itemHintAndWeight(p.itemGame, p.item, c.required);
        c.itemHintMask = maskHint(p.checkGame, p.check, p.itemGame); // weight stays the true item's
        if (p.major && !c.areaKey.empty())
            areaHasMajor.insert(c.areaKey);
        candidates.push_back(std::move(c));
    }

    // Required / foolish AREA pools (native's WotH/Foolish hint an area, not a specific item).
    // Unmapped MM checks roll up as "Unknown" — a useless hint target, so drop those keys.
    // WotH counts only HINTABLE required checks (fill.cpp:767) — a confined boss key can't make its own
    // area "way of the hero". areaHasMajor stays unfiltered (native barren counts majors regardless).
    std::unordered_set<std::string> areaHasHintableRequired;
    for (auto& c : candidates)
        if (c.hintable && c.required && !c.areaKey.empty())
            areaHasHintableRequired.insert(c.areaKey);
    std::vector<std::string> requiredAreas, foolishAreas;
    for (auto& entry : pareDown.areaHasRequired) {
        const std::string& key = entry.first;
        if (key.find("Unknown") != std::string::npos)
            continue;
        if (areaHasHintableRequired.count(key))
            requiredAreas.push_back(key);
        else if (!areaHasMajor.count(key)) // barren only if no WotH AND no major item (native parity)
            foolishAreas.push_back(key);
    }
    // Stable order (unordered_map iteration feeds the RNG picks) — sort so determinism never
    // depends on the STL's bucket layout.
    std::sort(requiredAreas.begin(), requiredAreas.end());
    std::sort(foolishAreas.begin(), foolishAreas.end());

    std::unordered_set<std::string> usedCheckKeys, usedAreaKeys;
    auto areaText = [&](const std::string& areaKey) -> Tri {
        size_t colon = areaKey.find(':');
        std::string plain = colon == std::string::npos ? areaKey : areaKey.substr(colon + 1);
        // MM region strings come prefixed from GetLocationNameForHint ("in Woodfall Temple") — strip
        // the "in " so the templates' own prepositions read correctly.
        if (areaKey.rfind("mm:", 0) == 0 && plain.rfind("in ", 0) == 0)
            plain = plain.substr(3);
        return EnglishOnly(plain);
    };

    // Area text for an OOT item wherever it landed, in either game. nullopt = the item is in no
    // check at all (starting item, or not shuffled in), so no hint may name a location for it.
    // yourPocket mirrors the native Hint flag: only hints that set it say "your pocket".
    auto itemAreaText = [&](const char* itemName, bool yourPocket = false) -> std::optional<Tri> {
        auto it = std::find_if(placements.begin(), placements.end(),
                               [&](const CwPlacedItem& p) { return p.itemGame == GAME_OOT && p.item == itemName; });
        if (it == placements.end())
            return std::nullopt;
        if (it->checkGame != GAME_OOT) {
            auto mk = mmLocationHints.find(it->check);
            return areaText("mm:" + (mk != mmLocationHints.end() ? mk->second : it->check));
        }
        if (yourPocket && it->check == "Link's Pocket")
            return PickTemplate(tmpl("RHT_YOUR_POCKET"), hintClarity, rng);
        auto ck = ootChecks.find(it->check);
        if (ck == ootChecks.end())
            return std::nullopt;
        return areaText("oot:" + ck->second.area);
    };

    // Weighted pick of a not-yet-used index from `pool` (indices into `candidates` or an area vector),
    // via a caller-supplied "already used" predicate. Returns -1 when nothing remains.
    auto pickUnused = [&](const std::vector<size_t>& idxs, const std::vector<std::string>& keys,
                          std::unordered_set<std::string>& used) -> int {
        std::vector<size_t> avail;
        for (size_t i : idxs)
            if (!used.count(keys[i]))
                avail.push_back(i);
        if (avail.empty())
            return -1;
        return static_cast<int>(avail[rng.below(static_cast<uint32_t>(avail.size()))]);
    };

    size_t totalStones = hintDump.value("stones", nlohmann::json::array()).size();
    int producedHints = 0, producedJunk = 0;

    // Required trials (English-only; see file header note).
    const Preset& preset = HintPresets()[hintDistribution];
    if (gossipStoneHints != 0 && preset.trialCopies > 0) {
        for (auto& trialName : hintDump.value("requiredTrials", nlohmann::json::array())) {
            if (totalStones == 0)
                break;
            std::string name = trialName.get<std::string>();
            Tri msg = EnglishOnly("The " + name + " is required to reach Ganon's Castle.");
            for (uint8_t copy = 0; copy < preset.trialCopies && totalStones > 0; ++copy, --totalStones) {
                ootHints.push_back({ { "checkName", "__TRIAL__" + name + std::to_string(copy) },
                                     { "type", "trial" },
                                     { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
                ++producedHints;
            }
        }
    }

    // Ganondorf hint. BuildGanondorfHint (StaticHints.cpp) picks the message BY INDEX from live
    // state, so a shuffled Master Sword needs all three variants, in native's hintKeys order.
    if (ganondorfHintOn) {
        const std::optional<Tri> laArea = itemAreaText("Light Arrows", true);
        const std::optional<Tri> msArea = itemAreaText("Master Sword", true);
        const bool msShuffled =
            options.value("shuffleMasterSword", 0) != 0 && options.value("startingMasterSword", 0) == 0;
        // Native reads index 1/2 whenever the sword is shuffled, so an unresolvable sword must skip
        // the whole hint (native then fills it) rather than answer those reads with the arrows' text.
        if (laArea && (!msShuffled || msArea)) {
            auto render = [&](const char* key) {
                Tri m = PickTemplate(tmpl(key), hintClarity, rng);
                ReplacePlaceholder(m, 1, *laArea);
                if (msArea)
                    ReplacePlaceholder(m, 2, *msArea);
                return m;
            };
            nlohmann::json msgs = nlohmann::json::array();
            auto push = [&](const Tri& m) { msgs.push_back({ { "en", m.en }, { "de", m.de }, { "fr", m.fr } }); };
            push(render("RHT_GANONDORF_HINT_LA_ONLY"));
            if (msShuffled) {
                push(render("RHT_GANONDORF_HINT_MS_ONLY"));
                push(render("RHT_GANONDORF_HINT_LA_AND_MS"));
            }
            ootHints.push_back(
                { { "checkName", kGanondorfHintKey }, { "type", "ganondorf" }, { "messages", std::move(msgs) } });
        }
    }

    // Altar hints (Fix 3): composed here (not left to native CreateChildAltarHint/CreateAdultAltarHint)
    // because native's FindItemsAndMarkHinted only searches ctx->allLocations (OOT's own checks) — a
    // dungeon reward cross-placed into MM comes back RC_UNKNOWN_CHECK and gets skipped, leaving a
    // literal "[[N]]" in the displayed hint. Resolving each reward via `candidates` (which spans both
    // games) fills every slot regardless of which game holds it.
    if (options.value("totAltarHint", 0) != 0) {
        // An unresolvable reward reads as "an unknown place" (pre-existing fill gap, not an
        // altar-composition bug — see UPSTREAM_MERGES.md); the altar text always fills every slot.
        auto rewardArea = [&](const char* itemName) -> Tri {
            return itemAreaText(itemName).value_or(EnglishOnly("an unknown place"));
        };
        auto endClause = [&](const char* jsonKey) -> Tri {
            std::string key = options.value(jsonKey, "");
            return key.empty() ? Tri{} : PickTemplate(tmpl(key.c_str()), hintClarity, rng);
        };
        // Mirrors CustomMessage::InsertNumber (CustomMessageManager.cpp:643): "|singular|plural|"
        // collapses to whichever branch matches num==1, then "[[d]]" becomes the number.
        auto insertNumber = [](std::string& s, int num) {
            size_t bar1 = s.find('|');
            if (bar1 != std::string::npos) {
                size_t bar2 = s.find('|', bar1 + 1);
                size_t bar3 = bar2 == std::string::npos ? std::string::npos : s.find('|', bar2 + 1);
                if (bar3 != std::string::npos) {
                    if (num == 1)
                        s.erase(bar2, bar3 - bar2);
                    else
                        s.erase(bar1, bar2 - bar1);
                }
            }
            size_t p;
            while ((p = s.find('|')) != std::string::npos)
                s.erase(p, 1);
            while ((p = s.find("[[d]]")) != std::string::npos)
                s.replace(p, 5, std::to_string(num));
        };
        auto withCount = [&](const char* templateKey, const char* countKey) -> Tri {
            std::string key = options.value(templateKey, "");
            if (key.empty())
                return {};
            Tri t = PickTemplate(tmpl(key.c_str()), hintClarity, rng);
            // Unconditional like native InsertNumber — no-op when the template has no [[d]]/bar, so a
            // count-based clause configured with 0 still fills [[d]] instead of leaving it literal.
            int count = options.value(countKey, 0);
            insertNumber(t.en, count);
            insertNumber(t.de, count);
            insertNumber(t.fr, count);
            return t;
        };

        static const char* kChildRewards[3] = { "Kokiri's Emerald", "Goron's Ruby", "Zora's Sapphire" };
        Tri childMsg = PickTemplate(tmpl("RHT_CHILD_ALTAR_STONES"), hintClarity, rng);
        for (int i = 0; i < 3; ++i)
            ReplacePlaceholder(childMsg, i + 1, rewardArea(kChildRewards[i]));
        childMsg += endClause("doorOfTimeTemplate");
        ootHints.push_back(
            { { "checkName", "__ALTAR_CHILD__" },
              { "type", "altarChild" },
              { "messages", { { { "en", childMsg.en }, { "de", childMsg.de }, { "fr", childMsg.fr } } } } });

        static const char* kAdultRewards[6] = { "Light Medallion", "Forest Medallion", "Fire Medallion",
                                                "Water Medallion", "Spirit Medallion", "Shadow Medallion" };
        Tri adultMsg = PickTemplate(tmpl("RHT_ADULT_ALTAR_MEDALLIONS"), hintClarity, rng);
        for (int i = 0; i < 6; ++i)
            ReplacePlaceholder(adultMsg, i + 1, rewardArea(kAdultRewards[i]));
        adultMsg += withCount("bridgeTemplate", "bridgeCount");
        adultMsg += withCount("gbkTemplate", "gbkCount");
        adultMsg += withCount("soulTemplate", "soulCount");
        adultMsg += withCount("winconTemplate", "winconCount");
        adultMsg += PickTemplate(tmpl("RHT_ADULT_ALTAR_TEXT_END"), hintClarity, rng);
        ootHints.push_back(
            { { "checkName", "__ALTAR_ADULT__" },
              { "type", "altarAdult" },
              { "messages", { { { "en", adultMsg.en }, { "de", adultMsg.de }, { "fr", adultMsg.fr } } } } });
    }

    if (gossipStoneHints != 0) {
        // Always-hint checks (native "Always" category: Big Poes, Mask Shop, frogs, skull-reward
        // counts, etc): one hint per exported always-check, alwaysCopies stone copies each, placed
        // before the weighted loop so it can't re-target these checks (mirrors native SetHintAccesible
        // exclusion). alwaysHintChecks is already settings-filtered on the OOT side — trust it.
        if (preset.alwaysCopies > 0) {
            for (auto& an : hintDump.value("alwaysHintChecks", nlohmann::json::array())) {
                if (totalStones == 0)
                    break;
                std::string checkName = an.get<std::string>();
                std::string checkKey = "oot:" + checkName;
                // An EXCLUDED always-check is junk-filled non-hintably; native skips it (IsHintable).
                if (usedCheckKeys.count(checkKey) || nonHintable.count(checkKey))
                    continue;
                auto locIt = ootChecks.find(checkName);
                auto plIt = ootPlacementIndex.find(checkName);
                if (locIt == ootChecks.end() || plIt == ootPlacementIndex.end())
                    continue;
                const auto& placed = placements[plIt->second];
                nlohmann::json itemHint = itemHintAndWeight(placed.itemGame, placed.item, false).first;
                // Unfiltered by advancement, so this check can hold a disguised trap — hint the disguise.
                nlohmann::json itemMask = maskHint(GAME_OOT, checkName, placed.itemGame);
                Tri msg = PickTemplate(locIt->second.locationHint, hintClarity, rng);
                ReplacePlaceholder(msg, 1, PickTemplate(itemHint, hintClarity, rng, MaskOrNull(itemMask)));
                uint8_t copies = std::min<uint8_t>(preset.alwaysCopies, static_cast<uint8_t>(totalStones));
                for (uint8_t copy = 0; copy < copies; ++copy) {
                    ootHints.push_back(
                        { { "checkName", "__ALWAYS__" + checkName + std::to_string(copy) },
                          { "type", "always" },
                          { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
                    ++producedHints;
                }
                totalStones -= copies;
                usedCheckKeys.insert(checkKey);
            }
        }

        // Start-known checks (native SetHintAccesible, e.g. Song from Impa): reserve them from the
        // weighted loop only — always-hints stay unaffected, matching native's ordering.
        for (auto& hn : hintDump.value("hintAccessibleChecks", nlohmann::json::array()))
            usedCheckKeys.insert("oot:" + hn.get<std::string>());

        std::vector<DistCategory> dist = preset.cats; // mutable local copy (weights zeroed on exhaustion)
        while (totalStones > 0) {
            uint32_t totalWeight = 0;
            for (auto& d : dist)
                totalWeight += d.weight;
            if (totalWeight == 0)
                break; // fall through to junk fill below
            uint32_t roll = totalWeight <= 1 ? 1 : (rng.below(totalWeight) + 1);
            uint32_t cursor = 0;
            size_t chosen = dist.size();
            for (size_t i = 0; i < dist.size(); ++i) {
                cursor += dist[i].weight;
                if (roll <= cursor) {
                    chosen = i;
                    break;
                }
            }
            if (chosen == dist.size())
                break;
            const std::string& cat = dist[chosen].name;
            Tri msg;
            bool placed = false;
            std::string usedKey;

            if (cat == "WotH" || cat == "Foolish") {
                auto& pool = (cat == "WotH") ? requiredAreas : foolishAreas;
                std::vector<size_t> idxs(pool.size());
                for (size_t i = 0; i < pool.size(); ++i)
                    idxs[i] = i;
                int pick = pickUnused(idxs, pool, usedAreaKeys);
                if (pick >= 0) {
                    usedKey = pool[pick];
                    msg = PickTemplate(tmpl(cat == "WotH" ? "RHT_WAY_OF_THE_HERO" : "RHT_FOOLISH"), hintClarity, rng);
                    ReplacePlaceholder(msg, 1, areaText(usedKey));
                    usedAreaKeys.insert(usedKey);
                    placed = true;
                }
            } else if (cat == "Song" || cat == "Overworld" || cat == "Dungeon") {
                std::vector<size_t> idxs;
                std::vector<std::string> keys(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i) {
                    keys[i] = (candidates[i].checkGame == GAME_OOT ? "oot:" : "mm:") + candidates[i].checkName;
                    bool eligible = candidates[i].hintable && ((cat == "Song" && candidates[i].song) ||
                                                               (cat == "Overworld" && candidates[i].overworld) ||
                                                               (cat == "Dungeon" && candidates[i].dungeon));
                    if (eligible)
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    const auto& cand = candidates[pick];
                    if (cand.checkGame == GAME_OOT) {
                        // OOT location HintTexts are full sentences with [[1]] = the item name.
                        msg = PickTemplate(cand.locationHint, hintClarity, rng);
                        ReplacePlaceholder(
                            msg, 1, PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                    } else {
                        // MM checks have no authored location sentence — compose item + region.
                        msg = PickTemplate(tmpl("RHT_CAN_BE_FOUND_AT"), hintClarity, rng);
                        ReplacePlaceholder(
                            msg, 1, PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                        ReplacePlaceholder(msg, 2, areaText(cand.areaKey));
                    }
                    usedCheckKeys.insert(usedKey);
                    placed = true;
                }
            } else if (cat == "NamedItem" || cat == "Random") {
                std::vector<size_t> idxs;
                std::vector<std::string> keys(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i) {
                    keys[i] = (candidates[i].checkGame == GAME_OOT ? "oot:" : "mm:") + candidates[i].checkName;
                    if (candidates[i].hintable && (cat == "Random" || candidates[i].required))
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    const auto& cand = candidates[pick];
                    msg = PickTemplate(tmpl(cand.dungeon ? "RHT_HOARDS" : "RHT_CAN_BE_FOUND_AT"), hintClarity, rng);
                    ReplacePlaceholder(msg, 1,
                                       PickTemplate(cand.itemHint, hintClarity, rng, MaskOrNull(cand.itemHintMask)));
                    ReplacePlaceholder(msg, 2, areaText(cand.areaKey));
                    usedCheckKeys.insert(usedKey);
                    placed = true;
                }
            }

            if (!placed) {
                dist[chosen].weight = 0; // pool exhausted for this category — never retry it
                continue;
            }
            ootHints.push_back({ { "checkName", "__STONE__" + std::to_string(producedHints) },
                                 { "type", cat },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
            ++producedHints;
            --totalStones;
        }

        // Junk fill for whatever's left (Useless preset, or every category exhausted).
        auto junkTemplates = nlohmann::json::array();
        for (auto& [key, val] : hintTextTable.items())
            if (key.rfind("RHT_JUNK", 0) == 0)
                junkTemplates.push_back(val);
        for (; totalStones > 0; --totalStones) {
            Tri msg = junkTemplates.empty()
                          ? EnglishOnly("They say that this and that are related.")
                          : PickTemplate(junkTemplates[rng.below(static_cast<uint32_t>(junkTemplates.size()))], 2, rng);
            ootHints.push_back({ { "checkName", "__JUNK__" + std::to_string(producedJunk) },
                                 { "type", "junk" },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
            ++producedJunk;
        }
    }

    // MM gossip pool (EnGs.cpp draws from this): every advancement item sitting on an OOT CHECK —
    // the checks MM's native stone draw can't see (MM checks, foreign-held or not, it already
    // covers itself). Weight mirrors requiredness so cross entries compete fairly (grill #3).
    for (auto& c : candidates) {
        if (c.checkGame != GAME_OOT || !c.hintable) // non-shuffled OOT checks are never hint targets
            continue;
        Tri itemName = PickTemplate(c.itemHint, 2, rng, MaskOrNull(c.itemHintMask));
        Tri area = areaText(c.areaKey);
        std::string text = itemName.en + " can be found " + (c.dungeon ? "hoarded in " : "at ") + area.en + " (OOT)";
        mmGossipPool.push_back({ { "weight", c.required ? 3 : 1 }, { "text", text } });
    }

    out["oot"] = std::move(ootHints);
    out["mm"] = { { "gossipPool", std::move(mmGossipPool) }, { "itemLocations", std::move(mmItemLocations) } };
    out["stats"] = { { "hintsProduced", producedHints }, { "junkProduced", producedJunk } };
    return out;
}

} // namespace ComboRando
