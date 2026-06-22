# ComboShip

**ComboShip is a cross-game randomizer for [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) (Ocarina of Time) and [2 Ship 2 Harkinian](https://github.com/HarbourMasters/2ship2harkinian) (Majora's Mask).**

## What it is

Like [OOTMM](https://ootmm.com/), ComboShip shuffles items across *both* games at once: a check in Ocarina of Time can hold a Majora's Mask item and vice-versa, and a single seed spans the two. Both games run together in one application; ComboShip builds that combined runtime on top of the existing Ship of Harkinian and 2 Ship 2 Harkinian ports.

## Features

- Anything within Ship and 2Ship is here. When those projects gets new updates, they are not far from inclusion here.
- Online Multiplayer through Anchor. And yes it works cross-game as well. Work together in OOT and MM, split or together.

## Future plans

- Possibly looking into some more OOTMM features, but it's not a priority yet.
- Possible Archipelago support as well, ideally through the existing SoH implementation

## Any bugs?

Probably many! Create an issue if you find any.

## The elephant in the room

"Is this vibecoded?"

It depends on your view. **Yes**, this project uses a lot of assistance from AI/LLMs in order quickly generate code, write commits, or create PRs. I am defining the rules, structure, and decisions on how it gets implemented, so I call it "AI-assisted". If you don't like it, I won't try to change your mind, it's up to you. My code principle for this project is combine the original SoH/2Ship instruction of "we try not to touch the source code" with a new "we try not to touch the port code". Any changes in SoH or 2Ship are modified with guardrails for ComboShip so the original port code remains intact. This is also key in order to keep pulling in new changes from upstream relatively painlessly. I have a whole workflow for this alone.

## Building

ComboShip currently builds on **Windows** only. macOS and Linux support will return later.

### Prerequisites

- Windows 10/11 (x64)
- Visual Studio 2022 (MSVC, with C++20 / C23 support)
- CMake 3.26 or newer

### Configure and build

From the `Combo/` directory, configure once to generate the Visual Studio solution:

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
