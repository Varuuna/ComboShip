#include "core/ComboGeneration.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "core/ComboAnchorNet.h"
#include "core/ComboContainer.h"
#include "core/ComboCrossItems.h"
#include "core/ComboDllApi.h"
#include "core/ComboFillDriver.h"
#include "core/ComboGoal.h"
#include "core/ComboSeedMath.h"
#include "core/ComboSeedState.h"
#include "rando/ComboPlaythrough.h"
#include "rando/CrossForeign.h"
#include "rando/CrossHints.h"

// ComboShip (#169): fire OOT's generation-completion hooks at most once per seed per machine, so the
// silent auto-load on every boot cannot re-roll over cosmetic/audio edits the user made by hand.
// force = fresh generation: still claim the seed (so later loads of it leave manual edits alone) but
// roll regardless, keeping vanilla's unconditional gen-only semantics.
void Combo_FireGenRollHooksOnce(uint64_t masterSeed, bool force) {
    if (!SOH_FireGenerationCompleteHooks)
        return;
    const bool claimed = ComboUI_ClaimGenRollSeed && ComboUI_ClaimGenRollSeed(masterSeed);
    if (claimed || force)
        SOH_FireGenerationCompleteHooks();
}

// Simple xorshift32 used for a random seed when none is provided.
int ComboRandRange(int minV, int maxV) {
    static uint32_t s =
        0x9E3779B9u ^ static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFFFFFFu);
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    int range = maxV - minV + 1;
    return minV + (range > 0 ? static_cast<int>(s % static_cast<uint32_t>(range)) : 0);
}



// ComboShip: write a seed's spoiler under its own hash-icon name. Returns the path (empty on failure).
// Worker-safe: pointing the CVar at it is a separate main-thread step (RememberComboSpoiler).
std::filesystem::path WriteComboSpoiler(const nlohmann::json& fileHash, const std::string& json) {
    std::error_code ec;
    std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
    auto path = ComboRando::ComboSpoilerPath(fileHash);
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "[ComboShip] could not write spoiler " << path.string() << std::endl;
        return {};
    }
    out << json;
    return path;
}

// ComboShip: mark a spoiler as the one a restart reloads. MAIN THREAD ONLY — this writes a CVar and
// saves the config, and libultraship's ConsoleVariable map is unlocked (same race as the apply below).
void RememberComboSpoiler(const std::filesystem::path& path) {
    if (!SOH_SetComboSpoilerPath || path.empty())
        return;
    // Absolute: WriteComboSpoiler returns a CWD-relative path, which stops resolving if the game is
    // ever launched from another directory (shortcut, launcher, debugger).
    std::error_code ec;
    auto abs = std::filesystem::absolute(path, ec);
    SOH_SetComboSpoilerPath((ec ? path : abs).string().c_str());
}

// Forward decl: defined later, called from RunComboFill on every successful in-game generation.
// playthroughOut (optional) receives the structured sphere playthrough for the consolidated file.
void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut = nullptr, const std::string& sohDump = "",
                                  const std::string& mmDump = "", ComboRando::CwGoal goal = {}, bool mmStart = false);

// The launcher's export pointers as fill hooks; comborando builds the same table from its own.
static CwFillHooks ComboLauncherFillHooks() {
    CwFillHooks h;
    h.SetSeedOot = SOH_SetComboRandoSeed;
    h.SetSeedMm = MM_SetComboRandoSeed;
    h.SetGoalOot = SOH_SetComboGoal;
    h.SetGoalMm = MM_SetComboGoal;
    h.SetStartingGame = SOH_SetComboStartingGame;
    h.DumpOot = SOH_DumpRandoStaticData;
    h.DumpMm = MM_DumpRandoStaticData;
    h.GetForced = SOH_GetForcedPlacements;
    h.ShuffleEntrances = SOH_ShuffleEntrancesForCombo;
    return h;
}

