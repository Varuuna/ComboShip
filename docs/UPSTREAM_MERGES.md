# Updating the vendored upstreams (libultraship / soh / mm)

`libultraship/`, `soh/`, and `mm/` are **vendored copies** (squash-imported as plain files), not
submodules or subtrees. They come from three different repositories and were then modified for
ComboShip. This document explains how to pull updates from upstream and — crucially — records
**every change we have to make to upstream code after a merge, and why**, so the next person
doesn't undo a load-bearing adaptation thinking it's a stray edit.

> Guiding rule: keep ComboShip's divergence from upstream as small and as *documented* as possible
> (see the HM64 principle). Every deviation below exists for a concrete reason stated inline in the
> code with a `ComboShip:` comment **and** logged in this file.

## Upstreams

| Folder | Upstream repo | Branch we track | Maps to |
|--------|---------------|-----------------|---------|
| `libultraship` | `github.com/Kenix3/libultraship` | `main` | repo root |
| `soh` | `github.com/HarbourMasters/Shipwright` | `develop` | `soh/` subdir |
| `mm` | `github.com/HarbourMasters/2ship2harkinian` | `develop` | `mm/` subdir |

These three are **coupled**: `soh@develop` and `mm@develop` track libultraship as a submodule and
already expect a recent libultraship (and its current header layout). Updating libultraship alone
breaks the soh/mm builds (they `#include` libultraship by path). **Update all three together** to
mutually-compatible upstream states.

## The update mechanism (vendor-branch + grafted ancestry)

Native `git subtree pull` cannot work — the squash import discarded the shared history, so there is
no common ancestor for a 3-way merge. We manufacture one:

1. Add upstreams as remotes (once): `up-lus`, `up-soh`, `up-mm`. Fetch `--filter=blob:none`
   (lazy blobs). Set `git config gc.auto 0` (auto-gc repack collides with promisor fetches →
   "Permission denied writing pack").
2. **Detect the fork point** — the upstream commit our copy was branched from — by matching our
   committed blob hashes against upstream trees (the commit with the highest match count).
3. Build a **vendor commit** = pristine upstream@fork placed at the folder's prefix (pure plumbing,
   no blobs): `GIT_INDEX_FILE=$(mktemp -u) git read-tree --prefix=<dir>/ <forktree>` →
   `git write-tree` → `git commit-tree` (no parent).
4. **Graft** it as an ancestor: `git commit-tree HEAD^{tree} -p HEAD -p <vendor_fork>` then
   `git merge --ff-only` (same tree → no file changes, just declares the relationship).
5. Build **vendor@tip** (parent = vendor_fork) → `git branch vendor-<name>`.
6. Pre-warm blobs: `git diff --stat <fork> <tip>` (one batched fetch).
7. `git merge vendor-<name>` → a real 3-way merge (base = fork). Only our locally-modified files
   can conflict.

**Future updates are easy:** rebuild a new vendor@tip (parent = previous vendor tip) and
`git merge` it. The recorded merge base makes each subsequent pull clean.

All work happens on a throwaway branch (e.g. `testing-merging`).

---

# Post-merge change log

For each merge, list every file we had to touch *after* the mechanical 3-way merge, and the reason.
Upstream often **reorganizes directories**, which turns our small customizations into conflicts
(our edit vs. "file moved"); re-home the customization to the new path and delete the old file.

## libultraship — `main` (fork `09dfab5fb` → tip `6fdfab32f`, 2026-06)

Upstream moved its public API into `include/` and split the renderer GUI backend into a
`Fast3dGui : Ship::Gui` subclass. Our ~20 local customizations had to be re-homed and, in one case,
re-architected to fit the new class split.

**Re-homed customizations (file moved upstream; same change re-applied at the new path):**

| Customization | New location | Why we have it |
|---|---|---|
| `Context::SetInstance()` + `SetResourceManager()` decls/impl | `include/ship/Context.h`, `src/ship/Context.cpp` | Combo runs per-game ResourceManagers and swaps the active one on an OOT↔MM transition **without destroying the previous one**, so each game's archives/resource cache stay resident. `SetInstance` lets a game make its Context current without `CreateInstance`. |
| `WindowClose()` bridge fn | `include/libultraship/bridge/windowbridge.h` (impl already auto-merged into `src/libultraship/bridge/windowbridge.cpp`) | Lets a game tear down the window on a game switch. |
| Removed `#undef _DLL` from `Folder/O2r/Otr Archive.h` (+ `FolderArchive.cpp` include) | `include/ship/resource/archive/*.h`, `src/ship/resource/archive/FolderArchive.cpp` | The `#undef _DLL` interferes with the shared-DLL CRT setup; removed so the dynamic CRT is honored. |
| `Gui::GetImGuiContext()` | `include/ship/window/gui/Gui.h`, `src/ship/window/gui/Gui.cpp` | ImGui's `GImGui` current-context global is a per-module static. Game DLLs must `ImGui::SetCurrentContext()` with libultraship's context; this getter returns it from inside the DLL. |
| `Gui::RegisterResourceFactories()` (+ `Gui::Init` rerouted through it) | `include/ship/window/gui/Gui.h`, `src/ship/window/gui/Gui.cpp` | Registers the Font + GuiTexture factories on the **currently active** ResourceManager. A game that creates its own RM (combo per-game RMs) must call this on it; upstream only registers them on whichever RM was active when the shared Gui was first created. |

**Re-architected to fit the new `Fast3dGui` split:**

- `Gui::RebuildFontTexture()` — made **virtual** in `Ship::Gui` (empty default) and the real
  implementation moved to **`Fast3dGui::RebuildFontTexture()`** (`include/fast/Fast3dGui.h`,
  `src/fast/Fast3dGui.cpp`). Upstream moved the `ImGui_Impl*` renderer backend out of `Gui.cpp`
  (now empty `ImGuiBackendInit/NewFrame`) into `Fast3dGui`, so the backend-specific font-texture
  invalidation (`ImGui_ImplOpenGL3_DestroyFontsTexture` / `ImGui_ImplDX11_InvalidateDeviceObjects`)
  must live there. **Why we need it at all:** renderer backends build the font texture once,
  lazily; a game that adds fonts to the shared atlas *after* the texture was built (e.g. MM
  re-adding its fonts after OOT booted in a combo build) leaves the atlas `TexReady=false` and
  `ImGui::NewFrame()` asserts "Font Atlas not built!". This forces a rebuild.

