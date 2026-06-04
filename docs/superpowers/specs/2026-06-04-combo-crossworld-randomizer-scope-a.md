# Cross-World Randomizer (Scope A — No-Logic) — Spec

**Status:** Design agreed 2026-06-04 (branch `randomizer`). Supersedes the implementation specifics in `docs/RANDOMIZER_ARCHITECTURE.md` (that doc is the aspirational v1.0 vision — its intent/UX is still valid, but its cross-game *logic*, from-scratch fill engine, and `z_player.c` edits are NOT what we build).

## Goal

Let items from OOT (soh) appear in MM (2ship) checks and vice versa, in a single ComboShip seed. A player switches between the two games (via the existing portal) to collect each other's items.

## Scope

**In scope (Scope A, no-logic):**
- One master seed produces a single combined item→check assignment across both games.
- Placement is **no-logic**: pure uniform random assignment, no reachability guarantee. (Both games already have a no-logic rando mode; we are not building any combined reachability/logic engine.)
- When a check holds the *other* game's item, pickup is diverted: the local game shows a generic "gift" model + text ("Sent to Termina/Hyrule: <item>") and the item is delivered to the other game when the player is there.
- Cross-game item *grant* uses a mapping table; shared items (Bow, Bombs, hearts…) grant 1:1; game-exclusive items with no analog are trophies (text only, no mechanical grant).

**Explicitly out of scope (deferred — see [[comboship-crossgame-randomizer]] memory, Scope B):**
- Cross-game *logic* (a check in one game requiring an item from the other to be reachable).
- Combined reachability / assumed-fill spanning both games.
- Completability guarantees of any kind.
- Rendering the real foreign item model in the wrong game.
- Anchor multiplayer integration (Anchor is unrelated and must NOT be used).

## Architecture