// ComboShip: worker that runs the combined-logic fill (or no-logic fallback) on a background
// thread, reports progress via the ComboGenProgress struct, and stashes placements.
void RunComboFill(std::string inputSeed, ComboRando::ComboGenProgress* progress) {
    auto fail = [&](const char* msg) {
        if (progress) {
            progress->SetError(msg);
            progress->success.store(false);
            progress->done.store(true);
            progress->running.store(false);
        }
        std::cerr << "[ComboShip] RunComboFill: " << msg << "\n";
        RestoreLoadedSlotGoal(); // a bailed generation must not leave the menu goal in the DLLs
        g_GenerateBusy.store(false);
    };

    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        fail("dump functions not resolved");
        return;
    }

    if (inputSeed.empty())
        inputSeed = std::to_string(ComboRandRange(0, 1000000));
    const uint32_t baseSeed = ComboHash(inputSeed.c_str());
    uint32_t masterSeed = baseSeed;

    // ComboShip (#136): the combo-owned goal. Read via soh.dll — the launcher has no CVar access.
    ComboRando::CwGoal goal;
    if (SOH_ReadComboGoalCVars) {
        int required = 0, total = -1;
        goal.hunt = SOH_ReadComboGoalCVars(&required, &total) != 0;
        goal.required = goal.hunt ? required : 0;
        goal.total = total; // kept even for bosses, so each game's own piece slider is forced to 0
    }
    if (goal.hunt && goal.required < 1) {
        fail("Triforce Hunt needs at least 1 required piece");
        return;
    }
    // ComboShip (#135): 0 = OOT, 1 = MM, 2 = Random (resolved per attempt below).
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    bool pinStartOot = false; // Random: after an MM-start attempt fails, fall back silently to OOT
    bool resolvedMmStart = false;

    std::string sohDump, mmDump, spoiler, lastFillError, sohHintDump;
    bool usedCombinedFill = false;
    nlohmann::json playthroughJson = nlohmann::json::array(); // structured sphere playthrough (combined-fill only)
    ComboRando::RequirednessResult pareDownResult;            // cross-hint Phase 3 WotH/foolish classification
    // ComboShip: checkName -> OOT area, computed once from sohHintDump right after the winning attempt's
    // dump (below) for the pare-down call.
    std::unordered_map<std::string, std::string> ootCheckAreasCache;

    // ComboShip: checkName -> area/region string, from each game's own dump, for the pare-down
    // (foolish-area rollup).
    auto buildOotCheckAreas = [](const std::string& hintDumpJson) {
        std::unordered_map<std::string, std::string> out;
        try {
            auto hd = nlohmann::json::parse(hintDumpJson.empty() ? "{}" : hintDumpJson);
            for (auto& c : hd.value("checks", nlohmann::json::array())) {
                std::string name = c.value("name", ""), area = c.value("area", "");
                if (!name.empty() && !area.empty())
                    out.emplace(std::move(name), std::move(area));
            }
        } catch (...) {}
        return out;
    };
    auto buildMmCheckAreas = [](const std::string& dumpJson) {
        std::unordered_map<std::string, std::string> out;
        try {
            auto d = nlohmann::json::parse(dumpJson.empty() ? "{}" : dumpJson);
            const auto locHints = d.value("locationHints", nlohmann::json::object());
            for (auto& [chk, region] : locHints.items())
                out.emplace(chk, region.get<std::string>());
        } catch (...) {}
        return out;
    };

    const bool haveOracles = Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems &&
                             Combo_SOH_Rando_GetReachableChecks && Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset &&
                             Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks &&
                             Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore;
    // Stale soh.dll: refuse rather than fall back to an ungated fill, which would silently reintroduce
    // portal-unreachable (softlockable) seeds. See docs/deviations/rando.md.
    if (haveOracles && !Combo_SOH_Rando_GetPortalOpen) {
        fail("stale soh.dll: Combo_SOH_Rando_GetPortalOpen missing, portal gate unavailable");
        return;
    }

    // Whole-fill retries (GAP-4): each attempt re-derives the master seed, so dumps, confined placement,
    // and prices re-roll deterministically per attempt. Budget lives in CrossWorldRando.h.
    const int kFillAttempts = ComboRando::kFillAttempts;
    for (int attempt = 0; attempt < kFillAttempts && !usedCombinedFill; ++attempt) {
        // Space each retry's seed far apart (golden-ratio step) so attempts don't correlate.
        masterSeed = baseSeed + attempt * 0x9E3779B9u;
        ResetCrossItemDedupForSeed(masterSeed);
        // Random's LAST attempt always resolves OOT, so Random can never hard-fail on an MM-start attempt.
        const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == kFillAttempts) &&
                             ResolveStartingGameMM(startCfg, masterSeed);

        CwPrologueOut pro;
        const CwPrologue prologue = ComboFillPrologue(ComboLauncherFillHooks(), masterSeed, mmStart, goal, pro);
        sohDump = pro.sohDump;
        mmDump = pro.mmDump;
        resolvedMmStart = mmStart; // these dumps used it, so whatever spoiler they end up in must record it
        if (prologue == CwPrologue::EmptyDump) {
            fail("empty static-data dump");
            return;
        }
        if (prologue == CwPrologue::TriforceShort) {
            fail((std::string("Triforce Hunt needs ") + std::to_string(goal.required) + " pieces but only " +
                  std::to_string(pro.poolPieces) + " are in the combined pool — raise the Combo menu's pool total")
                     .c_str());
            return;
        }
        if (prologue == CwPrologue::ShuffleFailed) {
            // A different masterSeed yields a different layout, so reroll rather than sending the user
            // to the settings; the post-loop check reports it if every attempt fails. Mirrors the loop
            // tail's MM restore, which this skips.
            lastFillError = "OOT entrance shuffle found no valid layout — relax the entrance settings";
            std::cout << "[ComboShip] RunComboFill: attempt " << (attempt + 1) << "/" << kFillAttempts
                      << " failed: " << lastFillError << "\n";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            if (Combo_MM_Rando_Restore)
                Combo_MM_Rando_Restore();
            continue;
        }
        const std::string& forcedOot = pro.forcedOot;
        if (!haveOracles)
            break; // no oracles -> no-logic fallback below; the dumps are still needed

        ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                            Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                            Combo_SOH_Rando_GetPortalOpen };
        ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                           Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };

        // ComboShip: honor OOT's logic/ALR settings per-game (MM stays all-reachable). The fill gates MM
        // on the portal region via ootOracle.GetPortalOpen; NO_LOGIC bypasses it.
        ComboRando::OotAccess ootAccess = ComboRando::OotAccessFromDump(sohDump);
        auto result =
            ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, progress, forcedOot,
                                               ootAccess, goal, mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);

        if (result.success) {
            spoiler = result.spoilerJson;
            usedCombinedFill = true;
            std::cout << "[ComboShip] RunComboFill: combined-logic fill succeeded (seed=" << masterSeed << ", attempt "
                      << (attempt + 1) << ")\n";
            // ComboShip: cross-hint schema dump (Phase 2) — must run on THIS attempt's still-live OOT
            // Context, before anything re-runs FinalizeSettings (which would re-roll RNG-derived state
            // like trial selection) or the reload-path force-off touches the hint options.
            if (SOH_DumpRandoHintData) {
                sohHintDump = SOH_DumpRandoHintData();
            }
            ootCheckAreasCache = buildOotCheckAreas(sohHintDump); // parsed once, reused below and after the loop
            // ComboShip: requiredness pare-down (Phase 3) — needs the STILL-LIVE oracle session, so it
            // runs before WriteComboPlaythrough (which restores MM internally). Doesn't restore itself;
            // the WriteComboPlaythrough call below (or the loop's own restore) does that once. Skipped
            // entirely when no enabled hint surface consumes requiredness (empty result = all non-required).
            const bool noLogic = ootAccess == ComboRando::OotAccess::NO_LOGIC;
            if (goal.hunt && noLogic) {
                // Any piece substitutes for any other and OOT may be unbeatable by design, so nothing
                // is meaningfully "required" — same compromise as MmOnlyMajoraGoal.
                std::cout << "[ComboShip] RunComboFill: pare-down skipped (No Logic + Triforce Hunt)\n";
            } else if (ComboRando::NeedsRequirednessPareDown(sohHintDump, mmDump)) {
                // NO_LOGIC: gate requiredness on MM only (OOT may be unbeatable by design).
                pareDownResult =
                    ComboRando::PareDownPlaythrough(result.spoilerJson, ootOracle, mmOracle, nullptr, sohDump, mmDump,
                                                    ootCheckAreasCache, buildMmCheckAreas(mmDump),
                                                    goal.hunt ? ComboRando::MakeTriforceHuntGoal(goal.required)
                                                    : noLogic ? ComboRando::MmOnlyMajoraGoal
                                                              : ComboRando::DefaultGanonMajoraGoal,
                                                    !noLogic, mmStart, progress);
            } else {
                std::cout << "[ComboShip] RunComboFill: pare-down skipped (no enabled hint surface needs "
                             "requiredness)\n";
            }
            // ComboShip: write the sphere-by-sphere playthrough log. Replays reachability via the
            // oracles BEFORE SOH_ApplyRandoPlacements restores the live OOT context, so it can't
            // corrupt the generated seed. Restores MM itself.
            WriteComboPlaythrough(result.spoilerJson, ootOracle, mmOracle, inputSeed, &playthroughJson, sohDump, mmDump,
                                  goal, mmStart);
        } else {
            lastFillError = result.error;
            std::cout << "[ComboShip] RunComboFill: attempt " << (attempt + 1) << "/" << kFillAttempts
                      << " failed: " << lastFillError << "\n";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
        }
        Combo_MM_Rando_Restore();
    }

    if (haveOracles && !usedCombinedFill) {
        std::string msg =
            std::string("combined fill failed after ") + std::to_string(kFillAttempts) + " attempts — " + lastFillError;
        // #135: explicit MM start hard-fails (Random would have fallen back to OOT by now), and the
        // usual cause is an OOT that an itemless strayed player cannot walk back out of.
        if (startCfg == 1)
            msg += " | Starting Game is Majora's Mask, which also requires the OOT->MM portal to stay "
                   "re-openable from an empty OOT start — loosen the OOT access settings (Closed Forest, "
                   "Door of Time, Lock Overworld Doors) or set Starting Game back to Ocarina of Time";
        fail(msg.c_str());
        return;
    }

    if (!usedCombinedFill) {
        spoiler = ComboRando::CrossWorldGenerateSpoiler(sohDump, mmDump, masterSeed);
        std::cout << "[ComboShip] RunComboFill: using no-logic fallback (seed=" << masterSeed << ")\n";
    }

    try {
        std::error_code ec;
        std::filesystem::create_directories(ComboRando::ConsolidatedDir(), ec);
        // ComboShip: all per-seed data (placements, foreign, settings, structured playthrough) is
        // assembled into one consolidated spoiler below and written to the pending file.

        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());

        // ComboShip: resolve human display names for foreign items from the dumps' items arrays
        // (each entry: {name, displayName}). The fill only carries itemName (the grant key:
        // English for OOT, RI_* for MM); displayName feeds toasts/shop text in the check's game.
        // Also carries each item's advancement flag (name -> is-progression) so the collecting game
        // knows whether a foreign item should play the held-up pickup animation.
        auto buildNameMap = [](const std::string& dump, std::unordered_map<std::string, bool>& advOut) {
            std::unordered_map<std::string, std::string> m;
            try {
                auto d = nlohmann::json::parse(dump);
                for (auto& it : d.value("items", nlohmann::json::array())) {
                    std::string n = it.value("name", "");
                    std::string dn = it.value("displayName", "");
                    if (n.empty())
                        continue;
                    advOut[n] = it.value("advancement", false);
                    if (!dn.empty())
                        m.emplace(std::move(n), std::move(dn));
                }
            } catch (...) {}
            return m;
        };
        std::unordered_map<std::string, bool> ootAdv, mmAdv;
        auto ootNames = buildNameMap(sohDump, ootAdv);
        auto mmNames = buildNameMap(mmDump, mmAdv);

        // ComboShip: OOT's curated ice-trap disguise set — carried into the apply payload (below) and
        // the consolidated spoiler so a reload restores it instead of deriving one from placements.
        nlohmann::json ootIceTrapModels = nlohmann::json::array();
        try {
            ootIceTrapModels = nlohmann::json::parse(sohDump).value("iceTrapModels", nlohmann::json::array());
        } catch (...) {}
        for (auto& fm : foreignArr) {
            std::string itemGame = fm.value("itemGame", "");
            std::string itemName = fm.value("itemName", "");
            if (itemGame != "mm" && itemGame != "oot")
                continue; // malformed marker: leave unstamped
            const auto& names = (itemGame == "mm") ? mmNames : ootNames;
            auto it = names.find(itemName);
            if (it != names.end()) {
                fm["displayName"] = it->second;
            }
            const auto& adv = (itemGame == "mm") ? mmAdv : ootAdv;
            auto ait = adv.find(itemName);
            if (ait != adv.end()) {
                fm["advancement"] = ait->second;
            }
        }

        // ComboShip: disguise cross-placed traps as a plausible progression item of their own game.
        ComboRando::AssignTrapDisguises(foreignArr, j.value("oot", nlohmann::json::object()),
                                        j.value("mm", nlohmann::json::object()), sohDump, mmDump, masterSeed);

        // The apply payloads (fed to each game's placement injection) hold the SENTINEL for foreign
        // checks — the check's own game places the sentinel and diverts the real item cross-game. The
        // consolidated spoiler placements (below) instead show the real item name for readability (#1).
        // Only OOT's is built here: OOT applies right after generation, whereas MM applies at slot-bind
        // and re-derives its payload from the consolidated seed (ApplyPayloadFromConsolidated).
        nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
        nlohmann::json ootSpoiler = ootApply; // copy real-name placements before sentinel overwrite
        nlohmann::json mmSpoiler = j.value("mm", nlohmann::json::object());
        for (const auto& fm : foreignArr) {
            std::string cg = fm.value("checkGame", "");
            std::string cn = fm.value("checkName", "");
            if (cn.empty())
                continue;
            std::string dn = fm.value("displayName", fm.value("itemName", ""));
            if (cg == "oot") {
                ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
                if (!dn.empty())
                    ootSpoiler[cn] = dn;
            } else if (cg == "mm" && !dn.empty()) {
                mmSpoiler[cn] = dn;
            }
        }
        // Reserved apply-only key (ootSpoiler was copied above, so it stays a pure placement map).
        ootApply["__iceTrapModels"] = ootIceTrapModels;

        // ComboShip: the gSaveContext-mutating apply (SOH_ApplyRandoPlacements) and the seed-hash set
        // MUST run on the main thread — the worker only computes. Stash their inputs for
        // Combo_FinalizeGenerate, which the main-thread file-select poll runs once it sees done.
        // The OOT seed-hash folds in input-seed + both settings dumps so the icons identify seed and
        // settings (same seed+settings -> matching icons across players).
        uint32_t displaySeed = ComboHash((inputSeed + sohDump + mmDump).c_str());
        g_FinalizeOotApply = ootApply.dump();
        g_FinalizeDisplaySeed = displaySeed;
        g_FinalizeMasterSeed = masterSeed;

        // ComboShip: file_hash = the 5 icon indexes the file-select shows, derived from displaySeed
        // exactly as OOT's GenerateHash (decimal padded to 10, five 2-digit pairs).
        std::string seedDigits = std::to_string(displaySeed);
        while (seedDigits.size() < 10)
            seedDigits = "0" + seedDigits;
        nlohmann::json fileHashArr = nlohmann::json::array();
        for (int i = 0; i < 5; ++i)
            fileHashArr.push_back(std::stoi(seedDigits.substr(i * 2, 2)));

        // ComboShip: assemble the single consolidated spoiler — the shareable artifact + the runtime
        // foreign source + remember/drop/hint data. Settings are CVar snapshots so a dropped seed
        // reproduces on any machine. Written now to the pending file (remembered); bound to a per-slot
        // file at Start (Combo_OnOOTSaveInit).
        auto parseOrEmpty = [](FnDumpData fn) -> nlohmann::json {
            if (!fn)
                return nlohmann::json::object();
            try {
                return nlohmann::json::parse(fn());
            } catch (...) { return nlohmann::json::object(); }
        };
        // ComboShip: suffix cross-game item-name collisions (e.g. "Mirror Shield") in the human-readable
        // placements so the consolidated file / plandomizer read unambiguously; each game strips its own
        // "(OOT)"/"(MM)" on apply. Foreign checks are skipped (carried by foreign[]).
        ComboRando::SuffixCrossGameItems(ootSpoiler, mmSpoiler, foreignArr, sohDump, mmDump);

        nlohmann::json consolidated;
        consolidated["fileType"] = "ComboShipRandomizer";
        consolidated["version"] = 1;
        consolidated["seed"] = inputSeed;
        consolidated["masterSeed"] = masterSeed;
        consolidated["displaySeed"] = displaySeed;
        consolidated["file_hash"] = fileHashArr;
        // Rolled shop/scrub/merchant prices (from the dumps) travel in the spoiler so the validator
        // and the reload path never guess them — unknown price is never treated as buyable.
        auto pricesOf = [](const std::string& dump) -> nlohmann::json {
            try {
                return nlohmann::json::parse(dump).value("prices", nlohmann::json::object());
            } catch (...) { return nlohmann::json::object(); }
        };
        consolidated["oot"] = { { "settings", parseOrEmpty(SOH_DumpRandoSettings) },
                                { "enabledTricks", parseOrEmpty(SOH_DumpEnabledTricks) },
                                { "placements", ootSpoiler },
                                { "prices", pricesOf(sohDump) },
                                { "iceTrapModels", ootIceTrapModels } };
        consolidated["mm"] = { { "settings", parseOrEmpty(MM_DumpRandoSettings) },
                               { "placements", mmSpoiler },
                               { "prices", pricesOf(mmDump) } };
        auto foreignEnriched = ComboRando::BuildForeignArray(foreignArr);
        consolidated["foreign"] = foreignEnriched;
        consolidated["playthrough"] = ComboRando::PlaythroughLines(playthroughJson);
        // ComboShip (#136): the goal is seed-bound — the runtime latch reads it back from the slot's
        // baked combo.rando, never from the live menu CVars.
        consolidated["goal"] = { { "type", goal.hunt ? "triforceHunt" : "bosses" },
                                 { "requiredPieces", goal.required },
                                 { "totalPieces", goal.total } };
        // ComboShip (#135): the resolved starting game — seed-bound like the goal, never re-rolled.
        consolidated["startingGame"] = resolvedMmStart ? "MM" : "OOT";
        // ComboShip (#90): OOT entrance layout — informational, reload re-derives it from masterSeed.
        {
            nlohmann::json ootEnt = nlohmann::json::array();
            if (SOH_DumpEntranceOverrides) {
                try {
                    ootEnt = nlohmann::json::parse(SOH_DumpEntranceOverrides());
                } catch (...) {}
            }
            consolidated["entrances"] = { { "oot", std::move(ootEnt) } };
        }
        // ComboShip: cross-game hint generation (Phase 3) — real per-seed hint assignments, from the
        // pare-down computed above. usedCombinedFill guards the no-logic fallback path (no oracles/
        // pare-down data there): that path ships with an empty hints payload, same as before Phase 3.
        consolidated["hints"] = usedCombinedFill ? ComboRando::Generate(masterSeed, sohDump, sohHintDump, mmDump,
                                                                        foreignEnriched, spoiler, pareDownResult)
                                                 : nlohmann::json{ { "version", 1 } };
        g_ConsolidatedJson = consolidated.dump(2);

        // This seed's own spoiler, so earlier seeds survive instead of being overwritten. The CVar that
        // makes it the remembered one is set by Combo_FinalizeGenerate (main thread).
        g_FinalizeSpoilerPath = WriteComboSpoiler(consolidated["file_hash"], g_ConsolidatedJson);
        std::cout << "[ComboShip] RunComboFill: placements computed; spoiler written to "
                  << g_FinalizeSpoilerPath.string() << "\n";

        if (progress) {
            progress->seed.store(masterSeed);
            // The reproducible token is the (resolved) input seed string, not masterSeed: paste it
            // back into the Seed field + same settings to reproduce. For a blank input this is the
            // concrete random string chosen above.
            std::strncpy(progress->seedStr, inputSeed.c_str(), sizeof(progress->seedStr) - 1);
            progress->seedStr[sizeof(progress->seedStr) - 1] = '\0';
            progress->foreignCount.store(static_cast<int>(foreignArr.size()));
            // Per-game contributed check counts = size of each settings-scoped dump pool.
            try {
                progress->ootCheckCount.store(
                    static_cast<int>(nlohmann::json::parse(sohDump).value("checks", nlohmann::json::array()).size()));
                progress->mmCheckCount.store(
                    static_cast<int>(nlohmann::json::parse(mmDump).value("checks", nlohmann::json::array()).size()));
            } catch (...) {}
            progress->success.store(true);
            progress->done.store(true);
        }
        g_ComboPendingFinalize.store(true);
    } catch (const std::exception& e) {
        fail((std::string("post-fill exception: ") + e.what()).c_str());
        return;
    }
    RestoreLoadedSlotGoal();
    g_GenerateBusy.store(false);
}

