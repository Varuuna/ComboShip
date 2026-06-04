# Cross-World Randomizer — Increment 2: Central-Assignment Generator + Rando-Only Saves

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Generate one combined no-logic placement across OOT + MM from a single master seed at OOT save creation, write the result into both games' (rando) save files, and emit a combined spoiler — replacing Increment 1's debug `cross_send` with real placement.

**Architecture:** A combo-layer generator (`combo/rando/CrossWorldRando.*`) runs at OOT save creation via a new `gComboGenerateCallback`. It pulls each game's check/item pools through new headless exports, does a single uniform no-logic assignment (master seed), writes a combined spoiler, then injects each game's slice: OOT in-memory (`Context::itemLocationTable` + `SetSeedGenerated`, consumed by the existing `Randomizer_InitSaveFile`), MM into its rando save file. Both games are forced to rando saves. Cross-placed items (an OOT check holding an MM item) are stored via a per-check **foreign-item marker**.

**Tech Stack:** C++17, `nlohmann::json` (shared), libultraship shared CVar store + `Gui`, the existing `extern "C" __declspec(dllexport)` SOH_/MM_ export pattern + `gCombo*Callback` function-pointer pattern, CMake (VS 2022, `build/x64`).

**Reference:** Spec `docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-scope-a.md` ("Increment 2 — Generation design" + "UX refinements"). Builds on Increment 1 (`combo/rando/CrossMailbox.h`, the mailbox channel). Memory `[[comboship-crossgame-randomizer]]`, `[[comboship-build-targets]]`, `[[document-post-merge-changes]]`, `[[comboship-hm64-principle]]`.

**Verification reality:** No unit-test harness. Per task: per-target clean build + artifact inspection (the combined spoiler JSON, the two save files) + in-game manual checks + `[ComboShip]` log lines. Build targets individually.

---

## Validated findings (from 2026-06-04 investigation — treat as ground truth)