**Build-system conflicts (kept our settings, merged in upstream's orthogonal improvements):**

- `CMakeLists.txt` — kept `CMAKE_MSVC_RUNTIME_LIBRARY = …DLL` (**dynamic CRT**). Upstream switched
  to the static CRT. The combo build links libultraship as a **shared DLL**, so all modules must
  share one CRT/heap; a static CRT trips the debug-heap assert `__acrt_first_block == header` when
  C++ objects allocated in one module are freed in another. Also kept upstream's new `/Z7`
  (`CMP0141` embedded debug info) default — orthogonal, helps sccache.
- `cmake/dependencies/common.cmake` — kept our prism **dynamic-CRT rewrite** (`/MTd`→`/MDd` on the
  prism target; prism hard-codes the static CRT via `target_compile_options`, which otherwise wins
  over `MSVC_RUNTIME_LIBRARY` and causes `LNK2038/LNK2005` when linking the shared DLL). Also kept
  upstream's new prism sccache/PDB launcher fix (touches different properties) and all new upstream
  dependencies added in this region (`monocypher`, `libtcc`/`tinycc`).

**Took upstream's version (our old customization superseded):**

- `src/fast/backends/gfx_dxgi.cpp` window-event dispatch — upstream's new code already does the
  null-guarding our customization added, plus the `dynamic_pointer_cast<Fast3dGui>` the new class
  split requires. Our older simple null-guard was dropped in favor of upstream's.

**Dropped (intentionally not carried forward):**

- `.github/workflows/build-validation.yml` — we don't ship upstream CI workflows; kept deleted.

**Build-time regression fixed after the merge (clean auto-merge, but broke the link):**

- `src/CMakeLists.txt` — the auto-merge re-introduced upstream's
  `set_target_properties(libultraship PROPERTIES ENABLE_EXPORTS TRUE / WINDOWS_EXPORT_ALL_SYMBOLS TRUE)`
  lines (our pre-merge copy had neither). That property is **incompatible with our custom `/DEF`
  export pipeline** just below it: `WINDOWS_EXPORT_ALL_SYMBOLS` appends its own `__create_def` as the
  last pre-link step, *after* our `=`-stripping `filter_def.cmake`, so the bogus `=` absolute COFF
  symbol survives and the linker fails with `LNK2001: unresolved external symbol =` /
  `LNK1120: 1 unresolved externals`. Fix: explicitly set
  `WINDOWS_EXPORT_ALL_SYMBOLS FALSE` (not just delete the line — the newly merged-in `libtcc`
  dependency sets `CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON` globally, so the property must be forced off
  on the target). **Watch for this on every libultraship merge.**

## soh — `develop`

Merged `soh/` only (mm / OTRExporter / ZAPDTR / libultraship are decoupled vendored copies and were
**not** updated from origin in this pass). Fork point `7c8fc85c5`; merged up to `develop` tip
`abcb3ad94`. Vendor-branch + grafted-ancestry mechanism as above (branch `vendor-soh`).

**Object-availability note (different from the libultraship merge):** the HarbourMasters/Shipwright
server **refuses by-SHA lazy blob fetches** (`upload-pack: not our ref …`), so the `blob:none`
promisor clone could not backfill blobs the way Kenix3's server did. Hydrate the soh blobs with a
**forced, filter-disabled shallow fetch** instead:
`git -c remote.up-soh.promisor=false -c remote.up-soh.partialclonefilter= fetch --refetch --depth=1 up-soh develop`.
A plain/incremental fetch is a no-op (git treats the missing blobs as legitimately-absent promisor
objects). Also: use git ≥ 2.40 so `GIT_NO_LAZY_FETCH=1` works and `read-tree` doesn't bulk-prefetch.

Only two files conflicted; the rest auto-merged (mostly upstream asset additions).

**Deviations re-applied / resolved (each also has a `ComboShip:`-style comment in code):**

- `soh/CMakeLists.txt` (MSVC runtime library) — kept **ours**: `MultiThreadedDebugDLL` /
  `MultiThreadedDLL` (dynamic CRT). Upstream uses the static `MultiThreaded*` variants. The dynamic
  CRT is **required** so `soh.dll`, `2ship.dll`, and the shared `libultraship.dll` share one CRT
  heap (see the CRT-uniformity work). Do not revert.

- `soh/soh/OTRGlobals.cpp` — upstream heavily refactored this file (moved `InitResourceManager` /
  `InitWindow` into the `OTRGlobals` **constructor**, changed `InitOTR()` → `InitOTR(int argc,
  char* argv[])`, renamed `SoH_ProcessDroppedFiles(std::string)` → `SoH_HandleConfigDrop(char*)`).
  Resolved by taking upstream's structure and **re-applying** our COMBO_BUILD changes onto it:
  - `sOOTResourceManager` static + its capture — moved into the constructor (right after the new
    `InitResourceManager`), since that's where the RM is now created. Same RM object as before.
  - `ImGui::SetCurrentContext(...)` after `InitWindow` (now in the constructor, before the first
    ImGui use `CreateFontWithSize`).
  - The COMBO scene-switch hooks (`OnSceneInit`→`SCENE_MIDOS_HOUSE` / `OnGameFrameUpdate`) re-added
    in `InitOTR` (the old `OnFileDropped` anchor was removed upstream; anchored before
    `RegisterImGuiItemIcons()` instead).
  - `SOH_PrepareForTransition` re-added after `DeinitOTR`; the ComboShip exports block
    (`SOH_Init`, callbacks, `SOH_ResumeGame`/`SOH_ReinitForResume`, `SOH_Extract`, …) re-added
    before `SoH_HandleConfigDrop`.
  - `SOH_Init` now calls `InitOTR(0, nullptr)` (upstream's new signature; combo drives extraction
    separately via `SOH_Extract`, and `RunExtract` no-ops on `argc<=1`).
  - **Dropped** the old `RegisterOOTResourceFactories` factoring (kept upstream's inline factory
    registration). It was only ever called from `Initialize`; per-game ResourceManagers made the
    "re-register after archive swap" rationale obsolete (`SOH_ReinitForResume` only calls
    `SetResourceManager`). The factory **set** is byte-for-byte identical to upstream's, so nothing
    is lost.

### Build-fix follow-up (to make `soh.dll` actually build against the merged libultraship)

soh@develop is coupled to libultraship **and** to OTRExporter/ZAPDTR (both are git submodules in
Shipwright, pinned `OTRExporter@32e088e28` / `ZAPDTR@ee3397a365`, fetched here as remotes
`up-otrx` / `up-zapd`). The merge surfaced a chain of issues; `soh.dll` builds clean once all are
applied:

1. **OTRExporter include relocations** (hand-patched to the merged LUS header layout):
   `VersionInfo.h` → `<ship/resource/Resource.h>` + `<fast/resource/ResourceType.h>`;
   `<utils/StrHash64.h>` → `<ship/utils/StrHash64.h>` in `DisplayListExporter.cpp`,
   `ExporterArchive{,O2R,OTR}.cpp`. **Kept** combo deviations: `Exporter.h` includes MM's
   `2shResourceType.h` unconditionally, and the build defines **both** `GAME_MM` and `GAME_OOT`
   (ComboShip builds ONE exporter for both games — do NOT adopt upstream's mutually-exclusive
   `#ifdef GAME_MM / #elif GAME_OOT` gating).
2. **`/Zc:preprocessor`** added to the combo **root** `CMakeLists.txt`. Upstream soh's randomizer
   X-macro enum headers (`randomizerEnums.h`) use C++20 `__VA_OPT__`, which MSVC only supports under
   the conforming preprocessor. Upstream sets this in their root; we have our own root.
3. **`SPDLOG_LEVEL_*` defined at combo root scope** (before the subdir `add_subdirectory`s).
   `libultraship/src/CMakeLists.txt` sets them only in its own subdir scope, so sibling subdirs
   (soh, mm) didn't inherit them → `LOG_LEVEL_GAME_PRINTS=${SPDLOG_LEVEL_OFF}` expanded to empty →
   `#if` C1017 in `soh/include/functions.h`.
4. **ZAPDTR `zapd_report`**: adopted the pin's `ZAPD/Main.cpp` + `ExecutableMain.cpp` (upstream
   renamed `zapd_main` → `zapd_report(argc, argv, &extractCount, &totalExtract)` and kept a
   `zapd_main` wrapper). soh's `Extract.cpp` calls `zapd_report`. Left `ZRom.cpp`/`CMakeLists.txt`
   untouched — they carry combo deviations (runtime MM-ROM CRC detection; `GAME_MM`+`GAME_OOT`).
