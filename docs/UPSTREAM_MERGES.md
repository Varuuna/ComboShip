# Updating the vendored upstreams (libultraship / soh / mm)

`libultraship/`, `soh/`, and `mm/` are **vendored copies** (squash-imported as plain files), not
submodules or subtrees. They come from three different repositories and were then modified for
ComboShip. This document explains how to pull updates from upstream and — crucially — records
**every change we have to make to upstream code after a merge, and why**, so the next person
doesn't undo a load-bearing adaptation thinking it's a stray edit.

> Guiding rule: keep ComboShip's divergence from upstream as small and as *documented* as possible
> (see the HM64 principle). Every deviation below exists for a concrete reason stated inline in the
> code with a `ComboShip:` comment **and** logged in this file.

After every merge that touches either randomizer, re-walk the fill-parity checklist in
[`COMBO_FILL_PARITY.md`](COMBO_FILL_PARITY.md) — it maps each native generation step (SoH `Fill()`,
2Ship `OnFileCreate`) to its combo-pipeline equivalent and tracks the known gaps.

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

## Running a merge pass (tooling)

The deterministic plumbing is automated. The semantic conflict resolution + build-fix chain stays
manual (it's judgement work). Three pieces:

- **`upstream-pins.json`** (repo root) — the source of truth: the last-merged upstream SHA per
  folder. Updating it is the **final step of every merge**. Both the local orchestrator and CI read
  it.
- **`scripts/upstream-merge.ps1`** — fetches the three upstreams, hydrates blobs (the forced
  filter-disabled refetch — HarbourMasters servers refuse by-SHA promisor fetches), rebuilds the
  `vendor-*` branches at the current tips (parent = previous vendor tip), and prints the
  **conflict-surface report** (which of *our* customized files upstream touched). With `-Merge` it
  also runs the 3-way merges (`-c merge.renames=false` for mm), leaving conflicts for a human.
- **CI** (`.github/workflows/upstream-merge.yml`, weekly + manual) — **auto-drafts the merge PR.**
  Every run writes a Step Summary (up-to-date vs updates-found, so the result is never ambiguous).
  When upstream has moved it reuses `upstream-merge.ps1` for the plumbing, runs the mechanical 3-way
  merge on a stable `bot/upstream-merge` branch (**committing conflict markers** — the PR is labelled
  `has-conflicts`), bumps `upstream-pins.json`, scaffolds `docs/merges/<date>.md`, and opens/updates a
  **draft PR to `develop`**. You finish it on that branch: resolve markers, work the build-fix chain,
  flesh out the merge log, then mark ready. The merge base is derived from `develop`'s own history
  (the 2nd parent of the most recent `merge(<key>):` commit), so it's correct without relying on
  pushed `vendor-*` branches. `build-artifacts.yml` / `clang-format.yml` are the PR gates.
  - **Secret:** set `UPSTREAM_PR_PAT` (a fine-grained PAT with **contents + pull-requests: write**) so
    the drafted PR triggers the build/format gates — a PR opened by the default `GITHUB_TOKEN` does
    **not** trigger other workflows. Without it the PR is still created; push any commit to the branch
    (or close/reopen) to kick the gates.
  - Running the local `scripts/upstream-merge.ps1` by hand still works exactly as before — use it when
    you'd rather drive the pass locally instead of finishing the bot's draft.

## Standing policy: libultraship branch (Kenix3 `port-maintenance`)

We vendor Kenix3 `libultraship` **`port-maintenance`** — *not* `main`. **Do not "correct" this back.**

`main` and `port-maintenance` are divergent lines. **Every Harbour Masters port tracks
`port-maintenance`**: Shipwright `develop`, 2ship `develop`, and Lighthouse all pin commits on it.
`main` carries engine refactors no port has adopted (Component system #1174, the Ship/Fast namespace
split #1177, list standardization #1178, the ResourceInitData/ResourceIdentifier rework #1187).

We tracked `main` until 2026-08-01 and cherry-picked port fixes across the gap one at a time — #1103
(`GetRawInstance`), #1126, #1141, #1157, #1121/#1164. That cost grew every pass while the branches
kept diverging, so the 2026-08-01 pass switched lines and retired all of those local copies. Adopting
`port-maintenance` also brought #1103 properly: **libultraship now owns the `Context` via
`unique_ptr`, `GetInstance()`/`SetInstance()` are gone, and callers use `GetRawInstance()`.**

soh/2ship are still vendored as the `soh/`/`mm/` **source folders only** — we never adopt their
submodule pins. If a port needs a LUS commit we don't have, cherry-pick it additively rather than
switching branches again.

## Standing policy: mm asset headers are TRACKED (match upstream)

Both upstream Shipwright (`soh/assets/**.h`) and 2ship (`mm/assets/**.h`) **commit** their
ZAPD-generated asset headers. Track them in our tree too. Earlier passes slimmed mm's out with
`git rm` (kept on disk only), which (a) diverged from upstream, (b) created a per-merge footgun, and
(c) broke ROM-less compiles. On an mm merge, resolve the `mm/assets/**.h` modify/delete conflicts by
**re-tracking from the vendor branch**: `git checkout vendor-mm -- mm/assets` (NOT `git rm`).

## Standing policy: the build version is DERIVED from the pins (auto save-gate)

SoH/2ship serialize some save fields as **flat arrays indexed by an enum** (e.g. `randoSettings` is a
`SaveArray("randoSettings", RSK_MAX, …)` indexed by `RandomizerSettingKey`). If an upstream merge
**inserts or reorders** entries in such an enum, every pre-merge save's array is the wrong length
and/or mis-indexed — reading a shifted key past the array end yields `null` and `.get<uint8_t>()`
**throws on startup** (the 2026-06-20 crash: #6723 added 34 `RSK_STARTING_*` keys mid-enum).

SoH's defense is the stale-rando-save guard (`SaveManager::StartupCheckAndInitMeta`), which renames a
rando save to `.bak` (with a popup) when its stored `buildVersion` differs from the running build.
Upstream trips it via per-release version bumps; ComboShip had **frozen** `CMAKE_PROJECT_VERSION` at
1.0.0, so it never fired.

**Fix (no per-merge action needed): the version is now derived in the root `CMakeLists.txt`** so it
changes automatically on every merge:

| Field | Source | Changes when… |
|-------|--------|---------------|
| `MAJOR` | `COMBO_EPOCH` (manual constant) | you bump it by hand |
| `MINOR` | first 4 hex of `upstreams.soh.mergedSha` | the **soh** pin moves |
| `PATCH` | first 4 hex of `upstreams.mm.mergedSha`  | the **mm** pin moves |

Because `upstream-pins.json` is bumped as the final step of every merge, the version (and thus the
build's `gBuildVersion{Major,Minor,Patch}`, which both `soh` and `2ship` `build.c.in` inherit from the
top-level project) changes on every merge, and the rando-save gate fires on its own. **No vendored
`SaveManager.cpp` edit, no per-merge version bump.** The opaque numeric triple is made legible by the
`PROJECT_BUILD_NAME` label (shown in-game) and the package filename
(`ComboShip-e<epoch>-soh<sha>-mm<sha>`).

**`COMBO_RELEASE_VERSION` (manual, e.g. `0.1.1`)** is separate from the pin-derived triple and is the
**sole authority for combosave compatibility** (gated at the launcher container level — see
`docs/deviations/boot-shutdown.md`). Only `major.minor` gates saves. Bump rule: `patch` = fixes that
cannot affect logic, in-game events, or save contents (saves survive); `minor`/`major` = anything
save-affecting (upstream pin updates, schema or rando-logic changes) and retires existing combosaves once.

**The two o2r version checks are NOT the same granularity** (this bit me once — get it right):

| Archive | Check | Granularity | Effect of a routine merge (minor/patch change) |
|---------|-------|-------------|-----------------------------------------------|
| **ROM** archive (`oot.o2r`/`oot-mq.o2r`, the player's extracted ROM) | `VerifyArchiveVersion` | **MAJOR only** | not invalidated — player keeps their ROM extract |
| **port** archive (`soh.o2r`/`2ship.o2r`, our bundled assets) | `sohArchiveVersionMatch` / `shipArchiveVersionMatch` | **full triple** | **invalidated** — game refuses to launch ("soh.o2r outdated") until regenerated |

So a routine merge does NOT force the player to re-extract their ROM, but it DOES require us to
**regenerate the port archives** (they're cheap build artifacts that ship with the release):

> **Per-merge step (after bumping `upstream-pins.json`): regenerate the port archives** so their
> embedded `--port-ver` matches the new derived version:
> `cmake --build <build> --target GenerateSohOtr Generate2ShipOtr --config <cfg>`
> (these `rm` + re-extract `soh.o2r`/`2ship.o2r` with `${CMAKE_PROJECT_VERSION}` and deploy to the
> runtime dir). A release build must run them; the local dev build does not do this automatically.
> Decision (2026-06-21): we keep the port check at upstream's full-triple rather than weakening it to
> MAJOR-only, and regenerate each merge.

**What still needs a manual `COMBO_EPOCH` (MAJOR) bump** (`CMakeLists.txt`): a ComboShip-**owned** save
or protocol break (save fields we add, Anchor wire format) — the pins won't move for those, so MAJOR
is the only knob that changes the version. (MAJOR also invalidates the ROM archives, forcing a full
re-extract — reserve it for when that's actually warranted.)

---
# Merge log

Each merge pass gets its **own dated file** under [`merges/`](merges/) — one per pull, listing every
file we had to touch after the mechanical 3-way merge and why. This keeps the per-merge required
changes easy to track (and to diff against the recurring-deviation list below). Newest first:

- [2026-08-01](merges/2026-08-01.md) — libultraship `a3f1e102e` → `bbb565bd9`, **switching line from
  Kenix3 `main` to `port-maintenance`**; soh `2c5762a0f` → `5a57a0cbc`; mm `e3310fe1b` → `ce4bf03ab`.
  All three mutually compatible for the first time. Adopted #1103: LUS owns the `Context` via
  `unique_ptr`, `GetInstance()`/`SetInstance()` gone, ~72 call sites moved to `GetRawInstance()`,
  soh's teardown now calls `DestroyInstance()` explicitly. Retired six local back-ports/cherry-picks.
  Fixed two Windows-only bugs in `scripts/upstream-merge.ps1`. No open regressions — the MM HUD
  A button turned out to be a pre-existing 2Ship sub-4:3 viewport bug, not merge damage.
- [2026-07-27](merges/2026-07-27.md) — soh `1ea720607` → `2c5762a0f` / mm `ed47d2ec9` → `e3310fe1b`
  (libultraship pin unchanged, but #1141 `G_SETTILESIZE_LERP` and #1157 `LoadGuiTexture(palettePath)`
  had to be hand-ported — both games now require them). Deleted our BetterSaveMenu hook block in
  favour of upstream #6984; reverted mm's `OTRGlobals::context` to `shared_ptr`; ported the MM
  excluded-checks round-trip to #1817's JSON-array form.
- [2026-07-25](merges/2026-07-25-extractors.md) — extractors only: ZAPDTR `ee3397a36` → `be1c68a79` /
  OTRExporter `32e088e28` → `c5465ba0b` (libultraship, soh, mm unchanged). ZAPD now uses LUS's
  StringHelper; re-merged our runtime MM-ROM detection over upstream's `#ifdef GAME_MM` PAL branch.
- [2026-07-13](merges/2026-07-13.md) — soh `8602c6d15` → `ff7eb482d` / mm `cfd1116a4` → `b3cc36628`
  (libultraship unchanged). Added the Saria's-Song-hint feature (drove the MM `sariaPriorityItems` save-init fix).
- [2026-07-06](merges/2026-07-06.md) — soh `aedddc21e` → `8602c6d15` / mm `c74ad0e38` → `cfd1116a4`
  (libultraship unchanged). No new deviations; conflicts were upstream evolution over existing combo edits.
- [2026-06-29](merges/2026-06-29.md) — soh `ec2a3d7aa` → `aedddc21e` / mm `3545e62e0` → `c74ad0e38`
  (libultraship unchanged). Moved the shared LUS OpenGL backend to the single prism shader (drove the MM shader-asset re-sync).
- [2026-06-22](merges/2026-06-22.md) — soh `74e1d4c20` → `ec2a3d7aa` (libultraship + mm unchanged).
  Custom Bottle Contents + Small Key tracking fixes.
- [2026-06-20](merges/2026-06-20.md) — soh `adf31d5eb` → `74e1d4c20` (libultraship + mm unchanged).
  Added 34 `RSK_STARTING_*` keys mid-enum (the save-array crash that motivated the derived build-version gate).
- [2026-06-15](merges/2026-06-15.md) — libultraship `a3f1e102e` / soh `adf31d5eb` / mm `3545e62e0`.
  Added the additive `Context::GetRawInstance()` shim; re-tracked `mm/assets`.
- [2026-06-03](merges/2026-06-03-initial-merge.md) — the initial three-way merge + first-launch
  runtime fixes.

When you finish a pass, add a new `merges/<YYYY-MM-DD>.md` and link it here.

# Preserved ComboShip deviations

Moved to [`deviations/`](deviations/) — one file per subsystem. Preserve every entry across
upstream merges (each also carries a `// ComboShip:` comment at the code site).