ComboShip runs OOT (`soh.dll`) and MM (`2ship.dll`) in **one process, alternating control** (the `for(;;)` loop in `combo/ComboShip.cpp`; only one game's loop is active at a time, each with its own `gSaveContext` and `ResourceManager`, sharing one `libultraship.dll` + UCRT heap). There is **no concurrency and no IPC** — cross-game data passes through shared process memory + a combo-owned file.

Three new pieces, all owned by the **combo layer** (the only code linked to both DLLs for the whole process lifetime):

1. **Central-assignment generator** (`combo/rando/CrossWorldRando.*`): pulls each game's check list + item pool, merges, shuffles once from the master seed (no-logic), assigns uniformly, pads with junk, then pushes each game its slice of the placement (native item *or* foreign descriptor).
2. **Cross-game mailbox** (`saves/combo/slot{N}.mailbox.json`, combo-owned, atomic-rename, crash-safe): a queue of items collected for the *other* game, not yet granted.
3. **New DLL exports** on each game so the combo layer can drive them without touching game source (HM64-clean):
   - enumerate pools, apply placements, drain mailbox.

Each game's existing randomizer machinery is reused for everything else (item give, draw, flag→check mapping, save persistence). The two fills are **not** run for assignment (the combo layer does assignment); each game's pool-*building* is reused.

### Key facts (verified 2026-06-04)

- **PRNG is cleanly separable.** OOT `rando_state` (`3drando/random.cpp:3`), MM `state` (`mm/2s2h/ShipUtils.cpp:303`), `default_state` (`soh/soh/ShipUtils.cpp:102`). Derive `oot_seed = Hash(master+"OOT")`, `mm_seed = Hash(master+"MM")` (FNV-1a: `SohUtils::Hash` / `Ship_Hash`). The combo layer's own assignment shuffle uses its own RNG seeded from the master.
- **Item identity / interchange = string names** (both spoilers already string-name items) + a cross-game grant mapping table. OOT items: `RandomizerGet` (`RG_*`); MM items: `RandoItemId` (`RI_*`). OOT checks: `RandomizerCheck` (`RC_*`); MM checks: `RandoCheckId` (`RC_*`).
- **Send branch points:** OOT `soh/soh/Enhancements/randomizer/hook_handlers.cpp:380` (inside `RandomizerOnPlayerUpdateForRCQueueHandler`; `rc` + `GetItemEntry` in hand). MM `mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp:37` (inside `CheckQueue`; `randoSaveCheck.randoItemId` in hand).
- **Receive:** drain mailbox on each game's player-update tick; grant via OOT `Randomizer_Item_Give`/`Item_Give`, MM `Rando::GiveItem`/`Item_Give`. (Built fresh — NOT via Anchor.)
- **Save paths:** OOT `Save/file{N+1}.sav` (versioned-section JSON, `soh/soh/SaveManager.cpp`); MM `saves/2ship/file{N}.json` (`nlohmann::json`, rando under `save.shipSaveInfo.rando`, `mm/2s2h/SaveManager/SaveManager.cpp`). Slot map: OOT slot N ↔ MM file N+1.
- **Foreign-item marker:** each game's per-check placement state needs one additive field recording that the check holds a foreign item + its descriptor (so the send branch knows to divert, and the check shows correctly in trackers). OOT: alongside `Context::itemLocationTable` / save randomizer section. MM: extend `RandoSaveCheck` (`mm/include/z64save.h:372`).

### Cross-game data structures (combo layer)

```cpp
// combo/rando/CrossWorldRando.h
enum ComboGameId : uint8_t { CW_GAME_OOT = 0, CW_GAME_MM = 1 };

// One placement: an item (named) assigned to a check (named), each tagged by game.
struct CwPlacement {
    ComboGameId checkGame;   // which game owns the location
    std::string checkName;   // RC_* spoiler name in checkGame's namespace
    ComboGameId itemGame;    // which game the item is from
    std::string itemName;    // RG_*/RI_* spoiler name in itemGame's namespace
};

// One queued cross-game delivery (the mailbox is a list of these, per slot).
struct CwMailboxEntry {
    ComboGameId srcGame;     // where it was collected
    ComboGameId dstGame;     // where it must be granted
    std::string itemName;    // item in srcGame's namespace
    std::string displayName; // human string for the "received" text
    std::string srcCheckName;// provenance (for spoiler/debug)
    bool delivered;          // set true once the dst game grants it
};
```

## Increment breakdown (each independently shippable & testable)

**Increment 1 — Mailbox + cross-game grant plumbing (no generation).**
Build the header-only mailbox module (`combo/rando/CrossMailbox.h`, compiled into all three modules), the in-game receive-drain on each game's player-update hook (an in-game file read — no new exports), and a *debug* "send item to other game" trigger (console command / menu button). De-risks the runtime channel — the hardest part — before any generation work.
*Verify:* debug-send an item in OOT, portal to MM, item is granted with "Received from Hyrule" text (and vice versa); mailbox file persists across a crash. (Detailed plan: `docs/superpowers/plans/2026-06-04-crossworld-randomizer-increment-1-mailbox.md`.)

**Increment 2 — Central-assignment generator + new-cross-world-seed start UX.**
Headless pool-enumeration + apply-placement exports; the combo-layer shuffle/assignment driven from a new generate-hook at OOT save creation; create both save files in rando mode; emit a combined spoiler for inspection. See "Increment 2 — Generation design" below for the trigger/ordering detail.
*Verify:* start a cross-world seed; both games' placement tables populate; combined spoiler shows OOT items in MM checks and vice versa; native (same-game) checks still grant normally in-game.

**Increment 3 — Send interception + foreign markers + presentation.**
Wire the real pickup paths (the two send branch points) to divert foreign items through the mailbox; add the per-check foreign markers; generic gift model + "Sent to…" text.
*Verify:* full loop — open an OOT check holding an MM item → "Sent to Termina" → portal to MM → item arrives; and the reverse.

## Increment 2 — Generation design (trigger, ordering, flow)

**When generation runs: at OOT save creation.** This is the single atomic point where ComboShip already creates both saves (`Sram_InitSave` in `soh/src/code/z_sram.c` fires `gComboSaveInitCallback` → `Combo_OnOOTSaveInit` → `MM_InitSaveFile`). At this moment OOT is fully live AND MM DLL code runs headlessly (proven by `MM_InitSaveFile` already executing here), so **both** games' pools are buildable in this one context.

**The ordering gotcha (verified 2026-06-04).** In `Sram_InitSave` (`z_sram.c:266-283`) the order is:
1. `if (QUEST_RANDOMIZER && Randomizer_IsSeedGenerated()) Randomizer_InitSaveFile();` — consumes OOT's `itemLocationTable` into the save.
2. `Save_SaveFile();` — OOT save written to disk.
3. `gComboSaveInitCallback(fileNum);` — the EXISTING combo callback; creates the MM save.

The existing callback (3) fires *after* OOT's placement is already consumed (1) and the OOT save is written (2) — **too late to inject OOT placements.** Generation must populate OOT's `itemLocationTable` and flip `Randomizer_IsSeedGenerated()` true *before* step (1).

**Required game-source change (one additive `#ifdef COMBO_BUILD` hook; HM64-documented).** Add an earlier generate-hook in `Sram_InitSave`, immediately when the rando quest is detected, before step (1):
```c
#ifdef COMBO_BUILD
    if (currentQuest == QUEST_RANDOMIZER && gComboGenerateCallback != NULL) {
        gComboGenerateCallback((int)gSaveContext.fileNum);   // builds both pools, assigns, applies OOT, marks seed generated
    }
#endif
```
`gComboGenerateCallback` is a new combo-registered callback pointer (mirror the existing `gComboSaveInitCallback` / `gComboSceneSwitchCallback` pattern in `OTRGlobals.cpp:2482-2488`, exported via a new `SOH_SetOnComboGenerateCallback`).

**Generation flow inside `gComboGenerateCallback` (combo layer):**
1. `SOH_BuildRandoPools()` → OOT check list + item pool as JSON (OOT live; reuses OOT pool builders — `GenerateItemPool` + location pool — NOT `Fill()`).
2. `MM_BuildRandoPools()` → MM check list + item pool as JSON (headless; reuses MM `GeneratePools` — NOT the logic fill — behind a shim that ensures static rando data + a scratch save context + CVar settings).
3. Combine both pools; pad with junk to match check count; **no-logic uniform random assignment** seeded from the master seed (combo layer's own RNG; pool enumeration itself is deterministic and needs no RNG).
4. `SOH_ApplyCrossPlacements(json)` → writes OOT `Context::itemLocationTable` (native item = real `RG_*`; foreign item = marker descriptor) and sets the Context's "seed generated" state so the subsequent `Randomizer_InitSaveFile()` consumes it unchanged.
5. Stash MM's placement slice for the post-save callback.

Then control returns to `Sram_InitSave`: step (1) `Randomizer_InitSaveFile()` persists OUR OOT placement; step (3) the existing post-save callback creates the MM save *with* the stashed MM placement (e.g. a new `MM_InitRandoSaveFile(fileNum, json)` superseding/extending the current `MM_InitSaveFile`).

**New exports (Increment 2):** `SOH_SetOnComboGenerateCallback(cb)`, `SOH_BuildRandoPools()`, `SOH_ApplyCrossPlacements(json)`; `MM_BuildRandoPools()`, `MM_InitRandoSaveFile(fileNum, json)` (or `MM_ApplyCrossPlacements(json)` + reuse `MM_InitSaveFile`). Pool/placement payloads cross the boundary as JSON strings (C-friendly).

**Headless pool-building — the assumption to validate first in Increment 2.** OOT's pool building is mostly static data + CVars. MM's `GeneratePools` reads `gSaveContext` and CVars; it must run without a full MM boot. The same headless context that `MM_InitSaveFile` already uses is the target; the first Increment 2 task is to confirm `MM_BuildRandoPools` can run there (static `Rando::Logic::Regions` present at DLL load, a zeroed/scratch save context, settings from CVars) and add a minimal shim if not.

**"Loading…" UX.** No-logic generation is fast (enumerate → shuffle → write, sub-second). Prototype approach: set a `gCombo.Generating` flag, render one "Generating cross-world seed…" frame, run generation synchronously in the hook, proceed. (Polished alternative, deferred: generate earlier on a background thread reusing OOT's existing `RandoGenerating` loading UI, so save-commit only consumes the result.)

**Settings source (prototype).** Reuse each game's existing rando settings menus — settings are libultraship CVars and persist across the shared context. The user configures OOT and MM rando settings as today, then triggers the cross-world generate. A unified combo settings panel is deferred. For no-logic, settings mainly select which check categories are shuffled.

**Seed.** A master seed (combo-layer CVar `gCombo.Seed`, or auto-random when blank) drives the assignment RNG; `oot_seed`/`mm_seed` are derived (`Hash(master+"OOT")` / `+"MM"`) only for any incidental per-game randomness.

### Increment 2 — UX refinements (user direction, 2026-06-04)

- **Randomizer is the ONLY save type in combo.** ComboShip is a randomizer-first experience: creating a save always produces a rando save — OOT `QUEST_RANDOMIZER`, MM `SAVETYPE_RANDO`. No normal/vanilla save creation is offered. This also turns on the default rando enhancements (e.g. cutscene skips) for free. (This is what makes the Increment-1 `IS_RANDO` ungate unnecessary long-term — but keep the unconditional combo receive anyway; it's harmless and robust.)
- **One combined settings window, one Generate.** A single combo-owned ImGui window edits BOTH OOT and MM rando settings together; a single "Generate" runs the central-assignment fill and writes the combined spoiler BEFORE the save files are created. Then save creation consumes the result (both games rando).
- **Bypass the per-game mandatory settings/seed screens.** Normally OOT file-select requires `Randomizer_IsSeedGenerated()` and MM creates its seed in its own `OnFileCreate`; the combo generate-hook (see above) drives both, so the standalone per-game settings screens are skipped/ignored.
- **Host the combined window in the always-available (OOT/shared) ImGui.** Known ComboShip limitation: both games register GUI windows into the one shared ImGui and there is no per-active-game window-visibility swap, so while MM runs only OOT's menus are visible. A combo-owned settings window registered in the shared/OOT GUI is therefore always reachable — and this is the right home for it regardless. (The menu-swap bug is broader than rando; only worth a dedicated fix if MM-specific menus are ever needed in combo.)
- **No-logic for now:** use a no-logic preset or a skip-settings default config for the generate step.

## Verification approach (whole feature)

No unit-test harness exists for combo/game C++. Verification per task is:
- **Builds:** per-target builds succeed (`cmake --build build/x64 --target <libultraship|soh|2ship|ComboShip> --config Debug`); build targets individually (see [[comboship-build-targets]]).
- **In-game manual:** documented click-path; observe behavior; read `x64/Debug/logs/Ship of Harkinian.log` and `[ComboShip]` SPDLOG lines.
- **Artifact inspection:** the combined spoiler JSON and `saves/combo/slot{N}.mailbox.json`.
- **Crash capture:** `x64/Debug/combo_abort_stack.txt` (vectored handler already installed).
- Pure-logic units (the assignment shuffle, mailbox (de)serialization) MAY get a small standalone test if a harness is stood up, but are otherwise validated via the combined spoiler + log assertions.

## HM64 compliance

All interception is in PORT code (hook_handlers / CheckQueue / SaveManager / BenPort / OTRGlobals) and new combo-layer files. No OOT/MM game-source (`soh/src`, `mm/src`) edits except, if unavoidable, additive `#ifdef COMBO_BUILD` plumbing documented in `docs/UPSTREAM_MERGES.md` with a `// ComboShip:` comment (see [[document-post-merge-changes]], [[comboship-hm64-principle]]).