5. **`g_exec_stack` cross-DLL data import**: `libultraship/include/fast/interpreter.h` now guards the
   declaration `extern __declspec(dllimport) GfxExecStack g_exec_stack;` for consumers
   (`!defined(libultraship_EXPORTS)`). soh's `CrashHandlerExt.cpp` references this libultraship data
   symbol; data symbols need explicit consumer-side dllimport (functions auto-thunk, data does not —
   the `__osMaxControllers` pattern). libultraship still exports it via the all-symbols `/DEF`.
6. **`SOH_Init` / `SOH_Extract`** in OTRGlobals.cpp adjusted to upstream's new signatures:
   `InitOTR()` → `InitOTR(0, nullptr)`; `Extractor::CallZapd` is now 4-arg (two atomic counters).

**Not yet done:** rebuild `2ship` + `ComboShip` against the merged deps (2ship shares OTRExporter;
mm itself is not yet upstream-merged), then runtime-test the OOT↔MM loop.

## mm — `develop`

Merged `mm/` only. Fork `558f59b06` → develop tip `04a1a43197`; vendor-branch + grafted ancestry
(branch `vendor-mm`), committed `6e336608b`. Blob hydration needed a **depth-580** forced
filter-disabled refetch (`git -c remote.up-mm.promisor=false -c remote.up-mm.partialclonefilter=
fetch --refetch --depth=580 up-mm develop`) because the fork is 561 commits behind the tip and the
HarbourMasters server refuses by-SHA fetches. Merge run with **`-c merge.renames=false`** (rename
detection mispaired deleted mm assets against soh assets → spurious soh conflicts).

**Merge conflict resolutions (354):**

- **350 `mm/assets/**.h` modify/delete → kept deleted from tracking** (`git rm`). These generated
  asset headers were never imported into our combo tree. BUT mm CODE `#include`s ~48 of them
  (object_*.h, gameplay_keep.h, icon_item_static_yar.h), so they must exist **on disk**. They're
  gitignored generated artifacts; restore the on-disk copies from upstream and leave them untracked:
  `git checkout vendor-mm -- mm/assets && git restore --staged mm/assets`. **On future mm merges,
  resolve these modify/deletes with `git rm --cached` (NOT `git rm`) to keep the on-disk copies.**
- `mm/src/code/title_setup.c`: kept both (COMBO includes + upstream `z64save.h`).
- `mm/2s2h/BenPort.h`: kept both (`MM_*` transition exports + upstream `CrashHandler_PrintExt`).
- `mm/2s2h/Extractor/Extract.cpp`: kept ours (combo console-window + try/catch hardening). Upstream
  renamed `zapd_main`→`zapd_report`; our retained call uses `zapd_report(argc, argv, nullptr, nullptr)`.
- `mm/2s2h/BenPort.cpp` (transition core): reset to upstream, re-applied the COMBO_BUILD delta onto
  upstream's refactored `ctor`/`RunExtract`/`Initialize` split (statics + `MM_NotifyComboTransition`/
  callbacks, the ctor OOT-context reuse path with per-game `sMMResourceManager`, `ImGui::SetCurrentContext`,
  `RebuildFontTexture`, `OTRAudio_Exit` `#ifndef COMBO_BUILD` guards, InitOTR reverse hooks, MM exports
  block). Dropped the obsolete `RegisterMMResourceFactories` factoring (Initialize owns the inline
  registration). **RUNTIME-VERIFY the OOT↔MM transition** — the reuse path was re-derived against
  upstream's new init structure (font atlas, audio re-init, Initialize re-run on the shared context).

**Build-fix chain (libultraship API drift — mm@develop pins LUS ~67 commits behind our main tip, so
mm used pre-migration APIs soh@develop had already moved past):**

1. **Texture methods** moved `Ship::Gui` → `Fast::Fast3dGui`: wrapped 90 call sites in
   `std::dynamic_pointer_cast<Fast::Fast3dGui>(…GetGui())->…` (`GetTextureByName`/`HasTextureByName`/
   `LoadTextureFromRawImage`/`LoadGuiTexture`/`GetTextureSize`) + `#include <fast/Fast3dGui.h>` across
   13 files (BenGui, Trackers, Rando, ShipUtils).
2. **`Ship::WindowBackend`/`FAST3D_*`** → `Fast::…` + `#include <fast/Fast3dWindow.h>` (4 files).
   `GetWindowBackend()`/`GetAvailableWindowBackends()` now return `int`/`vector<int>`: cast
   `(Fast::WindowBackend)`, and `availableWindowBackends` is `shared_ptr<vector<int32_t>>`.
3. **`Context::InitGfxDebugger` dropped** from our libultraship: mm now uses a local free
   `InitGfxDebugger()` (mirrors soh) in BenPort.cpp + `#include <fast/debug/GfxDebugger.h>`.
4. **`stbi_load_from_memory`/`STBI_rgb_alpha`**: `#include <stb_image.h>` in BenPort.cpp (impl linked
   from libultraship; stb dir already on the 2ship include path).
5. **`InitOTR()` → `InitOTR(0, NULL)`** in `mm/src/code/main.c` `MM_RunMain` (upstream added argc/argv).

`2ship.dll` builds clean (Debug). NEXT: rebuild ComboShip; runtime-test the loop.

## Runtime fixes (first launch — boots to OOT file-select)

