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

**Still broken after this merge (pre-existing, NOT caused by it):** `OTRExporter` (a separate,
non-updated vendored folder) still `#include`s libultraship's old header paths
(`src/resource/Resource.h`, `utils/StrHash64.h`) and old `Fast::` resource-type API, so a `soh`
build still fails in `OTRExporter.vcxproj` until OTRExporter is adapted to the merged libultraship.

## mm — `develop`

_(pending — to be filled in when mm is merged)_
