# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ComboShip is a unified launcher that combines **Ship of Harkinian** (OOT) and **2 Ship 2 Harkinian** (MM) into a single cohesive game experience (OoTMM-style). Both games build as separate DLLs and are dynamically loaded at runtime by the `ComboShip` executable.

## Build System

This project uses CMake with vcpkg for dependency management (Windows). The root `CMakeLists.txt` orchestrates all sub-projects.

### Windows (Visual Studio)

```bash
# Configure (from repo root — generates VS solution)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build everything (ComboShip + soh.dll + 2ship.dll)
cmake --build build --config Debug

# Build a specific target
cmake --build build --config Debug --target soh
cmake --build build --config Debug --target 2ship
cmake --build build --config Debug --target ComboShip
```

On Windows, vcpkg is bootstrapped automatically by CMake via `CMake/automate-vcpkg.cmake`. Required packages are installed automatically (zlib, bzip2, libzip, libpng, sdl2, glew, glfw3, nlohmann-json, tinyxml2, spdlog, libogg, libvorbis, opus, opusfile).

### Linux

```bash
cmake -S . -B build
cmake --build build --config Release
```

### Post-Build Output

After building, the `combo` CMake target copies all outputs to `x64/Debug/`:
- `ComboShip.exe`
- `soh.dll`, `2ship.dll`, `libultraship.dll`
- `soh.o2r`, `2ship.o2r` (built asset archives)

The player must separately provide `oot.o2r` and `mm.o2r` (extracted from their own ROM files).

### Packaging

```bash
cmake --build build --target package
```

## Architecture

### Layer Model

```
ComboShip.exe          -- Launcher: dynamically loads game DLLs
    soh.dll            -- OOT (Ship of Harkinian), minimal hooks added
    2ship.dll          -- MM (2 Ship 2 Harkinian), minimal hooks added
    libultraship.dll   -- Shared engine (graphics, audio, input, resource mgmt)
```

### Sub-Projects

| Directory | Target | Description |
|-----------|--------|-------------|
| `combo/` | `ComboShip` | Main launcher executable; currently loads one game DLL at a time |
| `soh/` | `soh` (DLL) | OOT game — git-subrepo from HarbourMasters/soh |
| `mm/` | `2ship` (DLL) | MM game — subtree/fork of 2 Ship 2 Harkinian |
| `libultraship/` | `libultraship` | Shared engine library |
| `ZAPDTR/ZAPD` | `ZAPD` | Asset extraction tool |
| `OTRExporter/` | `OTRExporter` | Asset export tool |

### Planned Combo Layer (`combo/` directory — not yet fully implemented)

The planned architecture adds these systems under `combo/`:
- `core/` — `UnifiedGameState`, `OOTAdapter`, `MMAdapter` (Bridge pattern)
- `systems/` — `SharedInventory`, `ProgressionSync`, `TransitionManager`, `CombinedSaveSystem`
- `hooks/` — Event hooks inserted into game code via `#ifdef COMBO_BUILD`
- `data/` — Cross-game mappings (items, scenes, progression flags)

See `docs/ARCHITECTURE.md` and `docs/IMPLEMENTATION_ROADMAP.md` for the full design.

### Key Design Principles

- **Minimal game code changes**: All OOT/MM-specific hooks are guarded with `#ifdef COMBO_BUILD` so upstream merges stay clean.
- **Composition over modification**: The combo layer wraps games via adapters rather than merging their code.
- **Shared libultraship**: Both games link against a single shared `libultraship` build located in the repo root (not inside `soh/` or `mm/`).

### O2R / OTR Asset System

- `.o2r` files are the asset archives used at runtime (successor to `.otr`)
- `soh.o2r` and `2ship.o2r` are built from game assets during compilation
- `oot.o2r` / `mm.o2r` are player-supplied (extracted from ROM using ZAPD)
- The `ComboShip` launcher checks for these files at startup and offers to run extraction if missing

## Upstream Merge Strategy

When pulling upstream changes from Ship of Harkinian or 2 Ship 2 Harkinian:

1. Conflicts will only occur at `#ifdef COMBO_BUILD` hook sites — always keep upstream code and re-add the hook.
2. If upstream changes a function signature that an adapter depends on, update the adapter, not the game code.
3. The `soh/` directory is tracked as a git-subrepo (see `soh/.gitrepo`).

