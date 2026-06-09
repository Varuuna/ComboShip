# ComboShip Increment 7 — Combined Settings Window + Generate UX

**Date:** 2026-06-05
**Branch:** `randomizer`
**Status:** SUPERSEDED (2026-06-09) by `docs/superpowers/specs/2026-06-09-combo-unified-settings-menu-design.md`. The cross-world Generate UX described here is preserved as the **Combo** tab of the unified menu; the standalone window is dropped.
**Parent spec:** `docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-combined-logic-design.md` (§5 "Combo Settings Window")
**Depends on (all DONE):** Inc1–6 + eager-MM-boot foundation. Combined fill (`combo/rando/CrossWorldRando.h` `CrossWorldCombinedFill`), oracles (`Combo_SOH_Rando_*` / `Combo_MM_Rando_*`), foreign markers (`combo/rando/CrossForeign.h`), mailbox, `SOH_ApplyRandoPlacements`, `MM_InitRandoSaveFile`, `SOH_Dump/MM_DumpRandoStaticData`.

---

## Goal

A single combo-owned window to configure **both** games' randomizer options and run **one** cross-world Generate, replacing the current flow where the fill runs automatically at save creation with a **hardcoded seed (`12345u`)** and **default CVars** (which leaves most non-chest checks unshuffled). After Inc7 the player sets options, presses **Generate**, watches a live **progress bar**, and only then creates a save that consumes the generated seed.

## Non-goals

- Optimizing fill performance (the O(checks²) reachability cost) — **explicitly deferred** to a future increment. Inc7 makes the long fill *visible and non-blocking*, not fast.
- New per-option UI polish beyond functional CVar-bound widgets.
- Multi-slot seed management; behavior matches the current single-active-seed model.

---

## Decisions (locked during brainstorming, 2026-06-05)

1. **Decoupled generation.** The Generate button runs the fill and stashes the result; save creation only *consumes* it. No auto-generate-at-save fallback — the existing OOT seed-gate (`Randomizer_IsSeedGenerated`) is what requires Generate before "Start Game."
2. **Uniform combo-owned renderer.** One ImGui rendering path fed by a per-game *option model*; not a reuse of either game's native settings UI.
3. **Source under `combo/`, hosted in `soh.dll`.** New code lives in the no-upstream `combo/` tree; it is compiled into `soh.dll` (which owns the menu slot at boot) under `#ifdef COMBO_BUILD`.
4. **Layout B.** Two settings tabs (OOT / MM) + a persistent action bar (seed field · Generate · status) visible regardless of active tab.
5. **Master seed via the games' own primitives.** `Ship_Hash` / `Ship_Random`, blank field = random — replacing the hardcoded `12345u`.
6. **Threaded generation with a live progress bar.** Mirrors SoH's threaded fill, but with a real percentage bar rather than a spinner.

---

## 1. Components & Ownership

| File | Module | Role |
|------|--------|------|
| `combo/gui/ComboRandoWindow.{h,cpp}` | combo-owned, compiled into `soh.dll` | `Ship::GuiWindow` subclass: tabs + action bar; builds the option model; reads/writes CVars; owns the progress struct; fires the Generate request. |
| `combo/gui/ComboGenProgress.h` | combo-owned header (single instance owned by the window) | `struct ComboGenProgress` with atomics for live progress (see §3). |
| `combo/rando/CrossWorldRando.h` | combo-owned (existing) | `CrossWorldCombinedFill` extended to accept a `ComboGenProgress*` and report progress; master seed already a parameter. |

**soh.dll seams (edits to existing vendored files, thin + `COMBO_BUILD`-guarded):**
- Register the window in `SohGui::SetupGuiElements` (one `make_shared` + `AddGuiWindow`).
- Add a menu-bar button to open it (one widget entry).
- Ensure the window persists across the OOT↔MM menu swap and is reachable before file-select.
- New export `SOH_SetOnComboGenerateRequestCallback` (see §3).
- Neutralize the save-time generate trigger (see §3).

**2ship.dll seam:** new export `MM_DumpRandoOptions` (see §2).

All seams documented in `docs/UPSTREAM_MERGES.md` with `// ComboShip:` comments per `document-post-merge-changes`.

---

## 2. Uniform Option Model + Metadata Adapters

The renderer consumes one uniform description per option:

```cpp
struct OptionDesc {
    std::string id;            // stable key (enum name)
    std::string cvar;          // CVar to read/write
    enum { Combo, Bool, Int } type;
    std::string label;         // display name
    std::vector<std::string> valueLabels; // for Combo: index -> label
    int minValue, maxValue;    // for Int
    std::string category;      // sidebar grouping
};
```

