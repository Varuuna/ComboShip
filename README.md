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

Check out the nightly builds to see if your issue has already been fixed for an upcoming release:
- [Windows](https://nightly.link/Varuuna/ComboShip/workflows/build-artifacts/develop/ComboShip-windows.zip)
- [Linux](https://nightly.link/Varuuna/ComboShip/workflows/build-artifacts/develop/ComboShip-linux.zip) / [Linux AppImage](https://nightly.link/Varuuna/ComboShip/workflows/build-artifacts/develop/ComboShip-linux-appimage.zip)
- [macOS](https://nightly.link/Varuuna/ComboShip/workflows/build-artifacts/develop/ComboShip-macos.zip)

If you can still reproduce it, please create an issue.

## Building

ComboShip builds on **Windows** (below), **macOS** (below) and **Linux** — see
[`docs/BUILDING_LINUX.md`](docs/BUILDING_LINUX.md) for Linux build, run and
AppImage packaging instructions.

### Windows

**Prerequisites** — Windows 10/11 (x64), Visual Studio 2022 (MSVC, with C++20 / C23 support), CMake 3.26 or newer.

From the repository root (the clone folder, which contains this README), configure once to generate the Visual Studio solution:

```powershell
cmake -B build/x64 -A x64
```

Helper scripts in `scripts/` wrap `cmake --build` and default to a Debug build (pass `--Release` for Release):

```powershell
./scripts/build-comboship.ps1  ->  ComboShip.exe
```

### macOS

**Prerequisites** — macOS 11 or newer (Apple Silicon or Intel), Xcode command line tools, and the
Homebrew dependencies below. Ninja is used, matching what both upstream ports document.

```bash
brew install cmake ninja libpng glew tinyxml2 nlohmann-json libzip opusfile libvorbis libogg opus spdlog glfw sdl2_net
```

```bash
cmake -S . -B build-macos -GNinja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-macos --target ComboShip -j10
```

That produces the full runtime in `build-macos/combo/` — the launcher, plus `libsoh.dylib`,
`lib2ship.dylib` and `libultraship.dylib` symlinked beside it, exactly as the Linux build stages
them. Run `./build-macos/combo/ComboShip` **from that directory**: the launcher itself finds its
modules by executable path, but the asset archives are still resolved relative to the working
directory (or `SHIP_HOME`).

#### A note on audio

libultraship defaults macOS to the CoreAudio backend, whose player binds the output device directly
and requests a hardcoded 44100 Hz. macOS persists a device's format, so that can leave your output
device reconfigured **system-wide, permanently** — a DisplayPort display pinned at 44100 instead of
48000 will cut out in every application, and it survives a reboot.

ComboShip therefore uses the SDL backend on macOS, and rewrites a saved `coreaudio` setting to
`sdl` — otherwise anyone who had already launched an earlier build would stay exposed, since
`coreaudio` is what the old default wrote for them. In practice this means **CoreAudio cannot be
selected on macOS**: picking it in Audio settings holds for the session and reverts on the next
launch. If you really want it, run with `SHIP_AUDIO_BACKEND=coreaudio` each time.

If your audio is already in that state, fix it in **Audio MIDI Setup** — select the device, set
Format back to **48000 Hz**.

#### A note on SDL2

Homebrew's `sdl2` is now an alias for **sdl2-compat** — the SDL2 API reimplemented on top of SDL3.
Real SDL2 is no longer in homebrew-core. ComboShip builds and runs fine against it, so the command
above is the quick path for a development build. It cannot be packaged, though: sdl2-compat loads
SDL3 at run time, which the `.app` cannot carry, so `cpack` refuses it.

It is not what upstream uses, though, and it is measurably worse here. A Time Profiler capture while
dragging tracker windows showed a noticeable share of main-thread time in ObjC/CoreFoundation churn
and Apple's GameController framework, which is how SDL3 drives macOS gamepads. Rebuilding against
genuine SDL2 reduced (but did not eliminate) the stutter.

For genuine SDL2, either use MacPorts `libsdl2` — what HarbourMasters' own macOS CI uses — or build
SDL2 and SDL2_net from source into a prefix and point CMake at it. Build **both**: Homebrew's
`sdl2_net` links against sdl2-compat, and two SDL2s in one process is worse than either alone.
HarbourMasters' Linux CI pins **2.30.3**:

```bash
cmake -S . -B build-macos -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$SDL2_PREFIX" -DSDL2_DIR="$SDL2_PREFIX/lib/cmake/SDL2" -DSDL2_net_DIR="$SDL2_PREFIX/lib/cmake/SDL2_net"
```

Setting `SDL2_DIR`/`SDL2_net_DIR` explicitly matters: CMake will not re-search once those cache
entries exist, so `CMAKE_PREFIX_PATH` alone silently keeps whatever was found first. Verify with
`otool -L build-macos/combo/ComboShip` — genuine 2.30.3 reports `current version 3001.3.0`,
sdl2-compat reports `3201.x`.

A source-built SDL installs itself as `@rpath/libSDL2-2.0.0.dylib`, which only resolves if the
dylib sits beside the binaries, so the build copies it into the runtime directory and into the
`.app` automatically. Homebrew's SDL has an absolute install name and needs no such copy; the build
picks between the two by reading the install name, so neither route needs a flag.

## Packaging

### Windows

`cpack` produces a single ZIP bundling the full runtime (`ComboShip.exe`, the engine and UI DLLs, both ports, and assets):

```powershell
cpack
```

### macOS

`cpack` produces an ad-hoc signed `ComboShip.app` inside a DMG. It needs genuine SDL2 (see the SDL2
note above) and the port archives. Generate those first — the install rules copy them from the
source tree (`soh/soh.o2r`, `mm/2ship.o2r`) and fail the package if either is missing:

```bash
cmake --build build-macos --target GenerateSohOtr && cmake --build build-macos --target Generate2ShipOtr
```

```bash
cd build-macos && cpack
```

The `.app` is self-contained: packaging copies every non-system dylib it uses into
`Contents/MacOS`, so it runs on a Mac without Homebrew. Those dylibs are built for the macOS they
were installed on, not for the 11.0 deployment target, so the bundle's `LSMinimumSystemVersion` is
set to the newest `minos` among them — a bundle built on macOS 14 requires macOS 14.

The bundle keeps its own settings and saves in `~/Library/Application Support/com.comboship.ComboShip`,
so it never touches a Ship of Harkinian or 2 Ship 2 Harkinian install. ROM-derived archives are
player-extracted into that folder and are deliberately never packaged.

## Contributing

ComboShip is built on two upstream projects, kept as vendored copies under `soh/` and `mm/`:

- Ship of Harkinian — https://github.com/HarbourMasters/Shipwright
- 2 Ship 2 Harkinian — https://github.com/HarbourMasters/2ship2harkinian

ComboShip-specific code lives in `combo/`, and changes to the vendored ports are kept minimal and guarded behind `COMBO_BUILD`. See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the design and [`docs/UPSTREAM_MERGES.md`](docs/UPSTREAM_MERGES.md) for how upstream changes are merged in.

## License

ComboShip combines two separately-licensed projects; each retains its own license. See the `soh/` and `mm/` directories for details.