// ComboShip: headless cross-world generation TEST (COMBO_GENTEST=<count>). Runs the combined fill
// over a seed range; a seed "succeeds" only if every advancement check in both games is reachable
// from an empty start (honoring the OOT->MM portal gate) — i.e. provably 100%-completable. Uses the
// same oracles as the real generator under the current CVars. Returns the FAILED seed count.
int RunComboGenTest(int numSeeds, uint32_t seedBase) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_SOH_Rando_GetPortalOpen && Combo_MM_Rando_Reset &&
          Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem &&
          Combo_MM_Rando_Restore)) {
        std::cerr << "[GENTEST] oracle exports unavailable — cannot run\n";
        return -1;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[GENTEST] dump functions not resolved — cannot run\n";
        return -1;
    }
    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_GetPortalOpen };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };

    std::cout << "[GENTEST] running " << numSeeds << " cross-world generations (seedBase=" << seedBase
              << ") — asserting every advancement item is reachable from an empty start in both games\n";
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    int failures = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < numSeeds; ++i) {
        const uint32_t baseSeed = seedBase + static_cast<uint32_t>(i);
        ComboRando::CombinedFillResult result{};
        bool pinStartOot = false; // #135: Random falls back to OOT after a failed MM-start attempt
        // Mirror RunComboFill's whole-fill retries: a seed rejected on attempt 0 is a PASS in-game
        // after one reroll, so counting it FAIL here would inflate the failure rate.
        for (int attempt = 0; attempt < ComboRando::kFillAttempts && !result.success; ++attempt) {
            const uint32_t seed = baseSeed + attempt * 0x9E3779B9u;
            const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == ComboRando::kFillAttempts) &&
                                 ResolveStartingGameMM(startCfg, seed);
            // Seeds before the dump (shop/scrub choices are seed-derived and made inside it); forced
            // placements before the shuffle, whose ItemReset wipes the placement they read.
            if (SOH_SetComboRandoSeed)
                SOH_SetComboRandoSeed(seed);
            if (MM_SetComboRandoSeed)
                MM_SetComboRandoSeed(seed);
            if (SOH_SetComboStartingGame)
                SOH_SetComboStartingGame(mmStart ? 1 : 0);
            std::string sohDump = SOH_DumpRandoStaticData();
            std::string mmDump = MM_DumpRandoStaticData();
            if (sohDump.empty() || mmDump.empty()) {
                std::cerr << "[GENTEST] empty dump — cannot run\n";
                return -1;
            }
            std::string forcedOot;
            if (SOH_GetForcedPlacements)
                forcedOot = SOH_GetForcedPlacements(seed);
            // Per-seed OOT entrance layout, exactly like the real generator (no-op when the options are off).
            if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(seed)) {
                result.error = "OOT entrance shuffle found no valid layout";
                if (mmStart && startCfg == 2)
                    pinStartOot = true;
                Combo_MM_Rando_Restore();
                continue; // a different masterSeed yields a different layout — reroll, like RunComboFill
            }
            result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, seed, ootOracle, mmOracle, nullptr, forcedOot,
                                                        ComboRando::OotAccessFromDump(sohDump), {},
                                                        mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);
            if (!result.success && mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore(); // reset the MM oracle's snapshot guard for the next fill
        }
        if (result.success) {
            std::cout << "[GENTEST]   seed " << baseSeed << " PASS\n";
        } else {
            std::cerr << "[GENTEST]   seed " << baseSeed << " FAIL: " << result.error << "\n";
            ++failures;
        }
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (failures == 0) {
        std::cout << "[GENTEST] RESULT: PASS — " << numSeeds << "/" << numSeeds
                  << " seeds fully completable (cross-game), " << ms << " ms\n";
    } else {
        std::cerr << "[GENTEST] RESULT: FAIL — " << failures << "/" << numSeeds
                  << " seeds could not place all progression reachably, " << ms << " ms\n";
    }
    return failures;
}

