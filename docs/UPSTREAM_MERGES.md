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

## Running a merge pass (tooling)

The deterministic plumbing is automated. The semantic conflict resolution + build-fix chain stays
manual (it's judgement work). Two pieces:

- **`upstream-pins.json`** (repo root) — the source of truth: the last-merged upstream SHA per
  folder. Updating it is the **final step of every merge**. Both the local orchestrator and CI read
  it.
- **`scripts/upstream-merge.ps1`** — fetches the three upstreams, hydrates blobs (the forced
  filter-disabled refetch — HarbourMasters servers refuse by-SHA promisor fetches), rebuilds the
  `vendor-*` branches at the current tips (parent = previous vendor tip), and prints the
  **conflict-surface report** (which of *our* customized files upstream touched). With `-Merge` it
  also runs the 3-way merges (`-c merge.renames=false` for mm), leaving conflicts for a human.
- **CI** (`.github/workflows/`): `upstream-check.yml` (weekly + manual) opens/updates a GitHub issue
  with new commits + conflict surface — **notify only, never merges**. `build.yml` is a
  GitHub-hosted Windows compile gate (all four targets, Debug) on PRs touching the vendored trees.

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

---
# Merge log

Each merge pass gets its **own dated file** under [`merges/`](merges/) — one per pull, listing every
file we had to touch after the mechanical 3-way merge and why. This keeps the per-merge required
changes easy to track (and to diff against the recurring-deviation list below). Newest first:

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