- **Console redirect (`libultraship/src/ship/Context.cpp`, `InitLogging`):** the merged soh `Initialize`
  now calls `InitLogging`, whose Debug+Win32 path does `FreeConsole()`+`AllocConsole()`+stdout-redirect
  — each game DLL stole the console into its own window instead of using ComboShip's. Guarded that block
  with `#if defined(_DEBUG) && defined(_WIN32) && !defined(COMBO_BUILD)` (the stdout_color sink stays, so
  soh/mm log into ComboShip's inherited console). `COMBO_BUILD` is defined for libultraship via the root
  `add_compile_definitions(COMBO_BUILD)`.

- **Alternate Assets default OFF:** upstream soh@develop (and 2ship) flipped the `AltAssets` default
  from `0` to `1`. With it ON and no HD/alt asset pack present, the ResourceManager probes an
  `alt/<path>` for every resource every frame → log spam (`Failed to load resource file at path
  alt/...`) and per-frame probe overhead. Combo ships no HD pack, so restored the default to `0`:
  `soh/soh/OTRGlobals.cpp` (`CVAR_SETTING("AltAssets")`, 3 sites) and `mm/2s2h/BenPort.cpp`
  (`"gEnhancements.Mods.AlternateAssets"`, load site). Users can still toggle it on.

**Known stale-user-data crash (NOT a code bug):** old randomizer spoilers in `x64/Debug/Randomizer/*.json`
(pre-merge seeds) reference renamed option variants (e.g. `"Bombchu Bag": "Off"` → now `"None"`) →
`Settings::ParseJson` → `assert(false)` at file-select. Clear stale rando seeds / use a non-rando save.

## Cross-World Randomizer (ComboShip feature) — Increment 1 (2026-06-04)

A net-new ComboShip feature (cross-game item delivery), not an upstream adaptation — but it adds
`#ifdef COMBO_BUILD`-guarded blocks to vendored **soh** and **mm** port files. Every block is guarded
and carries a `// ComboShip:` comment; **preserve these on future upstream merges** (they will not
conflict unless upstream rewrites the exact functions). Spec/plan:
`docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-scope-a.md` /
`docs/superpowers/plans/2026-06-04-crossworld-randomizer-increment-1-mailbox.md`.

The delivery channel is a header-only mailbox `combo/rando/CrossMailbox.h` (`namespace ComboRando`,
not an upstream file) backed by `saves/combo/slot{N}.mailbox.json`, keyed by the canonical 0-based
slot N. **Both engines hold N in `gSaveContext.fileNum` at runtime** — OOT directly, and MM because
`SaveManager_LoadSaveFile` stores `mmFileNum - 1` (the `+1` MM-file offset is on-disk only). So all
four mailbox sites use `gSaveContext.fileNum` as-is; do NOT subtract 1 on the MM side (that was an
early bug — it made OOT(N) and MM(N-1) miss each other and killed slot 0 via a `slot<0` guard).
Increment 1 proves the channel with debug send-triggers + a
placeholder blue-rupee grant on receive; real seed generation, pickup-interception, foreign-item
markers and the gift presentation are later increments.

**New, non-upstream files (no merge risk):**
- `combo/rando/CrossMailbox.h` — the shared mailbox module (compiled into soh.dll, 2ship.dll, ComboShip.exe).

**Build glue (preserve on merges):**
- `combo/CMakeLists.txt` — `find_package(nlohmann_json)` + link to `ComboShip`; `target_include_directories(ComboShip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})`.
- `soh/CMakeLists.txt` and `mm/CMakeLists.txt` — added `${CMAKE_CURRENT_SOURCE_DIR}/../combo` to each game target's include dirs so `"rando/CrossMailbox.h"` resolves.

**soh (`soh/soh/...`, all COMBO_BUILD-guarded):**
- `combo/ComboShip.cpp` — startup log of any leftover mailbox (slot 0) — diagnostic only.
- `Enhancements/randomizer/hook_handlers.cpp` — `RandomizerOnPlayerUpdateForCrossMailboxHandler` (drains OOT-bound mailbox entries on `OnPlayerUpdate`, placeholder blue-rupee grant; `0xFF` no-save guard) + its hook register/unregister/reset lifecycle (mirrors `onPlayerUpdateForItemQueueHook`). **Registered BEFORE the `if (!IS_RANDO) return;` gate** so it runs in any combo save, not just rando ones (the channel is a combo-level feature; no-op on empty mailbox). Guarded `#include "rando/CrossMailbox.h"`.
- `Enhancements/debugconsole.cpp` — `cross_send <itemName>` console command (enqueues an MM-bound entry for the current slot).

**mm (`mm/2s2h/...`, all COMBO_BUILD-guarded):**
- `Rando/MiscBehavior/CheckQueue.cpp` — `Rando_CrossMailboxDrain` (drains MM-bound entries each player-update, placeholder grant; guards the `0xFF` no-save sentinel and uses `gSaveContext.fileNum` directly as the slot) and `InitCrossMailboxDrain` (registers it via `COND_ID_HOOK(OnActorUpdate, ACTOR_PLAYER, true, …)` — **NOT rando-gated**: combo MM saves aren't `SAVETYPE_RANDO` until the Increment 2 generator, and the channel must deliver regardless; no-op on empty mailbox).
- `Rando/MiscBehavior/MiscBehavior.h` / `MiscBehavior.cpp` — declaration of `InitCrossMailboxDrain` + its call from `OnFileLoad` (alongside the CheckQueue hook).
- `DeveloperTools/SaveEditor.cpp` — debug button in `DrawRandoTab()` enqueuing an OOT-bound entry.

**Note:** the receive drains read the mailbox file every player-update frame — an accepted Increment-1
simplification (tiny file, correctness-first); a later increment can throttle or drain on
game-gain-control instead.

## Cross-World Randomizer — Increment 2, Task 3: OOT placement injection at save creation (2026-06-04)

Net-new ComboShip feature (OOT side of the combo rando injection pipeline). Only one vendored
game-source file is touched, and only via an additive `#ifdef COMBO_BUILD` guard.

**Game-source deviation (additive, guarded — preserve on future soh merges):**

- `soh/src/code/z_sram.c` (`Sram_InitSave`, ~line 266): new `#ifdef COMBO_BUILD` block fires
  `gComboGenerateCallback(fileNum)` immediately before `u8 currentQuest = ...` is read, then
  forces `questType[buttonIndex] = QUEST_RANDOMIZER`. **Why:** the combo generator must run
  (via `SOH_ApplyRandoPlacements`) and `SetSeedGenerated(true)` must be called **before**
  `Randomizer_IsSeedGenerated()` is tested two lines later; if the callback hasn't run yet,
  the `currentQuest == QUEST_RANDOMIZER && IsSeedGenerated()` branch is never taken and
  `Randomizer_InitSaveFile()` is never called. The force-to-RANDOMIZER is intentional: in a
  ComboShip session every save is a rando save (the combo generator owns placement).

**New non-upstream files (no merge risk):**
- `combo/rando/CrossWorldRando.h` — header-only combo spoiler generator (permutation phase 1).

**soh.dll exports added (`soh/soh/OTRGlobals.cpp`, all `extern "C" __declspec(dllexport)`):**
- `SOH_DumpRandoStaticData` — now runs the headless prep sequence
  (`GetLogic()->Reset()`, `FinalizeSettings({},{})`, `GenerateLocationPool()`) to dump only
  `ctx->allLocations` (the real shuffled-check set) instead of all RC_MAX entries. Required
  `#include "soh/Enhancements/randomizer/logic.h"` in `OTRGlobals.cpp` (Logic was forward-declared
  in SeedContext.h; `->Reset()` needs the full type).
- `SOH_ApplyRandoPlacements(const char* json)` — applies the combo generator's `{"check":"item",...}`
  map: `locationNameToEnum[name]` → rc, `itemNameToEnum[item]` → rg, `PlaceItemInLocation(rc,rg,false,false)`,
  then `SetSeedGenerated(true)`. Calls `ItemReset()` first so all locations start from RG_NONE.
- `SOH_SetOnComboGenerateCallback(void(*)(int))` — registers `gComboGenerateCallback`, the
  fn-pointer fired by the z_sram.c hook. Pattern mirrors `gComboSaveInitCallback`.

## Cross-World Randomizer — Increment 2, Task 4: MM rando save injection at save creation (2026-06-04)

MM side of the combo rando injection pipeline. No vendored MM game-source touched — the new export
lives in port code (`BenPort.cpp`), and placement is fed through MM's *existing*
`Rando::Spoiler::ApplyToSaveContext` path. The combo layer owns placement; MM's own generator
(`GeneratePools`/logic) is never run.

**2ship.dll export added (`mm/2s2h/BenPort.cpp`, `extern "C" __declspec(dllexport)`):**
- `MM_InitRandoSaveFile(int fileNum, const char* placementJson)` — creates a RANDO MM save for the
  given OOT slot from the combined spoiler's `"mm"` slice (`{ "<RC_name>": "<itemSpoilerName>", ... }`):
  1. `SaveManager_InitNewSaveForSlot(fileNum + 1)` — playable combo baseline (Human Link, South Clock
     Town, ocarina/songs), then restore `gSaveContext.fileNum = fileNum` (Sram_InitNewSave reset it) so
     `SaveManager_SaveCurrentForCombo` re-writes the right slot.
  2. `saveType = SAVETYPE_RANDO`, zero the rando struct + seed `foundDungeonKeys` (mirrors `OnFileCreate`).
  3. Build a minimal MM spoiler (`finalSeed:0`, empty `options`/`startingItems`, `checks` = the placement
     slice) and call `Rando::Spoiler::ApplyToSaveContext` — which writes `randoSaveChecks`. **Never** calls
     `GrantStartingItems`/`Item_Give` (headless; those need `gPlayState`). Marks the two always-eligible
     starting checks (Deku Mask / Song of Healing) like `OnFileCreate` does. Falls back to a vanilla save
     on any exception.
  4. `SaveManager_SaveCurrentForCombo()` persists the rando save to `saves/2ship/file{N+1}.json`.

**combo (`combo/ComboShip.cpp`):**
- `Combo_OnGenerate` now stashes the spoiler's `"mm"` slice into `g_PendingMMPlacements` (cleared first
  so a generator failure can't reuse a stale slice).
- `Combo_OnOOTSaveInit` calls `MM_InitRandoSaveFile(fileNum, g_PendingMMPlacements)` when a placement is
  stashed (the generate callback fires earlier in the same new-save flow), else falls back to the vanilla
  `MM_InitSaveFile`. Resolves the new `MM_InitRandoSaveFile` symbol from `2ship.dll`.

## Cross-World Randomizer — Increment 6: foreign markers + send interception + real grants (2026-06-04)

Wires cross-placed (foreign) items to the delivery channel: a check whose item belongs to the OTHER
game holds a per-game **sentinel** item; at pickup the game diverts the real item through the mailbox
instead of granting locally, and the receiving game grants the real item. Replaces the Increment 1
placeholder blue-rupee grants on both receive drains. All vendored-source edits are additive and
either `#ifdef COMBO_BUILD`-guarded or appended enum/table entries — **preserve on future merges.**

**New non-upstream file (no merge risk):**
- `combo/rando/CrossForeign.h` — header-only foreign-item marker map (`namespace ComboRando`), backed
  by `saves/combo/slot{N}.foreign.json`. Schema is per-game-keyed (`oot`/`mm`) check→{itemGame,
  itemName,displayName}. `itemName` is in the **destination** game's namespace (OOT English names,
  MM `RI_*` spoilerNames) since that game grants it. Defines the two sentinel name constants
  (`kForeignSentinelNameOOT = "Combo Foreign Item"`, `kForeignSentinelNameMM = "RI_COMBO_FOREIGN"`).

**Sentinel enum/table additions (appended — keep before the terminators so existing values/save data
are unchanged):**
- `soh/soh/Enhancements/randomizer/randomizerEnums/RandomizerGet.h` — `RG_COMBO_FOREIGN` before `RG_MAX`.
- `soh/soh/Enhancements/randomizer/item_list.cpp` — `itemTable[RG_COMBO_FOREIGN]` entry (harmless
  blue-rupee GIEntry; English name "Combo Foreign Item" → auto-registered in `itemNameToEnum`). Never
  granted — only resolved defensively by `GetFinalGIEntry`/`RetrieveItem` before the divert.
- `mm/2s2h/Rando/Types.h` — `RI_COMBO_FOREIGN` before `RI_MAX_TRAP`.
- `mm/2s2h/Rando/StaticData/Items.cpp` — `RI(RI_COMBO_FOREIGN, …)` entry (spoilerName "RI_COMBO_FOREIGN"
  via the `#id` macro; RITYPE_JUNK, blue-rupee model).

**combo (`combo/ComboShip.cpp`, `Combo_OnGenerate`):**
- After the fill, writes the foreign map (`WriteForeignFromAnnotations`) from the spoiler's `"foreign"`
  array, then builds the OOT/MM apply payloads with each foreign check's slot **overwritten by that
  game's sentinel name** (so the check's own game places the sentinel; the spoiler keeps the real
  foreign item names for readability).