// ComboShip: headless cross-world PLAYTHROUGH log (COMBO_PLAYTHROUGH=<seed>). Replays an
// already-generated spoiler sphere by sphere, listing each item in obtainable order across both
// games (OOT->MM portal honored) until BEATABLE (Ganon goal + Majora's Lair both reachable). Full
// log to saves/combo/slot0.playthrough.txt. Restores the MM oracle snapshot at the end (drives MM
// here). Called from the env-gated entry and from RunComboFill. See docs/deviations/rando.md.
void WriteComboPlaythrough(const std::string& spoilerJson, const ComboRando::OracleFns& ootOracle,
                                  const ComboRando::OracleFns& mmOracle, const std::string& seedLabel,
                                  nlohmann::json* playthroughOut, const std::string& sohDump, const std::string& mmDump,
                                  ComboRando::CwGoal goal, bool mmStart) {
    // Thin wrapper over the shared traversal (combo/rando/ComboPlaythrough.h); passes this build's
    // MM oracle-restore pointer. A playthroughOut here means the spoiler's playthrough section, which
    // lists progression only; the text-log path and the headless validator keep every step.
    ComboRando::RunPlaythrough(spoilerJson, ootOracle, mmOracle, seedLabel, Combo_MM_Rando_Restore, playthroughOut,
                               sohDump, mmDump,
                               ComboRando::OotAccessFromDump(sohDump) != ComboRando::OotAccess::NO_LOGIC,
                               /*progressionOnly*/ playthroughOut != nullptr, goal, mmStart);
}

