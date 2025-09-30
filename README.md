# ComboShip - Combined OOT and MM Build

This is a unified build system that combines both **Ship of Harkinian (OOT)** and **2 Ship 2 Harkinian (MM)** into a single CMake project.

## Overview

The ComboShip build system allows you to build both Zelda ports simultaneously from a single CMake configuration. It shares common dependencies (libultraship, ZAPD, OTRExporter) while maintaining separate game-specific builds.

## Project Structure

```
ComboShip/
├── Combo/              # Unified build directory
│   ├── CMakeLists.txt  # Main combined build file
│   ├── .gitignore      # Git ignore rules
│   ├── README.md       # This file
│   ├── CMake/          # CMake helper scripts
│   ├── libultraship/   # Shared game engine library
│   ├── ZAPDTR/         # Asset extraction tool
│   │   └── ZAPD/       # ZAPD executable
│   ├── OTRExporter/    # Asset exporter utilities
│   ├── soh/            # OOT game code
│   └── mm/             # MM game code
├── OOT/                # Reference: Original OOT project
└── MM/                 # Reference: Original MM project
```

**Note:** The `OOT/` and `MM/` folders are reference directories. All active development happens in the `Combo/` directory.

## Features

- **Unified Build**: Build both games with a single CMake command
- **Shared Dependencies**: libultraship, ZAPD, and OTRExporter are built once at the root level
- **Flexible Configuration**: Supports all platforms (Windows, Linux, macOS, Switch)
- **Warning Suppression**: Optional warning suppression for cleaner builds
- **Git Integration**: Automatically captures branch, commit hash, and tags

## Prerequisites

- CMake 3.26.0 or higher
- C++20 compatible compiler
- C23 compatible compiler (for C code)
- Python 3 (for asset extraction)
- Git (for version information)

### Platform-Specific Requirements

**Windows:**
- Visual Studio 2022 or later
- vcpkg (automatically bootstrapped)

**Linux:**
- GCC or Clang
- Development libraries: SDL2, OpenGL, etc.

**macOS:**
- Xcode Command Line Tools
- Minimum deployment target: macOS 10.15

## Building

### Quick Start

```bash
# From the Combo directory
mkdir build
cd build

# Configure
cmake ..

# Build both games
cmake --build . --target combo

# Or build individually
cmake --build . --target soh    # Build OOT only
cmake --build . --target 2ship  # Build MM only
```

### Configuration Options

```bash
# Suppress warnings (default: ON)
cmake -DSUPPRESS_WARNINGS=OFF ..

# Set build type
cmake -DCMAKE_BUILD_TYPE=Release ..

# Windows: Choose architecture
cmake -A x64 ..      # 64-bit (default)
cmake -A Win32 ..    # 32-bit
```

## Build Targets

- **combo**: Meta-target that builds both `soh` and `2ship`
- **soh**: Ship of Harkinian (OOT) executable
- **2ship**: 2 Ship 2 Harkinian (MM) executable
- **libultraship**: Shared game engine library
- **ZAPD**: Asset extraction and processing tool
- **OTRExporter**: Asset export utilities

## Asset Extraction

Both games require ROM files for asset extraction:

```bash
# Extract assets for both games
cmake --build . --target ExtractAssets

# The build system will prompt for ROM files if not found
```

## Installation

```bash
# Install both games
cmake --build . --target install

# Or use CPack for packaging
cpack
```

## CMake Variables

The following CMake cache variables are set:

- `CMAKE_PROJECT_GIT_BRANCH`: Current git branch
- `CMAKE_PROJECT_GIT_COMMIT_HASH`: Short commit hash (7 chars)
- `CMAKE_PROJECT_GIT_COMMIT_TAG`: Git tag if on tagged commit
- `PROJECT_BUILD_NAME`: "Combo Build"
- `PROJECT_TEAM`: "github.com/harbourmasters"

## Directory Layout

### Combo Directory Structure
- **CMake/**: CMake helper scripts and configuration files
- **libultraship/**: Shared game engine and framework
- **ZAPDTR/ZAPD/**: Asset extraction tool
- **OTRExporter/**: Asset packaging utilities
- **soh/**: Ship of Harkinian (OOT) game logic and assets
- **mm/**: 2 Ship 2 Harkinian (MM) game logic and assets

## Troubleshooting

### CMake Configuration Fails

1. Ensure you're in the `Combo` directory
2. Verify CMake version: `cmake --version`
3. Check that all subdirectories exist:
   - `libultraship/`
   - `ZAPDTR/ZAPD/`
   - `OTRExporter/`
   - `soh/`
   - `mm/`
   - `CMake/`

### Build Errors

1. Clean the build directory: `rm -rf build/*`
2. Reconfigure: `cmake ..`
3. Check compiler compatibility (C++20/C23 required)

### Missing Dependencies (Windows)

vcpkg should automatically install dependencies. If it fails:
1. Delete `build/_vcpkg` directory
2. Reconfigure to trigger fresh vcpkg bootstrap

## Contributing

This is a combined build system for two separate projects:
- Ship of Harkinian: https://github.com/HarbourMasters/Shipwright
- 2 Ship 2 Harkinian: https://github.com/HarbourMasters/2ship2harkinian

## License

Each game project maintains its own license. See the respective project directories for details.