# ComboShip

**ComboShip is a cross-game randomizer for [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (Ocarina of Time) and [2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (Majora's Mask).**

**DISCLAIMER: THIS IS AN UNOFFICIAL PROJECT AND USES AI AS PART OF THE DEVELOPMENT. IT IS NOT CREATED OR HANDLED BY THE HARBOUR MASTERS TEAM. BRING ANY IDEAS, QUESTIONS, OR CONCERNS TO ME DIRECTLY!**

## What it is

Like [OOTMM](https://ootmm.com/), ComboShip shuffles items across *both* games at once: a check in Ocarina of Time can hold a Majora's Mask item and vice-versa, and a single seed spans the two. Both games run together in one application; ComboShip builds that combined runtime on top of the existing Ship of Harkinian and 2 Ship 2 Harkinian ports.

## Features

- Anything within Ship and 2Ship is here. When those projects gets new updates, they are not far from inclusion here.
- Online Multiplayer through Anchor. And yes it works cross-game as well. Work together in OOT and MM, split or together.

## Future plans

- Possibly looking into some more OOTMM features, but it's not a priority yet.
- Possible Archipelago support as well, ideally through the existing SoH implementation

## Got issues?

Check out the (nightly build)[https://nightly.link/Varuuna/ComboShip/workflows/build-artifacts/develop/ComboShip-windows.zip] to see if your issue has already been fixed for an upcoming release.
If you can still reproduce it, please create an issue.

## Building

ComboShip currently builds on **Windows** only. macOS and Linux support will return later.

### Prerequisites

- Windows 10/11 (x64)
- Visual Studio 2022 (MSVC, with C++20 / C23 support)
- CMake 3.26 or newer

### Configure and build

From the repository root (the clone folder, which contains this README), configure once to generate the Visual Studio solution:

```powershell
cmake -B build/x64 -A x64
```

Helper scripts in `scripts/` wrap `cmake --build` and default to a Debug build (pass `--Release` for Release):

```powershell
./scripts/build-comboship.ps1  ->  ComboShip.exe
```

## Packaging

`cpack` produces a single Windows ZIP bundling the full runtime (`ComboShip.exe`, the engine and UI DLLs, both ports, and assets):

```powershell
cpack
```

## Contributing

ComboShip is built on two upstream projects, kept as vendored copies under `soh/` and `mm/`:

- Ship of Harkinian — https://github.com/HarbourMasters/Shipwright
- 2 Ship 2 Harkinian — https://github.com/HarbourMasters/2ship2harkinian

ComboShip-specific code lives in `combo/`, and changes to the vendored ports are kept minimal and guarded behind `COMBO_BUILD`. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and [`docs/UPSTREAM_MERGES.md`](docs/UPSTREAM_MERGES.md) for how upstream changes are merged in.

## License

ComboShip combines two separately-licensed projects; each retains its own license. See the `soh/` and `mm/` directories for details.