// Env-gated entry: COMBO_PLAYTHROUGH=<seed> generates that seed headless, then writes its log.
void RunComboPlaythrough(const std::string& inputSeed) {
    if (!(Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
          Combo_SOH_Rando_PlaceItem && Combo_SOH_Rando_GetPortalOpen && Combo_MM_Rando_Reset &&
          Combo_MM_Rando_SetOwnedItems && Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem &&
          Combo_MM_Rando_Restore)) {
        std::cerr << "[PLAYTHROUGH] oracle exports unavailable\n";
        return;
    }
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) {
        std::cerr << "[PLAYTHROUGH] dump functions not resolved\n";
        return;
    }
    ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                        Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem,
                                        Combo_SOH_Rando_GetPortalOpen };
    ComboRando::OracleFns mmOracle = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                       Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };
    std::string seedStr = inputSeed.empty() ? "1" : inputSeed;
    const uint32_t baseSeed = ComboHash(seedStr.c_str());
    std::string sohDump, mmDump;
    ComboRando::CombinedFillResult fill{};
    const int startCfg = SOH_ReadComboStartingGameCVar ? SOH_ReadComboStartingGameCVar() : 0;
    bool pinStartOot = false, resolvedMmStart = false; // #135, same fallback as RunComboFill
    // Mirror RunComboFill including its retries — the player's seed may have come from attempt 1, and
    // validating only attempt 0 would either report "did not generate" or log a world they never got.
    for (int attempt = 0; attempt < ComboRando::kFillAttempts && !fill.success; ++attempt) {
        const uint32_t masterSeed = baseSeed + attempt * 0x9E3779B9u;
        const bool mmStart = !pinStartOot && !(startCfg == 2 && attempt + 1 == ComboRando::kFillAttempts) &&
                             ResolveStartingGameMM(startCfg, masterSeed);
        if (SOH_SetComboRandoSeed)
            SOH_SetComboRandoSeed(masterSeed);
        if (MM_SetComboRandoSeed)
            MM_SetComboRandoSeed(masterSeed);
        if (SOH_SetComboStartingGame)
            SOH_SetComboStartingGame(mmStart ? 1 : 0);
        sohDump = SOH_DumpRandoStaticData();
        mmDump = MM_DumpRandoStaticData();
        if (sohDump.empty() || mmDump.empty()) {
            std::cerr << "[PLAYTHROUGH] empty static-data dump\n";
            return;
        }
        // Read before the entrance shuffle: its ItemReset wipes the placement this reads.
        std::string forcedOot;
        if (SOH_GetForcedPlacements)
            forcedOot = SOH_GetForcedPlacements(masterSeed);
        if (SOH_ShuffleEntrancesForCombo && !SOH_ShuffleEntrancesForCombo(masterSeed)) {
            fill.error = "OOT entrance shuffle found no valid layout";
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore();
            continue;
        }
        fill = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, nullptr, forcedOot,
                                                  ComboRando::OotAccessFromDump(sohDump), {},
                                                  mmStart ? ComboRando::GAME_MM : ComboRando::GAME_OOT);
        if (!fill.success) {
            if (mmStart && startCfg == 2)
                pinStartOot = true;
            Combo_MM_Rando_Restore();
        } else {
            resolvedMmStart = mmStart;
        }
    }
    if (!fill.success) {
        std::cerr << "[PLAYTHROUGH] seed '" << seedStr << "' did not generate: " << fill.error << "\n";
        return;
    }
    // restores MM at the end
    WriteComboPlaythrough(fill.spoilerJson, ootOracle, mmOracle, seedStr, nullptr, sohDump, mmDump, {},
                          resolvedMmStart);
}