**soh send interception + real grant (`hook_handlers.cpp`, COMBO_BUILD-guarded):**
- `RandomizerOnPlayerUpdateForRCQueueHandler` — new branch: when `loc->GetPlacedRandomizerGet() ==
  RG_COMBO_FOREIGN`, calls `OOT_SendForeignCheck` (enqueue mailbox to the item's home game, "Sent to
  Termina" toast, mark `RCSHOW_COLLECTED` + tracker recalc so it never re-queues) instead of queueing
  a local grant. Per-slot foreign map is cached (`OOT_LookupForeign`).
- `RandomizerOnPlayerUpdateForCrossMailboxHandler` — placeholder blue rupee replaced with the real
  grant: `itemNameToEnum[itemName]` → `RetrieveItem(rg).GetGIEntry_Copy()` →
  `GiveItemEntryWithoutActor`, plus a "Received from Termina" toast.
- Added `#include "rando/CrossForeign.h"`.

**mm send interception + real grant (`Rando/MiscBehavior/CheckQueue.cpp`, COMBO_BUILD-guarded):**
- `CheckQueue` giveItem lambda — new branch on the **raw** `randoSaveCheck.randoItemId ==
  RI_COMBO_FOREIGN` (before `ConvertItem`): marks obtained, calls `Rando_SendForeignCheck` (enqueue +
  "Sent to Hyrule" toast + `SaveManager_SaveCurrentForCombo`), and returns without granting.
- `Rando_CrossMailboxDrain` — placeholder blue rupee replaced with `GetItemIdFromName(itemName)` →
  `Rando::GiveItem(ri)`, plus a "Received from Hyrule" toast.
- Added `#include "rando/CrossForeign.h"`.

**Known limitation (functional polish, not blocking):** the foreign map's `displayName` currently
falls back to the raw spoiler name, so MM-bound items show as `RI_*` in the "Sent/Received" toasts
(OOT-bound items already show English names). Improving requires the per-game dumps to carry a
human-readable display name. Tracked for a later pass.

**Runtime-verified 2026-06-05:** opened a foreign OOT chest (Deku Tree Map Chest holding
`RI_MAGIC_JAR_BIG`) → "Sent to Termina" toast → portal to MM → mailbox entry `delivered: true`
(MM's drain granted it). Full Increment 6 loop confirmed end-to-end.

## Cross-World Randomizer — Eager MM boot (replaces headless warm-up) (2026-06-05)

The MM rando oracle needs MM's region graph at OOT-generate time, before MM would normally boot.
The Inc3 approach (`MM_InitRandoLogic` → `ShipInit::InitAll()` at startup) faked a headless MM and
crashed: `InitAll()` runs MM's entire UI/cosmetic/audio init surface, which dereferences a null
`GameInteractor::Instance` and then `ResourceManager`-loads MM assets through OOT's RM. Replaced by
**eagerly booting MM for real at startup** — one OOT→MM→OOT transition with MM's game loop skipped,
reusing the existing transition machinery. (Runtime-verified: boots to file-select, generation runs,
round-trip + Inc6 delivery all work.)

**Game-source deviation (additive, COMBO_BUILD-guarded — preserve on future mm merges):**
- `mm/src/code/main.c` (`MM_RunMain` tail): the final `Graph_ThreadEntry(0)` is gated on
  `gComboBootOnly` so `MM_BootForCombo` can run MM's full init without entering the blocking loop.
  `extern int gComboBootOnly;` declared near the `InitOTR` forward-decl.

**MM port code (`mm/2s2h/BenPort.cpp`):**
- `extern "C" int gComboBootOnly` definition; `MM_BootForCombo()` export (sets `sComboTransitionActive`
  + `gComboBootOnly`, runs `MM_RunMain`, clears the flag).
- **Deleted** `MM_InitRandoLogic()` (and the throwaway-singleton workaround from `f54b3cece`), plus its
  lazy-init caller at the top of `Combo_MM_Rando_Reset` (the region graph is now built by eager boot).

**OOT port code (`soh/soh/OTRGlobals.cpp`):**
- `SOH_ResumeForeground()` export = `SOH_ReinitForResume()` + `ImGui::SetCurrentContext`, no game loop
  (reactivates OOT as foreground after the eager MM boot).
- `EnsureOracleInit()` (the OOT oracle init) no longer calls `GenerateItemPool()`. That builds OOT's
  item pool purely for OOT's OWN fill (which the combo layer never runs — the combined cross-world fill
  owns placement) and asserts `itemPool.size() <= locCount`; under headless default settings the pools
  aren't balanced for a real fill, so it aborted on file creation. The oracle only needs reachability
  (`ReachabilitySearch` reads logic/region state + `allLocations` from `GenerateLocationPool`;
  `GenerateStartingInventory` doesn't touch `itemPool`).

**combo (`combo/ComboShip.cpp`):** the warm-up block is replaced by the eager-boot sequence
(`SOH_PrepareForTransition` → `MM_BootForCombo` → `MM_PrepareForTransition` → `SOH_ResumeForeground`);
the main loop starts with `mmBooted = true` so the first portal transition is a `MM_ResumeGame`. The
stale `MM_InitRandoLogic` resolution was removed.

**GUI lifecycle fix (`soh/soh/OTRGlobals.cpp`, found via runtime testing 2026-06-05):** the OOT
transition path tore down + rebuilt the shared Gui every OOT↔MM transition
(`SOH_PrepareForTransition` → `SohGui::Destroy()`, `SOH_ReinitForResume` →
`SohGui::SetupGuiElements()`). MM deliberately does NOT (see `MM_PrepareForTransition`): the shared
Gui persists, each game's windows are set up once. On the OOT rebuild the Gui still held the old
windows, so `AddGuiWindow` rejected the duplicates → the new windows never got `InitElement`'d → their
`calloc`-backed buffers stayed `0xCD` → freeing them on the next rebuild crashed
(`MessageViewer::~MessageViewer`, access violation on the 2nd MM→OOT return). Fixed by making OOT match
MM: removed the `Destroy()`/`SetupGuiElements()` calls from the transition path — OOT's windows persist
(fully initialized) and only the active RM/audio/menu are swapped. **Runtime-verified: multiple
OOT↔MM round-trips now work.**

**Open follow-ups (not blocking, tracked):**
- **Combined-fill performance:** the fill is O(checks²)-ish (~5000 checks, per-item double-oracle
  reachability + JSON round-trips) and takes minutes synchronously on the main thread (window appears
  frozen). Needs incremental reachability / binary interchange + a "Generating…" frame. See the
  combined-logic spec's perf note.
- **Foreign `displayName` polish** (see Inc6 section above).
- **Combo settings window (Increment 7):** without it, both games' rando options sit at defaults, so
  most non-chest checks aren't shuffled at runtime.

## UIWidgets empty-combobox UB + combo-rendered MM rando menu (2026-06-09)

**`mm/2s2h/BenGui/UIWidgets.hpp` (and `soh/soh/SohGui/UIWidgets.hpp`) — fix to a vendored upstream
bug:** every `UIWidgets::Combobox` template overload declared `const char* longest;` **uninitialized**,
then assigned it only inside the loop that scans the options for the widest entry. If the options
container is empty, `longest` stays garbage and the immediately-following
`CalcComboWidth(longest, ...)` → `ImGui::CalcTextSize` dereferences it → crash. Changed to
`const char* longest = "";` in all overloads, with a `// ComboShip:` comment explaining why. This is a
genuine upstream bug; OOT's `std::vector<std::string>` overload happened to already have the
initializer, but its three other overloads (map / `vector<const char*>` / fixed array) did not — those
were fixed too. WHY it surfaced: MM's rando "Seed" combobox reads `Rando::Spoiler::spoilerOptions`,
which is empty when the combo layer renders MM's always-available rando menu while MM is backgrounded.

**`mm/2s2h/BenPort.cpp` (`Combo_EnsureBenMenu()`):** belt-and-suspenders for the same symptom —
`spoilerOptions` is populated by `Rando::Init()` → `RefreshOptions()` at boot, but in the
combo/backgrounded render path that vector can be empty. `Combo_EnsureBenMenu()` (called by every MM
menu export — ExportMenu / InvokeCallback / EvalDisabled / DrawCustom) now calls
`Rando::Spoiler::RefreshOptions()` when `spoilerOptions.empty()`, so the menu has real options to draw.
`RefreshOptions` is idempotent (clears + repopulates) and only runs when empty. No new include needed
(`2s2h/Rando/Spoiler/Spoiler.h` was already included).

**Note:** other MM rando tabs (Logic / Items / etc.) may have their own backgrounded-live-state crashes;
out of scope here, handled as they surface.

## Menu code extraction to combo-owned headers (2026-06-10)

The ComboShip-written menu glue that lived inside the vendored trees was consolidated into
combo-owned, header-only units under `combo/menu/` (already on both games' include paths, so no
game CMake changes). The headers compile INTO each game DLL — only the SOURCE ownership moved —
so future upstream pulls conflict at most on small per-game glue blocks, never on the algorithms:

- **`combo/menu/ComboMenuExport.h`** — the two-pass count/reserve/fill CwMenu serializer that was
  copy-pasted in `soh/soh/SohGui/SohMenu.cpp` and `mm/2s2h/BenGui/BenMenu.cpp` (~260 lines each).
  Each game now keeps only its `WidgetTypeToCwKind` mapping + a Policy struct
  (`SohExportPolicy` / `BenExportPolicy`) + a one-line delegate. Pointer-stability invariant
  (reserve == fill) is now assert-checked and unit-tested
  (`libultraship/tests/combo_menu_export_tests.cpp`, mock-based, runs in `lus_tests`).
- **`combo/menu/ComboMenuDrawContent.h`** — the shared `Menu::DrawContent` ImGui body (~230 lines
  each) that mirrored the upstream `DrawElement` layout inside comboui's window. Each game keeps a
  Hooks shim (`SohDrawHooks` / `BenDrawHooks`) + a ~30-line wrapper. Upstream `DrawElement` is
  untouched. WHY a TU-glue header (`#error` guard, no own ImGui include): it must resolve each
  game's OWN UIWidgets/draw functions and per-module compile context.
- **`combo/menu/ComboMenuSharedContext.h`** — `ComboMenuContext::UseSharedImGuiContext()`, replacing
  2 static + 3 inline duplicated copies in `soh/soh/OTRGlobals.cpp` / `mm/2s2h/BenPort.cpp`.

Intentional micro-deviations from the pre-extraction bodies (do not "fix" back): empty tooltips are
no longer copied into owned-string storage (output still `""`); MM's redundant
`GetVectorIndexOf(sidebarOrder, …)` fallback clause was dropped (subsumed by the `std::find` over
`visibleSidebars`); OOT's widget label width now uses the clamped column count like MM (visible
only below 800px window width).

Net effect: vendored menu diff vs upstream shrank from ~1,630 to ~907 added lines across soh/ + mm/.

## Eager-MM-boot export bug: SOH_PrepareForTransition was never exported (2026-06-11)

**`soh/soh/OTRGlobals.cpp` (`SOH_PrepareForTransition`):** the founding-commit declaration put
`__declspec(dllexport)` on its own line BEFORE `extern "C"` — MSVC silently ignores the declspec
in that arrangement (warning C4091, invisible because soh compiles with `/w`). The function was
never in soh.dll's export table, so ComboShip.exe's eager-MM-boot gate (which requires all four
transition exports) failed on EVERY launch since 2026-06-05, printing one stderr line nobody saw.
Consequences while hidden: `ShipInit::InitAll` never ran at startup, `Rando::Logic::Regions`
stayed empty (0 of 315), the settings-scoped `MM_DumpRandoStaticData` emitted 0 checks, and the
cross-world fill never placed a single cross-game item (`mmCount=0`, `foreign=[]`) — masked by
graceful fallbacks everywhere else (boot-on-first-portal-transition, place-anywhere fill).
Fixed by using the canonical `extern "C" __declspec(dllexport)` form. Verified: eager boot
completes, 315 regions, MM dump 876 checks, spoiler mmCount=876 with populated foreign array.

**`mm/2s2h/BenPort.cpp` (`MM_DumpRandoStaticData`):** kept a permanent file-based canary
(`saves/combo/debug-mmdump.json`: pool sizes + per-reason emit-drop counters). File-based because
2ship.dll's spdlog default logger is never configured in combo (the shared Context owns logging in
soh's module) — SPDLOG_* calls from 2ship.dll go nowhere; remember this when adding MM-side logs.

## hook_handlers.h re-added (combo-owned) (2026-06-11)

**`soh/soh/Enhancements/randomizer/hook_handlers.h`** — upstream has NO such header at our vendor
tip (functions in hook_handlers.cpp are static/self-registered). ComboShip re-added it to expose
`OOT_LookupForeign` (the per-slot foreign-item map lookup) to `Messages/MerchantMessages.cpp` and
the check tracker for cross-game item display names. The file contains only `#ifdef COMBO_BUILD`
declarations and MUST be preserved on future upstream merges — an upstream pull that "removes the
deleted file" silently breaks the foreign shop/tracker name builds.

## Sturdy shutdown: clean deinit of both games (2026-06-11)

Closing the game on the window's X sometimes crashed or froze, and window resize/position changes
were never saved. Three intertwined causes, all from MM staying resident (eager MM boot) while the
shutdown path only deinitted SOH:

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`OTRAudio_Exit`):** the unconditional
`audio.thread.join()` terminates (`std::system_error`) when the thread was already joined — which
is exactly the case at combo shutdown for whichever game was BACKGROUND (its `*_PrepareForTransition`
already ran `OTRAudio_Exit`). Guarded with `audio.thread.joinable()` in both games. This was the
"crash on X" (deterministic when closing from MM; soh's `DeinitOTR` re-ran `OTRAudio_Exit` on the
already-joined OOT audio thread).

