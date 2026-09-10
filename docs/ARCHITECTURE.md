# ComboShip Architecture

## Overview

ComboShip runs **Ocarina of Time** (Ship of Harkinian) and **Majora's Mask** (2 Ship 2 Harkinian)
as a single program that can hand off back and forth between the two games at runtime, while keeping
each game's source close enough to upstream that updates can still be merged in.

> **Status note:** This document describes what is *actually built* and stays high-level; the
> per-feature deviation records live in [`deviations/`](deviations/). The headline cross-game
> features — a shared **cross-world randomizer**, immediate **cross-game item delivery**, **Anchor**
> online co-op, and **cross-game hints** — are implemented (see [Cross-Game Features](#cross-game-features)).
> A single unified/merged save format is **not** built: the two saves are linked by slot. Earlier
> revisions described an adapter/observer class architecture that was never built; it has been removed.

## Design Goals

1. **One program, both games** — boot into OOT, transition into MM and back, on a shared window and engine.
2. **Independent upstream tracking** — keep `soh/` and `mm/` mergeable with upstream SoH / 2S2H.
3. **Minimal game-code modification (HM64 principle)** — touch the decomp source as little as possible.
   Combo behavior lives in port code, hooks, and enhancements; the few unavoidable edits to game
   source are tiny and fenced with `#ifdef COMBO_BUILD`.

## How It Actually Works

The build splits the engine into a shared DLL and produces two game DLLs plus a thin launcher exe.
The launcher loads both game DLLs, resolves a small set of exported C entry points, and drives a
loop that boots one game and resumes the other across transitions.

### Architecture Diagram

```
┌──────────────────────────────────────────────────────────────┐
│  ComboShip.exe  (combo/ComboShip.cpp + combo/core/)            │
│   • LoadLibrary soh.dll + 2ship.dll, resolve exports           │
│   • Ensure O2R archives exist (extract if missing)             │
│   • Bidirectional game-switch loop (boot once, resume after)   │
└───────────────┬───────────────────────────┬──────────────────┘
                │ C exports + callbacks      │ C exports + callbacks
        ┌───────▼─────────┐          ┌────────▼────────┐
        │   soh.dll (OOT) │          │ 2ship.dll (MM)  │
        │  decomp + hooks │          │  decomp + hooks │
        │  + SoH enhance. │          │  + 2S2H enhance.│
        └───────┬─────────┘          └────────┬────────┘
                │                              │
                └──────────────┬───────────────┘
                       ┌───────▼─────────────────┐
                       │  libultraship.dll        │
                       │  (shared engine: GFX,    │
                       │   audio, input, window,  │
                       │   resource management)   │
                       └──────────────────────────┘
```

`COMBO_BUILD` (defined globally in the top-level `CMakeLists.txt`) is what switches the engine from
a statically-linked single binary into the shared `libultraship.dll` + `soh.dll` + `2ship.dll`
layout and enables the transition hooks in both games.

### Exported Entry Points

The launcher talks to each game purely through exported C functions resolved with `GetProcAddress`
(see `combo/core/ComboDllApi.{h,cpp}`). Missing optional exports are tolerated (null-checked); only
the core ones are required. The tables below are the **core transition surface** — a representative
subset. The full surface (Anchor transport, randomizer oracle exports, cross-game item delivery,
save callbacks) is larger and lives in that one header; see [`deviations/`](deviations/) per feature.
`scripts/check-export-bindings.ps1` verifies every pointer is declared, resolved, and named after
the symbol it resolves from.

| OOT export (`soh.dll`)         | Purpose                                                      |
|--------------------------------|--------------------------------------------------------------|
| `SOH_Init`                     | Build OOT context + resource manager + window (required)     |
| `SOH_RunMain`                  | Run the OOT game loop; returns on exit/transition (required) |
| `SOH_Extract`                  | Launch the OOT asset extractor if archives are missing       |
| `SOH_SetOnNewSaveCallback`     | Notify the launcher when a new OOT save is created           |
| `SOH_SetOnSceneSwitchCallback` | Notify the launcher when the Mask Shop transition fires      |
| `SOH_PrepareForTransition`     | Quiesce OOT before handing off to MM                         |
| `SOH_ResumeGame`               | Resume OOT after returning from MM                           |
| `SOH_NotifyComboReturn`        | Tell OOT a return-from-MM is in progress                     |
| `SOH_Deinit`                   | Fully tear down the OOT context at program exit              |

| MM export (`2ship.dll`)        | Purpose                                                      |
|--------------------------------|--------------------------------------------------------------|
| `MM_InitArchives`              | (required) MM archive bring-up                               |
| `MM_Extract`                   | Launch the MM ROM extractor if `mm.o2r` is missing           |
| `MM_InitRandoSaveFile`         | Create the rando MM save matching an OOT slot (0 = ok)        |
| `MM_RunGame`                   | Boot MM into the given file number                           |
| `MM_ResumeGame`                | Resume MM after returning from OOT                            |
| `MM_NotifyComboTransition`     | Tell MM an OOT→MM handoff is in progress                      |
| `MM_SetOnComboReturnCallback`  | Notify the launcher when the Clock Tower return fires         |
| `MM_PrepareForTransition`      | Quiesce MM before handing back to OOT                        |

### Boot & Transition Loop

`main()` in `combo/ComboShip.cpp`:

1. Load `soh.dll` and `2ship.dll`; resolve exports.
2. Ensure both ROM archives exist (`oot*.o2r`, and `mm.o2r` / `mm.zip` / `mm.otr`). If either is
   missing, create the window from the bundled `soh.o2r` (`SOH_InitWindowOnly`) and run comboui's
   combo-owned extraction screen, which gathers both ROMs with progress bars.
3. `SOH_Init()` — bring up OOT.
4. Register the OOT new-save and scene-switch callbacks.
5. Run the **bidirectional switch loop**:
   - **OOT side:** first entry calls `SOH_RunMain`; later entries call `SOH_ResumeGame`. Entering
     the **Mask Shop** sets a pending MM file number, so the loop calls `SOH_PrepareForTransition`,
     `MM_NotifyComboTransition`, registers the MM→OOT return callback, and switches to MM.
   - **MM side:** first entry calls `MM_RunGame`; later entries call `MM_ResumeGame`. Entering the
     **Clock Tower** sets a pending-return flag, so the loop calls `MM_PrepareForTransition`,
     `SOH_NotifyComboReturn`, and switches back to OOT.
   - The one-time per-process init (heaps/threads) runs only on the **first** entry into each game;
     subsequent entries resume on the shared context/window.
   - **Ctrl+R reset:** `ResetHandler` calls `Combo_HandleReset`. OOT foreground → normal reset (lands on
     the boot sequence). MM foreground → `MM_RequestComboReturn` reuses the return path (MM saves only if
     autosave is on) and flags `SOH_ResumeGame` to leave `gComboReturnFileNum = -1`, so `title_setup.c`
     boots to `Title_Init` (first-boot) instead of resuming the save.
6. On loop exit, `SOH_Deinit()` tears down the OOT context that was kept alive across transitions.

### Shared Window, Context, and Resources

- The OOT window and `Ship::Context` stay alive across an OOT↔MM transition rather than being
  destroyed and recreated (which is what would otherwise open a second window).
- Each game has its **own `ResourceManager`**; the active one is swapped on transition.
- MM audio name maps are kept resident across transitions so MM audio still resolves after the
  first handoff.

### Save Linkage

When a new OOT save is created, the launcher's `Combo_OnOOTSaveInit` calls `MM_InitRandoSaveFile` to
create the matching MM save (**OOT slot N → MM file N+1**), deriving MM's placement payload fresh
from the bound consolidated seed. The MM save is always `SAVETYPE_RANDO` — ComboShip has no vanilla
mode, and a vanilla MM save would silently disable every `IS_RANDO` behavior. On the OOT→MM handoff, MM's
`title_setup` loads that file and spawns the player in South Clock Town. This is save *linkage by
slot*, not a unified/merged save format. Cross-game item grants are written directly into the linked
save of whichever game is dormant (see [Cross-Game Features](#cross-game-features)).

## Cross-Game Features

These are what ComboShip adds on top of the two ports. Each is summarized here; the load-bearing
details and merge-survival notes live in [`deviations/`](deviations/).

- **Cross-world randomizer** — one seed shuffles items across *both* games; an OOT check can hold an
  MM item and vice-versa. The launcher runs the combined fill (`RunComboFill`), driving each game's
  oracle exports (`*_Rando_Reset` / `SetOwnedItems` / `GetReachableChecks` / `PlaceItem`) so the fill
  reasons over both games' reachability at once, honoring the OOT→MM portal gate. See
  [`deviations/rando.md`](deviations/rando.md).
- **Cross-game item delivery** — collecting a foreign check grants the item immediately into the
  *other* game's resident save via a save-only export (the target is usually the dormant game, so its
  save isn't ticking underneath us), and shares it over Anchor. Replaced the old JSON mailbox
  (issue #3). See [`deviations/rando.md`](deviations/rando.md).
- **Anchor online co-op** — the persistent TCP socket + receive thread live in the launcher so the
  connection survives OOT↔MM transitions. Each game redirects its Anchor transport through
  launcher-registered callbacks. Supports presence, remote-player puppets, shared-progression item
  sync, and late-join resync, cross-game. See [`deviations/anchor.md`](deviations/anchor.md).
- **Cross-game hints & shared trackers** — hints can point across games; check/item trackers show
  both games. See [`deviations/rando.md`](deviations/rando.md) and
  [`deviations/tracker.md`](deviations/tracker.md).

## Game-Code Integration

Edits to decomp source (`soh/src/`, `mm/src/`) are kept minimal and fenced:

```c
#ifdef COMBO_BUILD
    if (gComboStartFileNum >= 0) {
        // combo-only setup
    }
#endif
```

The combo footprint in game source is only a handful of lines per file (e.g. `title_setup.c`,
`graph.c`, `main.c`, `z_sram.c`). Anything larger lives in the port/enhancement layers
(`soh/soh/`, `mm/2s2h/`) or in the launcher.

## Handling Upstream Updates

When pulling from upstream Ship of Harkinian or 2 Ship 2 Harkinian:

1. **Game code (`soh/src/`, `mm/src/`)** — most changes merge cleanly; conflicts only occur at the
   small `#ifdef COMBO_BUILD` hook sites.
2. **Enhancements (`soh/soh/`, `mm/2s2h/`)** — generally merge cleanly.
3. **Build system** — may need adjustments in the top-level `CMakeLists.txt`; keep the per-game
   `CMakeLists.txt` files as close to upstream as possible.

**Resolving a hook conflict** — keep the upstream code, re-add the fenced hook:

```c
// Resolution: take upstream body, re-insert the COMBO_BUILD hook
void SomeUpstreamFunction(void) {
    DoUpstreamThing();
#ifdef COMBO_BUILD
    Combo_OnSomething();
#endif
}
```

## Build System

- The top-level `CMakeLists.txt` defines `COMBO_BUILD`, adds the shared `libultraship`, `ZAPD`, and
  `OTRExporter`, and `add_subdirectory(combo)` for the launcher.
- A meta target builds everything: `add_custom_target(combo ALL DEPENDS soh 2ship ComboShip)`.
- `combo/CMakeLists.txt` builds `ComboShip.exe` from `combo/ComboShip.cpp` (`main()` and the boot
  sequence) plus the launcher modules in `combo/core/`, and, as a POST_BUILD step, copies the three
  DLLs, the port `.o2r` archives, and extractor assets next to the exe.
- Convenience build scripts live in `scripts/` (`build-libultraship.ps1`, `build-soh.ps1`,
  `build-2ship.ps1`, `build-comboship.ps1`). Build targets individually rather than rebuilding
  everything.

## Future Direction

Still **not implemented**, recorded as intent:

- **Combined save format** — a single save containing both games' data plus shared state (today the
  two saves are linked by slot number, with cross-game items written into each game's own save).
- **Seamless transition polish** — masking load time, transition effects.

(Shared inventory and progression sync, once listed here as future work, now exist for randomizer
items via cross-game delivery and for co-op via Anchor.)

If/when the remaining items are built, they should follow the same principles: logic in the launcher
and port layers, with only minimal `#ifdef COMBO_BUILD` hooks in game source.