Two adapters build `std::vector<OptionDesc>`:

- **OOT (in-DLL, no export):** the window lives in `soh.dll`, so it reads `Rando::Settings::GetInstance()->GetAllOptions()` directly and maps each `Option` (CVar name, labels, category, hidden flag) into an `OptionDesc`. Hidden/menu-internal options are skipped.
- **MM (new export `MM_DumpRandoOptions`):** returns JSON for each entry of `Rando::StaticData::Options` (`id`, `cvar`, `defaultValue`) **plus** the value→label maps that currently live in `mm/2s2h/Rando/Menu.cpp`. The export surfaces those labels so the combo renderer can show the same choices MM's own menu does. The window parses the JSON into `OptionDesc`s.

**CVar binding:** the renderer reads/writes via `CVarGetInteger/SetInteger`, `CVarGetString/SetString` on `cvar`. The single shared libultraship CVar store makes MM's CVars fully readable/writable from `soh.dll` with no per-DLL caching. Changing a value writes the CVar immediately; the next Generate reads current CVar state.

---

## 3. The Generate Action (decoupled, threaded, progress-reporting)

### Trigger seam
- `soh.dll` exports `SOH_SetOnComboGenerateRequestCallback(void (*cb)(const char* inputSeed, ComboGenProgress*))`. The combo exe registers a handler at startup (next to the existing callback wiring in `ComboShip.cpp`).
- The window's Generate button calls a soh-internal trampoline that invokes the registered callback, passing the seed string from the action bar and a pointer to the window-owned `ComboGenProgress`. The call **returns immediately**.

