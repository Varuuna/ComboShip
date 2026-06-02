# ComboShip Architecture

## Overview

ComboShip runs **Ocarina of Time** (Ship of Harkinian) and **Majora's Mask** (2 Ship 2 Harkinian)
as a single program that can hand off back and forth between the two games at runtime, while keeping
each game's source close enough to upstream that updates can still be merged in.

> **Status note:** This document describes what is *actually built*. The deep cross-game features
> (shared inventory, progression sync, a combined save format) are **not implemented** — see
> [Future Direction](#future-direction). Earlier revisions of this file described an adapter/observer
> class architecture that was never built; it has been removed.

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
│  ComboShip.exe  (combo/ComboShip.cpp)                          │
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
(see `combo/ComboShip.cpp`). Missing optional exports are tolerated (null-checked); only the core
ones are required.

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
| `MM_InitSaveFile`              | Create the MM save matching an OOT slot                      |
| `MM_RunGame`                   | Boot MM into the given file number                           |
| `MM_ResumeGame`                | Resume MM after returning from OOT                            |
| `MM_NotifyComboTransition`     | Tell MM an OOT→MM handoff is in progress                      |
| `MM_SetOnComboReturnCallback`  | Notify the launcher when the Clock Tower return fires         |
| `MM_PrepareForTransition`      | Quiesce MM before handing back to OOT                        |

### Boot & Transition Loop

`main()` in `combo/ComboShip.cpp`:

1. Load `soh.dll` and `2ship.dll`; resolve exports.
2. Ensure OOT archives exist (`soh.o2r` / `oot*.o2r`), running `SOH_Extract` if not.
3. Ensure the MM ROM archive exists (`mm.o2r` / `mm.zip` / `mm.otr`), running `MM_Extract` if not.
4. `SOH_Init()` — bring up OOT.
5. Register the OOT new-save and scene-switch callbacks.
6. Run the **bidirectional switch loop**:
   - **OOT side:** first entry calls `SOH_RunMain`; later entries call `SOH_ResumeGame`. Entering
     the **Mask Shop** sets a pending MM file number, so the loop calls `SOH_PrepareForTransition`,
     `MM_NotifyComboTransition`, registers the MM→OOT return callback, and switches to MM.
   - **MM side:** first entry calls `MM_RunGame`; later entries call `MM_ResumeGame`. Entering the
     **Clock Tower** sets a pending-return flag, so the loop calls `MM_PrepareForTransition`,
     `SOH_NotifyComboReturn`, and switches back to OOT.
   - The one-time per-process init (heaps/threads) runs only on the **first** entry into each game;
     subsequent entries resume on the shared context/window.
7. On loop exit, `SOH_Deinit()` tears down the OOT context that was kept alive across transitions.

### Shared Window, Context, and Resources

- The OOT window and `Ship::Context` stay alive across an OOT↔MM transition rather than being
  destroyed and recreated (which is what would otherwise open a second window).
- Each game has its **own `ResourceManager`**; the active one is swapped on transition.
- MM audio name maps are kept resident across transitions so MM audio still resolves after the
  first handoff.

### Save Linkage

When a new OOT save is created, the launcher's `Combo_OnOOTSaveInit` calls `MM_InitSaveFile` to
create the matching MM save (**OOT slot N → MM file N+1**). On the OOT→MM handoff, MM's
`title_setup` loads that file and spawns the player in South Clock Town. This is save *linkage by
slot*, not a unified/merged save format.

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
- `combo/CMakeLists.txt` builds `ComboShip.exe` from `combo/ComboShip.cpp` and, as a POST_BUILD step,
  copies the three DLLs, the port `.o2r` archives, and extractor assets next to the exe.
- Convenience build scripts live in `scripts/` (`build-libultraship.ps1`, `build-soh.ps1`,
  `build-2ship.ps1`, `build-comboship.ps1`). Build targets individually rather than rebuilding
  everything.

## Future Direction

These were goals in the original design and are **not yet implemented**. They are recorded here as
intent, not as existing architecture:

- **Shared inventory** — items obtained in one game appearing in the other.
- **Progression sync** — flags/progress shared across games.
- **Combined save format** — a single save containing both games' data plus shared state (today the
  two saves are merely linked by slot number).
- **Seamless transition polish** — masking load time, transition effects.

If/when these are built, they should follow the same principles: logic in the launcher and port
layers, with only minimal `#ifdef COMBO_BUILD` hooks in game source.
