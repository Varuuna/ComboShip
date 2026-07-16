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

## Standing policy: libultraship branch (Kenix3 `main`)

We **always** vendor Kenix3 `libultraship` `main`. soh/2ship are vendored as the `soh/`/`mm/`
**source folders only** — we never adopt their submodule pins. If soh or 2ship *source* hits a
build break around **window/Context init-deinit or other non-game infrastructure**, check whether
that upstream pins a different LUS branch (e.g. soh@develop moved to Kenix3 `port-maintenance` for
the "Untangle Context destructor" PR #1103) and **cherry-pick the specific commit(s) into our
vendored `libultraship/` — additively where possible — rather than switching the branch we track.**
Concrete instance handled in the 2026-06-15 pass: the `GetInstance()`→`GetRawInstance()` rename
(see that section below).

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

- [2026-07-06](merges/2026-07-06.md) — soh `aedddc21e` → `8602c6d15` / mm `c74ad0e38` → `cfd1116a4`
  (libultraship unchanged). No new deviations; conflicts were upstream evolution over existing combo edits.
- [2026-06-15](merges/2026-06-15.md) — libultraship `a3f1e102e` / soh `adf31d5eb` / mm `3545e62e0`.
  Added the additive `Context::GetRawInstance()` shim; re-tracked `mm/assets`.
- [2026-06-03](merges/2026-06-03-initial-merge.md) — the initial three-way merge + first-launch
  runtime fixes.

When you finish a pass, add a new `merges/<YYYY-MM-DD>.md` and link it here.

# Preserved ComboShip deviations (independent of any single merge)

These are net-new ComboShip features/fixes that add `#ifdef COMBO_BUILD`-guarded (or otherwise
load-bearing) changes to the vendored trees. They are **not** tied to one merge pass — preserve them
on every future merge (they conflict only if upstream rewrites the exact functions). Each also
carries a `// ComboShip:` comment at the code site.
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
- `MM_InitRandoSaveFile(int fileNum, const char* placementJson, const unsigned char* ootName8)` — creates a RANDO MM save for the
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
  `MM_InitSaveFile`. Resolves the new `MM_InitRandoSaveFile` symbol from `2ship.dll`. Both init exports
  also take the OOT-entered file name (`SOH_GetCurrentPlayerName`, same font codes in both games) so the
  MM save is created with the player's name instead of the LINK default.

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

**`mm/2s2h/BenPort.cpp` (`MM_DumpRandoStaticData`):** a debug-build-only file-based canary
(`saves/combo/debug-mmdump.json`: pool sizes + per-reason emit-drop counters), gated `#ifndef NDEBUG`
so it never ships in a Release build (the drop counters are still computed; only the file write is
gated). File-based because
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

## Foreign OOT items render real models in the MM world (2026-06-13)

**Why:** the mirror of the OOT-side foreign rendering (commit `164460dce`). An MM check holding the
foreign sentinel (`RI_COMBO_FOREIGN`, an OOT-bound item) drew a blue rupee because `Rando::DrawItem`
had no case for it. Now it renders the real OOT model via the same cross-RM mechanism OOT already
uses for MM items, just in the opposite direction (`"__OTR__@oot:"` paths resolved against OOT's
resident ResourceManager). All real logic is combo-owned; the game-source footprint is one guarded
function plus one guarded include + case.

**`soh/src/code/z_draw.c` (vendored, COMBO_BUILD-guarded — preserve on future soh merges):** added a
self-contained `GetItem_GetDrawTableEntry(drawId, outDlists, maxDlists, outXluStart, outScale)`
immediately after `GetItem_Draw`. The exact OOT analog of MM's same-named function
(`mm/src/code/z_draw.c`, added earlier for the reverse direction): it decodes one `sDrawItemTable`
row into submission-ordered OTR dlist paths + OPA/XLU split + optional uniform scale, for the
"self-contained" draw funcs only (`GetItem_DrawOpa0`/`Opa0Xlu1`/`Xlu01`/`EggOrMedallion`/`Compass`/
`MaskOrBombchu`/`MagicArrow`/`Opa10Xlu2`/`Opa1023`/`Opa10Xlu32`/`SmallRupee`(0.7 scale)/`BulletBag`/
`Wallet`). Funcs needing extra runtime state (segment-8 scrolls, billboard, grayscale, per-instance
prim/env globals, special matrices) return 0 → MM falls back to its sentinel. No original lines
moved/deleted. On future merges: if upstream changes the `sDrawItemTable` draw-func set or row
layout, re-check the func→order mapping here.

**Combo-owned (no further vendored churn):**
- `combo/menu/ComboItemDrawOOT.h` — soh.dll exports `OOT_GetItemDrawInfo` / `OOT_GetItemAnimDrawInfo`
  (C ABI in `ComboItemDrawABI.h`). Mirror of `ComboItemDrawMM.h`. Resolves the foreign map's English
  `itemName` → `itemNameToEnum` → `RetrieveItem(rg).GetGIEntry_Copy().gid` → `GetItem_GetDrawTableEntry`.
  The anim export always returns 0 (OOT has no skeletal-animated foreign class). Included once from
  `soh/soh/Enhancements/randomizer/item_list.cpp` under COMBO_BUILD (mirror of the `ComboItemDrawMM.h`
  include in `mm/2s2h/BenPort.cpp`).
- `combo/menu/ComboForeignDrawMM.h` — 2ship.dll consumer `MM_DrawComboForeign(RandoCheckId)`. Mirror
  of `Randomizer_DrawComboForeign` (`soh/.../draw.cpp`): `MM_LookupForeign` → `GetProcAddress(soh.dll,
  OOT_GetItemDrawInfo)` → route paths with `"__OTR__@oot:"` → submit OPA/XLU layers (per-check
  per-slot cache + sentinel fallback). MM passes the `RandoCheckId` straight into `Rando::DrawItem`,
  so no GetItemEntry-stamping analog is needed.

**`mm/2s2h/Rando/DrawItem.cpp` (port code, COMBO_BUILD-guarded):** `#include "ComboForeignDrawMM.h"`
(outside the `extern "C"` block) + a `case RI_COMBO_FOREIGN: MM_DrawComboForeign(randoCheckId);` in
`Rando::DrawItem`.

## Cross-game erase: deleting a slot wipes both OOT and MM saves (issue #1, 2026-06-19)

**Why:** a ComboShip save *slot* (file 1/2/3) is one combined OOT+MM playthrough, but each game's
"Erase" only deleted its own save, orphaning the other. Issue #1 asks OOT's Erase to also delete MM's
save for the slot; implemented **bidirectionally** (either game's Erase wipes both). Same launcher-owned
seam shape as the cross-item delivery work: the erasing game fires a launcher-registered callback with
the 0-based slot, and the launcher calls the OTHER game's save-only delete export. The launcher does no
index math — MM's 1-based JSON naming (`file{N+1}.json`) is hidden inside `MM_DeleteSaveFile`. No loop:
the delete exports remove files directly and never re-enter a menu erase path.

**soh (`soh/soh/SaveManager.cpp`, all `#ifdef COMBO_BUILD`):**
- `SOH_DeleteSaveFile(int fileNum)` export — calls `DeleteZeldaFile` directly (NOT `Save_DeleteFile`,
  so OOT's own erase seam does not re-fire). Called by the launcher when MM erases.
- `gComboDeleteForeignSave` fn-pointer + `SOH_SetDeleteForeignSave` setter export (mirrors the
  cross-item `SOH_SetCrossDeliver` seam shape).
- `Save_DeleteFile` (the erase-only choke; sole caller is `z_file_copy_erase.c`, NOT `CopyZeldaFile`)
  fires `gComboDeleteForeignSave(fileNum)` after the local delete.

**mm (`mm/2s2h/BenPort.cpp`):**
- `MM_DeleteSaveFile(int fileNum)` export — `fileNum` is the 0-based slot; deletes both
  `SaveManager_GetFileName(fileNum + 1, false)` and the `(…, true)` backup (mirrors
  `Enhancements/DifficultyOptions/DeleteFileOnDeath.cpp`). Called by the launcher when OOT erases.
- `gMMComboDeleteForeignSave` fn-pointer + `MM_SetDeleteForeignSave` setter export.

**mm game-source (`mm/src/overlays/gamestates/ovl_file_choose/z_file_copy_erase.c`, COMBO_BUILD-guarded —
the only vendored-source touch):** `FileSelect_EraseConfirm` fires `gMMComboDeleteForeignSave(selectedFileIndex)`
right after `Sram_EraseSave` on erase confirm. Unavoidable because MM has no port-level erase choke —
`Sram_EraseSave` only zeroes the flash buffer; the JSON is deleted later via the flash-write validation
path. On future merges, keep this guarded block in the erase-confirm branch.

**combo (`combo/ComboShip.cpp`):** resolves the four new symbols, defines `DeleteForeignSaveFromOOT` /
`DeleteForeignSaveFromMM` (route 0-based slot to the other game), and registers them via
`SOH_SetDeleteForeignSave` / `MM_SetDeleteForeignSave` alongside the other startup callbacks.
## Anchor transport moved to the launcher (combo-owned connection) — Phase 1 (2026-06-17)

**Why:** Anchor (upstream SoH online co-op) owned its own TCP socket + receive thread inside
`soh.dll`, so the connection died on every OOT↔MM portal transition and could never be shared with
MM. ComboShip now owns ONE persistent connection in `ComboShip.exe` that survives transitions and
will later route packets to whichever game is active. OOT's Anchor is **not relocated** — only its
transport is redirected through a minimal COMBO_BUILD seam; all packet/handler/menu/dummy-player
logic stays byte-intact in `soh/soh/Network/Anchor/`. A relay probe (since removed) confirmed the
public hm64 server relays our packet types peer-to-peer, so no server changes are needed.

**`soh/soh/Network/Network.h` + `Network.cpp` (vendored, COMBO_BUILD-guarded — preserve on future
soh merges):** under `COMBO_BUILD`, `Enable`/`Disable` no longer open a socket or spawn
`ReceiveFromServer`; they call launcher-registered hooks `gComboAnchorConnect`/`gComboAnchorDisconnect`.
`SendDataToRemote` routes to `gComboAnchorSend` instead of `SDLNet_TCP_Send`. Two new members feed
the launcher's receive thread back in: `InjectIncomingJson` (reuses `HandleRemoteJson`) and
`SetConnectedFromCombo` (drives `OnConnected`/`OnDisconnected`). The original socket bodies are kept
intact under `#else` for non-combo builds. No original lines deleted.

**`soh/soh/Network/Anchor/Anchor.cpp` (vendored, COMBO_BUILD-guarded):** `SendJsonToRemote` sends
immediately via `Network::SendJsonToRemote` under `COMBO_BUILD` (the launcher owns the thread-safe
outgoing queue), instead of pushing to the game-side `outgoingPacketQueue` that nothing would drain
without the local receive thread. Non-combo path unchanged.

**`soh/soh/OTRGlobals.cpp` (vendored, COMBO_BUILD-guarded):** six new exports — `SOH_SetAnchorSend`,
`SOH_SetAnchorConnect`, `SOH_SetAnchorDisconnect` (store the launcher's transport hooks),
`SOH_Anchor_RecvJson`, `SOH_Anchor_OnConnected`, `SOH_Anchor_OnDisconnected` (drive the in-place
Anchor). `declspec` follows `extern "C"` (the export-visibility ordering trap).

**Combo-owned (no further vendored churn):**
- `combo/ComboShip.cpp` — `namespace ComboAnchor` owns the SDL_net socket + receive thread + a
  thread-safe outgoing queue, framing NUL-delimited JSON exactly like the old `Network`. It
  registers `Send`/`Connect`/`Disconnect` into soh at boot and dispatches inbound packets via
  `SOH_Anchor_RecvJson`. `ComboAnchor::Shutdown()` joins the thread BEFORE any `FreeDll` (the thread
  calls into soh.dll). `#define SDL_MAIN_HANDLED` precedes the SDL include so SDL doesn't hijack
  `main`.
- `combo/CMakeLists.txt` — `ComboShip` now links `SDL2_net` (`-static` on the static-md triplet),
  mirroring soh's linkage; SDL2main is intentionally not linked.

On future merges: if upstream restructures `Network`/`Anchor` transport, re-apply the COMBO_BUILD
`#else` split. The launcher-side connection and dispatch are combo-owned and merge-independent.

## MM cross-RM display lists must not be eagerly resolved (foreign-draw crash fix) (2026-06-17)

**Why:** entering MM with a cross-world seed that placed a foreign OOT item at an MM check crashed on
the first MM draw: `Rando::DrawItem → MM_DrawComboForeign → gSPDisplayList → ResourceMgr_LoadGfxByName
→ std::vector<Gfx>::operator[]` out of bounds. `MM_DrawComboForeign` submits the OOT model's display
lists as `__OTR__@oot:…` routed paths. MM's `gSPDisplayList` stub eagerly resolves any `__OTR__`
pointer via `ResourceMgr_LoadGfxByName` (→ MM's own RM), but a cross-game `@oot:` path isn't in MM's
archives, so it returned a DisplayList with an empty `Instructions` vector and `&Instructions[0]`
crashed. Cross-RM-routed paths must instead reach the Fast3D interpreter
(`gfx_dl_otr_filepath_handler_custom`, `libultraship/src/fast/interpreter.cpp`), which parses
`@<game>:`, routes via `CrossRMRegistry`, and already null-guards bad routes. This is also the likely
cause of the "corrupted MM save" reports (a crash mid-MM leaves a half-written save).

**`mm/src/code/stubs.c` (vendored, COMBO_BUILD-guarded — preserve on future mm merges):** in
`gSPDisplayList`, when the OTR-signature path is a route marker (`imgData[7] == '@'`), emit it as a
`G_DL_OTR_FILEPATH` command (`gDma1p(pkt, G_DL_OTR_FILEPATH, imgData, 0, G_DL_PUSH)`) and return,
instead of eager-resolving. This is the **exact mirror** of the OOT-side guard already present in
`soh/soh/GbiWrap.cpp:gSPDisplayList` (commit for the OOT-foreign-in-MM feature) — MM was simply
missing the symmetric half. No original lines deleted; non-combo builds keep eager resolution.

## Fast3D guard: never branch into an unbound N64 segment (foreign-draw crash class) (2026-07-11)

**Why:** a cross-game foreign item can submit a raw `G_DL` that references a segment the host game
never bound (e.g. an MM item's animated-material segment 8 that OOT's foreign draw didn't set up).
`SegAddr` returns the raw `0x0S……` address as-is, and `gfx_dl_handler_common` branched into it with
no validity check → the interpreter ran garbage as GBI (tell: an impossible `G_PUSH_SHADER` no asset
emits) → null-shader deref in `GfxSpTri1`. Hit by the foreign Moon's Tear (`gGiMoonsTearItemDL`);
the skull-token flame was the same class, patched ad-hoc earlier.

**`libultraship/src/fast/interpreter.cpp` (COMBO_BUILD-guarded — preserve on future lus merges):**
`gfx_dl_handler_common` **and** `gfx_dl_index_handler` now reject a resolved target still in the
unresolved segment range (`ComboIsUnresolvedSegmentTarget`: null, or `<= 0x0FFFFFFF` and not a real
module address on Windows) — log once per target and skip the command (same `return false` skip the
OTR-filepath handler uses for a bad route). The predicate mirrors the existing timg validation in
`gfx_set_timg_handler_rdp`. A legit native DL always binds its segment first, so this only ever fires
on cross-game garbage.

## Cross-game Moon's Tear render: replicate the animated material + billboard (2026-07-11)

**Why:** the foreign MM Moon's Tear draws a two-tex-scroll animated material on segment 8 (item body
+ glow) via `AnimatedMat_Draw(gGiMoonsTearTexAnim)` and a billboard on the glow. The cross-game
export carried neither, so the item drew wrong (and, before the guard above, crashed on the unbound
segment). Generalizes the ad-hoc skull-token `xluSeg8TexScroll` flame path to resource-driven
animated materials.

**Combo-owned (no vendored churn):** `combo/menu/ComboForeignAnim.h` gains type-1 (DualScroll)
support + `ComboForeignTexAnim_Run`/`_Restore` (loads the owning game's `TextureAnimation` via
`CrossRMRegistry`, binds seg 8 on OPA+XLU, restores after the DLs). `combo/menu/ComboItemDrawABI.h`
+ `ComboItemDrawMM.h` add `matAnimPath`/`matAnimBindOpa`/`matAnimBillboard` (MM matches the tear by
its DL string). `soh/.../randomizer/draw.cpp` binds/billboards/restores around the DL submission.

**`mm/src/code/z_draw.c` (comment only):** the Moon's Tear branch comment in the combo-owned
`GetItem_GetDrawTableEntry` no longer claims the scroll/billboard "are dropped" (the consumer now
replicates them).

## MM Anchor adapter — Phase 2a (MM joins the shared connection) (2026-06-17)

**Why:** MM had no online presence — when a co-op player crossed into MM, peers saw their stale last
OOT location ("Happy Mask Shop") because OOT's Anchor went dormant and nothing on the MM side sent
updates. Phase 2a adds an MM-side Anchor adapter that piggybacks on the launcher-owned connection
(no second socket, no MM Anchor menu) and sends MM client-state with a namespaced scene id.

**Combo-owned / new (no vendored churn):**
- `mm/2s2h/Network/Anchor/MMAnchor.{h,cpp}` (new) — standalone MM Anchor adapter. Sends via the
  launcher callback `gMMComboAnchorSend`, receives via the `MM_Anchor_RecvJson` export, is
  activated/deactivated by the launcher on transitions. Emits JSON shapes matching soh's Anchor
  (`UPDATE_CLIENT_STATE`/`ALL_CLIENT_STATE`) for cross-client interop. Reads the same process-global
  `gRemote.Anchor.*` CVars OOT's menu wrote (literals spelled out — MM lacks soh's prefix macro).
  Scene ids are namespaced (`MM_ANCHOR_SCENE_NAMESPACE = 1000`, fits `s16`) so MM and OOT scene
  numbers never collide in the shared roster / presence matching. Exports: `MM_SetAnchorSend`,
  `MM_Anchor_RecvJson`, `MM_Anchor_Activate`, `MM_Anchor_Deactivate`. Picked up by MM's existing
  `GLOB_RECURSE 2s2h/*.cpp` (CMake reconfigure required after adding the files).
- `combo/ComboShip.cpp` — `ComboAnchor` now tracks `sActiveGame`, routes inbound packets to the
  active game, registers `MM_SetAnchorSend`, and calls `SetActiveGame(0|1)` at each transition
  (activates MM's adapter on OOT→MM, deactivates on the way back). OOT self-reactivates via its own
  GameInteractor hooks, so it needs no explicit activate.

**`soh/soh/Network/Anchor/AnchorRoomWindow.cpp` (vendored, COMBO_BUILD-guarded — preserve on future
soh merges):** the room window labels a peer whose `sceneNum >= 1000` (an MM peer) as "Majora's Mask"
instead of running its namespaced id through OOT's `SohUtils::GetSceneName` (which would render a
bogus OOT scene name). It also suppresses the seed-mismatch warning for MM peers (`sceneNum >= 1000`)
— OOT's rando seed and MM's seed aren't comparable, so the check would always false-positive;
real cross-game seed verification (both games reporting the shared combo masterSeed) is Phase 3.
Minimal stopgap; Phase 4 moves the room window into the unified combo UI with real MM scene names.
No original lines deleted.

## MM Anchor adapter — Phase 2b (remote-player puppet + PLAYER_UPDATE) (2026-06-17)

**Why:** make co-op partners visible in MM. Adds per-frame pose broadcast and a "puppet" actor that
renders remote players' Link across all five transformation forms.

**Combo-owned / new (no vendored churn):**
- `mm/2s2h/Network/Anchor/MMAnchor.{h,cpp}` extended to the canonical Anchor field set + `PLAYER_UPDATE`
  send/receive, `RefreshClientActors`, and the `ShouldActorInit`/`OnActorUpdate` hooks.
- `mm/2s2h/Network/Anchor/DummyPlayer.cpp` (new) — the puppet actor. **Ported from the canonical
  2S2H Anchor PR (HarbourMasters/2ship2harkinian#1349, by the SoH Anchor author)**, adapted to
  ComboShip's launcher-owned transport (`MMAnchor` instead of a socket-owning `Anchor`) and
  `gRemote.Anchor.*` CVar keys. Spawns `ACTOR_PLAYER` → re-tags to `ACTOR_ITEM_INBOX`/`ACTORCAT_NPC`
  with `DummyPlayer_*` funcs; inits with `gPlayerSkeletons[transformation]` + a mask segment; reuses
  vanilla `Player_DrawGameplay`; respawns on form change. All five forms render through stock code.
- Implementation notes vs canonical: joint buffers are serialized as plain int arrays (nlohmann
  reserves `std::vector<u8>` for its binary type); `posRot` is read via the existing `Vec3f`/`Vec3s`
  converters (no `from_json<PosRot>` in this project's `BenJsonConversions.hpp`); client-state carries
  BOTH a namespaced `sceneNum` (OOT roster display) and a raw `sceneId` (MM same-scene puppet match).

No vendored MM source was modified for 2b (unlike the canonical PR, which added `OnSceneSpawnActors`/
`OnPlayerSfx` hooks to `z_actor.c` — ComboShip uses the existing `OnSceneInit`/`OnActorUpdate` hooks
instead, avoiding any vendored edit).

## MM Anchor adapter — Phase 2c (shared-progression item sync) (2026-06-18)

**Why:** make a check collected by one co-op player benefit the whole team. ComboShip chose
*shared-progression* co-op (decided 2026-06-17), not the canonical PR's *multiworld/routed-ownership*
model — so a locally-obtained check's item is broadcast to all teammates rather than released to an
owner. The apply path still mirrors the canonical (`ConvertItem` → junk fallback → `Rando::GiveItem`).

**Combo-owned (MMAnchor):** `SendPacket_GiveItem`/`HandlePacket_GiveItem` + `GIVE_ITEM` dispatch. The
wire carries the **raw** `randoItemId` + its `randoCheckId`; each receiver runs `ConvertItem` against
its *own* progressive state (so progressive items resolve to the receiver's correct tier) and marks
`RANDO_SAVE_CHECKS[rc]` obtained to avoid double-collection. `applyingRemoteItem` guards the
grant→broadcast loop; `targetTeamId` scopes to the team; self-broadcasts are ignored by clientId.
Flag sync is intentionally deferred (mirrors the canonical, whose `HandlePacket_SetFlag` is stubbed).

**`mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp` (vendored MM rando, COMBO_BUILD-guarded — preserve on
future mm merges):** at the existing local check-grant point, one guarded call
`MMAnchor_BroadcastCheckItem((int)CUSTOM_ITEM_PARAM, (int)randoSaveCheck.randoItemId)` (placed while
`CUSTOM_ITEM_PARAM` still holds the checkId, before it's overwritten with the item id on the next
line) + one extern declaration in the file's existing COMBO_BUILD include block. No original lines
moved/deleted. The function is a no-op unless Anchor is active, so non-co-op rando play is unaffected.

## MM Anchor adapter — Phase 2d (late-join / reconnect resync) (2026-06-18)

**Why:** bring a late-joining or reconnecting co-op client up to the team's current progression.

**Combo-owned (MMAnchor, no vendored churn):** ported the canonical `UPDATE_TEAM_STATE` /
`REQUEST_TEAM_STATE` (2S2H PR #1349) onto the launcher transport. On save (`AfterEndOfCycleSave`) and
in reply to a `REQUEST_TEAM_STATE`, a client serializes its whole `gSaveContext.save` (via
`BenJsonConversions`, with the rando-check array compacted — **7 fields**, dropping the canonical's
`multiWorldTeamIndex` since ComboShip is shared-progression, not multiworld). A client requests team
state on `OnSaveLoad` and on connect-while-in-game. On receive it restores receiver-local fields
(bottle contents, non-zero ammo, checksum, fileCreatedAt, `newf`, dpad/button layout, playerName),
then commits **only** `saveInfo` + `shipSaveInfo` — top-level `Save` fields (scene/entrance/time/day/
`playerForm`/cycle) are intentionally left untouched so the receiver isn't relocated — then re-runs
`Rando::CheckTracker::OnFileLoad` / `ActorBehavior::OnFileLoad` / `ShipInit::Init("IS_RANDO")`. Queued
packets ride along and are replayed through the normal incoming queue. Known canonical tradeoff
(accepted): the resync overwrites the receiver's HP/magic/rupees/respawn/scene-flags with the team's.
Same-game only (MM `permanentSceneFlags`/commit-hash layout). No vendored MM source modified for 2d.

## ComboShip-owned unified ROM extraction (OoT + MM) (2026-06-21)

**Why:** ComboShip needs BOTH an OoT and an MM ROM. The old launcher extracted them headlessly
(per-game `SOH_Extract`/`MM_Extract`, native OS dialogs, no progress bar) before any window existed.
Upstream's friendly ImGui extraction (`RunExtract`'s "ROM Extraction" modal) is per-game and can't host
a single "give me both ROMs" gate. ComboShip now owns a unified screen that asks for both ROMs up
front (auto-scan + Browse), requires both, and extracts each with a progress bar.

**Vendored (additive, `COMBO_BUILD`-guarded, minimal):**
- `soh/soh/OTRGlobals.cpp` — `InitOTR` split into `SOH_InitWindowOnly()` (the `OTRGlobals` ctor:
  window + ImGui + `SohGui::SetupMenu`, needs only the bundled `soh.o2r`, **no ROM**) and
  `SOH_FinishInit()` (the ROM-dependent `Initialize()` + managers + `SetupGuiElements`). Non-combo
  `InitOTR` keeps the original ctor → `RunExtract` → finish ordering (so `SOH_Init` is unchanged for the
  fast path). Added UI-less primitives `SOH_ValidateRom` / `SOH_StartExtraction` (background
  `std::async` → `Extractor::CallZapd`) / `SOH_GetExtractionProgress` (poll atomics; bool-ish values as
  `int` for a clean C ABI).
- `mm/2s2h/BenPort.cpp` — `MM_ValidateRom` / `MM_StartExtraction` / `MM_GetExtractionProgress` mirror
  (no init split — MM has no window of its own; `CallZapd` is context-independent so MM extracts fine
  against OOT's shared window). The `*Extract` workers catch exceptions → `done && !success`, never
  crashing the launcher.

**Combo-owned:**
- `combo/ComboExtract.h` — the C ABI (callback fn-ptr typedefs + `ComboExtractCallbacks`).
- `combo/gui/ComboExtractScreen.cpp/.h` (in comboui) — `ComboUI_RunExtraction(cb)`: owns the
  libultraship frame loop (`HandleEvents`/`StartDraw`/`StartFrame`/`RunGuiOnly`/`EndDraw`/`EndFrame`,
  same sequence as `RunExtract`) and the screen — auto-scans the working dir and classifies ROMs via the
  validate callbacks, per-slot Browse (native `GetOpenFileNameA`), Extract gated on both valid, Quit
  exits, then **sequential** single progress bar per game. Must `ImGui::SetCurrentContext` (per-module
  `GImGui`).
- `combo/ComboShip.cpp` — new ordering: detect missing ROM archives → if any, `SOH_InitWindowOnly()` →
  load comboui early → `ComboUI_RunExtraction()` (exit 1 on quit/failure) → `SOH_FinishInit()`; else the
  monolithic `SOH_Init()` fast path. Also **fixed `OOTArchivesExist()`**: it counted the PORT archive
  `soh.o2r` (always present) as the OoT ROM — so a real first run skipped OOT extraction and then
  hard-exited in `Initialize()`. It now checks only `oot.o2r` / `oot-mq.o2r`.

Verified: fast path (archives present) boots straight to title unchanged; first-run path opens the
extraction screen (`OoT=1 MM=1`). The old `SOH_Extract`/`MM_Extract` exports remain for non-combo use
but the launcher no longer calls them.

**`CallZapd` must return `true` on success (re-survive on every re-vendor).** Upstream `CallZapd`
returns `false` unconditionally (native flow gates on exceptions, not the return value), but
`SOH_/MM_StartExtraction` use it as the combo screen's success flag — so a `false` return makes a
*successful* extract read as "failed". The soh re-vendor `19427b200` reverted this and OOT extraction
broke; restored in `soh/soh/Extractor/Extract.cpp` to mirror the MM sibling (catch throw → verify
archive exists → `return true`). Also Release links `/SUBSYSTEM:WINDOWS` (no console window; Debug
keeps it), `+/ENTRY:mainCRTStartup` since ComboShip has its own `main()` and doesn't link SDL2main.

**Combined config renamed to `comboship.json` (issue 24).** OOT + MM share one libultraship Context, so
there is a single config file. `OTRGlobals.cpp` now names it `comboship.json` (COMBO_BUILD-guarded; `#else`
keeps `shipofharkinian.json` for standalone soh) to make the combined nature explicit and to gate the
first-launch settings import (absent file = fresh install). New combo-owned export `SOH_ApplyImportedConfig`
installs a launcher-merged config into the live `Config` (`SetBlock` + `Save` + `CVarLoad` + controller
reload). MM's `2ship2harkinian.json` literals are untouched (standalone-only, off the combo path).

## Cross-game Check/Item tracker windows (collision fix + active-game gating) (2026-06-21)

**Why:** Both soh and mm register tracker windows named identically — `"Check Tracker"`,
`"Check Tracker Settings"`, `"Item Tracker"`, `"Item Tracker Settings"`. There is ONE shared
libultraship `Gui` across both DLLs, and `Gui::AddGuiWindow` keys by display name and **rejects
duplicates**. OOT boots first, so all four of MM's tracker windows were silently dropped and could
never show. Separately, the shared Gui draws *every* registered window each frame, so the
backgrounded game's tracker `DrawElement` would run against that DLL's stale per-module ImGui
context. The user wants the popout trackers to follow the active game (OOT↔MM).

**Vendored (additive, `COMBO_BUILD`-guarded, minimal):**
- `mm/2s2h/BenGui/BenGui.cpp` — MM's 4 tracker windows are registered with a `COMBO_MM_TRACKER_SUFFIX`
  (`"##MM"`) appended to their names. ImGui renders only the text before `##`, so the visible title
  stays `"Check Tracker"`/`"Item Tracker"`, but the Gui map key (and ImGui ID) is unique → both games
  coexist. Non-combo builds get an empty suffix (unchanged).
- `mm/2s2h/Rando/Menu.cpp` — the two "Popout Settings" buttons resolve their window **by name**
  (`BenGui/Menu.cpp` → `GetGuiWindow(windowName)`), so their `WindowName(...)` carries the same
  `COMBO_MM_TRACKER_SUFFIX`.
- `soh/.../randomizer_item_tracker.cpp`, `soh/.../randomizer_check_tracker.cpp`,
  `mm/.../ItemTracker/ItemTracker.cpp`, `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — each tracker's
  `Draw()` calls `ComboMenuContext::UseSharedImGuiContext()` (from `combo/menu/ComboMenuSharedContext.h`)
  before any ImGui call, the same pattern already used by `SOH_DrawSettings`. This is the leading fix
  for OOT trackers not appearing even foreground; if a build shows they still don't, the next step is
  per-stage logging (registration → visibility → draw-reached → context).

**Combo-owned:**
- `combo/gui/ComboTrackerVisibility.cpp` (in comboui) — `ComboUI_OnForegroundGame(int game)` hides the
  background game's 4 tracker windows (remembering each one's intent) and restores the foreground
  game's; `ComboUI_RestoreTrackerIntent()` puts both games' intent back before shutdown config save so
  a backgrounded-at-exit game doesn't persist its tracker as "off". Uses CVar + `GuiWindow::Show/Hide`
  (no ImGui calls → no context concern in comboui).
- `combo/ComboShip.cpp` — resolves the two exports and calls `ComboUI_OnForegroundGame` after the eager
  MM boot (OOT foreground) and at each portal transition (next to `ComboAnchor::SetActiveGame`);
  `ComboUI_RestoreTrackerIntent` runs at the start of teardown.

The active-game gating is also what keeps the two games' identically-titled inner `ImGui::Begin("Item
Tracker")` / `ImGui::Begin("Check Tracker")` calls from ever running in the same frame.

## Toast/notification window: collision fix + active-game gating (issue #28, 2026-06-22)

**Why:** Same class of bug as the trackers above. Both soh and mm already have a near-identical
notification (toast) system, and both register their window under the identical name
`"Notifications Window"`. The single shared libultraship `Gui::AddGuiWindow` **rejects duplicates**, and
OOT boots first, so MM's notification window was silently dropped — MM toasts (item pickups, the
ComboShip "Sent to Hyrule" cross-world send, Anchor events) never appeared in the combined build, even
though the emitters exist and work in standalone MM. The user wants the active game's toasts to show.

**Vendored (one line, `COMBO_BUILD`-guarded):**
- `mm/2s2h/BenGui/BenGui.cpp` — MM's notification window registration appends `COMBO_MM_TRACKER_SUFFIX`
  (`"##MM"`) to its name, exactly like the tracker windows. Map-key only; the name isn't displayed
  (`Notification::Window::Draw` titles its ImGui windows `notification#<id>`). No change to MM's
  notification behavior. Non-combo builds get the empty suffix (unchanged).

**Combo-owned:**
- `combo/gui/ComboTrackerVisibility.cpp` — `ComboUI_OnForegroundGame` now also gates the notification
  window. Unlike trackers, `Notification::Window` overrides `Draw()` and ignores `IsVisible()` (and
  `GuiElement::Update()` runs `UpdateElement()` unconditionally), so `Show()/Hide()` does NOT suppress
  it. Instead the background game's window is `RemoveGuiWindow`'d (dropped from the draw loop entirely,
  stopping both `Draw` and `UpdateElement`) and the foreground game's is re-added — the SAME
  already-initialized object (a `weak_ptr` handle; the window stays owned by its game DLL's
  `mNotificationWindow` member, so no cross-DLL ownership / shutdown-dtor hazard, and no fresh window
  is created → no 0xCD re-registration crash). No CVar/intent bookkeeping (notifications have no user
  open/close toggle), so `ComboUI_RestoreTrackerIntent` is unchanged.

The "Sent to Hyrule" toast itself was already implemented (`mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp`,
`Rando_SendForeignCheck`); this change only makes MM's window survive registration so it can show.

## Cross-world pool: inject settings-added skill items (2026-06-22)

**Why:** The cross-world dump (`SOH_DumpRandoStaticData`) builds the combined fill's item pool from each
check's **vanilla** item (`loc->GetVanillaItem()`). That silently omits every item the *settings ADD* to
the pool — most importantly the shuffled "skill" items: `RG_OPEN_CHEST`, `RG_SPEAK_*`, `RG_CLIMB`,
`RG_CRAWL` (when `RSK_SHUFFLE_OPEN_CHEST` / `_SPEAK` / `_CLIMB` / `_CRAWL` are on). Those grant the logic
flags `CAN_OPEN_CHEST` / `CAN_SPEAK_*` etc. — and **every chest, deku scrub, and shop check gates on
them** (e.g. `logic.cpp` chest access = `CheckRandoInf(RAND_INF_CAN_OPEN_CHEST)`). With the items absent
from the pool, the oracle never grants the flags, so all chests/scrubs/shops are logically unreachable
(OOT showed 145/470 reachable with a "full" inventory), and the assumed fill dead-ends. Standalone SoH
works because it fills from the real `GenerateItemPool()`, which adds these items; our combined fill took
a vanilla-per-check shortcut that drops them.

**Vendored (`COMBO_BUILD`-guarded, `soh/soh/OTRGlobals.cpp`):** after the per-check dump loop, inject the
enabled skill items into the emitted pool, overwriting an equal number of junk slots so items stay 1:1
with checks. Swim/Grab need no injection (they map to Progressive Scale/Strength, already carried by the
vanilla pool). Verified: OOT reachable 145→460, seeds 1234–1238 generate (5/5).

**Known limitation / follow-up:** RESOLVED — superseded by the real-pool rework below (2026-07-07),
which sources the pool from `GenerateItemPool()` and deletes this skill-injection block.

## Cross-world pool: real generated pool + confinement fidelity (2026-07-07)

**Why:** The skill-injection above only patched 4 item families. Every other settings-added item that is
not a check's vanilla item was still dropped (OOT: Triforce pieces, WinCon Triforce, Skeleton Key, Roc's
Feather, ocarina buttons, mask-quest masks, magic-bean pack, progressive identity/counts; MM: Boss/Enemy
Souls, Clock items, ocarina buttons, swim, bonus songs, Tycoon wallet, Triforce). Missing advancement
items → unreachable locations. Separately, the cross fill ignored placement **confinement** (own-dungeon
keys/boss keys, dungeon rewards, restricted songs, MM stray fairies), shuffling them anywhere.

**Fix — source the pool from each game's real generator, confine via each game's own code:**
- `soh/.../3drando/fill.cpp`: extracted the restricted-song block into `PlaceRestrictedSongs()` (pure
  extract-method, `Fill()` unchanged) and added `COMBO_BUILD`-guarded `ComboFillConfined()` — it *calls*
  Fill()'s own functions (`GenerateItemPool`, `RandomizeDungeonRewards`, per-dungeon `RandomizeOwnDungeon`,
  `PlaceRestrictedSongs`, `RandomizeDungeonItems`), skipping shops/entrances/Link's Pocket and the free
  Assumed/FastFill. Temp `GetMinVanillaShopItems` is injected for reachability then erased (mirrors
  Fill()'s entrance-validation trick). Declared in `fill.hpp`.
- `soh/.../OTRGlobals.cpp` `SOH_DumpRandoStaticData`: runs `ComboFillConfined()`, then partitions
  `allLocations` by `GetItemLocation(rc)->GetPlacedRandomizerGet()` into `fixed[]` (confined) vs `checks[]`
  (empty/fillable), and emits the residual `itemPool` as `pool[]`. Skill-injection block deleted.
- `mm/2s2h/BenPort.cpp` `MM_DumpRandoStaticData`: calls upstream `PreplaceConfinedItems(checkPool,
  itemPool)`, captures the confined placements (checkPool diff → `RANDO_SAVE_CHECKS`) as `fixed[]`, emits
  the reduced `itemPool` as `pool[]` and reduced `checkPool` as `checks[]`.
- `combo/rando/CrossWorldRando.h`: dump schema gains `pool[]` (real item pool) and `fixed[]` (locked
  pre-placements); `parsePool` reads them (falls back to per-check `vanillaItem` for an older DLL). Locked
  placements are seeded into `placements`/`filledChecks` each pass so `reachableFixpoint` credits them
  when their check is reached (collected-in-place, unlike owned-from-start Link's Pocket).

**Invariant:** `pool.size() >= checks.size()` for both games — surplus is junk (excluded-location
fills, MM overflow) that the cross fill drops; a shortfall (`pool < checks`) would leave checks
unfilled and is warned. Shuffled shopsanity slots aren't in `itemPool` (`CountEmptyLocations(false)`
excludes shops), so the OOT dump adds each shuffled shop slot's vanilla buy item to `pool[]`. Link's
Pocket is excluded from the dump entirely — it stays owned by `SOH_GetForcedPlacements`.

## Cross-world Link's Pocket placement (2026-06-21)

Link's Pocket is a rando-only OOT check with no vanilla item, so it's absent from the cross-world
dump and the combined fill never placed it — leaving it unset, which crashed save creation
(`Item_Give(0xFF)` assert) and ignored `RSK_LINKS_POCKET`.

- `soh/.../OTRGlobals.cpp`: new `SOH_GetForcedPlacements` returns Link's Pocket's item. For the
  dungeon-reward case it now reads the item `RandomizeDungeonRewards` already placed at
  `RC_LINKS_POCKET` (inside the preceding `SOH_DumpRandoStaticData`), instead of re-rolling a separate
  LCG. The old re-roll disagreed with the fill's pick, so one dungeon reward was orphaned (nowhere in
  the spoiler → altar hint "an unknown place") and another duplicated. Non-dungeon-reward modes
  (advancement/any/nothing) unchanged.
- `soh/.../savefile.cpp`: `StartingItemGive` skips an unresolved (ITEM_NONE/MOD_NONE) item instead of
  asserting — safety net for any residual unplaced save-creation check.
- `combo/rando/CrossWorldRando.h` + `ComboShip.cpp`: the fill reserves forced items out of the pool,
  treats them as owned-from-start for logic, and appends them to the OOT placements.

## Cross-game items: immediate dual-context delivery (replaces the JSON mailbox) — issue #3 (2026-06-19)

**Why:** the cross-world randomizer delivered a foreign item (an item whose home is the *other*
game) via a JSON "mailbox" (`combo/rando/CrossMailbox.h` + `saves/combo/slot{N}.mailbox.json`) that
the target game drained **per-frame, only while that game was active**. So an item never landed
until you switched into the target game, on a disk stash + poll. Under eager-MM-boot both games'
`gSaveContext` are always resident (one active, one dormant), so we now grant the item into the
**dormant target game's resident save immediately** at detection — no stash, no poll — and persist
it then and there (survives quitting before ever switching games). The same "deliver item X to
game G" mechanism also serves networked co-op: a collected foreign item is broadcast and routed to
each teammate's correct game regardless of which game they're currently in.

**Footprint:** net vendored complexity went **down** — the JSON mailbox and both per-frame drain
handlers (`Rando_CrossMailboxDrain`, `RandomizerOnPlayerUpdateForCrossMailboxHandler`) and all their
hook registration/zeroing plumbing were deleted. `CrossMailbox.h` is gone; its `GameId` enum moved
into `combo/rando/CrossForeign.h` (which stays — still maps each check → foreign item + target game
at detection). The routing **policy** lives in the combo layer; only the irreducible
grant-into-own-save shims live in the DLLs.

**Key insight (de-risks the dormant grant):** save-only grant primitives already exist on both
sides and never touch `gPlayState`, so a frozen dormant play state is safe — MM
`GiveItemForOracle` (the fill oracle's headless grant, `BenPort.cpp`) and OOT `Randomizer_Item_Give`
(`randomizer.cpp`, save-direct; `Magic_Fill` ignores `play`, `Rupees_ChangeBy` null-guards
`gPlayState`). We deliberately do **not** use `Rando::GiveItem`/`GiveItemEntryWithoutActor` (their
`Item_Give` paths stage onto a live play state).

**`soh/soh/OTRGlobals.cpp` (vendored, COMBO_BUILD-guarded):** four new exports —
`SOH_GrantCrossItem` (resolve OOT English name → `Randomizer_Item_Give` → `SaveManager::SaveFile`),
`SOH_MarkForeignObtained` (mark a foreign OOT check collected, save-only, for network idempotency),
and the setters `SOH_SetCrossDeliver` / `SOH_SetMarkForeignObtained` storing the launcher routing
callbacks `gComboCrossDeliver` / `gComboMarkForeignObtained`. `declspec` follows `extern "C"`.

**`mm/2s2h/BenPort.cpp` (vendored, COMBO_BUILD-guarded):** the MM analogs — `MM_GrantCrossItem`
(resolve RI_* via the existing `Combo_MM_SpoilerNameToItemId` map → `GiveItemForOracle` →
`SaveManager_SaveCurrentForCombo`), `MM_MarkForeignObtained` (set `RANDO_SAVE_CHECKS[].obtained`
via the existing `Combo_MM_CheckNameToCheckId` map), and the `MM_SetCrossDeliver` /
`MM_SetMarkForeignObtained` setters with their `gMMCombo*` globals.

**Detection rewire (vendored, both COMBO_BUILD-guarded, net reduction):**
`soh/.../randomizer/hook_handlers.cpp` `OOT_SendForeignCheck` and
`mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp` `Rando_SendForeignCheck` now call the cross-deliver seam
+ the Anchor broadcast instead of `ComboRando::Enqueue`. Drains + `InitCrossMailboxDrain` and its
registrations in `Rando.cpp` / `MiscBehavior.{cpp,h}` were removed.

**Networked path (combo-owned + minimal vendored):** a ComboShip-private `COMBO_CROSS_ITEM` packet
(the public hm64 server relays unknown types peer-to-peer — no server change). MM side lives in the
combo-owned `MMAnchor.{h,cpp}` (`SendPacket_CrossItem`/`HandlePacket_CrossItem` + dispatch +
`MMAnchor_BroadcastCrossItem`). **OOT side** (`soh/soh/Network/Anchor/Anchor.cpp`, vendored,
COMBO_BUILD-guarded) is kept minimal: cross-item send/receive are *free functions* over Anchor's
public members, so the only edit to the vendored `Anchor` class is **one** dispatch branch — no new
member methods. Both receive handlers guard own-clientId echo + team, then route through
`gComboCrossDeliver` (grant into target) and `gComboMarkForeignObtained` (mark source check, so the
receiver won't physically collect it later and double-deliver). The grant exports bypass the
check-collect path, so applying a received item never re-broadcasts.

**`combo/ComboShip.cpp` / `combo/rando/CrossForeign.h` / `CrossWorldRando.h`:** the
`DeliverCrossItem` + `MarkForeignObtained` dispatchers (route `targetGame`/`srcGame` 0=OOT/1=MM to
the right DLL), registered into both DLLs before `SOH_Init`. `CrossForeign.h` gained the `GameId`
enum; `CrossWorldRando.h` now includes it directly. Debug tools (`debugconsole.cpp` `cross_send`,
`SaveEditor.cpp` cross-send button) were repointed to the deliver seam.

**Known limitation (accepted, co-op race):** if both teammates physically collect their own copy of
the same foreign check before the sync arrives, the target item can be granted twice (counted items
double) — the same class of race the same-game item sync (2c) already tolerates.

**On future merges:** if upstream restructures the Anchor receive dispatch, re-apply the single
`COMBO_CROSS_ITEM` branch; the handlers themselves are COMBO_BUILD free functions that don't depend
on Anchor internals beyond its public members.

**Save-slot note (added on cherry-pick to `fix/randomizer-improvements`):** the foreign map is
written once per seed to canonical slot 0 but looked up at runtime by `gSaveContext.fileNum`;
`LoadForeignForGame` falls back to slot 0 when the per-slot file is absent so saves in File 2/3
still resolve foreign items (names + models). The immediate-delivery grant targets the resident
save by `fileNum` directly, so it is unaffected.

## File-select seed-hash icons for combo seeds (issue #32, 2026-06-23)

**Why:** stock SoH draws five item icons on the file-select as a visual seed hash so players can
match seeds/spoiler logs. The combo build showed five Deku Nuts (or an identical set every seed):
the combo generator owns generation and never runs OOT's `Playthrough_Init`, which is where vanilla
calls `GenerateHash()` to fill `Rando::Context::hashIconIndexes` — so the array stayed all-zero.
Scope is OOT only: combo boots into OOT's file-select and enters MM via transition, so MM's
file-select is never shown (MM's `finalSeed = 0` gap in `MM_InitRandoSaveFile` is a known follow-up).

**Change (vendored `soh`, minimal):** new export `SOH_SetComboSeedHash(uint32_t)` in
`OTRGlobals.cpp` (just after `SOH_ApplyRandoPlacements`) calls `ctx->SetHash(...)` + `GenerateHash()`
— mirrors stock `playthrough.cpp:64-73`. Added `#include ".../3drando/spoiler_log.hpp"` for the
declaration. Persistence/render are stock-wired (`SaveManager` copies into `fileMetaInfo.seedHash`;
`z_file_choose.c` renders it).

**Change (combo-owned, `ComboShip.cpp`):** `RunComboFill` computes a settings-aware display hash
`ComboHash(inputSeed + sohDump + mmDump)` and passes it to the new export *after*
`SOH_ApplyRandoPlacements` (which `ItemReset`s). Folding both static dumps in makes the icons
identify seed **and** settings: each dump is built from its game's live CVars and carries only the
settings-scoped pool, so OOT *and* MM shuffle/starting-item settings both vary the icons. Accepted
limitation (symmetric): a logic-only setting that doesn't change the shuffled pool (e.g. tricks)
won't change the dump, so won't change the icons.

**On future merges:** if upstream restructures `GenerateHash`/`SetHash` or the file-select hash
render, re-point the new export; the combo-side hash derivation is independent.

## File-select quest menu locked to Randomizer (2026-06-27)

**Why:** the combo build drives a randomizer through OOT's normal save flow; showing the Normal /
Master Quest / Boss Rush quest options on file-select is confusing since they're never the intent.

**Change (vendored `soh`, one COMBO_BUILD-guarded block):** in
`soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`, the `MIN_QUEST`/`MAX_QUEST` macros
(which seed `questType` at init and bound the L/R carousel wrap) are redefined to `QUEST_RANDOMIZER`
under `COMBO_BUILD`. The carousel therefore starts on Randomizer and every L/R wrap lands back on it,
so the other quests are unreachable — no draw/branch code was deleted (the non-combo build is
byte-intact via the `#else`).

**On future merges:** if upstream changes the quest enum or the carousel wrap logic, re-check that
the locked `MIN_QUEST == MAX_QUEST == QUEST_RANDOMIZER` still resolves to Randomizer on every L/R
path (incl. the Master-Quest-absent skip loop).

## Non-blocking combo generation: worker thread + file-select driven (2026-06-27)

**Why:** combo generation ran synchronously on the render thread, freezing the game (no music, no
progress) for its whole duration. Stock SoH stays responsive by running the fill on a worker thread
while the main loop keeps running (it polls `RandoGenerating` in `FileChoose_UpdateRandomizer`,
swaps to gallop music, draws "Generating…", plays a fanfare). Combo couldn't naively copy that: its
fill calls into the single-threaded game DLLs (dumps, oracles, and the `gSaveContext`-mutating
apply), and a prior whole-pipeline-off-thread attempt crashed.

**Design:** split the pipeline. The launcher (`combo/ComboShip.cpp`) runs dump→fill→playthrough on a
worker thread (`g_GenerateThread`) and stashes the result; the `gSaveContext` **apply** runs on the
main thread via `Combo_FinalizeGenerate`, polled each frame from the file-select loop. Generation is
hard-gated to the file-select screen so the worker can't race a live game tick. The launcher owns the
single `ComboGenProgress` and shares a read-only pointer with soh.

**Vendored `soh` deviations:**
- `OTRGlobals.cpp`/`.h`: new combo exports `SOH_TriggerComboGenerate` (now arg-less; reads the
  `gGeneral.ComboSeed` CVar, gates on + sets `RandoGenerating`), `SOH_SetComboProgressPtr` /
  `SOH_GetComboGenProgress` / `SOH_GetComboGenPercent`, `SOH_SetOnComboFinalizeCallback` /
  `SOH_PollComboFinalize`, and `SOH_IsOnFileSelect` (matches `gGameState->main == FileChoose_Main`,
  since `::init` is cleared after init). The generate-request callback type changed to
  `void(*)(const char*)`.
- `z_file_choose.c` (`COMBO_BUILD`-guarded): `RSM_GENERATE_RANDOMIZER` → `SOH_TriggerComboGenerate`;
  `RSM_OPEN_RANDOMIZER_SETTINGS` → open the comboui menu (`gOpenWindows.Menu`); the
  `FileChoose_UpdateRandomizer` "generating" branch polls `SOH_PollComboFinalize` and clears
  `RandoGenerating` when done; a "Generating… XX%" line is drawn from `SOH_GetComboGenPercent`.

**On future merges:** if upstream restructures the file-select randomizer menu (`RSM_*` actions) or
`FileChoose_UpdateRandomizer`, re-apply the two action repoints + the finalize poll. If `GameState`'s
`main` field or `FileChoose_Main` moves, re-check `SOH_IsOnFileSelect`.

## Consolidated combo spoiler: share/drop + remember-seed + sphere hints (2026-06-28)

**Why:** combo generation scattered per-seed data (`slot{N}.foreign.json`, `slot0.playthrough.txt`)
and kept the result only in memory — no sharing, no remembering, regenerate every session. Now one
consolidated `Randomizer/save{N}-Randomizer-<hash>.json` (+ a `Randomizer/Last-Generated-Randomizer.json`
pending file) holds everything (both games' settings, placements, foreign map, structured playthrough,
hash); it's the runtime foreign source, the remembered seed, the shareable drag-drop artifact, and the
hint data. Mostly combo-owned (`combo/ComboShip.cpp`, `combo/rando/CrossForeign.h`,
`combo/gui/ComboMenu.*`, `ComboGenProgress.h`). Vendored deviations:

- **`soh` `OTRGlobals.cpp`/`.h`** — new combo exports: `SOH_DumpRandoSettings`/`SOH_RestoreRandoSettings`
  (CVar-block snapshot/restore so a dropped seed reproduces cross-machine), `SOH_PrepRandoContext`
  (refactored out of `SOH_DumpRandoStaticData`'s prep so reload/drop can build the settings-scoped pool
  before re-applying placements — the dump now calls it), `SOH_RequestComboReload`/
  `SOH_SetOnComboReloadCallback` (launcher reload seam), `SOH_GetActiveFileNum`, and
  `Combo_SOH_GetObtainedChecks` (hint state).
- **`soh` `randomizer.cpp`** — `Rando_HandleSpoilerDrop` also accepts `fileType=="ComboShipRandomizer"`
  (sets `CVAR_GENERAL("ComboDroppedFile")`); the SoH spoiler path is unchanged.
- **`soh` `z_file_choose.c`** (`COMBO_BUILD`) — `FileChoose_UpdateRandomizer` reloads a dropped combo
  file (priority) or the remembered pending seed (first frame) via `SOH_RequestComboReload`.
- **`mm` `BenPort.cpp`** — `MM_DumpRandoSettings`/`MM_RestoreRandoSettings` (MM options are CVar-backed;
  restore runs before `MM_InitRandoSaveFile`) and `Combo_MM_GetObtainedChecks` (hint state).

**On future merges:** the apply/prep must stay main-thread (the worker only computes). If upstream
changes the rando settings/option CVar scheme, re-check the dump/restore. If the spoiler-drop handler
or `FileChoose_UpdateRandomizer` is restructured, re-apply the combo `fileType` accept + the reload
routing.

## Live-apply settings changed from the combo menu (2026-06-28)

**Why:** the games' native UIWidgets call `ShipInit::Init(cvar)` after a widget change to re-run the
enhancement registered for that CVar; comboui only set the CVar, so combo-menu changes didn't apply
until the next `ShipInit::InitAll` (game boot / new save). New exports `SOH_MenuApplyCVarChange` /
`MM_MenuApplyCVarChange` (OTRGlobals.cpp / BenPort.cpp) run `ShipInit::Init(cvar)`; the comboui menu
model + `ComboWidgetRender` call them after each change. Fixes #27. **On future merges:** if the
native UIWidgets stop using `ShipInit::Init`-on-change, revisit.

## Shared-settings consolidation + dev-tool window fixes (issue #22, 2026-06-28)

**Why:** three related issues in the shared menu/window subsystem. (a) #22 — MM ignored the Shared
tab's Graphics/Audio/Controls and the per-game tabs redundantly exposed them. (b) Opening MM's "Save
Editor" showed OOT's: the shared `Gui::AddGuiWindow` keys by display name and rejects duplicates, so
MM's identically-named dev windows were dropped (OOT boots first). (c) Dev-tool windows only offered a
"popout" button, never rendering inline.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `mm/2s2h/BenGui/BenGui.cpp` — the 9 MM dev/debug windows whose names collide with OOT's (Save Editor,
  Actor/Collision/Message Viewer, Audio Editor, Mod Menu, Hook Debugger, Input Viewer (+Settings)) now
  register with `COMBO_MM_TRACKER_SUFFIX` (`"##MM"`), same mechanism as the trackers — map-key/ID only,
  visible title unchanged, empty in standalone. (Cosmetic Editor / Time Splits Window / DL Viewer differ
  from OOT's strings already, so they don't collide.)
- `mm/2s2h/BenGui/BenMenu.cpp` — matching `WindowName(...)` popout refs carry the same suffix
  (`COMBO_MM_WINDOW_SUFFIX`). New `#ifdef COMBO_BUILD` inline `WIDGET_CUSTOM` entries render each dev
  window's `DrawElement()` inline (skipped when popped out, so no double-draw); live-world viewers
  (Actor/Collision/Message/DL/Event Log) gate on `Combo_MmIsForeground()` so they don't read MM's
  dormant/swapped play state when the MM tab is opened while OOT is foreground.
- `soh/soh/SohGui/SohMenuDevTools.cpp` + `OTRGlobals.cpp` — the same inline `WIDGET_CUSTOM` treatment for
  OOT's dev tools, gating live viewers on `Combo_OotIsForeground()`. Both `Combo_*IsForeground` helpers
  resolve comboui's `ComboUI_GetForegroundGame` once.
- `mm/2s2h/BenPort.cpp` — exports `MM_ApplyAudioVolume(seqPlayer, vol)` (→ `AudioSeq_SetPortVolumeScale`,
  the apply path MM uses instead of ShipInit) and `MM_ReloadControls()` (reloads MM's ControlDeck ports
  from the shared `gSettings.Controllers.*` CVars); `Combo_MmIsForeground()` queries comboui's
  foreground game. All inside the existing `COMBO_BUILD` export block.

**Combo-owned:**
- `combo/gui/ComboAudioBridge.{h,cpp}` — one-way mirror of OOT's canonical `gSettings.Volume.*` (int
  0-100) into MM's `gSettings.Audio.*` (float 0-1). `MirrorIfVolumeCVar` fires from the
  `ComboWidgetRender` apply-step; `SyncAllToMM` runs on MM entry. Graphics needs no bridge (one shared
  window) and Controls' data is already shared CVars.
- `combo/gui/ComboForeground.h` + `ComboTrackerVisibility.cpp` — cache the foreground game from the
  existing `ComboUI_OnForegroundGame` callback; expose `ComboUI::GetForegroundGame()` (C++) and
  `ComboUI_GetForegroundGame()` (C ABI, for MM's gating). On MM entry the callback also runs
  `ComboAudio::SyncAllToMM()` + `MM_ReloadControls` so changes made while MM was dormant take effect.
- `combo/gui/ComboMenu.cpp` — the per-game tab sidebar filter now also hides MM's
  Graphics/Audio/Controls/General (they live only in Shared, on shared CVars). **On future merges:** if
  MM's audio CVar names/scale change, update `kMap` in `ComboAudioBridge.cpp` (note: the OOT `Volume.*`
  defaults — Master 40, rest 100 — are duplicated in `kMap`'s `defaultPct` and must match
  SohMenuSettings.cpp, else an untouched slider reads as 0 and silences MM).

## Combo-side Advanced Resolution editor (issue #26 #3, 2026-06-29)

**Why:** SoH's Advanced Resolution editor (`soh/soh/SohGui/ResolutionEditor.cpp`) is built from
machinery the flat C-ABI menu snapshot can't carry — dynamic `WIDGET_TEXT` names set per-frame by a
`PreFunc`, an aspect-ratio combobox bound to a C++ static via `ValuePointer` (no CVar), two
`WIDGET_CUSTOM` draw lambdas, and a per-page `UpdateResolutionVars` MenuUpdate func. In the overlay
those widgets render broken (empty `{} x {}` readouts, an empty-CVar combobox, placeholder customs,
dead aspect/enable controls). User chose a combo-side reimplementation (works regardless of which
game is foreground) over a game-side custom-draw dependency.

**Combo-owned (no vendored edits):**
- `combo/gui/ComboResolutionEditor.{h,cpp}` — `TryRenderResolutionWidget(w)`, called at the top of
  `ComboWidgetRender::RenderWidget`. Intercepts the resolution widgets **by name/cvar** and renders
  combo controls over the effective `gSettings.AdvancedResolution.*` CVars (libultraship's shared
  `Fast3dGui::ApplyResolutionChanges` reads them live each frame). Stateless per-frame: the CVars are
  the source of truth. Live dims read from `Fast::Interpreter` via `Fast3dWindow::GetInterpreterWeak`.
  Owns the Enable checkbox + calls `SaveConsoleVariablesNextFrame` after writes so changes persist.
  Scope = core controls; niche extras (horizontal-res-field alt, NeverExceedBounds/ExceedBoundsBy,
  IgnoreAspectCorrection) intentionally omitted.

**On future merges (coupling — re-verify):** this **shadows specific SoH widget names** —
`"Aspect Ratio"` (empty-cvar combo), `"AspectRatioCustom"`, `"MoreResolutionSettings"`, the
`"Viewport dimensions"`/`"Internal resolution"` readout prefixes, the `"...is overriding these
settings"` / `"Click to disable N64 mode"` advisories — and **transcribes SoH's preset tables**
(aspect labels + X/Y, pixel-count labels + values, clamps). If SoH renames those widgets or changes
the tables, re-sync `ComboResolutionEditor.cpp` (else the widgets silently fall back to the broken
generic render). CVar prefix `gSettings.AdvancedResolution` comes from `CMake/lus-cvars.cmake`.

## Inline controller bindings on Shared → Controls (issue #26 #1, 2026-06-29)

**Why:** SoH's Controls page is popout-only (no inline bindings widget), so the combo overlay showed
just a "Popout Bindings Window" button — the user had to detach a floating window to rebind.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `soh/soh/SohGui/SohMenuSettings.cpp` — a new "Controller Bindings Inline" `WIDGET_CUSTOM` in the
  Controls section draws the "Configure Controller" (`SohInputEditorWindow`) inline, reusing the
  issue #22 inline-window mechanism (get the registered `GuiWindow`; skip when `IsVisible()`/popped
  out to avoid double-draw; `Update()` then `DrawElement()`). No foreground gate — the input editor
  only touches the shared `ControlDeck`, not OOT play state.

One inline editor (OOT's) covers both games: controls are shared `gSettings.Controllers.*` CVars and
`MM_ReloadControls` reloads MM from them, and MM's Controls sidebar is hidden in the overlay (above),
so no MM-side change is needed. **On future merges:** if SoH renames the "Configure Controller"
window, update the name string here.

## MM starting items + OOT items in MM shops (issues #39 #40, 2026-07-01)

**Why:** The combo MM save is created headless by `MM_InitRandoSaveFile`, which stored starting
items but never granted them (#39). And `EnGirlA_RandoBuyFunc` granted shop items directly, bypassing
the `RI_COMBO_FOREIGN` cross-delivery that `CheckQueue` uses, so OOT items bought in MM shops were
never delivered or saved (#40).

**Vendored (`COMBO_BUILD`-guarded):**
- `mm/2s2h/BenPort.cpp` — `MM_InitRandoSaveFile` now calls `Rando::GrantStartingItems()` with
  `gPlayState` forced `NULL`, baking items into the save like native `OnFileCreate` (whose `Item_Give`
  null-guards make it headless-safe). The forced `NULL` defends against a stale eager-boot `gPlayState`.
- `mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp` + `MiscBehavior.h` — `Rando_SendForeignCheck` exposed as
  `Rando::MiscBehavior::SendForeignCheck` for reuse.
- `mm/2s2h/Rando/ActorBehavior/EnGirlA.cpp` — `EnGirlA_RandoBuyFunc` routes `RI_COMBO_FOREIGN` through
  `SendForeignCheck` (sets `obtained`+`cycleObtained`, skips local give).
- `mm/2s2h/Rando/ConvertItem.cpp` — `IsItemObtainable` gains a `RI_COMBO_FOREIGN` case
  (`!hasObtainedCheck`); without it foreign shop items stayed obtainable and restocked/re-delivered.

## MM shader assets synced to merged libultraship (OpenGL transition crash, 2026-07-02)

**Why:** The 2026-06-29 upstream merge moved the shared libultraship OpenGL backend to the single
prism shader (`shaders/opengl/default.shader.glsl`), but `mm/assets/custom/shaders/` still shipped
the pre-merge split `default.shader.fs`/`.vs`. Shader sources load from the ACTIVE game's
ResourceManager, so with MM active any new shader-variant compile missed in `2ship.o2r` and hit the
`abort()` in `gfx_opengl.cpp` ("Failed to load default fragment shader, missing f3d.o2r?") — every
OOT→MM transition crashed on the OpenGL backend. D3D11/Metal masked it (their filenames didn't
change), which is why Windows runs never saw it.

**Vendored (assets only, no code):** `mm/assets/custom/shaders/` replaced with byte-identical copies
of `libultraship/src/fast/shaders/` (add `.glsl`, drop dead `.fs`/`.vs`, refresh stale `.hlsl` /
`.metal`) — now matching `soh/assets/custom/shaders/`. Requires regenerating `2ship.o2r`
(`Generate2ShipOtr`). Rule for future merges: whenever the shared LUS shader sources change, both
ports' `assets/custom/shaders/` must be re-synced and their `.o2r` regenerated.

## Cosmetics editors: scope the owning game's RM (cross-game open crash, 2026-07-02)

**Why:** `Context::GetInstance()->GetResourceManager()` returns the FOREGROUND game's RM. Opening
MM's Cosmetic Editor from the combo menu while OOT is foreground made its palette patchers
(`ShadePaletteNewBase` etc.) `LoadResource` MM paths through OOT's RM → null → crash on
`GetRawPointer()` (mirror case for OOT's editor while MM is foreground). The
`combo/gui/ComboWidgetRender.h` design note places RM scoping GAME-SIDE in the menu exports, but it
was never implemented.

**Port code (BenPort.cpp / OTRGlobals.cpp):** the four menu exports per game
(`*_MenuInvokeCallback`, `*_MenuApplyCVarChange`, `*_MenuEvalDisabled`, `*_MenuDrawCustom`) now wrap
their body in `Ship::ResourceManagerScope(CrossRMRegistry::Get("mm"/"oot"))` — no-op when that game
is foreground or not yet registered. Covers every inline widget/window draw and CVar-change apply.

**Vendored (`COMBO_BUILD`-guarded, +22/-0 lines):** `mm/2s2h/BenGui/CosmeticEditor.cpp` and
`soh/soh/Enhancements/cosmetics/CosmeticsEditor.cpp` — same scope at the top of
`DrawElement()`. Needed because the POPOUT window is drawn by the shared Gui loop directly,
bypassing the menu exports.

## Audio resource loads pinned to the owning game's RM (audio-thread race, 2026-07-02)

**Why:** each game's audio thread loads fonts/sequences/samples through the swappable ACTIVE RM
(`ResourceGetDataByName` → `Context::GetResourceManager()`). Any `ResourceManagerScope` swap on
another thread (combo-menu exports above, foreign draws) races it: during MM Cosmetic Editor
"Randomize All" (long scope, global RM = MM's), OOT's audio thread resolved an OOT soundfont
against MM's RM → null → unguarded `sf->numDrums` crash in `Audio_GetDrum`
(`soh/src/code/audio_playback.c:366`). Audio semantically always wants its OWN game's assets, so
its loads now bypass the global via `CrossRMRegistry::GetOrActive("oot"/"mm")` — falls back to the
active RM pre-registration / non-combo. Game source (`audio_*.c`) untouched.

**libultraship (combo-owned):** `CrossRMRegistry` gains `GetOrActive()` and a `std::shared_mutex`
(Get() is now called from audio threads, not just the interpreter — the old no-mutex comment's
"add one when multi-threaded" condition triggered).

**Port code:** `soh/soh/ResourceManagerHelpers.cpp` + `mm/2s2h/BenPort.cpp` — the audio bridge fns
(`ResourceMgr_LoadSeqByName`/`LoadSeqPtrByName`/`LoadAudioSample`/`LoadAudioSoundFontByName`/
`...ByCRC`) load via the pinned RM (soh side behind a `COMBO_OWN_RM()` macro with an `#else`
preserving upstream behavior).

**Vendored (`COMBO_BUILD`-guarded):** both games' `resource/importer/AudioSoundFontFactory.cpp`
(nested sample loads, 11 sites each via the `COMBO_OWN_RM()` macro), `AudioSampleFactory.cpp` and
`AudioSequenceFactory.cpp` (one archive `LoadFile` each) — these factories run on the RM worker
pool and read the global mid-load, so they need the same pinning. `#else` keeps upstream lines.

**Residual (known, pre-existing):** non-audio factories (Skeleton/Animation/etc.) still nested-load
via the global — that's load-bearing for `ResourceManagerScope` (ComboForeignAnim's scoped foreign
loads rely on it). Their exposure is limited to async loads in flight during a scope; unchanged.

## ShipInit::Init scoped to the owning game's RM (HUD Editor preset crash, 2026-07-03)

**Why:** `ShipInit` init funcs include the cosmetic patchers (`ShadePaletteRevert` → `LoadResource`),
and they fire from UI paths outside the scoped menu exports — e.g. MM's POPOUT HUD Editor
(`HudEditor.cpp` preset/color handlers call `ShipInit::Init`), drawn by the shared Gui loop where
the ACTIVE RM is the foreground game's. Selecting a HUD preset while OOT was foreground loaded MM
palettes through OOT's RM → null → crash. Scoping `Init` itself fixes every caller at once instead
of scoping window-by-window.

**Vendored (`COMBO_BUILD`-guarded):** `mm/2s2h/ShipInit.hpp` + `soh/soh/ShipInit.hpp` —
`ShipInit::Init` pins the owning game's RM via `Ship::OwnRMScope` (no-op when that game is
foreground or pre-registration). `OwnRMScope` lives in `CrossRMRegistry` (out-of-line) because
including `ResourceManagerScope.h`/`Context.h` from these widely-included headers drags in SDL,
whose `#define main SDL_main` breaks `GameState::main` users (e.g. `CustomLogoTitle.cpp`). Makes
the `*_MenuApplyCVarChange` export scopes redundant but they stay as documentation of the
export-boundary rule.

## MM resume: reset magicLevel like Sram_OpenSave (magic meter outline, 2026-07-03)

**Why:** the combo resume shortcut (`title_setup.c` `gComboStartFileNum` block) loads the save
directly, skipping `Sram_OpenSave`'s post-load `magicLevel = 0` (z_sram_NES.c) that re-arms the
magic-meter grow animation. A save written mid-game stores `magicLevel` 1/2, so on every re-entry
the trigger (`Interface_Update`: `isMagicAcquired && magicLevel == 0`) never fired and runtime
`magicCapacity` stayed 0 — outline drawn at zero width while the fill showed correctly. First
entry was fine because it CREATES the save (default `magicLevel` 0).

**Vendored (inside the existing `COMBO_BUILD` block):** `mm/src/code/title_setup.c` — one line,
`magicLevel = 0` after the save load, mirroring `Sram_OpenSave`.

## Shared Item Tracker: master panel + hold-to-swap dormant peek (2026-07-04)

**Why:** with both games in one process the item tracker should be controllable from one place and
able to show the OTHER (dormant) game's inventory. A dormant game has no gPlayState/input, so its
tracker draw needs save-context-only gating and RM-pinned texture loads; MM's save must be
headless-loaded before its first visit so a peek shows real items instead of boot defaults.

**Combo-owned (comboui):** `combo/gui/ComboTrackerBridge.{h,cpp}` — canonical `gCombo.Tracker.*`
appearance (icon size, opacity, window type, draggable) mirrored into both games' CVars, seeded
from OOT on first run. `ComboTrackerCommon.h` — per-game window/CVar table + `SetTracker`.
`ComboTrackerSwap.{h,cpp}` — per-frame reconcile of `gCombo.Tracker.ShownGame` (-1 follow / 0 OOT /
1 MM), the click-hold gesture (drag cancels; `HoldMomentary` = peek), true-intent stash CVars for
crash recovery. Shared > Item Tracker panel in `ComboMenu.cpp`.

**Port code:** `soh/soh/OTRGlobals.cpp` — `SOH_SetOnLoadSaveCallback` (launcher mirrors the active
OOT slot for the MM-side load) + `Combo_OotIsForeground` helper; `mm/2s2h/BenPort.cpp` —
`MM_LoadSaveForCombo` (headless SaveManager load, same path as the resume shortcut); launcher
wiring in `combo/ComboShip.cpp`.

**Vendored (`COMBO_BUILD`-guarded):** `soh/.../randomizer_item_tracker.cpp` — dormant draw: OOT RM
scope, `gComboTrackerPeekSaveLoaded` signal, stale pause/combo-button override.
`soh/.../GameInteractor.cpp` — `IsSaveLoaded` judges by save context alone while the peek flag is
set. `mm/.../ItemTracker/ItemTracker.cpp` — MM RM scope, visibility-gate skip while dormant,
shared Draggable toggle, and a main-viewport pin (a window given its own viewport is force-rendered
opaque — showed as a black tracker background during the hold gesture).

## Shared Check Tracker: hold-to-swap peek + shared window type (2026-07-04)

**Why:** the same dormant-peek feature for the check tracker. Extra wrinkles vs the item tracker:
MM's tracker hard-requires `gPlayState`/`IS_RANDO`, its in-logic refresh throttles on the game
frame counter (frozen while dormant), a headless save load never fires `OnFileLoad` (scene-check
map stays empty), and the current-scene filter derefs `gPlayState`. MM also has no native
window-type CVar, so the shared Floating/Window switch is read directly from
`gCombo.CheckTracker.WindowType` in its seam (OOT's is mirrored by the bridge as usual).

**Combo-owned (comboui):** `ComboTrackerSwap.{h,cpp}` generalized to per-tracker instances (item +
check) with independent ShownGame/HoldMomentary/true-intent CVars; at most one tracker arms per
press (topmost wins on overlap); the logic window key gained a leading space (" Combo Tracker
Swap") so it sorts before BOTH tracker windows in the name-ordered Gui map. `ComboTrackerBridge`
mirrors the check window type into OOT. Shared > Check Tracker panel in `ComboMenu.cpp`
(visibility, window type, shown game, peek mode — colors/filters stay per-game).

**Vendored (`COMBO_BUILD`-guarded):** `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — MM RM scope +
visibility-gate skip while dormant; save-gate bypass with lazy `initializeSceneChecks`; ImGui-frame
throttle branch in `RefreshChecksInLogic` so availability refreshes during the peek; `gPlayState`
guard on the current-scene filter; main-viewport pin + window-type flags at `Begin` (original call
kept under `#else`). `soh/.../randomizer_check_tracker.cpp` — OOT RM scope +
`gComboTrackerPeekSaveLoaded` around `DrawElement`; display-gate skip (stale pause/combo-button
state while dormant).

## Foreign items: full get-item presentation + model coverage + spoiler names (issues #4 #2 #1, 2026-07-07)

**Why:** a foreign check (holding the OTHER game's item) used to divert BEFORE the get-item pipeline —
instant/silent, blue-rupee sentinel model, no held-up animation, and the consolidated spoiler listed
the sentinel name. Now a foreign item is presented in the collecting game like a native item (real
model, real name, held-up animation gated by the skip-animation setting), and only the grant is
diverted cross-game.

**Foreign-item importance carried across (drives the animation):** the per-game dumps now emit an
`advancement` flag per item (`soh/.../OTRGlobals.cpp` `SOH_DumpRandoStaticData` items array;
`mm/2s2h/BenPort.cpp` `MM_DumpRandoStaticData`). The combo generator maps it into the foreign array
(`combo/ComboShip.cpp`), and it rides in `ComboRando::ForeignItem::advancement`
(`combo/rando/CrossForeign.h`). KNOWN SIMPLIFICATION: importance is binary (advancement vs not), so a
foreign lesser/token/small-key over-animates vs its native 3-tier skip behavior — cosmetic only.

**OOT (`COMBO_BUILD`-guarded — preserve on merges):**
- `Enhancements/randomizer/item_list.cpp` — `RG_COMBO_FOREIGN` entry is now `MOD_RANDOMIZER` with
  `textId = TEXT_RANDOMIZER_CUSTOM_ITEM` so it flows through the normal get-item presentation + custom
  message (draw func already `Randomizer_DrawComboForeign`).
- `Enhancements/randomizer/hook_handlers.cpp` — `RandomizerOnPlayerUpdateForRCQueueHandler` no longer
  diverts foreign early; it overrides the get-item category by home-importance. `OOT_SendForeignCheck`
  replaced by `OOT_DeliverForeign(rc)` (cross-deliver + Anchor share + toast only; guarded against
  `RC_UNKNOWN_CHECK`), called at grant time. item00 guard tightened to genuinely-empty MOD_NONE.
- `Enhancements/randomizer/randomizer.cpp` — `Randomizer_Item_Give` intercepts `RG_COMBO_FOREIGN` at
  the top → `OOT_DeliverForeign(comboForeignCheck)`, no local grant. Single choke for both the held-up
  and dropped-collectible paths.
- `Enhancements/randomizer/Messages/ItemMessages.cpp` — `BuildComboForeignMessage` (foreign
  `displayName` in the get-item textbox).
- `Network/Anchor/HookHandlers.cpp` — `OnItemReceive` skips broadcasting `RG_COMBO_FOREIGN` (the real
  cross-item is shared via `OOT_DeliverForeign`'s `Anchor_BroadcastCrossItem`).
- `src/code/z_draw.c` — `GetItem_GetDrawTableEntry` exposes `GetItem_DrawSkullToken` (static body,
  animated flame dropped) so GS tokens render cross-game.

**MM (`COMBO_BUILD`-guarded — preserve on merges):**
- `2s2h/Rando/MiscBehavior/CheckQueue.cpp` — the foreign branch presents (name + get-item cutscene
  when important) then `SendForeignCheck`s instead of returning early; `ShouldShowForeignCutscene`
  helper; emplace `showGetItemCutscene` foreign override.
- `src/code/z_draw.c` — `GetItem_DrawSkullToken` static-body case (symmetry).

**Combo-owned (no merge risk):** `combo/menu/ComboItemDrawMM.h` — `MM_FillOwlDrawInfo` renders owl
statues via `gOwlStatueOpenedDL` (held-up position may need playtest tuning — no translate carried).
`combo/ComboShip.cpp` — consolidated spoiler `oot`/`mm` placement arrays show real foreign names
(apply payloads keep the sentinel).

**Playtest-pending:** MM songs cross-game render (env-color path looks correct statically); owl-statue
held-up position; foreign item landing on a starting check (Link's Pocket etc.) delivering at
save-init.

## Ending gated on both final bosses defeated (2026-07-07)

**Why:** OOT and MM each fired their own credits the instant their final boss died — but only one game
ticks at a time, so beating Ganon played OOT's full ending while Majora was still alive (and vice
versa). Now the ending plays only after BOTH are dead: the first kill plays its death cutscene then
warps the player back to the cross-game portal to finish the other game; the second kill lets that
game's native ending run as the finale.

**Combo-owned (`combo/ComboShip.cpp`, no merge risk):** `Combo_OnFinalBossDefeated(game, fileNum)`
records each kill in a per-slot sidecar (`Randomizer/save{N}-ComboCompletion.json`, `{oot,mm}` bools),
returns 1 iff both are dead. Loaded on OOT save-load (`Combo_OnOOTSaveLoad`) so it survives
quit/resume and MM's Song-of-Time cycles. Registered into both DLLs via the new setters below.

**Port seams (`COMBO_BUILD`-guarded — preserve on merges):**
- `soh/soh/OTRGlobals.cpp` — `gComboFinalBossDefeated` pointer + `SOH_SetFinalBossDefeatedCb` export.
- `mm/2s2h/BenPort.cpp` — `gComboFinalBossDefeated` pointer + `MM_SetFinalBossDefeatedCb` export.

**Vendored boss seams (`COMBO_BUILD`-guarded — preserve on merges, ~13 lines each):**
- `soh/src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.c` — death cutscene `case 20`: if not both
  dead, warp to `ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP` (child, no cutscene) instead of the Chamber
  of the Sages credits. Reuses the existing MM→OOT portal arrival point (see title_setup.c).
- `mm/src/overlays/actors/ovl_Boss_07/z_boss_07.c` — Majora's Wrath death: if not both dead, warp to
  `ENTRANCE(SOUTH_CLOCK_TOWN, 0)` (no cutscene) instead of the Termina Field `0xFFF7` credits.

**Deviation from plan:** the OOT first-kill warp targets the Happy Mask Shop area (not Temple of Time)
because the OOT→MM portal is the Happy Mask Shop, only reachable as child in the Market — adult Link at
Temple of Time couldn't reach it.

**Playtest-pending:** both orders (Ganon-first and Majora-first); portal reachable after each warp;
resume-after-first-kill keeps the flag; finale plays on the second kill.

---

## Headless rando playthrough validator (`comborando --playthrough`)

`comborando` (own `EXCLUDE_FROM_ALL` target) forward-simulates a finished cross-world seed to judge
beatability with an exact item-by-item sphere trace, so a seed's completability (and, when stuck, the
exact reason) can be verified headless. Traversal lives in `combo/rando/ComboPlaythrough.h`
(`ComboRando::RunPlaythrough`, shared with the in-game generator).

**Port seams (`COMBO_BUILD`-guarded — preserve on merges):**
- `soh/soh/Enhancements/Lang/Lang.cpp` — `Lang::Translate` returns the raw key instead of asserting when
  language data isn't loaded, **gated on `gComboHeadlessRando`** (set only by `SOH_InitRandoHeadless`,
  never the game). Lets the headless option/trick tables build without the ResourceManager/assets. In-game
  the flag is false → the assert is unchanged (byte-identical behavior).
- `soh/soh/OTRGlobals.cpp` — `gComboHeadlessRando` flag + `Rando::Settings::CreateOptions()` in
  `SOH_InitRandoHeadless` (wires RSK CVar names so a spoiler's settings reach the Context headless).

**Tricks honored by fill + oracle (`soh/soh/OTRGlobals.cpp`):** the player's enabled tricks live in the
`EnabledTricks` CVar (CSV of stable NameTags, written by the rando menu); nothing pushed them into the
Context, so `SetAllToContext` left every trick off — the cross-world **fill** and the reachability oracle
both ran trick-less. `Combo_ApplyEnabledTricks()` now applies that CVar to `ctx->GetTrickOption` after every
`SetAllToContext` (in `SOH_PrepRandoContext` + `EnsureOracleInit`), so a seed generated with tricks enabled
is generated *and* validated with them. Exports `SOH_DumpEnabledTricks` / `SOH_SetEnabledTricks` /
`SOH_SetAllTricks` drive it for the validator; the consolidated spoiler carries `oot.enabledTricks`.
NOTE: this changes generation — seeds made with tricks on become trick-dependent (intended).

**Combo-owned oracle fix (MM dump):**
- `mm/2s2h/BenPort.cpp` `MM_DumpRandoStaticData` — when boss remains aren't shuffled, `GeneratePools`
  drops `RCTYPE_REMAINS` checks, so the four Remains never reach the oracle even though Moon/Majora access
  gates on `RemainsCount()`. Emit each non-shuffled Remains as a `fixed` placement of its vanilla item
  (credited when its boss-warp check is reachable). Mirrors the OOT vanilla-shop Deku Shield fix.

**Follow-ups (not done):** the in-game apply of the new Remains fixed-placements isn't playtested
(comborando doesn't apply placements); the port-touching seams aren't runtime-verified in-game; naming the
exact trick that unblocks Pass 2 (vs. the blocking location) would need bisection.

## MM save init: sariaPriorityItems required by upstream Saria's-Song hint (2026-07-14)

The 2026-07-13 upstream merge added the Saria's-Song-hint feature; `Rando::Spoiler::ApplyToSaveContext`
now hard-reads `spoiler["sariaPriorityItems"]` (SariasSongHint.cpp). ComboShip's `MM_InitRandoSaveFile`
(`mm/2s2h/BenPort.cpp`) builds a synthetic spoiler that lacked the key → `type_error.302` → every combo
rando save fell back to a vanilla MM save. Fixed by supplying an empty array (combo seeds carry no MM
hint priorities; cross-game hints are a future feature).

## Cherry-pick: LUS PR #1121 — round interpolated texture tile sizes (2026-07-14)

`libultraship/src/fast/interpreter.cpp`: cherry-picked unmerged upstream PR
Kenix3/libultraship#1121 (commit `c66cebe2f`, fixes #1119 / Shipwright#6666). Interpolated
float tile coords truncated to int made the rendered texture window alternate 32×32/32×31
across interpolation phases → animated water/lava flicker above 20 FPS. New
`GetTileSizeFromCoordinates()` rounds via `lroundf`. Drop this local copy once the PR lands
upstream and the pin passes it.

## MM rando save-init strips + combo-return fixes (2026-07-14)

**Why:** Combo MM rando saves started with the vanilla Kokiri Sword / Hero's Shield and the combo
baseline's force-granted Magic — `MM_InitRandoSaveFile` mirrored native `OnFileCreate` but missed
its "Remove Sword & Shield" step, and never cleared the baseline's `isMagicAcquired`. Separately,
the MM→OOT return crashed (UAF in `DungeonInfo::IsVanilla`) and the moon crash kicked the player
back to OOT instead of restarting the MM cycle.

**Vendored (`COMBO_BUILD`-guarded):**
- `mm/2s2h/BenPort.cpp` — `MM_InitRandoSaveFile` strips sword/shield equip values and
  `isMagicAcquired` alongside the existing Ocarina/Deku-Mask/songs strip; the MM→OOT portal
  trigger now requires `spawnNum == 1` (the South Clock Town door) so cycle resets (moon crash /
  Song of Time respawn at spawns 0/2/3/6 in `SCENE_INSIDETOWER`) stay in MM.
- `soh/src/code/title_setup.c` — the combo-return jump fires `GameInteractor_ExecuteOnLoadGame`
  after `Sram_OpenSave` like `FileChoose_LoadGame` does; `Save_LoadFile` recreates `gRandoContext`,
  and without the hook the check tracker's region-table `ctx` dangled → UAF on the next recalc.

## Both-games tracker model (2026-07-14)

**Why:** Trackers were foreground-follow with a per-tracker game-swap; the user wants both games'
trackers visible together, one master enable, and a single customization home in the Combo menu.
The combo layer carries the model (`ComboTrackerSwap` rewritten to a per-frame reconcile of derived
per-game CVars: master enable + per-kind HideBackground + hold-to-peek; `ComboTrackerVisibility`
now follows only the settings popouts; both Shared tracker panels inline the games' own settings
sidebars with a divider). The old ShownGame/TrueIntent/HoldMomentary CVars are migrated + cleared
on boot.

**Vendored (`COMBO_BUILD`-guarded, additive):**
- `mm/2s2h/Enhancements/Trackers/ItemTracker/ItemTracker.cpp` + `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp`
  — MM's trackers `Begin()` distinct ImGui identities (`"Item Tracker##MM"`, split-group `##<n>MM`,
  `"Check Tracker##MM"`) so both games' windows can exist simultaneously with their own rects
  (previously both games drew into the SAME ImGui window; only the Gui-map key carried `##MM`).
- `mm/2s2h/Rando/Menu.cpp` — "Item/Check Tracker Settings Inline" `WIDGET_CUSTOM` widgets (the
  SoH issue-#22 inline-window mechanism) so the Shared panels can embed MM's settings; skipped
  while popped out.
- `mm/2s2h/Enhancements/Trackers/ItemTracker/ItemTrackerSettings.cpp` +
  `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — the Enable/Disable window buttons inside the
  settings content are hidden in combo builds (`gWindows.ItemTracker`/`.CheckTracker` are derived
  from the combo master toggle every frame; the buttons would fight it).

**On future merges:** if upstream renames the tracker ImGui windows or the settings windows,
update `combo/gui/ComboTrackerCommon.h` (`kKinds[].imguiWin`) and the inline-widget window names.

## Cross-game hints (closes GAP-2/GAP-3, 4 phases, 2026-07-14/15)

**Why:** native `CreateAllHints`/`CreateWarpSongTexts`/`PareDownPlaythrough` never ran for combo
seeds (GAP-3's interim was forcing hint settings off, vanilla NPC text). This feature runs a
combo-owned equivalent (`combo/rando/CrossHints.h::Generate`, Phase 3) after the pare-down
(`ComboPlaythrough.h`, Phase 3) and wires both games to *display* its pre-rendered output — no
runtime lookups on either game's side, since only the combo layer sees both worlds.

**Phase 1 (bug fixes preceding the feature):**
- `mm/2s2h/Rando/Rando.cpp`/`.h` — new `GetItemLocationHintName(randoItemId, exact)`: resolves a
  hint's location whether the item lives in an MM check or was cross-placed into OOT (family-B),
  replacing ad hoc `FindItemPlacement` + `GetLocationNameForHint` call pairs at 6 call sites
  (`DmStk.cpp`, `EnKgy.cpp`×2, `EnTimeTag.cpp`, `EnTalk.cpp`×2, `EnZow.cpp`) that broke for
  cross-placed items (no `RandoCheckId` to find).
- `mm/2s2h/BenPort.cpp` — dump additions feeding `GetItemLocationHintName`'s and CrossHints's data
  needs (locationHints/weightClass — see Phase 2).
- `soh/soh/OTRGlobals.cpp` — hint dump + apply-time hookup for the combo hint layer.

**Phase 2 (schema/data exports):**
- `soh/soh/Enhancements/randomizer/3drando/hints.cpp`/`.hpp` — `GetAlwaysHintCandidates()` (resolved
  always-hint check list) and per-piece `CreateChildAltarHint()`/`CreateAdultAltarHint()` exposed
  (combo owns hint distribution separately from `CreateStaticHints()`'s bundle).
- `soh/soh/Enhancements/randomizer/Messages/StaticHints.cpp` — skulltula reward + 100-skulls hint
  text now check `RG_COMBO_FOREIGN` and substitute the real cross-placed item's display name via
  `OOT_LookupForeign` (previously showed the sentinel's own placeholder hint).
- `soh/soh/OTRGlobals.cpp` — `SOH_DumpRandoHintData` (checks/items/hintTextTable/requiredTrials
  schema `CrossHints.h` consumes).

**Phase 3 (generation + OOT injection):**
- `combo/rando/CrossHints.h` (new) — `ComboRando::Generate`: seeded (`masterSeed ^ 0x48494E54`)
  weighted hint distribution mirroring `hintSettingTable`, drawing candidates from both games'
  dumps with no world bias; outputs `{oot: [...], mm: {gossipPool, itemLocations}, stats}`.
  Superseded the ComboMenu-owned sphere-hint panel (removed from `combo/gui/ComboMenu.cpp`/`.h`).
- `combo/rando/ComboPlaythrough.h` — `RequirednessResult`/pare-down parsing feeding WotH/Foolish
  hint categories (closes GAP-2).
- `combo/ComboShip.cpp` — `SOH_ApplyComboHints` call after OOT placement apply (generate + reload
  paths).
- `soh/soh/OTRGlobals.cpp` — `SOH_ApplyComboHints` applies the consolidated `hints.oot[]` array as
  real `Rando::Hint` MESSAGE-type entries (gossip stones, trials, Ganondorf).
- `soh/soh/SaveManager.cpp` — combo MESSAGE hints round-trip all 3 languages (`comboMessagesEn/De/Fr`)
  since the native per-hint save schema is current-language-only.

**Phase 4 (MM gossip stones + Family-B upgrade + docs, this phase):**
- `mm/2s2h/Rando/ActorBehavior/EnGs.cpp` — `GetRandomCheck` folds `hints.mm.gossipPool` entries
  (loaded lazily per save-slot, cached like `MM_LookupForeign`) into the SAME weighted draw via the
  existing `100 + (w-1)*strength` formula — one RNG source, no bias. A cross entry has no
  `RandoCheckId`; it's returned via a new `outForeignText` out-param the caller displays directly,
  short-circuiting the native item/location lookup. Excluded from the purchasable-repeat pool
  (`repeatableOnlyObtained`) since MM can't see OOT's obtained-state.
- `mm/2s2h/Rando/Rando.cpp` — `GetItemLocationHintName`'s family-B path tries `hints.mm.itemLocations`
  (Phase-3 region-rendered text) first, falling back to Phase 1's raw check-name string for seeds
  generated before the hints object existed.
- `combo/rando/CrossForeign.h` — `MmHints`/`LoadHintsMM(slot)`: per-slot lazy loader for the
  consolidated file's `hints.mm` object, mirroring `LoadForeignForGame`'s never-throws contract.

**Code-review fixes (2026-07-15):**
- `soh/soh/OTRGlobals.cpp` — `SOH_DumpRandoHintData`'s `dump()` moved inside the try + uses
  `error_handler_t::replace`, so malformed UTF-8 in authored hint text can no longer throw
  `type_error.316` across the extern "C" boundary.
- `combo/rando/CrossHints.h` — native "Always"-hint checks (Big Poes, Mask Shop, frogs, skull-reward
  counts, etc) are now actually distributed: `Preset` gained `alwaysCopies` (mirroring
  `hintSettingTable`'s 0/1/2/2), and `Generate` places one hint per exported `alwaysHintChecks` entry
  (× copies) before the weighted loop, using the same location+item composition as the other
  categories. Previously these were exported but never consumed, so native always-hints never landed
  on a gossip stone.

**Known v1 limitations (documented, not bugs):** trial/gossip text for cross entries is English-only
(no translation source); MM can't exclude an already-obtained OOT item from its own gossip pool
(only its own-game repeat-hint pool is protected); Ganondorf's combined-hint phrasing variant isn't
mirrored.

**Settings-persistence fix (2026-07-16):** the silent file-select auto-reload
(`Combo_OnReloadRequest(NULL)`) was writing the pending seed's `gRando.*` CVars over the user's
configured settings, which then leaked into `comboship.json`. Fix, all in `combo/ComboShip.cpp`:
- OOT: snapshot the user's settings (`SOH_DumpRandoSettings`) before the seed's are restored for
  reproduction, then restore the snapshot right after `SOH_ApplyRandoPlacements`/hints — OOT only
  reads settings CVars at that prep step, never again during play.
- MM: `MM_RestoreRandoSettings(mmSettings)` no longer runs at reload time. The seed's MM settings are
  stashed (`g_PendingMMSettingsJson`) and applied in `Combo_OnOOTSaveInit`, immediately before
  `MM_InitRandoSaveFile` (the only place MM reads them), then the user's snapshot
  (`g_UserMMSettingsSnapshot`) is restored right after.
- An explicit dropped-file load (non-null path) is a deliberate seed switch: its settings are left in
  place instead of being restored back (`g_ComboReloadRestoreUserMM`).

**Settings-persistence review follow-up (2026-07-16):** `Combo_FinalizeGenerate` (a fresh in-game
generate, not a reload) now clears `g_PendingMMSettingsJson`/`g_UserMMSettingsSnapshot`/
`g_ComboReloadRestoreUserMM` — a stale pending-seed's MM settings were otherwise left to apply at the
next slot-bind over the freshly generated seed's placements. An explicit drop also applies its MM
settings to CVars immediately (not just at slot-bind), matching OOT's immediate baseline switch, so
quit-before-Start can't persist a mixed OOT=seed/MM=old-user `comboship.json`.

Known limitation (not fixed — see `randomizer_check_objects.cpp` `UpdateImGuiVisibility`, called from
`SohMenuRandomizer.cpp`): it reads ~67 `CVAR_RANDOMIZER_SETTING(...)` CVars directly rather than
`gRandoContext->GetOption()`. During a combo session the OOT Randomizer settings menu shows (and this
function reacts to) the user's live config, not the loaded seed's — opening that menu mid-session can
compute check-tracker visibility against the wrong option set. Rewriting ~67 vendored reads to go
through the rando context was judged too large/risky for this fix; left as a documented gap.

## Anchor auto-reconnect on boot restored (2026-07-16)

**Why:** `Combo_FinishInit` (OTRGlobals.cpp) had a `COMBO_BUILD` branch that `CVarClear`ed
`gAnchor.Enabled` on every boot ("Anchor always starts DISABLED") instead of auto-connecting, so
Anchor stayed disconnected after a restart. That predated the launcher wiring the Anchor connect
transport before `SOH_Init`; with the transport now registered first, boot-time `Enable()` opens a
real socket rather than wedging on "Connecting…". Dropped the combo-only branch so boot auto-connects
from the persisted `gAnchor.Enabled` flag, matching upstream SoH (a deviation removed, not added).

## Anchor co-op sync hardening, bug 3: MM time-travel duplicate grants (2026-07-16)

**Why:** MM's `RandoSaveCheck` has two flags: `cycleObtained` (wiped every Song of Time,
`OnCycleSave.cpp`) and `obtained` (permanent). Co-op broadcast and cross-game delivery were driven by
the give-lambda running again each cycle, re-sharing/re-delivering an already-permanent check.

Fixes, all `COMBO_BUILD`:
- `CheckQueue.cpp`: capture `obtained` BEFORE the grant; only call `MMAnchor_BroadcastCheckItem` /
  `SendForeignCheck` (cross-deliver) the first time a check becomes permanently obtained. Local grant
  (`Rando::GiveItem`) is untouched — renewables still re-give locally, only re-SHARING is suppressed.
- `gComboCrossDeliver`/`gMMComboCrossDeliver` gained a `srcCheckName` parameter. The launcher's
  `DeliverCrossItem` (`combo/ComboShip.cpp`) dedups on it: the same wire `COMBO_CROSS_ITEM` packet
  reaches both DLLs' queues (an explicit `originGame` filter exception), so whichever games later
  process their own copy could each independently deliver — one shared in-memory set closes that.
- `MMAnchor::HandlePacket_UpdateTeamState` / OOT's `UpdateTeamState.cpp`: resync now unions rather than
  replaces permanent progress — MM snapshots local `obtained` flags before the wholesale
  `shipSaveInfo` assignment and restores any the incoming state lacked; OOT only advances
  `RandomizerCheckStatus` (progressive enum) instead of unconditionally overwriting it, so a
  stale/incomplete peer's resync can't un-collect a check.

## Anchor co-op sync hardening, bug 1: MM shop buys never broadcast (2026-07-16)

**Why:** MM broadcast co-op progress only from `CheckQueue.cpp` (the physical rando check path);
Bomb/Curiosity shop buys grant directly through `EnGirlA_RandoBuyFunc` (`EnGirlA.cpp`, `EnFsn.cpp`
just forwards to the same `buyFunc`) without ever calling `MMAnchor_BroadcastCheckItem`, so shop
purchases never reached teammates.

Fix: factored the bug-3 first-time-obtained broadcast guard into a shared seam,
`Rando::MiscBehavior::BroadcastCheckObtainedIfFirst` (`MiscBehavior.h`/`CheckQueue.cpp`), and wired
`EnGirlA_RandoBuyFunc` to call it (both the normal buy and the OOT-bound foreign-item buy branch,
which gets the same wasObtained guard as `CheckQueue.cpp`'s foreign path). `CheckQueue.cpp`'s own
broadcast call now goes through the same seam instead of calling `MMAnchor_BroadcastCheckItem`
directly, so future MM grant paths have one shared, idempotent broadcast point to hook into.

## Anchor co-op sync hardening, bug 2: launcher-owned both-games resync (2026-07-16)

**Why:** the resync button was OOT-only and foreground-only (`soh/soh/Network/Anchor/Menu.cpp:132`);
`REQUEST_TEAM_STATE`'s dormant answer path was broken in both games (OOT's `PumpDormant` REQUEST
branch didn't set `isDormantApply` like its `GIVE_ITEM` branch did, so `IsSaveLoaded()` always failed;
MM's `SendTeamStateFromSave` gated on `IsSaveLoaded()`, which requires `gPlayState` — always null while
MM is dormant); and nothing let a dormant sibling itself REQUEST a resync (MM's
`SendPacket_RequestTeamState` is `isActive`-gated).

Fixes, all `COMBO_BUILD`:
- `Anchor::PumpDormant` (`soh/soh/Network/Anchor/Anchor.cpp`) now wraps the `REQUEST_TEAM_STATE`
  branch in `isDormantApply` like the `GIVE_ITEM` branch already did.
- `MMAnchor::SendTeamStateFromSave` (`mm/2s2h/Network/Anchor/MMAnchor.cpp`) now judges by
  `gSaveContext.fileNum` instead of `IsSaveLoaded()`, so it answers even while MM is dormant.
- New dormant-safe request seam per game: `Anchor::RequestResyncDormantSafe()` /
  `MMAnchor::RequestResyncDormantSafe()`, exported as `SOH_Anchor_RequestResync()` /
  `MM_Anchor_RequestResync()`. MM's bypasses `SendJson`'s `isActive` gate (constructs+sends the
  `REQUEST_TEAM_STATE` JSON directly) since the whole point is a dormant MM asking for a resync too.
- Launcher orchestration (`combo/ComboShip.cpp`): both exports are called, unconditionally, on every
  (re)connect — a late-joiner/reconnect resync now pulls a peer's OOT AND MM progress, and this
  client's own dormant sibling gets asked too. Per-game `originGame` packet isolation is untouched;
  orchestration happens at the launcher, not inside either game's filter.
- Manual control: a "Resync team state" button in the combo-owned Shared > Settings > Network panel
  (`combo/gui/ComboMenu.cpp`), resolving both exports the same way the existing combo-gen syms are
  resolved (`GetModuleHandleA`/`GetProcAddress` — comboui.dll has no other way to call into the game
  DLLs) and calling both. This is NOT the full "Ship of Harkinian -> Network settings" migration to
  combo-owned UI (separate follow-up) — just the resync control. The existing OOT Menu.cpp button is
  unchanged and still works.

## Anchor co-op sync: code-review fixes on bugs 1-3 (2026-07-16)

**Why:** review of the above three entries found the bug-3 union was incomplete (MM still lost
permanent progress on resync) and three smaller issues in the bug-2 plumbing.

Fixes, all `COMBO_BUILD`:
- `MMAnchor::HandlePacket_UpdateTeamState` (`mm/2s2h/Network/Anchor/MMAnchor.cpp`): the bug-3 union
  only covered `RANDO_SAVE_CHECKS[i].obtained`; the wholesale `saveInfo`/`shipSaveInfo` assignment
  still let a stale peer erase `weekEventReg`, owned masks, quest items, upgrade tiers, and heart
  containers. Now snapshots those before the assignment and OR/max-merges them back in: `weekEventReg`
  (byte-wise OR, MM's analog of OOT's `eventChkInf`), `inventory.items[24..47]` (mask ownership slots,
  restore-if-local-non-empty), `inventory.questItems` (OR), `inventory.upgrades` (per-field max via
  `gUpgradeMasks`/`gUpgradeShifts`), `playerData.healthCapacity` (max).
- `soh/soh/Network/Anchor/Packets/UpdateTeamState.cpp`: `SetIsSkipped` was unconditional next to the
  now-progressive `SetCheckStatus`; a stale peer with `isSkipped=false` could un-skip a local skip. Now
  only applies the incoming skip when it's `true` and local isn't already.
- `SOH_Anchor_RequestResync`/`MM_Anchor_RequestResync` (`OTRGlobals.cpp`/`MMAnchor.cpp`): wrapped in
  try/catch — both call into JSON/CVar code with no prior guard, and are `extern "C"` exports the
  launcher calls, so a throw would have unwound across the DLL boundary.
- `combo/ComboShip.cpp`: the auto-resync-on-connect call moved off the network `ReceiveLoop` thread. It
  now sets an `std::atomic<bool> sResyncPending` flag; the existing per-frame `PumpDormant` (already
  running on the active game's thread) drains it once and fires both resync exports there, avoiding a
  race with `PumpDormant`'s own `isDormantApply`/`gPlayState` use. Also scoped the cross-item dedup set
  (`sAppliedCrossChecks`) to the active seed — cleared via `ResetCrossItemDedupForSeed` whenever
  `masterSeed` changes (regen or reload-from-file), so a check name reused across seeds isn't dropped
  as a false duplicate. `ResetCrossItemDedupForSeed` runs on the generation worker thread while
  `DeliverCrossItem` runs on the game thread, so both now take `sAppliedCrossChecksMutex`.

## Cross-hint playtest fixes: color, dump size, altar (2026-07-16)

**Why:** playtest of the cross-hints feature found 3 issues: hint text displayed with no color,
Debug seed-gen was slow due to a ~2MB hint-schema JSON dump per fill, and the altar hint showed a
literal `[[3]]`/`[[N]]` for any dungeon reward cross-placed into MM.

**Fix 1 (color lost):** `soh/soh/OTRGlobals.cpp`'s `Combo_CustomMessageToJson` exported hint text with
`MF_RAW`, which never runs `EncodeColors` — the native `colors` vector (never itself serialized) was
silently dropped, so the reconstructed `CustomMessage` on the combo side had no colors and rendered
plain. Switched to `MF_ENCODE`, which bakes colors into `%g`/`%w`-style escapes while the vector is
still attached and leaves `[[N]]`/`&`/`^`/`|sing|plur|` untouched, so combo's substitution and the
existing display path are unaffected.

**Fix 2 (perf, partial — safe wins only, per explicit scope):**
- `soh/soh/OTRGlobals.cpp`'s `SOH_DumpRandoHintData`: `hintTextTable` trimmed from all ~1646 `RHT_*`
  entries to `Combo_IsUsedHintTemplate`'s allowlist (the WotH/Foolish/CanBeFoundAt/Hoards/Ganondorf/
  junk/altar + option-driven end-clause templates `CrossHints.h` can actually emit). Checks/items
  dumps were NOT trimmed: an attempt to filter them to the seed's placed set (`checks[]`/`items[]`
  restricted via a caller-supplied filter) caused a reproducible crash in headless verification and
  was reverted — flagged as a follow-up, not shipped. Net effect: ~2.05MB -> ~1.53MB dump (seed 1).
- `combo/ComboShip.cpp`: `buildOotCheckAreas(sohHintDump)` was re-parsed twice (once for the pare-down
  call, once for the foreign-array enrichment after the fill loop) — now parsed once into
  `ootCheckAreasCache` and reused. `Combo_FinalizeGenerate`'s `ComboHintsPresentInJson`/
  `ComboHintsJsonFrom` both re-parsed the whole consolidated spoiler just to check/extract the
  `hints` field — merged into one `ComboHintsJsonFrom` that returns the parsed sub-object directly.
- `combo/rando/CrossHints.h`'s `NeedsRequirednessPareDown`: also skips the pare-down when
  `hintDistribution` is 0 ("Useless" preset — no WotH/Foolish category at all), not just when gossip
  stones are off; conservative for every other distribution (WotH/Foolish always nonzero there).

**Fix 3 (altar `[[N]]` literal):** native `CreateChildAltarHint`/`CreateAdultAltarHint`
(`3drando/hints.cpp`) resolve reward locations via `FindItemsAndMarkHinted`, which only searches
`ctx->allLocations` (OOT's own checks) — a reward cross-placed into MM comes back `RC_UNKNOWN_CHECK`
and is skipped, leaving `InsertNames` with fewer areas than template slots.
- `soh/soh/OTRGlobals.cpp`: `SOH_ApplyRandoPlacements` now skips its own
  `CreateChildAltarHint()`/`CreateAdultAltarHint()` calls when `sComboHintsPresent` (combo supplies
  the altar hint instead, via `SOH_ApplyComboHints`'s new `"__ALTAR_CHILD__"`/`"__ALTAR_ADULT__"`
  sentinels -> `RH_ALTAR_CHILD`/`RH_ALTAR_ADULT`, added as `HINT_TYPE_MESSAGE`); `CreateStaticHints()`
  (called at the end of `SOH_ApplyComboHints`) self-skips the already-enabled key, so native never
  overwrites combo's version. Back-compat (no combo hints payload) is unaffected — those two calls
  still run as before.
  `SOH_DumpRandoHintData`'s options now also resolve the exact end-clause template key + count for
  each option family (bridge/Ganon's-boss-key/Ganon's-soul/win-condition + door-of-time), mirroring
  `hint.cpp`'s `GetBridgeReqsText`/`GetGanonBossKeyText`/`GetGanonsSoulText`/`GetWinconText`/altar
  door-of-time branch exactly (same `Is()` checks) — the combo side gets a template NAME + count, not
  an enum ordinal to reinterpret, so there's no ordinal-drift risk if the enums change.
- `combo/rando/CrossHints.h`: composes both altar hints from `RHT_CHILD_ALTAR_STONES`/
  `RHT_ADULT_ALTAR_MEDALLIONS`, resolving each reward (`Kokiri's Emerald`/`Goron's Ruby`/
  `Zora's Sapphire`/5 medallions + Light Medallion) by scanning the FULL placement list (not the
  advancement-filtered candidate list — a reward's advancement stamp isn't guaranteed reliable) for
  an OOT-owned item of that name, then resolving its check's area via `ootChecks`/`mmLocationHints`
  regardless of which game holds the check. Appends the resolved end clauses (door-of-time for child;
  bridge+GBK+soul+wincon+text-end for adult), replicating `InsertNumber`'s `|singular|plural|`+`[[d]]`
  substitution. Only emitted when `totAltarHint` is on (matches native gating; off leaves the earlier
  "No Hint" fix's behavior untouched).
  **Known residual gap:** one dungeon-reward item occasionally isn't found in the placement list at
  all for a given seed (pre-existing fill/dump completeness gap, not something introduced by this
  composition) — degrades to "an unknown place" for that one slot rather than crashing or leaving a
  literal `[[N]]`; needs its own investigation, out of scope here.

**Verified:** all 4 targets (soh/2ship/ComboShip/comborando) build clean; headless
`comborando.exe --seed <n>` run repeatedly (multiple seeds, 3x each) with no crash; same seed run
twice produces byte-identical `hints` and placements (determinism preserved); consolidated spoiler's
`hints.oot[]` altar entries contain `%`-color codes and every `[[N]]` slot filled (no literal
placeholder) except the one known residual gap above.

## Native barren predicate: major-item signal (2026-07-16)

**Why:** Native (`fill.cpp CalculateBarren`) marks a region barren iff it has NO WotH item AND
NO major item (`Item::IsMajorItem`, `item.cpp`). ComboShip's cross-hint rollup had only a WotH
signal (`areaHasRequired`), so it over-marked barren: a region holding a major-but-not-required
item (e.g. a second progressive copy) was wrongly foolish.

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`, COMBO_BUILD pool/fixed):** each `pool[]`
and `fixed[]` entry now also emits `"major": RetrieveItem(rg).IsMajorItem()` beside `advancement`.
`IsMajorItem` reads the live Context options, same as `IsAdvancement`, so it's valid during the dump.

**MM:** no `IsMajorItem` equivalent; `MM_DumpRandoStaticData` is unchanged and emits no `major`
flag. `ParseSpoilerPlacements` falls back to `major = advancement` when the flag is absent, so MM
placements treat every advancement item as major (conservative — never over-marks barren).

**`combo/rando/ComboPlaythrough.h`:** `CwPlacedItem` gains `major`; `ParseSpoilerPlacements` loads
`majorByName` from the dump (fallback to advancement) and stamps each placement.
**`combo/rando/CrossHints.h`:** a region enters the foolish pool only if it has no WotH item AND
no major item (`areaHasMajor`). Deliberately produces fewer barren regions than before (native parity).

**If future upstream touches `Item::IsMajorItem`:** re-check the dump flag and the barren derivation.

## OOT hearts as junk in the combo fill (2026-07-17)

**Why:** MM already dumps hearts as non-advancement (`BenPort.cpp` `isAdvancement` skips
`RITYPE_HEALTH`). OOT's `IsAdvancement()` marks Piece of Heart / Heart Container / Treasure-Game
Heart as advancement, bloating the OOT advancement pool the cross-fill must place reachably. Hearts
are never logic-required under glitchless, so treating them as junk shrinks dead-ends. Only caveat:
high `RSK_DAMAGE_MULTIPLIER` (8x/16x) — conservative, matches MM, never a softlock.

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`):** a local `comboIsAdv(rg)` returns false for
`RG_PIECE_OF_HEART`/`RG_HEART_CONTAINER`/`RG_TREASURE_GAME_HEART`, else `IsAdvancement()`. Used at
every advancement emit site (pool, fixed, fallback, items). `item_list.cpp`/`IsAdvancement()` is NOT
touched, so native single-game SoH is unchanged.

## Honor OOT logic/accessibility settings in the combo fill (2026-07-17)

**Why:** The cross-fill ignored OOT's `RSK_LOGIC_RULES` and `RSK_ALL_LOCATIONS_REACHABLE` — it always
ran an all-reachable assumed fill. Native OOT relaxes: No Logic fast-fills everything; ALR-off places
with logic only until beatable. The combo fill now honors these **per-game**: OOT relaxes, MM always
stays all-reachable (portal is ungated at runtime, so MM reachability must never degrade).

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`):** the dump gains an `"accessibility"` block
(`noLogic`, `allLocationsReachable`, `lockOverworldDoors`, `forceMaskShopKey`) read from the live
Context. Defaults (ALL_REACHABLE) if the prep throws.

**`combo/rando/CrossWorldRando.h`:** `enum class OotAccess { ALL_REACHABLE, BEATABLE_ONLY, NO_LOGIC }`
+ `OotAccessFromDump` (No Logic wins; else ALR-off => BEATABLE_ONLY). `CrossWorldCombinedFill` takes a
defaulted `OotAccess` param. Per mode:
- **ALL_REACHABLE:** unchanged; `toPlace = advItems` unreordered so a fixed seed is bit-identical.
- **NO_LOGIC:** assumed-fill only MM advancement; OOT advancement rides the junk fast-fill.
- **BEATABLE_ONLY:** assumed-fill the full set, but a single dead-ended OOT item is stranded to the
  junk fast-fill (MM dead-ends still retry the pass).
Validation classifies unreachable advancement by **item-game**: MM unreachable is always fatal/retry;
OOT unreachable is fatal only under ALL_REACHABLE, tolerated (logged) under relaxed modes. BEATABLE_ONLY
additionally requires the win still holds (`reachableFixpoint` now also returns the final `ootOwned`).

**Portal-key guard:** with Overworld Keys ON + Force Mask Shop Key OFF under a relaxed mode and
`portalCheckName=""`, the fill can't guarantee the Mask Shop Key's reach-path — it warns loudly. The
intended combo config is Force ON (key locked at a sphere-0 KF check → exempt from relaxation) or
Overworld Keys OFF. **Future portal:** when a real gate is wired (`portalCheckName` at
`combo/ComboShip.cpp`), treat the Mask Shop Key + reach prerequisites as MM-advancement-equivalent
(exempt from OOT relaxation) or hard-fail NO_LOGIC — TODO left at the call site.

**`combo/rando/ComboPlaythrough.h`:** `MmOnlyMajoraGoal` — under NO_LOGIC the pare-down (WotH) gates
requiredness on MM only, since OOT may be structurally unbeatable from empty. Wired at both
`PareDownPlaythrough` call sites (`ComboShip.cpp`, `ComboRandoHeadless.cpp`).

**`combo/ComboRandoHeadless.cpp` (`--playthrough` verdict):** a No Logic seed that is OOT/Ganon
unbeatable but keeps MM fully reachable + Majora beatable is downgraded from FAIL to PASS (No Logic).

**Hearts:** see the preceding "OOT hearts as junk" section — shrinks the OOT advancement pool.
