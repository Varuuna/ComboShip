# Extractors (ZAPDTR + OTRExporter)

ComboShip vendors **one shared copy** of each extractor (`ZAPDTR/`, `OTRExporter/`), wired at
`CMakeLists.txt:266-269` — not the per-game submodules soh and mm use. Both games extract through the
same ZAPD binary, so anything upstream gates on a `GAME_MM` / `GAME_OOT` compile define has to become a
runtime decision here.

Both are tracked in `upstream-pins.json` with `"manual": true`, meaning the automation deliberately
skips them: neither `scripts/upstream-merge.ps1` nor the `merge` job of
`.github/workflows/upstream-merge.yml` can merge them (both use a hardcoded
`libultraship`/`soh`/`mm` list, and the script needs a `vendor-<key>` branch). The workflow's detect
job filters on that flag — without it, a new extractor commit would show as moved forever and open a
bogus merge PR every week.

Bump them by hand: fetch `up-zapd` / `up-otrx`, apply upstream's own
`<oldPin>..<newPin>` diff over the vendored tree, then re-run CMake configure so the
`file(GLOB Source_Files__Utils ...)` in `ZAPD/CMakeLists.txt` picks up added/removed files.

## Deviations to preserve

### `ZAPDTR/ZAPD/ZRom.cpp` — runtime MM detection instead of `#ifdef GAME_MM`
Upstream decides which DMA entries are yar-compressed inside `#ifdef GAME_MM`, because at that point
the XMLs haven't been parsed so it can't tell MM from OOT. One shared ZAPD can't use a compile-time
define, so we derive `isMMRom` (and `isMMPalRom`) from the ROM CRC before the loop and gate on that.

**When upstream touches this block, re-merge rather than take theirs** — upstream will reintroduce the
`#ifdef`. The CRC lists must also gain any new MM checksum upstream adds, or that ROM silently skips
yar decompression.

### `ZAPDTR/ZAPD/ZTexture.cpp`
Additive texture handling (+39 lines) for combo asset export.

### `ZAPDTR/ZAPD/CMakeLists.txt`, `OTRExporter/CMakeLists.txt`, `OTRExporter/OTRExporter/CMakeLists.txt`
Uniform **dynamic CRT** (`MultiThreadedDebugDLL` / `MultiThreadedDLL`) and shared-`libultraship.dll`
wiring, plus VS-platform guards. Required so the extractors link against the same CRT as the
single shared LUS — see [`../UPSTREAM_MERGES.md`](../UPSTREAM_MERGES.md).

### `OTRExporter/OTRExporter/*` — per-game include and CRT fixes
`AnimationExporter.cpp` and friends drop upstream's `#ifdef GAME_MM` / `#elif GAME_OOT` include
switching in favour of the resource headers reachable from the shared build; `ExporterArchive.h` drops
an `#undef _DLL` that fought the dynamic CRT. `VersionInfo.cpp`/`Exporter.h`/`Main.cpp` carry small
combo build fixes.