**`mm/2s2h/BenPort.cpp` (`MM_Deinit`, new export):** 2ship holds a `shared_ptr` to the SHARED
Context (forward-transition reuse path in the OTRGlobals ctor) and nothing ever released it, so
`~Ship::Context` — the ONLY place window geometry is saved (`SaveWindowToConfig` + `Config::Save`)
and spdlog is shut down — never ran. `MM_Deinit` wraps MM's `DeinitOTR`. ComboShip.exe calls it
BEFORE `SOH_Deinit` (BenGui::Destroy dereferences the live Context), so soh's `DeinitOTR` releases
the LAST reference and `~Context` runs on the main thread. This is what fixed window-resize
persistence.

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`DeinitOTR`), `libultraship`
(`CrossRMRegistry::Unregister`, new):** both resident ResourceManagers were pinned by the
`sOOT/sMMResourceManager` statics and the `CrossRMRegistry` map, deferring their destruction to
DLL-unload static destructors — where `~ResourceManager`'s thread pool joins its worker threads
UNDER THE LOADER LOCK and deadlocks. This was the "freeze on X". Each game's `DeinitOTR` now
unregisters its RM and nulls its static (`#ifdef COMBO_BUILD`), so RM destruction happens during
the explicit deinit calls on the main thread, before any `FreeLibrary`.

