# Upstream pull — 2026-07-25 (extractors only)

Extractor-only pass for issue #82. `libultraship`, `soh` and `mm` untouched.

- **ZAPDTR**: `ee3397a36` → `be1c68a79` (4 commits, tip of `up-zapd/develop`)
- **OTRExporter**: `32e088e28` → `c5465ba0b` (3 commits, tip of `up-otrx/develop`)

Note both extractors are now **ahead of what soh and mm themselves pin** — at their current pins
(soh `1ea720607`, mm `ed47d2ec9`) both games still point at the older `ee3397a36` / `32e088e28`.
Done by hand, not via `scripts/upstream-merge.ps1`; see
[`../deviations/extractors.md`](../deviations/extractors.md) for the procedure.

### What upstream changed

- **StringHelper removed** from ZAPD in favour of LUS's (`ZAPDTR#38`, `OTRExporter#46`).
  `ZAPD/Utils/StringHelper.{cpp,h}` deleted; 77 call sites swapped to
  `#include <ship/utils/StringHelper.h>`. That path matches our merged LUS layout
  (`libultraship/include/ship/utils/StringHelper.h`) exactly, so no shim was needed.
- **MM PAL support** (`ZAPDTR#32`/`#36`, `OTRExporter#43`/`#45`): PAL 1.0/1.1/GC checksums, ROM
  offsets and filelist routing in `ZRom.cpp` + `rom_info.py`; MM PAL text handling in `ZTextMM.cpp`.
- **Uninitialized-member fixes** (`ZAPDTR#37`): default initializers in `SkinLimbStructs.h` and
  `SetMesh.h`.

### Post-merge changes

- `ZAPD/ZRom.cpp` — **conflict, re-merged.** Upstream added its PAL yar-index ranges inside
  `#ifdef GAME_MM`; we keep our runtime CRC detection instead (one shared ZAPD serves both games), so
  the PAL branch was folded into `isMMRom` / new `isMMPalRom` flags rather than taking upstream's
  version. See [`../deviations/extractors.md`](../deviations/extractors.md).
- **Re-run CMake configure after this bump.** `ZAPD/CMakeLists.txt` picks up `Utils/*.cpp` via
  `file(GLOB ...)`, which is cached; without a reconfigure the build still tries to compile the
  deleted `StringHelper.cpp` (`error C1083`).

### Known limitation

MM PAL extraction is **inert**: `rom_info.py` and `ZRom.cpp` now route PAL ROMs to
`assets/extractor/filelists/mm_pal.txt` / `mm_gc_pal.txt`, but those filelists do not exist in our
vendored `mm/` — nor in 2Ship's `develop` tip. A PAL ROM fails on the missing filelist instead of the
previous unmatched-checksum path; neither works, so this is not a regression. PAL becomes usable only
once 2Ship ships the filelists.
