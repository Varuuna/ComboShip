// combo/rando/CrossHints.h
// ComboShip: cross-game hint generation (Phase 3). Runs once per successful fill, after the
// requiredness pare-down, and composes the FINAL pre-rendered hint text for both games — the games
// only display it, never look anything up themselves. Mirrors OOT's own hintSettingTable/
// DistributeAndPlaceHints weighted-distribution shape, but draws candidates from BOTH games' dumps
// with no world bias (grill resolution #3): items are weighted by importance only (OOT: required/
// advancement; MM: weightClass), never by which game they happen to live in.
//
// All randomness here goes through ONE seeded RNG (CwRng(masterSeed ^ 0x48494E54)) for determinism.
//
// Design note (documented simplification vs native OOT hints, Phase 3 scope):
//  - Native "Always"-hint checks (Big Poes, Mask Shop, frogs, Malon, skulltula counts, etc) are NOT
//    mirrored here — those stay OOT-local and are unaffected by cross-game placement, so native
//    CreateStaticHints() (run after this) already covers them without a dedicated gossip-stone slot.
//  - Trial hints are English-only (the dump only exports the English trial name).
//  - Ganondorf's combined "Light Arrows + Master Sword" phrasing variant is not mirrored; combo
//    always uses the Light-Arrows-only template (still correct, just less detailed in that one case).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

#include "ComboPlaythrough.h" // ParseSpoilerPlacements, RequirednessResult
#include "CrossWorldRando.h"  // CwRng