Shutdown order (ComboShip.cpp cleanup): `MM_Deinit()` (if MM ever booted) → `SOH_Deinit()` →
`FreeDll(comboui/2ship/soh)`. Everything thread-owning must be dead before the first FreeDll.

**`mm/2s2h/DeveloperTools/MessageViewer.h` (follow-up — freeze moved here after the fixes above):**
with MM_Deinit in place, BenGui::Destroy now actually destroys MM's window objects, and
`~MessageViewerWindow` froze the debug heap: it does `free(mTextIdBuf)` / `free(mCustomMessageBuf)`,
but those are only allocated in `InitElement()` — which NEVER ran in combo, because the shared Gui
rejected MM's window as a duplicate name (OOT registers its own "Message Viewer" first;
`Gui::AddGuiWindow` rejects + skips `Init()`). The members were raw uninitialized `char*` (0xCD
debug fill) and freeing them hung `_free_dbg`. Fixed by null-initializing both members
(`free(nullptr)` is a no-op). Audited all other MM GuiWindow destructors — MessageViewerWindow is
the only one freeing InitElement-allocated raw pointers. The rejected-duplicate window class
(known from the resume path) is worth remembering for any new MM window whose destructor frees
state allocated in `InitElement()`.

**`libultraship` `Gui::ImGuiWMShutdown`/`ImGuiBackendShutdown` (signature change — next crash in the
chain):** with teardown actually reaching `~Context`, `Fast3dGui::ImGui{WM,Backend}Shutdown` AV'd:
they called `Ship::Context::GetInstance()->GetWindow()`, but they run from `~Window` INSIDE
`~Context`, where the Context weak_ptr is already expired (GetInstance() == nullptr). This killed
the process BEFORE `~Context` reached `Config::Save()` — i.e. even with MM_Deinit in place, window
geometry still wasn't persisted. Fixed by threading the `Ship::Window*` (which `ShutDownImGui`
already received) through both virtuals instead of using GetInstance(). Affects Gui.h/Gui.cpp/
Fast3dGui.h/Fast3dGui.cpp; a Gui.h change recompiles nearly everything in soh+mm.

