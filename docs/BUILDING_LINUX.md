# Building and running ComboShip on Linux

The Linux build is developed and CI-tested on Ubuntu 24.04 (noble). Three ways
to get a runnable ComboShip, easiest first:

1. **AppImage** (recommended for players) — the `ComboShip-linux-appimage` CI
   artifact. Self-contained: bundles SDL2, tinyxml2, spdlog and the rest of the
   shared-library dependencies, runs on any reasonably current x86_64 distro.
2. **Plain CI artifact** (`ComboShip-linux`) — the raw binary tree. It links
   against the *build host's* system libraries (Ubuntu 24.04), so on another
   distro it fails unless the same ABI versions are installed — e.g.
   `libtinyxml2.so.10: cannot open shared object file`. Use the AppImage or
   build from source instead of chasing those.
3. **Build from source** — below.

Either way, the game archives `oot.o2r` / `mm.o2r` are extracted from your own
OoT and MM ROMs — the launcher offers extraction on first boot; nothing
copyrighted ships in the artifacts.

## Running the AppImage

```sh
chmod +x ComboShip-*-linux-x86_64.AppImage   # the exec bit is lost in the artifact zip
./ComboShip-0.2.1-linux-x86_64.AppImage
```

On first run it creates a writable `ComboShip-data/` directory next to the
AppImage holding saves, config, mods and the game archives; set `SHIP_HOME` to
put it somewhere else. Drop already-extracted `oot.o2r` / `mm.o2r` files in
there to skip the extraction screen.

On the ROM-setup screen you can drag and drop your ROMs onto the window, or
click Browse — the file picker needs `zenity` or `kdialog` installed (virtually
every desktop distro ships one).

## Building from source

### Dependencies (Ubuntu 24.04)

```sh
sudo apt-get install -y \
    build-essential cmake ninja-build python3 \
    libusb-dev libusb-1.0-0-dev libsdl2-dev libsdl2-net-dev libpng-dev libglew-dev \
    nlohmann-json3-dev libtinyxml2-dev libspdlog-dev \
    libogg-dev libopus-dev opus-tools libopusfile-dev libvorbis-dev \
    libzip-dev zipcmp zipmerge ziptool
```

Ubuntu 22.04 (jammy) is not supported: the build expects noble's SDL 2.30.x and
tinyxml2 10.x. Other distros need the equivalent `-dev` packages (SDL2 ≥ 2.30,
tinyxml2 10, spdlog, fmt, GLEW, libzip, nlohmann-json, ogg/opus/opusfile/vorbis,
libpng, libusb).

### Build

```sh
cmake -B build-cmake -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target libultraship soh 2ship comboui ComboShip -j 4
cmake --build build-cmake --target GenerateSohOtr Generate2ShipOtr -j 4   # port o2rs
```

Notes:

- **Build in this order** (libultraship → soh → 2ship → the combo targets) if
  you split the invocation; soh and 2ship share OTRExporter/ZAPD.
- **Memory:** some soh/2ship translation units peak above 1 GB each. `-j 4`
  fits a 16 GB machine; scale jobs to roughly RAM/4 GB, not core count.
- **ccache** is worth setting up for iteration: add
  `-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache`
  to the configure line.
- `comborando` (headless seed validator) is an optional extra target.

### Run

The runtime layout is portable: the launcher dlopens `./libsoh.so`,
`./lib2ship.so` and `./comboui.so` and reads/writes everything relative to the
current directory. The build already stages all of that in `build-cmake/combo/`,
so:

```sh
cd build-cmake/combo
cp ../soh/soh.o2r ../mm/2ship.o2r .   # once, after GenerateSohOtr/Generate2ShipOtr
./ComboShip
```

If you copy the tree elsewhere (or to another machine), the baked-in RUNPATH
pointing at the build tree goes dead — run with the directory on the library
path:

```sh
LD_LIBRARY_PATH="$PWD" ./ComboShip
```

(That still requires the system libraries above; the AppImage exists so end
users don't need any of this.)

### Package an AppImage locally

```sh
./scripts/linux/make-appimage.sh build-cmake _packages
```

Produces `_packages/stage/` (the plain runnable tree, same layout as the
`ComboShip-linux` CI artifact) and
`_packages/ComboShip-<version>-linux-x86_64.AppImage`. Downloads `linuxdeploy`
into `_packages/` on first use; needs `curl`, no FUSE required. The AppImage
pieces live in `combo/linux/` (`AppRun`, `ComboShip.desktop`, `ComboShip.png`)
and the version comes from `COMBO_RELEASE_VERSION` in the top-level
`CMakeLists.txt`.