**OOT (`soh.dll`), all in `soh/soh/Enhancements/randomizer/`:**
- Headless pool build: `ctx->FinalizeSettings({},{})` (`settings.cpp:2477`) → `ctx->GenerateLocationPool()` (`SeedContext.cpp:178`, fills `ctx->allLocations`) → `GenerateItemPool()` (`3drando/item_pool.cpp:149`, fills file-static `itemPool`). Needs `Random_Init(seed)` first if used (`3drando/random.cpp:8`). StaticData already inited at OOT boot (`OTRGlobals.cpp:945-948,1602`); `Rando::Settings::GetInstance()->AssignContext(ctx)` already done.
- Read placements: iterate `ctx->allLocations`; `ctx->GetItemLocation(rc)->GetPlacedRandomizerGet()` → `RandomizerGet`.
- Inject placements: `ctx->PlaceItemInLocation(rc, rg, false, false)` (`SeedContext.cpp:138`); then `ctx->SetSeedGenerated(true)` (`SeedContext.cpp:374`). `Randomizer_IsSeedGenerated()` (`OTRGlobals.cpp:2404`) reads `mSeedGenerated`.
- Name maps: `StaticData::locationNameToEnum` / `itemNameToEnum` (`static_data.h:76-77`); reverse via `StaticData::GetLocation(rc)->GetName()` and `StaticData::RetrieveItem(rg).GetName().GetEnglish()`.
- Save-init: `Randomizer_InitSaveFile()` (`savefile.cpp:240`) reads RSK options + a few specific checks (Link's Pocket etc.) and writes `gSaveContext`; consumes our pre-populated `itemLocationTable` when `IsSeedGenerated()` is true. Called from `Sram_InitSave` (`soh/src/code/z_sram.c:268-271`) when `questType[buttonIndex]==QUEST_RANDOMIZER`.
- Quest forced in `FileChoose_Init` (`z_file_choose.c:3107-3109`, set to `QUEST_RANDOMIZER`); seed-gate at `FileChoose_UpdateRandomizerMenu` (`z_file_choose.c:777`). `gComboSaveInitCallback` pattern at `OTRGlobals.cpp:2482-2486`; `gComboGenerateCallback` does NOT exist yet.

**MM (`2ship.dll`), in `mm/2s2h/`:**
- `Rando::Logic::GeneratePools(RandoSaveInfo&, vector<RandoCheckId>&, vector<RandoItemId>&)` (`Rando/Logic/GeneratePools.cpp:15`): reads `saveInfo.randoSaveOptions[RO_*]` + `CVarGetString("gRando.ExcludedChecks","")` + `Rando::Logic::Regions` + `StaticData::Checks/Items`; calls `Ship_Random`. No `gPlayState`/`gSaveContext`. **Regions is empty until `ShipInit::InitAll()` runs** (only in full `InitOTR`). → **need `MM_InitRandoRuntime()` shim** (`ShipInit::InitAll()` + `Rando::StaticData::PopulateCheckNames()`; safe headless).
- Inject: `RANDO_SAVE_CHECKS[rc].randoItemId = ri; .shuffled = true;` (+ `.price` for shops). `RandoSaveCheck` at `mm/include/z64save.h:372`. Or `Rando::Spoiler::ApplyToSaveContext(json)` (`Rando/Spoiler/Apply.cpp:14`).
- `saveType = SAVETYPE_RANDO` must be set. Combo MM save (`MM_InitSaveFile`→`SaveManager_InitNewSaveForSlot`, `SaveManager.cpp:140`) currently does NOT set it → MM save is vanilla (the bug the user saw). **Do NOT call `GrantStartingItems` headless** (`Item_Give`→`gPlayState`); it runs at MM's save-load.
- Spoiler interchange keys: check = `StaticData::Checks[rc].name`; item = `StaticData::Items[ri].spoilerName`. `RO_LOGIC` values (`Types.h:3005`): GLITCHLESS=0, NO_LOGIC=1, NEARLY_NO_LOGIC=2, VANILLA=3.
- Exports in `BenPort.cpp` (`extern "C" __declspec(dllexport)`).

**Shared:** CVar store is one instance across both DLLs (`consolevariablebridge.cpp`). Combined settings window best hosted in `soh.dll` (`SohGui::SetupGuiElements`, runs at boot + OOT resume), writing MM's `gRando.Options.*` + OOT's `gRandoSettings.*`.

---

## Phasing (scope-check: this increment is large; build/verify phase-by-phase)

- **Phase 1 — Generator pipeline, NATIVE-ONLY placement (this plan, full detail).** Validate the entire headless-generate → inject → combined-spoiler → rando-only-save pipeline with each game's items staying in its OWN checks (no cross-placement yet), driven by ONE combo generator + master seed. De-risks everything mechanical. *Verify:* generate → both saves rando, combined spoiler lists both games' placements, native checks deliver in-game.
- **Phase 2 — Cross-placement + foreign-item markers (outline below).** Assignment cross-places; add a per-check foreign descriptor to both saves + spoiler.
- **Phase 3 — Rando-only file-select UX + combined settings window (outline below).**
- **Increment 3 (separate plan) — real send interception at pickup + gift presentation** (consumes the foreign markers; receive already works from Increment 1).

---

## Phase 1 — File structure

- Create: `combo/rando/CrossWorldRando.h` (+ logic in header-only or a `.cpp` compiled into ComboShip) — the combo-layer generator: pull pools, assign, spoiler, orchestrate injection.
- Modify: `mm/2s2h/BenPort.cpp` — add `MM_InitRandoRuntime`, `MM_BuildRandoPools`, `MM_InitRandoSaveFile` exports.
- Modify: `soh/soh/OTRGlobals.cpp` — add `gComboGenerateCallback` + `SOH_SetOnComboGenerateCallback`, `SOH_BuildRandoPools`, `SOH_ApplyRandoPlacements` exports.
- Modify: `soh/src/code/z_sram.c` — fire `gComboGenerateCallback` before the quest read.
- Modify: `combo/ComboShip.cpp` — resolve new exports; register the generate callback; orchestrate at save creation.
- Modify: `soh/src/.../z_file_choose.c` + `mm/2s2h/SaveManager/SaveManager.cpp` — minimal rando-only enforcement.

Interchange JSON (combo ↔ DLLs): pools = `{"checks":[{"id":<int>,"name":"..."}], "items":[{"id":<int>,"name":"...","prog":<bool>}]}`; placement = `{"placements":[{"check":"<name>","item":"<name>"}]}` (names are each game's own spoiler strings).

---

### Task 1: MM headless rando-runtime shim + de-risk validation

**Files:** Modify `mm/2s2h/BenPort.cpp` (new export near the other `MM_*` exports ~line 2582).

- [ ] **Step 1: Add the export**

```cpp
// ComboShip: populate Rando::Logic::Regions + StaticData names WITHOUT a game loop, so headless
// pool-building (MM_BuildRandoPools) works at OOT-save-creation time. ShipInit::InitAll() just fires
// the region-registration lambdas (compile-time constants only). Idempotent-safe to call once.
extern "C" __declspec(dllexport) void MM_InitRandoRuntime(void) {
    static bool inited = false;
    if (inited) return;
    ShipInit::InitAll();                       // fills Rando::Logic::Regions
    Rando::StaticData::PopulateCheckNames();   // fills check name table
    inited = true;
    SPDLOG_INFO("[ComboShip] MM_InitRandoRuntime: Regions populated ({} entries)",
                Rando::Logic::Regions.size());
}
```
Add the needed includes at the top of BenPort.cpp if absent: `#include "2s2h/ShipInit.hpp"`, `#include "Rando/Logic/Logic.h"`, `#include "Rando/StaticData/StaticData.h"`. (Verify exact paths against existing includes; mm uses `2s2h/...` and `Rando/...` forms.)

- [ ] **Step 2: Build 2ship**

Run: `cmake --build build/x64 --target 2ship --config Debug`
Expected: clean.

- [ ] **Step 3: De-risk validation (temporary log)**

Temporarily, at the end of `MM_InitRandoRuntime`, also build a scratch pool and log its size to PROVE headless pools work:
```cpp
    RandoSaveInfo scratch{};
    for (auto& [id, opt] : Rando::StaticData::Options) scratch.randoSaveOptions[id] = opt.defaultValue;
    std::vector<RandoCheckId> checks; std::vector<RandoItemId> items;
    Rando::Logic::GeneratePools(scratch, checks, items);
    SPDLOG_INFO("[ComboShip] MM headless pools: {} checks, {} items", checks.size(), items.size());
```
Build 2ship. (This validates the spec's key risk. The actual call from the combo layer comes in Task 2; this temporary block is removed in Task 2.)

- [ ] **Step 4: Commit**

```bash
git add mm/2s2h/BenPort.cpp
git commit -m "feat(mm-rando): MM_InitRandoRuntime export for headless pool-building (+ de-risk log)"
```
End every commit body with: `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`

> **Controller note:** after this builds, run ComboShip once and confirm the log shows a non-zero MM check/item count (proves the headless shim). If it's zero, STOP — the central-assignment approach needs rework (escalate). This is the gate for the rest of the increment.

---

### Task 2: MM pool-enumeration + rando-save-write exports

**Files:** Modify `mm/2s2h/BenPort.cpp`.

- [ ] **Step 1: `MM_BuildRandoPools`** — returns MM's checks+items as JSON for given options. Replace the Task-1 temporary validation block; build the scratch `RandoSaveInfo` from an options JSON (`{"options":{"RO_NAME":val,...}}`, default to each option's default when absent), call `GeneratePools`, serialize check IDs+names (`StaticData::Checks[rc].name`) and item IDs+spoilerNames (`StaticData::Items[ri].spoilerName`). Return a `static std::string` (process-lifetime; caller copies before next MM call):

```cpp
extern "C" __declspec(dllexport) const char* MM_BuildRandoPools(const char* optionsJson) {
    MM_InitRandoRuntime();
    RandoSaveInfo scratch{};
    for (auto& [id, opt] : Rando::StaticData::Options) scratch.randoSaveOptions[id] = opt.defaultValue;
    try {
        auto oj = nlohmann::json::parse(optionsJson);
        for (auto& [id, opt] : Rando::StaticData::Options)
            if (oj.contains("options") && oj["options"].contains(opt.name))
                scratch.randoSaveOptions[id] = oj["options"][opt.name].get<uint32_t>();
    } catch (...) {}
    std::vector<RandoCheckId> checks; std::vector<RandoItemId> items;
    Rando::Logic::GeneratePools(scratch, checks, items);
    nlohmann::json out;
    for (auto rc : checks) out["checks"].push_back({ {"id",(int)rc}, {"name", Rando::StaticData::Checks[rc].name} });
    for (auto ri : items) out["items"].push_back({ {"id",(int)ri}, {"name", Rando::StaticData::Items[ri].spoilerName} });
    static std::string buf; buf = out.dump(); return buf.c_str();
}
```
(Verify `RandoStaticOption` field names `.name`/`.defaultValue`/`.cvar` against `StaticData.h`.)

- [ ] **Step 2: `MM_InitRandoSaveFile`** — write a rando MM save for a slot from a placement JSON (`{"finalSeed":N,"options":{...},"checks":{"<name>":"<itemSpoilerName>",...}}`, the `ApplyToSaveContext` schema):

```cpp
extern "C" __declspec(dllexport) void MM_InitRandoSaveFile(int fileNum, const char* placementJson) {
    MM_InitRandoRuntime();
    SaveManager_InitNewSaveForSlot(fileNum + 1);   // vanilla baseline (sets gSaveContext.save + combo first-cycle)
    gSaveContext.save.shipSaveInfo.saveType = SAVETYPE_RANDO;
    memset(&gSaveContext.save.shipSaveInfo.rando, 0, sizeof(RandoSaveInfo));
    try {
        Rando::Spoiler::ApplyToSaveContext(nlohmann::json::parse(placementJson));  // fills randoSaveChecks + options; NOT GrantStartingItems
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[ComboShip] MM_InitRandoSaveFile parse/apply failed: {}", e.what()); return;
    }
    gSaveContext.fileNum = (s16)fileNum;   // canonical 0-based slot (match SaveManager_LoadSaveFile)
    nlohmann::json j; j["newCycleSave"]["save"] = gSaveContext.save;
    j["version"] = CURRENT_SAVE_VERSION; j["type"] = "2S2H_SAVE";
    SaveManager_WriteSaveFile(SaveManager_GetFileName(fileNum + 1), j);
    SPDLOG_INFO("[ComboShip] MM_InitRandoSaveFile wrote rando save for slot {}", fileNum);
}
```
Add includes: `#include "Rando/Spoiler/Spoiler.h"`, `#include "2s2h/SaveManager/SaveManager.h"`. Confirm `ApplyToSaveContext` does NOT call `GrantStartingItems` (per investigation it doesn't in the apply path — verify; if it does, replicate its body minus the grant).

- [ ] **Step 3: Build 2ship** — `cmake --build build/x64 --target 2ship --config Debug`. Clean.
- [ ] **Step 4: Commit** — `git add mm/2s2h/BenPort.cpp` → `feat(mm-rando): MM_BuildRandoPools + MM_InitRandoSaveFile headless exports`.

---

### Task 3: OOT pool-enumeration + placement-apply exports

**Files:** Modify `soh/soh/OTRGlobals.cpp` (new exports near `Randomizer_*`/`SOH_*`).

- [ ] **Step 1: `SOH_BuildRandoPools`** — build OOT pools headless, return JSON (checks from `ctx->allLocations`, items from the `itemPool` global + reading already-placed fixed items). Sequence per the validated findings:

```cpp
extern "C" __declspec(dllexport) const char* SOH_BuildRandoPools(const char* /*optionsJson*/) {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    // Options already come from gRandoSettings CVars via Settings::SetAllToContext(); the combined
    // window writes those CVars before generation. Apply them to the context here:
    Rando::Settings::GetInstance()->SetAllToContext();   // verify exact accessor name
    ctx->GetLogic()->Reset();
    ctx->FinalizeSettings({}, {});
    ctx->GenerateLocationPool();
    nlohmann::json out;
    for (RandomizerCheck rc : ctx->allLocations)
        out["checks"].push_back({ {"id",(int)rc}, {"name", Rando::StaticData::GetLocation(rc)->GetName()} });
    // Item pool: build it, then read out the RG list (the to-place items).
    GenerateItemPool();
    extern std::vector<RandomizerGet> itemPool;   // 3drando/item_pool.cpp global
    for (RandomizerGet rg : itemPool)
        out["items"].push_back({ {"id",(int)rg}, {"name", Rando::StaticData::RetrieveItem(rg).GetName().GetEnglish()} });
    static std::string buf; buf = out.dump(); return buf.c_str();
}
```
(Verify: the exact `Settings` apply method, `GetLocation`/`RetrieveItem`/`GetName().GetEnglish()` accessors, and that `itemPool` is declared extern in `item_pool.hpp`. Adjust to real signatures.)

- [ ] **Step 2: `SOH_ApplyRandoPlacements`** — inject a placement JSON (`{"placements":[{"check":"<name>","item":"<name>"}]}`) into `itemLocationTable` and mark generated:

```cpp
extern "C" __declspec(dllexport) void SOH_ApplyRandoPlacements(const char* placementJson) {
    auto ctx = OTRGlobals::Instance->gRandoContext;
    try {
        auto j = nlohmann::json::parse(placementJson);
        for (auto& p : j["placements"]) {
            RandomizerCheck rc = Rando::StaticData::locationNameToEnum[p["check"].get<std::string>()];
            RandomizerGet  rg  = Rando::StaticData::itemNameToEnum[p["item"].get<std::string>()];
            ctx->PlaceItemInLocation(rc, rg, false, false);
        }
    } catch (const std::exception& e) { SPDLOG_ERROR("[ComboShip] SOH_ApplyRandoPlacements: {}", e.what()); return; }
    ctx->SetSeedGenerated(true);
    SPDLOG_INFO("[ComboShip] SOH_ApplyRandoPlacements: applied + seed marked generated");
}
```
(Phase 1 is native-only: `p["item"]` is always an OOT item name. Foreign items / markers = Phase 2.)

- [ ] **Step 3: Build soh** — `cmake --build build/x64 --target soh --config Debug`. Clean.
- [ ] **Step 4: Commit** — `git add soh/soh/OTRGlobals.cpp` → `feat(soh-rando): SOH_BuildRandoPools + SOH_ApplyRandoPlacements headless exports`.

---

### Task 4: The `gComboGenerateCallback` hook (game source, additive)

**Files:** Modify `soh/soh/OTRGlobals.cpp` (callback ptr + setter export) and `soh/src/code/z_sram.c` (fire it).

- [ ] **Step 1: Callback pointer + export** in `OTRGlobals.cpp` (mirror `gComboSaveInitCallback` at ~2482):
```cpp
extern "C" void (*gComboGenerateCallback)(int fileNum) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateCallback(void (*cb)(int fileNum)) {
    gComboGenerateCallback = cb;
}
```

- [ ] **Step 2: Fire it in `Sram_InitSave`** before the quest read (`z_sram.c:266`). Add the extern near the existing `gComboSaveInitCallback` extern (`z_sram.c:16`):
```c
#ifdef COMBO_BUILD
extern void (*gComboGenerateCallback)(int fileNum);
#endif
```
And before the `u8 currentQuest = ...;` line (266):
```c
#ifdef COMBO_BUILD
    if (gComboGenerateCallback != NULL) {
        gComboGenerateCallback((int)fileChooseCtx->buttonIndex);
        fileChooseCtx->questType[fileChooseCtx->buttonIndex] = QUEST_RANDOMIZER;  // force rando (combo is rando-only)
    }
#endif
```
Document in `docs/UPSTREAM_MERGES.md` (additive COMBO_BUILD game-source change).

- [ ] **Step 3: Build soh** — clean.
- [ ] **Step 4: Commit** — `git add soh/soh/OTRGlobals.cpp soh/src/code/z_sram.c` → `feat(soh): gComboGenerateCallback fired at OOT save creation (forces rando quest)`.

---

### Task 5: Combo-layer generator + orchestration

**Files:** Create `combo/rando/CrossWorldRando.h`; modify `combo/ComboShip.cpp`.

- [ ] **Step 1: `combo/rando/CrossWorldRando.h`** — the no-logic combined assignment (header-only, depends on `nlohmann/json` + `<random>`-free deterministic RNG seeded from master). Pseudocode-complete:
  - `std::string CrossWorldGenerate(const std::string& sohPoolsJson, const std::string& mmPoolsJson, uint32_t masterSeed)` →
    1. Parse both pools. Build a combined list of "items" (each tagged `game`) and "checks" (each tagged `game`).
    2. Deterministic shuffle (FNV-1a-seeded LCG matching the games' PRNG constants, or a simple seeded `std::mt19937`-free LCG — implement inline since `Math.random`-style isn't available; use the master seed) — Phase 1: assign each game's items to that SAME game's checks only (native-only), shuffled. Pad with junk if counts differ.
    3. Emit three JSONs: OOT placement (`{"placements":[{check,item}]}`, OOT-only), MM placement (`ApplyToSaveContext` schema: `{"finalSeed":...,"checks":{name:itemSpoiler}}`), and a combined spoiler (`saves/combo/slotN.spoiler.json`).
  - Reuse `ComboRando::GameId` from `CrossMailbox.h`.
  - (Full code authored during implementation against the parsed pool shapes; keep the assignment a pure function for later unit-testability.)

- [ ] **Step 2: Resolve new exports in `ComboShip.cpp`** — add fn-ptr typedefs + `GetSym` for `SOH_SetOnComboGenerateCallback`, `SOH_BuildRandoPools`, `SOH_ApplyRandoPlacements`, `MM_InitRandoRuntime`, `MM_BuildRandoPools`, `MM_InitRandoSaveFile` (mirror the existing resolution block ~line 173-200).

- [ ] **Step 3: The generate callback** in `ComboShip.cpp`:
```cpp
static void Combo_OnGenerate(int fileNum) {
    std::string sohPools = SOH_BuildRandoPools ? SOH_BuildRandoPools("{}") : "{}";
    std::string mmPools  = MM_BuildRandoPools  ? MM_BuildRandoPools("{}")  : "{}";
    uint32_t master = /* from gCombo.Seed CVar, or a fixed value for now */ 12345u;
    auto result = ComboRando::CrossWorldGenerate(sohPools, mmPools, master); // returns {ootJson, mmJson, spoiler}
    if (SOH_ApplyRandoPlacements) SOH_ApplyRandoPlacements(result.ootPlacements.c_str());
    g_pendingMMPlacement = result.mmPlacements;  // stash for the post-save MM step
    // write combined spoiler to saves/combo/slot{fileNum}.spoiler.json
}
```
Register it after the other callbacks (~line 282): `if (SOH_SetOnComboGenerateCallback) SOH_SetOnComboGenerateCallback(Combo_OnGenerate);`

- [ ] **Step 4: MM save creation with placement** — in `Combo_OnOOTSaveInit` (the existing post-save callback, ComboShip.cpp:99), replace `MM_InitSaveFile(fileNum)` with `MM_InitRandoSaveFile(fileNum, g_pendingMMPlacement.c_str())` (falling back to `MM_InitSaveFile` if empty).

- [ ] **Step 5: CMake** — `combo/rando/CrossWorldRando.h` is header-only; no CMake change (combo already includes `rando/`). Build ComboShip — `cmake --build build/x64 --target ComboShip --config Debug`. Clean.

- [ ] **Step 6: Commit** — `git add combo/rando/CrossWorldRando.h combo/ComboShip.cpp` → `feat(combo-rando): central-assignment generator + save-creation orchestration (native-only)`.

---

### Task 6: Minimal rando-only enforcement

**Files:** Modify `soh/src/.../z_file_choose.c`, `mm/2s2h/SaveManager/SaveManager.cpp`.

- [ ] **Step 1: OOT** — `FileChoose_Init` (`z_file_choose.c:3107`): under `#ifdef COMBO_BUILD` set `questType[0..2] = QUEST_RANDOMIZER`. `FileChoose_UpdateRandomizerMenu` (~`z_file_choose.c:777`): under COMBO_BUILD make the proceed-to-name-entry unconditional (drop the `Randomizer_IsSeedGenerated()` gate — the combo generate runs at save commit). (Task 4 already forces the quest at commit, so this is the UX side.)
- [ ] **Step 2: MM** — confirm `MM_InitRandoSaveFile` (Task 2) already produces a `SAVETYPE_RANDO` save, so the combo MM save is rando. No `SaveManager_InitNewSaveForSlot` change needed beyond what Task 2 routes through. (If the combo still calls `MM_InitSaveFile` anywhere for a real save, redirect to `MM_InitRandoSaveFile`.)
- [ ] **Step 3: Build soh + 2ship** — clean. Document COMBO_BUILD deltas in `docs/UPSTREAM_MERGES.md`.
- [ ] **Step 4: Commit** — `feat(combo): force randomizer-only saves (OOT quest + MM SAVETYPE_RANDO)`.

---

### Task 7: End-to-end verification (native-only) + docs

- [ ] **Step 1: Build all** (individually): libultraship (if changed), soh, 2ship, ComboShip.
- [ ] **Step 2: Generate** — launch ComboShip, create a new save on slot 1. Confirm: console/log shows `[ComboShip]` generate lines; `saves/combo/slot0.spoiler.json` exists listing OOT checks→OOT items and MM checks→MM items; OOT save is `QUEST_RANDOMIZER`; `saves/2ship/file1.json` has `saveType: SAVETYPE_RANDO` and populated `randoSaveChecks`.
- [ ] **Step 3: In-game (OOT)** — play; confirm a known OOT check gives its spoiler-listed item (native delivery via the existing rando path still works under our injected placement).
- [ ] **Step 4: In-game (MM)** — transition to MM; confirm MM is a rando save (rando enhancements/cutscene-skips active) and a known MM check gives its spoiler-listed item.
- [ ] **Step 5: Document** — `docs/UPSTREAM_MERGES.md` Increment 2 section (the new exports, the `gComboGenerateCallback` hook, the file-select forcing). Commit.

---

## Phase 2 — Cross-placement + foreign markers (outline; detail after Phase 1)

- Generator: assignment now mixes pools (an OOT check may get an MM item, vice versa), still no-logic.
- **Foreign-item marker schema:** OOT — a side map `RC → {srcGame, itemName}` persisted in the rando save section (the `itemLocationTable` only holds `RandomizerGet`, so foreign placements need a parallel store + a sentinel/placeholder `RG` for the slot). MM — extend `RandoSaveCheck` (or a parallel array) with a foreign descriptor; `.randoItemId` set to a `RI_FOREIGN`-style sentinel.
- Combined spoiler shows the cross-placements.
- *Verify:* spoiler shows OOT items in MM checks and vice versa; markers persist in both saves.

## Phase 3 — Rando-only file-select UX + combined settings window (outline)

- Combined settings window: a `Ship::GuiWindow` subclass in `soh.dll` (`SohGui::SetupGuiElements`, COMBO_BUILD), editing OOT `gRandoSettings.*` + MM `gRando.Options.*` (shared CVars), with one "Generate" that sets the master seed CVar. Hosted in the always-available OOT Gui (sidesteps the menu-swap limitation). Menu entry via `SohMenu`.
- Full file-select polish: optionally skip the OOT quest menu entirely (`FileChoose_UpdateMainMenu` → straight to name entry); ensure no path can create a non-rando save.

## Increment 3 (separate plan) — real send interception + gift presentation

- At real pickup, read the Phase-2 foreign marker → divert to the Increment-1 mailbox + show the generic gift model + "Sent to …" text (send branch points `hook_handlers.cpp:380` / `CheckQueue.cpp:37`). Receive already works (Increment 1).

---

## Self-Review notes

- **Spec coverage:** Phase 1 implements the spec's "Increment 2 — Generation design" mechanics (gComboGenerateCallback at OOT save creation, headless pools, inject-then-existing-save-init) minus cross-placement (Phase 2) and the combined window (Phase 3). The headless-MM risk the spec flagged is retired by Task 1's `MM_InitRandoRuntime` shim + its validation gate.
- **Open confirmations for the implementer (verify against real headers, adjust inline):** exact `Rando::Settings` apply-to-context method name; `itemPool` extern declaration location; `RetrieveItem(rg).GetName().GetEnglish()` accessor chain; `RandoStaticOption`/`RandoStaticCheck`/`RandoStaticItem` field names; whether `Spoiler::ApplyToSaveContext` calls `GrantStartingItems` (must be skipped headless); `CURRENT_SAVE_VERSION` constant name; the MM include paths.
- **Type consistency:** export names used identically across `ComboShip.cpp` resolution and the DLL definitions: `SOH_SetOnComboGenerateCallback`, `SOH_BuildRandoPools`, `SOH_ApplyRandoPlacements`, `MM_InitRandoRuntime`, `MM_BuildRandoPools`, `MM_InitRandoSaveFile`. Interchange JSON shapes are defined once (Phase 1 file-structure section) and reused.
- **HM64:** only additive `#ifdef COMBO_BUILD` game-source edits (`z_sram.c`, `z_file_choose.c`); everything else in port/combo code. Document each in `docs/UPSTREAM_MERGES.md`.