namespace ComboRando {

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

// Picks one clear/ambiguous/obscure variant from a {clear:{en,de,fr}, ambiguous:[...], obscure:[...]}
// entry (the shape SOH_DumpRandoHintData's Combo_HintTextToJson emits), per the player's hintClarity
// option (0=Obscure, 1=Ambiguous, 2=Clear — matches RO_HINT_CLARITY_* ordering). Falls back toward
// clear when the requested tier has no variants recorded.
inline Tri PickTemplate(const nlohmann::json& entry, int clarity, CwRng& rng) {
    auto pickFrom = [&](const nlohmann::json& arr) -> Tri {
        size_t idx = arr.size() > 1 ? rng.below(static_cast<uint32_t>(arr.size())) : 0;
        return FromJson(arr[idx]);
    };
    if (clarity <= 0) {
        auto obs = entry.value("obscure", nlohmann::json::array());
        if (!obs.empty())
            return pickFrom(obs);
    }
    if (clarity <= 1) {
        auto amb = entry.value("ambiguous", nlohmann::json::array());
        if (!amb.empty())
            return pickFrom(amb);
    }
    return FromJson(entry.value("clear", nlohmann::json::object()));
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
    std::string itemKey;                                       // raw item key (RG english name / RI_* spoiler name)
    nlohmann::json itemHint = nlohmann::json::object(); // same shape as locationHint
    uint32_t weight = 1;
    bool required = false;
};

// Weighted category mirror of OOT's hintSettingTable (hints.cpp:153). junkWeight fills any stones
// left once every category's weight/pool is exhausted (native's "no weighted types left" fallback).
struct DistCategory {
    std::string name;
    uint32_t weight;
};
struct Preset {
    uint8_t trialCopies;
    uint32_t junkWeight;
    std::vector<DistCategory> cats; // order: WotH, Foolish, Song, Overworld, Dungeon, NamedItem, Random
};
inline const std::array<Preset, 4>& HintPresets() {
    static const std::array<Preset, 4> kPresets{ {
        { 0, 1, {} }, // Useless: no dedicated categories -> always junk
        { 1,
         6,
         { { "WotH", 7 }, { "Foolish", 4 }, { "Song", 2 }, { "Overworld", 4 }, { "Dungeon", 3 }, { "NamedItem", 10 },
           { "Random", 12 } } }, // Balanced
        { 1,
         0,
         { { "WotH", 12 }, { "Foolish", 12 }, { "Song", 4 }, { "Overworld", 6 }, { "Dungeon", 6 }, { "NamedItem", 8 },
           { "Random", 8 } } }, // Strong
        { 1,
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
inline nlohmann::json Generate(uint32_t masterSeed, const std::string& sohDumpJson,
                               const std::string& sohHintDumpJson, const std::string& mmDumpJson,
                               const nlohmann::json& foreignArray, const std::string& spoilerJson,
                               const RequirednessResult& pareDown) {
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
    for (auto& [chk, region] : mmDump.value("locationHints", nlohmann::json::object()).items())
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

    // Family-B upgrade data (Phase 4 consumes this): MM items placed at an OOT check, keyed by the
    // MM item's own RI_* name -> "in <area> (OOT)".
    for (auto& fm : foreignArray) {
        if (fm.value("checkGame", "") != "oot" || fm.value("itemGame", "") != "mm")
            continue;
        std::string itemName = fm.value("itemName", "");
        std::string area = fm.value("checkArea", fm.value("checkName", ""));
        if (!itemName.empty())
            mmItemLocations[itemName] = "in " + area + " (OOT)";
    }

    // Build the candidate list from the same placements the pare-down scored, so requiredness lines
    // up exactly with what gets hinted.
    auto placements = ParseSpoilerPlacements(spoilerJson, sohDumpJson, mmDumpJson);
    std::vector<HintCandidate> candidates;
    candidates.reserve(placements.size());
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
        if (p.itemGame == GAME_OOT) {
            auto it = ootItemHints.find(p.item);
            c.itemHint = it != ootItemHints.end() ? it->second : nlohmann::json::object();
            c.weight = c.required ? 3 : 1;
        } else {
            auto it = mmItems.find(p.item);
            std::string dn = it != mmItems.end() ? it->second.displayName : p.item;
            c.itemHint = { { "clear", { { "en", dn }, { "de", dn }, { "fr", dn } } } };
            c.weight = it != mmItems.end() ? std::max<uint32_t>(1, it->second.weightClass) : 1;
        }
        candidates.push_back(std::move(c));
    }

    // Required / foolish AREA pools (native's WotH/Foolish hint an area, not a specific item).
    std::vector<std::string> requiredAreas, foolishAreas;
    for (auto& [key, hasRequired] : pareDown.areaHasRequired)
        (hasRequired ? requiredAreas : foolishAreas).push_back(key);

    std::unordered_set<std::string> usedCheckKeys, usedAreaKeys;
    auto areaText = [&](const std::string& areaKey) -> Tri {
        size_t colon = areaKey.find(':');
        std::string plain = colon == std::string::npos ? areaKey : areaKey.substr(colon + 1);
        return EnglishOnly(plain);
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

    // Ganondorf hint: find the Light Arrows placement (always an OOT item) wherever it landed.
    if (ganondorfHintOn) {
        auto it = std::find_if(candidates.begin(), candidates.end(),
                               [](const HintCandidate& c) { return c.itemGame == GAME_OOT && c.itemKey == "Light Arrows"; });
        if (it != candidates.end()) {
            Tri msg = PickTemplate(tmpl("RHT_GANONDORF_HINT_LA_ONLY"), hintClarity, rng);
            ReplacePlaceholder(msg, 1, areaText(it->areaKey));
            ootHints.push_back({ { "checkName", kGanondorfHintKey },
                                 { "type", "ganondorf" },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
        }
    }

    if (gossipStoneHints != 0) {
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
                    bool eligible = (cat == "Song" && candidates[i].song) ||
                                    (cat == "Overworld" && candidates[i].overworld) ||
                                    (cat == "Dungeon" && candidates[i].dungeon);
                    if (eligible)
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    msg = PickTemplate(candidates[pick].locationHint, hintClarity, rng);
                    usedCheckKeys.insert(usedKey);
                    placed = true;
                }
            } else if (cat == "NamedItem" || cat == "Random") {
                std::vector<size_t> idxs;
                std::vector<std::string> keys(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i) {
                    keys[i] = (candidates[i].checkGame == GAME_OOT ? "oot:" : "mm:") + candidates[i].checkName;
                    if (cat == "Random" || candidates[i].required)
                        idxs.push_back(i);
                }
                int pick = pickUnused(idxs, keys, usedCheckKeys);
                if (pick >= 0) {
                    usedKey = keys[pick];
                    const auto& cand = candidates[pick];
                    msg = PickTemplate(tmpl(cand.dungeon ? "RHT_HOARDS" : "RHT_CAN_BE_FOUND_AT"), hintClarity, rng);
                    ReplacePlaceholder(msg, 1, PickTemplate(cand.itemHint, hintClarity, rng));
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
            Tri msg = junkTemplates.empty() ? EnglishOnly("They say that this and that are related.")
                                            : PickTemplate(junkTemplates[rng.below(static_cast<uint32_t>(junkTemplates.size()))],
                                                          2, rng);
            ootHints.push_back({ { "checkName", "__JUNK__" + std::to_string(producedJunk) },
                                 { "type", "junk" },
                                 { "messages", { { { "en", msg.en }, { "de", msg.de }, { "fr", msg.fr } } } } });
            ++producedJunk;
        }
    }

    // MM gossip pool preview (Phase 4 wires EnGs.cpp to actually draw from this): every OOT-owned
    // advancement item's location, phrased for an MM stone to say. Weight mirrors the item's own
    // required/weight standing so cross entries compete fairly with MM's native pool (grill #3).
    for (auto& c : candidates) {
        if (c.itemGame != GAME_OOT)
            continue;
        Tri itemName = PickTemplate(c.itemHint, 2, rng);
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