## ComboShip Hooks Implemented

All hooks are guarded with `#ifdef COMBO_BUILD`.

### `soh/soh/OTRGlobals.cpp`
- `SOH_Init()` — exported DLL entry point; calls `InitOTR()` (creates context, loads archives, creates window)
- `SOH_RunMain()` — exported; runs the OOT game loop; caller must call `SOH_Init()` first
- `SOH_Deinit()` — exported; tears down OTR context and frees heaps (skipped in non-COMBO standalone path)
- `SOH_SetOnNewSaveCallback(cb)` — registers a callback fired when OOT creates a new save file (used to mirror save into MM)
- `SOH_SetOnSceneSwitchCallback(cb)` — registers a callback fired when a trigger scene is entered (used to switch to MM)
- `SOH_PrepareForTransition()` — stops OOT audio thread and flushes saves before handing off to MM

### `soh/src/code/main.c`
- `SOH_RunMain()` — wraps the original `Main()` loop for DLL export; standalone path unchanged

### `soh/src/code/z_sram.c`
- Calls `gComboSaveInitCallback` when a new save file is created

### `soh/src/code/graph.c`
- Calls `gComboSceneSwitchCallback` when the trigger scene (Mido's house / Mask Shop) is entered, then sets `gGameState->running = false` to exit the OOT game loop

### `mm/2s2h/BenPort.cpp`
- `MM_Extract(workDir)` — exported; runs MM asset extraction
- `MM_InitArchives()` — exported; loads MM archives into a dormant archive manager
- `MM_ArchiveCount()` — exported; returns number of loaded MM archives
- `MM_InitSaveFile(fileNum)` — exported; initialises an MM save slot mirroring the OOT slot
- `MM_RunGame(fileNum)` — exported; full MM init + game loop for the given save slot
- `MM_NotifyComboTransition()` — exported; sets `sComboTransitionActive = true` so MM reuses the existing LUS context/window instead of creating a new one

### `mm/2s2h/SaveManager/SaveManager.cpp`
- Saves MM data per OOT file slot when `MM_InitSaveFile` is called

### libultraship (no changes)
- `libultraship` is built **STATIC** — each game DLL has its own copy of all LUS code including `Ship::Context::mContext` (a `static std::weak_ptr<Context>`)
- `Context::GetInstance()` returns `mContext.lock()`; the caller must keep the returned `shared_ptr` alive

## Window Ownership

- **OOT creates and owns the window** via `SOH_Init()` → `InitOTR()` → `OTRGlobals::Initialize()` → `context->InitWindow(...)`
- **MM reuses the existing window** on OOT→MM transition via the `sComboTransitionActive` flag in `mm/2s2h/BenPort.cpp`. When this flag is set, MM's `InitOTR` equivalent skips window/context creation and calls `Ship::Context::GetInstance()` instead.
- Do **not** try to pre-create the window in `ComboShip.exe` before `SOH_Init()`. The `Ship::Gui` constructor calls `GuiWindow::Init()` on each registered window, which eventually calls `Context::GetInstance()->GetWindow()` — but the window doesn't exist yet, causing a null-deref crash.

## Boot Flow (ComboShip.exe)

1. Load `soh.dll` + `2ship.dll`, resolve exports
2. Ensure OOT archives exist (extract if missing)
3. Ensure MM ROM archive exists (extract if missing)
4. `SOH_Init()` — creates OOT context, loads archives, creates the shared window
5. `SOH_RunMain()` — blocks until OOT exits (or trigger scene fires)
6. If `g_PendingMMFileNum >= 0`: `SOH_PrepareForTransition()`, `MM_NotifyComboTransition()`, `MM_RunGame(slot)`
7. `SOH_Deinit()` — tears down OOT context
8. Unload DLLs

## Code Standards

- C++20 / C23
- MSVC on Windows (`/MP` parallel builds, `/utf-8` encoding)
- Warnings suppressed in LUS and decomp source via `SUPPRESS_WARNINGS=ON` (default)
- `CONTROLLERBUTTONS_T` is defined globally as `uint32_t`