// ComboShip: synchronous generate — used only by the headless COMBO_AUTOGEN_SEED path. The UI
// registers Combo_OnGenerateThreaded instead. Reentrancy-guarded via g_GenerateBusy.
void Combo_OnGenerateRequest(const char* inputSeed, ComboRando::ComboGenProgress* progress) {
    if (g_GenerateBusy.exchange(true)) {
        // Already running — ignore the duplicate request.
        if (progress) {
            progress->SetError("generate already in progress");
            progress->done.store(true);
        }
        return;
    }
    RunComboFill(std::string(inputSeed ? inputSeed : ""), progress);
}

// ComboShip: UI-driven (non-blocking) generate — registered as the generate-request callback and
// invoked on the main thread from SOH_TriggerComboGenerate. Spawns the worker so the main loop keeps
// rendering + playing music + animating progress. The previous worker is always finished by now
// (reentry is gated on RandoGenerating in soh + g_GenerateBusy here), but join it to recycle the
// std::thread object. The gSaveContext apply happens later on the main thread (Combo_PollFinalize).
void Combo_OnGenerateThreaded(const char* inputSeed) {
    // Reject if a worker is running OR a finalize is still pending (apply not yet run on main thread).
    if (g_ComboPendingFinalize.load() || g_GenerateBusy.exchange(true)) {
        std::cerr << "[ComboShip] generate already in progress — ignoring duplicate request\n";
        return;
    }
    if (g_GenerateThread.joinable())
        g_GenerateThread.join(); // recycle the finished previous worker's thread object
    g_ComboProgress.Reset();
    g_ComboProgress.done.store(false);
    g_ComboProgress.running.store(true);
    std::string seed(inputSeed ? inputSeed : "");
    // RunComboFill clears g_GenerateBusy when it finishes; the finalize gate then blocks re-trigger
    // until the main-thread apply runs. Call RunComboFill directly (busy is already held).
    g_GenerateThread = std::thread([seed]() { RunComboFill(seed, &g_ComboProgress); });
}