**`soh/soh/OTRGlobals.cpp` + `mm/2s2h/BenPort.cpp` (`DeinitOTR`, GImGui null-out — last crash in the
chain):** after a fully clean main(), the process still AV'd in CRT exit: soh.dll's atexit dtor for
`itemTrackerNotes` (static ImVector) called `ImGui::MemFree`, which dereferences the MODULE-LOCAL
`GImGui` — `ImGui::DestroyContext` (in ~Context) only nulls libultraship's copy; each game DLL's
GImGui still pointed at the freed context. Fixed: both games' DeinitOTR end with
`ImGui::SetCurrentContext(nullptr)` (COMBO_BUILD-guarded). Found via the new last-chance crash
filter in ComboShip.cpp (`ComboLateCrashFilter` -> combo_late_crash.txt) which covers the
post-Context window where lus's CrashHandler is gone/unusable; the filter + cerr shutdown markers
are kept permanently.

## Dual-game title screen logos (2026-06-12)

**`soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c`:** the OOT title screen now shows BOTH games'
logos — OOT's shrunk and shifted left, MM's title logo (mask + Zelda logo + subtitle, ROM-extracted
textures resolved against MM's ResourceManager via the G_COMBO_RM_PUSH/POP interpreter bracket) on
the right. Both games' flame-effect mask grids (OOT 3x3 `gTitleEffectMask*`, MM 2x3
`gTitleScreenDisplayEffectMask*` + per-cell `gTitleScreenFlame0-3`) draw behind their logos, scaled
and repositioned with them; MM's oscillating effect colors are replicated in the combo header
(OOT's EnMag doesn't carry MM state). The early-out therefore sits BEFORE the original effect-grid
combiner setup, so the skipped vendored region spans from `gDPSetCycleType` through the MQ
subtitle. All draw code
is combo-owned (`combo/title/ComboTitleLogos.h`, included after the LOGO_* macros so it can reuse
them and the EnMag_Draw* helpers). The vendored logo block in `EnMag_DrawInner` is left BYTE-INTACT
for upstream merges; a `#ifdef COMBO_BUILD`-guarded early-out (`if (ComboTitle_DrawLogos(...))
goto ...;` + a guarded label after the block) skips it at runtime, so non-combo builds compile the
file unchanged. On future merges, keep the include + the goto wrapper and re-check
that the duplicated OOT-logo draw sequence in ComboTitleLogos.h still matches upstream's (combiner
setup, texture names, MQ subtitle branch).

**`soh/CMakeLists.txt`:** added `${CMAKE_SOURCE_DIR}/combo/title` to the include dirs (next to the
existing `combo/menu` entry).

## Cross-world fill rework: advancement flags + oracle lookup maps (2026-06-12)

**Why:** the combined fill was rewritten (combo-owned `combo/rando/CrossWorldRando.h`) from
"logic-place every pool item with a full oracle round-trip each" to the SoH-shaped assumed fill:
logic-place only advancement items in batches, fast-fill junk, and fix the semantic bug where the
assumed set included already-placed items (placed items are now collected by a driver-level
cross-game sphere fixpoint — required because foreign placements can't be represented in either
game's native state). Measured: 82s → ~6.5s per generate (Debug, 1151 checks), and the silent
place-anywhere fallback is deleted (loud `result.error` after 10 failed passes instead).

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`, 2 sites):** each check entry now also emits
`"advancement": RetrieveItem(vanillaRG).IsAdvancement()` so the combo fill can partition the pool.
Inside the existing ComboShip export block.

**`mm/2s2h/BenPort.cpp` (`MM_DumpRandoStaticData`):** same flag, MM predicate mirrors
GlitchlessLogic's progression test: `randoItemType != RITYPE_JUNK && != RITYPE_HEALTH`.

**`mm/2s2h/BenPort.cpp` (oracle block):** added build-once function-local-static lookup maps
`Combo_MM_SpoilerNameToItemId` / `Combo_MM_CheckNameToCheckId`; `Combo_MM_Rando_SetOwnedItems` and
`Combo_MM_Rando_PlaceItem` use them instead of per-name linear scans over `StaticData::Items` /
`Checks` (SetOwnedItems runs once per reachability query — the fill's hot path). Plus
`#include <unordered_map>` at the top. All inside the existing ComboShip export blocks.

**On future merges:** if upstream reshapes `RandoItemType`, `StaticData::Items/Checks`, or SoH's
`Item::IsAdvancement`, re-check the dump flag predicates and the two lookup-map builders.
