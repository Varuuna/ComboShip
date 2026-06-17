# ComboShip

**ComboShip is a cross-game randomizer for [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (Ocarina of Time) and [2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (Majora's Mask).**

> **Just want to play?** Grab a build from the [Releases](../../releases) page — you don't need any of the below.

## What it is

Like [OOTMM](https://ootmm.com/), ComboShip shuffles items across *both* games at once: a check in Ocarina of Time can hold a Majora's Mask item and vice-versa, and a single seed spans the two. Both games run together in one application; ComboShip builds that combined runtime on top of the existing Ship of Harkinian and 2 Ship 2 Harkinian ports.

For how the combined runtime is put together (the shared engine, per-game resources, transitions), see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Building

ComboShip currently builds on **Windows** only. macOS and Linux support will return later.

### Prerequisites

- Windows 10/11 (x64)
- Visual Studio 2022 (MSVC, with C++20 / C23 support)
- CMake 3.26 or newer
- Python 3 (used during asset extraction)
- Git
- vcpkg — bootstrapped automatically during configure; no manual install needed

### Configure and build

From the `Combo/` directory, configure once to generate the Visual Studio solution:

```powershell
cmake -B build/x64 -A x64
```

Then build the targets in dependency order. Helper scripts in `scripts/` wrap `cmake --build` and default to a Debug build (pass `--Release` for Release):

```powershell
./scripts/build-libultraship.ps1   # shared engine  -> libultraship.dll
cmake --build build/x64 --target soh   --config Debug   # OOT -> soh.dll
cmake --build build/x64 --target 2ship --config Debug   # MM  -> 2ship.dll
./scripts/build-comboship.ps1      # launcher       -> ComboShip.exe
```

Build order is always **libultraship → soh → 2ship → ComboShip**. Building `ComboShip` also rebuilds `soh` if it is stale. The runnable output lands in `build/x64/combo/<config>/`, with the DLLs, port assets, and resources copied next to `ComboShip.exe`.

On first run, the games prompt for your OOT and MM ROMs and extract assets themselves — there is no separate extraction step.

## Packaging

`cpack` produces a single Windows ZIP bundling the full runtime (`ComboShip.exe`, the engine and UI DLLs, both ports, and assets):

```powershell
cpack
```

The archive is written to `_packages/`. ROM-derived assets are intentionally excluded — testers supply their own ROMs.

## Project layout

```
Combo/
├── combo/          # ComboShip layer: launcher, cross-game randomizer, shared menu/UI
├── soh/            # Ship of Harkinian (OOT) port  -> soh.dll
├── mm/             # 2 Ship 2 Harkinian (MM) port  -> 2ship.dll
├── libultraship/   # Shared game engine            -> libultraship.dll
├── ZAPDTR/         # Asset extraction tool (ZAPD)
├── OTRExporter/    # Asset export utilities
├── CMake/          # CMake helper scripts and packaging config
├── scripts/        # Build helper scripts (PowerShell)
└── docs/           # Architecture notes and upstream-merge logs
```

## Contributing

ComboShip is built on two upstream projects, kept as vendored copies under `soh/` and `mm/`:

- Ship of Harkinian — https://github.com/HarbourMasters/Shipwright
- 2 Ship 2 Harkinian — https://github.com/HarbourMasters/2ship2harkinian

ComboShip-specific code lives in `combo/`, and changes to the vendored ports are kept minimal and guarded behind `COMBO_BUILD`. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and [`docs/UPSTREAM_MERGES.md`](docs/UPSTREAM_MERGES.md) for how upstream changes are merged in.

## License

ComboShip combines two separately-licensed projects; each retains its own license. See the `soh/` and `mm/` directories for details.