// ComboShip: cross-hint Phase 3 — "hints" only contains "oot" for a seed CrossHints::Generate actually
// ran on; older/no-logic-fallback seeds keep the Phase 2 {"version":1} scaffold and fall back to the
// pre-Phase-3 force-off behavior (back-compat).
// ComboShip: slices the "hints" sub-object out of the consolidated spoiler once (parse-once — this
// used to be two separate re-parses of the whole consolidated blob just to check/extract one field).
nlohmann::json ComboHintsJsonFrom(const std::string& consolidatedJson) {
    try {
        return nlohmann::json::parse(consolidatedJson).value("hints", nlohmann::json::object());
    } catch (...) { return nlohmann::json::object(); }
}

// ComboShip: main-thread finalize — the gSaveContext-mutating apply + seed-hash set. Runs from
// Combo_PollFinalize on the main thread once the worker has stashed its result. NEVER call from the
// worker thread (that race crashed the prior threaded attempt).
void Combo_FinalizeGenerate() {
    nlohmann::json hints = ComboHintsJsonFrom(g_ConsolidatedJson);
    bool hintsPresent = hints.contains("oot");
    if (SOH_SetComboHintsPresent)
        SOH_SetComboHintsPresent(hintsPresent ? 1 : 0);
    if (SOH_ApplyRandoPlacements) {
        SOH_ApplyRandoPlacements(g_FinalizeOotApply.c_str());
        std::cout << "[ComboShip] Combo_FinalizeGenerate: OOT placements applied\n";
    } else if (SOH_SetSeedGenerated) {
        SOH_SetSeedGenerated(1);
    }
    if (SOH_SetComboSeedHash)
        SOH_SetComboSeedHash(g_FinalizeDisplaySeed);
    RememberComboSpoiler(g_FinalizeSpoilerPath); // worker wrote the file; the CVar is ours to set
    g_FinalizeSpoilerPath.clear();
    if (hintsPresent && SOH_ApplyComboHints)
        SOH_ApplyComboHints(hints.dump().c_str());
    // #169: a fresh generation always re-rolls (vanilla gen-only semantics), and claims the seed on
    // the way through so later loads of THIS seed leave the user's manual edits alone.
    Combo_FireGenRollHooksOnce(g_FinalizeMasterSeed, /*force=*/true);
    // A fresh generation's live MM CVars already ARE this seed's settings, so slot-bind must fall
    // through to reading them directly — clear any stale reload-restore state left by an unstarted
    // pending seed (else it would apply THAT seed's MM settings over this generation's placements).
    g_PendingMMSettingsJson.clear();
    g_UserMMSettingsSnapshot.clear();
    g_ComboReloadRestoreUserMM = false;
    g_ComboProgress.running.store(false);
}

// ComboShip: poll callback the file-select loop calls each frame on the main thread. Runs the
// pending finalize (apply) when the worker has succeeded. Returns 1 once generation is fully
// resolved (finalized or failed) so the caller can clear RandoGenerating; 0 while still working.
int Combo_PollFinalize() {
    if (g_ComboPendingFinalize.exchange(false)) {
        Combo_FinalizeGenerate();
        return 1;
    }
    // No pending finalize: resolved iff the worker is done and not still running.
    return (g_ComboProgress.done.load() && !g_GenerateBusy.load()) ? 1 : 0;
}