### Worker thread (in the combo exe)
The handler launches a `std::thread` (joined on the next request, SoH's pattern) that runs the refactored fill — essentially today's `Combo_OnGenerate` body, minus the save-init coupling, plus a seed parameter and progress reporting:
1. Dump static data (`SOH_/MM_DumpRandoStaticData`).
2. `masterSeed = Ship_Hash(inputSeed)` (see §4).
3. `CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, &progress)` — updates `progress` as it runs.
4. `Combo_MM_Rando_Restore()`.
5. Write `slot{N}.spoiler.json`, foreign map (`WriteForeignFromAnnotations`), build per-game apply payloads with foreign sentinels.
6. `SOH_ApplyRandoPlacements(ootApply)`; stash `g_PendingMMPlacements`.
7. Set OOT seed-generated true (so the gate passes) — via a small soh export or by having `SOH_ApplyRandoPlacements` set it.
8. Mark `progress.done = true` with `success`/`error`/`seed`/`foreignCount`.

### Progress struct (window-owned, single instance)
```cpp
struct ComboGenProgress {
    std::atomic<int> phase;      // 0 Pools, 1 Progression, 2 Junk, 3 Verify
    std::atomic<int> placed;     // advancement items placed
    std::atomic<int> total;      // advancement items total
    std::atomic<bool> done;
    std::atomic<bool> success;
    std::atomic<uint32_t> seed;
    std::atomic<int> foreignCount;
    // error string guarded by a small mutex, or a fixed char buffer
};
```
The window polls each frame: while `!done`, draw an ImGui `ProgressBar(placed/total)` with the phase label inside a modal overlay (input to the rest of the window disabled). On `done`, show the status line (`✓ Seed 0x… · N cross-world placements` or the error) and join the thread.

### Save creation no longer generates
`Sram_InitSave`'s current `gComboGenerateCallback` call (which runs the fill) is **dropped/neutralized under `COMBO_BUILD`**. By save-creation time the OOT placements are already applied, the MM slice is stashed, and seed-generated is set — all from the button. `Combo_OnOOTSaveInit` still consumes the stashed MM slice into `MM_InitRandoSaveFile` (unchanged).

**Verification checkpoint (sequencing):** confirm OOT's rando `Context` (the applied `itemLocationTable`) is **not reset** between the button press and save creation. If it is, re-apply the stashed OOT placements at save-init from the stash instead of relying on the earlier apply.

---

## 4. Master Seed

The action bar has an **Input Seed** text field (blank = random), matching MM's `gRando.InputSeed` model:
- `inputSeed = field` if non-empty, else `std::to_string(Ship_Random(0, 1000000))`.
- `masterSeed = Ship_Hash(inputSeed)` → passed to `CrossWorldCombinedFill` (replaces `12345u`).
- **Propagate for in-game RNG parity:** set `gRando.InputSeed` (MM) and OOT's seed string from the same `inputSeed` before each game's save is written, so any in-game randomization (traps, cosmetics, etc.) is deterministic from the one master seed.

`Ship_Hash` / `Ship_Random` are libultraship exports available to all modules — identical to what OOT's `GenerateRandomizer` and MM's `OnFileCreate` already use.

---

## 5. Bypassing Per-Game Settings Screens

- **OOT:** keep the existing seed-gate (`z_file_choose.c` ~777, `Randomizer_IsSeedGenerated`) **active** — it is the mechanism that requires pressing Generate before "Start Game." No new bypass.
- **MM:** the rando MM save is written headlessly by `MM_InitRandoSaveFile` (combo), so MM's own `OnFileCreate` self-generation is off the combo save-creation route. **Verify** it does not double-run; gate it under `COMBO_BUILD` if it does.
- Combo already forces `QUEST_RANDOMIZER` (OOT) / `SAVETYPE_RANDO` (MM); no vanilla save path in combo mode.

---

## 6. Window Layout (B)

```
┌ Combo Randomizer ─────────────────────────────┐
│ [ OOT Settings ]  MM Settings                  │  ← tabs
│ ┌────────────┬──────────────────────────────┐ │
│ │ ▸ Logic    │ Logic Rules    [Glitchless ▾] │ │  ← category sidebar + widgets
│ │ ▸ Dungeons │ Open Forest    [Closed ▾]     │ │
│ │ ▸ Shuffles │ Shuffle Songs  [☑]            │ │
│ │ ▸ Hints    │ …                             │ │
│ │ ▸ Start    │                               │ │
│ └────────────┴──────────────────────────────┘ │
│ Seed [____________(blank=random)]  [Generate]  │  ← persistent action bar
│ ✓ Seed 0xA3F1 · 14 cross-world placements      │  ← status line
└────────────────────────────────────────────────┘
```
- Tabs select which game's options show; the **action bar is always visible**.
- Each game's sidebar categories come from its option model's `category` field (OOT from `Rando::Settings` groups; MM derived from option-id prefixes / a category field in the export).
- During generation, a modal overlay over the whole window shows the progress bar (§3).
- Opened from a menu-bar button; reachable before file-select; persists across an OOT↔MM transition.

---

## 7. HM64 / Combo-Ownership Compliance

New logic is combo-owned (`combo/gui/*`, `combo/rando/*`). Touches to vendored `soh/`/`mm/` are thin, additive, `COMBO_BUILD`-guarded:
- `MM_DumpRandoOptions` export (BenPort.cpp / Rando).
- `SOH_SetOnComboGenerateRequestCallback` export + seed-generated setter (OTRGlobals.cpp).
- Window registration + menu entry in SohGui.
- Neutralize the save-init generate trigger (the `gComboGenerateCallback` call site).
- One `target_sources` add in `soh/CMakeLists.txt` for the combo window source.

Each documented in `docs/UPSTREAM_MERGES.md` + `// ComboShip:` comments per `document-post-merge-changes`. No `soh/src` or `mm/src` game-source edits.

---

## 8. Verification (manual; no unit harness — per parent spec)

- Toggling an option changes which checks are shuffled in `saves/combo/slot{N}.spoiler.json`.
- Fixed seed twice → identical spoiler; blank seed → differs each run.
- Generate shows a **moving progress bar** (not a frozen window) through the long phase, then a status line (seed + foreign count); errors surface in the status line.
- "Start Game" is **blocked** until Generate has run (seed-gate); allowed after.
- Window opens before file-select and **persists** across an OOT↔MM transition.
- Crash capture via `x64/Debug/combo_abort_stack.txt`.

---

## 9. Open Risks / Checkpoints

| Risk | Severity | Mitigation |
|------|----------|------------|
| MM oracle called from a worker thread (cross-DLL, MM paused) is thread-unsafe | MEDIUM | Snapshot/restore of `gSaveContext` should isolate it; nothing else touches MM state during generation. **Fallback:** cooperative main-thread stepping (a few placements/frame) — still non-frozen, still updates the bar. |
| OOT rando Context reset between button press and save creation | MEDIUM | Verify; if reset, re-apply stashed OOT placements at save-init. |
| MM `OnFileCreate` self-generation double-runs | LOW | Verify; gate under `COMBO_BUILD` if needed. |
| MM value-labels not all reachable from `StaticData` (live in Menu.cpp) | LOW | `MM_DumpRandoOptions` explicitly surfaces the Menu.cpp label maps. |
| Long fill (minutes) frustrates testing | LOW (Inc7) | Progress bar makes it tolerable; true fix (incremental reachability) is a deferred future increment. |

---

## Out of Scope (future)

- **Fill performance** (incremental reachability to turn minutes into seconds) — deferred by explicit decision.
- Spoiler-log viewer UI, hint/trap detail editors beyond CVar widgets, multi-seed management.
