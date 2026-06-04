# ComboShip Cross-World Randomizer — Combined-Logic Spec

**Date:** 2026-06-04 (revised)
**Branch:** `randomizer`
**Status:** Design complete; per-increment specs/plans to follow.
**Supersedes:** `2026-06-04-combo-crossworld-randomizer-scope-a.md` (the no-logic stepping stone),
Increment 2 plan Phases 2-3.
**Reuses unchanged:** Increment 1 mailbox (`combo/rando/CrossMailbox.h`, delivery channel, receive
drains), Increment 2 Phase 1 pipeline (static-data exports, `gComboGenerateCallback`,
`SOH_ApplyRandoPlacements`, `MM_InitRandoSaveFile`, `Combo_OnGenerate` orchestration).

---

## Goal

A **feature-complete cross-world randomizer** for ComboShip (OOT "soh" + MM "2ship" in one process):

- One **Generate** produces a single combined seed across both games.
- Items — **including progression** — are placed **across both worlds**. An OOT chest can hold an
  MM progression item and vice-versa.
- **Logic is respected**: the combined seed is guaranteed completable. Cross-world progression works —
  reaching an MM item sitting in an OOT chest is gated by *OOT* logic; collecting it delivers it to MM
  and unlocks further MM checks.
- A single combo-owned settings window configures both games before save creation.

### Locked decisions (user, 2026-06-04)

- **Cross-items MUST include real progression.** Restricting cross-placement to non-progression is
  explicitly rejected.
- **No swap-on-top.** Independent per-game seeds with post-hoc swapping cannot preserve logic.
- **Combined-logic fill is the only acceptable model.** A single fill that understands both worlds.
- **Combo-level settings UI** before save creation.
- **Foreign-item presentation = generic "gift" model + text** (no foreign model rendering).

## Non-goals

- Merging the two games' logic codebases. We orchestrate two intact engines.
- Shared item identity. An OOT Bow != an MM Bow mechanically. **An item always belongs to, and
  functions fully in, its OWN game.** A check in the other world merely holds it; collecting that
  check sends the item home via the mailbox, where it is granted as the real, functional item.
- New foreign-item art/models.
- Anchor multiplayer integration.

---

## Architecture Overview

ComboShip runs OOT (`soh.dll`) and MM (`2ship.dll`) in **one process, alternating control** (the
`for(;;)` loop in `combo/ComboShip.cpp`; only one game's loop is active at a time, each with its own
`gSaveContext` and `ResourceManager`, sharing one `libultraship.dll` + UCRT heap). There is **no
concurrency and no IPC** — cross-game data passes through shared process memory.

Three pillars, all **combo-layer owned**:

1. **Per-game reachability oracles** — thin `extern "C"` exports on each DLL wrapping the existing
   logic engines. Uniform interface: `Reset / SetOwnedItems / GetReachableChecks / PlaceItem`.
2. **Combined multiworld fill** (`combo/rando/CrossWorldRando.*`) — an assumed fill over the union
   of both games' pools, joined by the portal, driving both oracles.
3. **Cross-game delivery** (already built) — the mailbox channel (`combo/rando/CrossMailbox.h`,
   `saves/combo/slot{N}.mailbox.json`) plus send-interception at pickup and receive-drain on
   player update.

---

## 1. Per-Game Reachability Oracles

### Uniform oracle interface

Both games expose the same four operations (as `extern "C" __declspec(dllexport)` function-pointer
exports, called by the combo layer):

```
Reset()                          // Clear logic/inventory scratch state
SetOwnedItems(itemNamesJson)     // Load an owned-item multiset into the engine
GetReachableChecks() -> json     // Checks reachable under current owned set
PlaceItem(checkName, itemName)   // Commit a placement (for save consumption)
```

Interchange across the DLL boundary is by **string item/check names** (both games already
string-name items/checks in spoilers). No enum merge.

Performance budget: the combined fill calls `SetOwnedItems + GetReachableChecks` once per item
placed (~150-250 advancement items). Each game's reachability search is sub-millisecond. Total
generation target: under 5 seconds (synchronous, with "Generating..." frame).

### OOT oracle (`SOH_RandoOracle_*`)

**Backing code** (all in `soh/soh/Enhancements/randomizer/`):

- **Item state:** `Logic::ApplyItemEffect(item, true)` applies an item's effect to the logic
  scratch state (`logic.cpp`). `Logic::Reset(true)` clears all state and rebuilds a fresh scratch
  `SaveContext`. Items are identified by `RandomizerGet` enum, mapped from string names via
  `Rando::StaticData::itemNameToEnum`.
- **Reachability:** `ReachabilitySearch(targetLocations)` (`fill.cpp:512`). Takes a vector of
  `RandomizerCheck` locations, returns the subset reachable with current logic state. Uses a
  fixpoint BFS over the region graph (`areaTable`), evaluating conditions that read the logic
  scratch state. Supports age/time variants (child/adult, day/night) natively.
- **Placement:** `Context::PlaceItemInLocation(rc, rg, false, false)` (`SeedContext.cpp:138`).
  Records the placement in `itemLocationTable` for save consumption.
- **State reset:** `Logic::Reset(true)` + `Regions::AccessReset()` + `Context::LocationReset()`
  + `ApplyStartingInventory()`.

**Init sequence** (must complete before oracle is callable; already happens at OOT boot):
`Context::CreateInstance` -> `InitStaticData` -> `FinalizeSettings({},{})` -> `RegionTable_Init`
-> `GenerateLocationPool` -> `GenerateItemPool` -> `GenerateStartingInventory`.

**OOT oracle is live whenever OOT runs.** No warm-up needed.

**Exports** (new, in `OTRGlobals.cpp`):

```cpp
extern "C" __declspec(dllexport) void SOH_RandoOracle_Reset(void);
// Calls Logic::Reset(true), Regions::AccessReset(), Context::LocationReset(),
// ApplyStartingInventory(). Leaves the oracle in a clean "empty inventory" state.

extern "C" __declspec(dllexport) void SOH_RandoOracle_SetOwnedItems(const char* itemNamesJson);
// JSON array of item name strings, e.g. ["Hookshot","Bow","Bombs"].
// For each: look up RG via itemNameToEnum, call PlaceItemInLocation into a dummy
// location with applyEffectImmediately=true (so the item's logic effect is live).
// Alternative: call Logic::ApplyItemEffect directly for each item.

extern "C" __declspec(dllexport) const char* SOH_RandoOracle_GetReachableChecks(void);
// Calls ReachabilitySearch(ctx->allLocations). Returns JSON array of reachable check
// name strings. Static buffer, caller copies before next call.

extern "C" __declspec(dllexport) void SOH_RandoOracle_PlaceItem(
    const char* checkName, const char* itemName);
// Final placement for save consumption. Calls PlaceItemInLocation(rc, rg, false, false).
```

### MM oracle (`MM_RandoOracle_*`)

**Backing code** (all in `mm/2s2h/Rando/Logic/`):

- **Region graph:** `Rando::Logic::Regions` (`Logic.cpp:10`), a `std::map<RandoRegionId, RandoRegion>`.
  Each region has checks, exits, and connections, all gated by `std::function<bool()>` conditions
  that read global state (`gSaveContext`, `gCurrentRegionTime`, `RANDO_SAVE_OPTIONS`, CVars).
- **Item state:** Mutates `gSaveContext` directly (inventory, quest items, rando flags). Items are
  identified by `RandoItemId` enum, mapped from string names via `Rando::StaticData::Items[ri].spoilerName`.
- **Reachability:** `FindReachableRegions(startRegion, reachableSet, regionTimeStates)` (`Logic.cpp:162`).
  BFS/DFS over the region graph, evaluating conditions with `gCurrentRegionTime` set per-region.
  Returns a set of reachable region IDs; checks within reachable regions whose conditions also pass
  are the reachable checks.
- **Placement:** `RANDO_SAVE_CHECKS[rc].randoItemId = ri; .shuffled = true;`.

**MM time model** (critical for correct reachability):

MM's 3-day clock is abstracted as **45 discrete time slices** (`TIME_DAY1_AM_06_00` through
`TIME_NIGHT3_AM_05_00`), tracked per-region as a `uint64_t` bitmask in `RegionTimeState`.
`gCurrentRegionTime` holds the bitmask for the region currently being evaluated. Region conditions
use `AT(slice)`, `BEFORE(slice)`, `AFTER(slice)`, `BETWEEN(start,end)` macros that test bits against
this global.

Time expansion: `TimeLogic::ExpandTimeForward(timeSlices, region)` propagates accessible time forward
within a region. If a region allows `canStayOverTime`, the player can wait; expansion halts at
`timeStayRestrictions` that fail. This is a sequential, one-way process — once halted, later times
are unreachable from that path.

Clock Shuffle: when enabled, half-day ownership (6 half-days across 3 days) gates which time slices
are accessible. Clock items (RI_TIME_DAY_1..RI_TIME_PROGRESSIVE) unlock half-days. The oracle must
recompute `TimeLogic::GetOwnedTimeSlices()` and re-expand all region times whenever a clock item is
added to the owned set.

**For the headless oracle:** no real game clock is needed. The oracle:
1. Initializes region time states from `GetOwnedTimeSlices()` (all slices if no clock shuffle).
2. Sets `gCurrentRegionTime` per-region before evaluating conditions.
3. Calls `FindReachableRegions` to discover reachable regions.
4. Iterates reachable regions' checks, evaluating each check's condition with the region's time set.
5. When a clock item is added to owned set, recomputes owned time slices and re-expands all regions.

This is exactly what `ApplyGlitchlessLogicToSaveContext` (`GlitchlessLogic.cpp:18`) already does
during MM's own fill — the oracle wraps the same core.

**gSaveContext scratch management:** MM's logic conditions read `gSaveContext` directly (not a
separate scratch state). The oracle must:
- Snapshot `gSaveContext` before the fill begins.
- Use a zeroed/initialized scratch `gSaveContext` during oracle queries.
- Restore the real `gSaveContext` after generation completes.
This prevents generation from corrupting the live save. The snapshot/restore is a simple `memcpy`
of the `SaveContext` struct.

**MM warm-up (VALIDATED — not a risk):**

`Rando::Logic::Regions` is populated by `ShipInit::InitAll()`, which runs all `RegisterShipInitFunc`
lambdas registered under the `"*"` category. This normally happens during MM's `InitOTR()` boot
sequence. At OOT save-creation time (when generation runs), MM has NOT booted — `Regions` is empty.

An earlier attempt calling `ShipInit::InitAll()` *before* `SOH_Init()` crashed because the shared
libultraship `Context` didn't exist yet. However, calling it **after `SOH_Init()` completes works
without issues** (tested 2026-06-04) — the shared Context exists, and MM's init funcs run cleanly
in that environment.

**Warm-up mechanism:** `MM_InitRandoLogic()` export, called from `ComboShip.cpp` after `SOH_Init()`
returns:
1. Calls `ShipInit::InitAll()` — populates `Regions` + runs other MM init funcs.
2. Calls `Rando::StaticData::PopulateCheckNames()`.
3. Idempotent (static `bool inited` guard).

**Exports** (new, in `BenPort.cpp`):

```cpp
extern "C" __declspec(dllexport) void MM_InitRandoLogic(void);
// Warm-up: ShipInit::InitAll() + PopulateCheckNames(). Idempotent. Call after SOH_Init().

extern "C" __declspec(dllexport) void MM_RandoOracle_Reset(void);
// Snapshot gSaveContext, zero a scratch context, initialize region time states from
// GetOwnedTimeSlices(), clear reachable set. Leaves oracle in "empty inventory" state.

extern "C" __declspec(dllexport) void MM_RandoOracle_SetOwnedItems(const char* itemNamesJson);
// JSON array of item name strings. For each: look up RI via StaticData lookup, call
// Rando::GiveItem or set gSaveContext fields directly. If a clock item, recompute
// owned time slices and re-expand all region times.

extern "C" __declspec(dllexport) const char* MM_RandoOracle_GetReachableChecks(void);
// Initialize regionTimeStates, call FindReachableRegions(RR_MAX, ...), then iterate
// reachable regions' checks evaluating each condition. Return JSON array of reachable
// check name strings.

extern "C" __declspec(dllexport) void MM_RandoOracle_PlaceItem(
    const char* checkName, const char* itemName);
// Final placement: RANDO_SAVE_CHECKS[rc].randoItemId = ri; .shuffled = true.

extern "C" __declspec(dllexport) void MM_RandoOracle_Restore(void);
// Restore the snapshotted gSaveContext. Called after generation completes.
```

---

## 2. Combined Multiworld Fill

Lives in the combo layer (`combo/rando/CrossWorldRando.*`), the only code linked against both DLLs.
Runs an **assumed fill over the union** of both games' pools, joined by the portal.

### Pool construction

At generation time, the combo layer calls each game's existing static-data dumps
(`SOH_DumpRandoStaticData`, `MM_DumpRandoStaticData` — already built) to get the full check + item
lists. Each check and item is tagged with its source game.

Items are classified:
- **Advancement** (progression): items that unlock new checks. Each game's logic engine knows which
  of its own items are advancement. The combo layer queries this via the oracle: an item is
  advancement if adding it to an empty-plus-starting-inventory set causes any new check to become
  reachable. (Alternatively, each game tags advancement in its static data — OOT has
  `Item::IsAdvancement()`, MM can derive from pool categorization.)
- **Junk** (non-advancement): everything else. Freely interchangeable, no placement constraint.

### Portal model

- **OOT is reachable from start.** The player begins in OOT.
- **MM checks become reachable only when the OOT->MM portal location is reachable in OOT.**
  The portal is the combo transition point (currently `SCENE_MIDOS_HOUSE`). When OOT's oracle
  reports the portal's region as reachable, MM's reachable checks are added to the candidate pool.
- **Return gating** (OOT checks requiring having visited MM): not modeled initially. The portal
  is one-way for logic purposes (OOT->MM unlocks MM; returning to OOT is assumed free). Revisit
  if any real OOT logic requires it.

### Assumed fill algorithm

The combined fill uses the same assumed-fill discipline each game uses for its own randomizer,
lifted to the union:

```
1. Collect all advancement items from both games into advancementPool.
   Collect all junk items from both games into junkPool.
   Collect all checks from both games into emptyChecks.

2. Shuffle advancementPool (deterministic, seeded from master seed).

3. For each item I in advancementPool:
   a. Remove I from the "assumed available" set.
   b. Reset both oracles.
   c. Push all REMAINING assumed OOT items into the OOT oracle.
      Push all REMAINING assumed MM items into the MM oracle.
   d. Query OOT oracle: reachableOOT = SOH_RandoOracle_GetReachableChecks().
   e. If portal region is in reachableOOT:
        Query MM oracle: reachableMM = MM_RandoOracle_GetReachableChecks().
      Else:
        reachableMM = {}.
   f. candidateLocations = (reachableOOT ∪ reachableMM) ∩ emptyChecks.
   g. If candidateLocations is empty: BACKTRACK (retry with different placement order,
      or retry entire seed — same discipline as each game's solo fill).
   h. Place I in a random candidate location (deterministic choice from master seed).
      Remove that location from emptyChecks.
      If I is an OOT item placed in an MM check (or vice versa), mark it as a foreign
      placement in the combined spoiler.

4. After all advancement items placed:
   FAST-FILL junk: for each remaining emptyCheck, assign a random junk item from
   junkPool (no reachability check needed). Junk is freely interchangeable across worlds.

5. Verify completability: reset both oracles with empty inventory, replay the placement
   sequence sphere-by-sphere, confirm 100% of advancement items are reachable. (Sanity
   check — the assumed fill guarantees this by construction, but verify defensively.)

6. Commit placements:
   - OOT items in OOT checks: SOH_RandoOracle_PlaceItem(check, item).
   - MM items in MM checks: MM_RandoOracle_PlaceItem(check, item).
   - Foreign placements: mark in combo-owned foreign-item map (see section 4).
   - Apply OOT placements: SOH_ApplyRandoPlacements (existing export).
   - Apply MM placements: written into MM save via MM_InitRandoSaveFile (existing export).
```

**Key invariant:** advancement items from game A placed in game B's checks create cross-world
dependencies. The assumed fill handles this correctly because the "assumed available" set includes
items from BOTH games, and reachability is computed separately per game (each oracle only receives
its own game's items). An OOT item in an MM check is reachable if MM's check is reachable (with
assumed MM items) AND the portal is reachable in OOT (with assumed OOT items).

### Junk handling

Junk carries no logic constraint. After progression is placed, every remaining empty check (in
either world) is filled with junk with no reachability check. Junk is freely interchangeable across
worlds and substitutable — an MM check's junk slot may hold an OOT green rupee, and vice-versa.
This absorbs any size mismatch between the two games' check/item counts. Junk delivered cross-world
routes through the normal mailbox path but never gates anything.

### Combined spoiler

Output: `saves/combo/slot{N}.spoiler.json`, containing:
- Master seed.
- Per-game settings snapshot.
- OOT placement section: `{checkName: itemName}` for all OOT checks.
- MM placement section: `{checkName: itemName}` for all MM checks.
- Foreign-placement annotations: for each cross-placed check, `{checkGame, checkName, itemGame, itemName}`.
- Sphere breakdown (optional, for debugging): ordered spheres showing which items unlock which checks.

---

## 3. Cross-Game Delivery

### Mailbox (already built — Increment 1)

`combo/rando/CrossMailbox.h`: header-only, compiled into all three modules. File-backed
(`saves/combo/slot{N}.mailbox.json`), atomic-rename writes, crash-safe. `Enqueue / LoadPending /
MarkAllDelivered`.

### Send interception (Increment 6)

At the two existing pickup branch points:
- **OOT:** `hook_handlers.cpp:380` (inside `RandomizerOnPlayerUpdateForRCQueueHandler`; `rc` +
  `GetItemEntry` in hand).
- **MM:** `CheckQueue.cpp:37` (inside `CheckQueue`; `randoSaveCheck.randoItemId` in hand).

At pickup, the game checks the combo-owned foreign-item map (section 4) for the current check.
If the check is marked foreign:
1. **Do not grant the item locally.**
2. Enqueue a `MailboxEntry` with `{srcGame=thisGame, dstGame=otherGame, itemName=foreignItemName}`.
3. Show the "gift" presentation: generic model + "Sent to Termina: <displayName>" (OOT) or
   "Sent to Hyrule: <displayName>" (MM).
4. Mark the check as obtained (so it doesn't re-trigger).

If the check is NOT foreign: grant normally (existing behavior, unchanged).

### Receive drain (already built — Increment 1)

OOT: `RandomizerOnPlayerUpdateForCrossMailboxHandler` in `hook_handlers.cpp` (runs every frame,
loads pending entries, grants items, marks delivered). Currently grants placeholder
`ITEM_RUPEE_BLUE`; Increment 6 replaces with the real item grant via the item-name-to-give mapping.

MM: equivalent drain (verify implementation status; may need completion).

**Real item grant (Increment 6):** the receive drain looks up the `itemName` from the mailbox entry
in the receiving game's item table and calls the game's give function:
- OOT: `Randomizer_Item_Give(rg)` or `Item_Give(gPlayState, item)`.
- MM: `Rando::GiveItem(ri)` or `Item_Give(gPlayState, item)`.
Display: "Received from Hyrule/Termina: <displayName>".

---

## 4. Foreign-Item Marker Schema

Cross-placed items need a marker so the send-interception code knows "this check holds a foreign
item — divert to mailbox instead of granting locally."

### Design: combo-owned side map (no game save format changes)

A **combo-layer JSON file** per slot: `saves/combo/slot{N}.foreign.json`.

```json
{
  "oot": {
    "RC_KOKIRI_SWORD_CHEST": { "itemGame": "mm", "itemName": "Deku Mask", "displayName": "Deku Mask" },
    "RC_BOTTOM_OF_THE_WELL_CHEST": { "itemGame": "mm", "itemName": "Bow", "displayName": "Hero's Bow" }
  },
  "mm": {
    "RC_CLOCK_TOWN_SOUTH_CHEST": { "itemGame": "oot", "itemName": "Hookshot", "displayName": "Hookshot" }
  }
}
```

Written by the combo generator at generation time. Read by each game's send-interception code at
pickup time.

**Why a side file instead of extending save formats:**
- OOT's `itemLocationTable` is runtime-only (not persisted to save); there's no per-check save
  field to extend. Adding one would require modifying OOT's save format (upstream-invasive).
- MM's `RandoSaveCheck` could be extended, but doing so touches `z64save.h` (game source) and
  changes the save format version.
- A combo-owned file is fully in the combo layer — no game-source changes, no save format changes,
  no upstream merge conflict risk. Both games already read combo files (the mailbox).

**Runtime lookup:** each game loads the foreign map at save-load time (or on first check trigger)
and caches it in memory. The check is a hash-map lookup — `O(1)` per pickup.

**For the oracle fill:** the foreign map is a byproduct of the combined fill. Every time the fill
places a game-A item in a game-B check, it records the mapping. The map is written atomically
(same temp-rename pattern as the mailbox) alongside the combined spoiler.

**Placeholder items in each game's placement table:** when a check holds a foreign item, the
check's own game still needs *something* in its placement slot (so the check isn't treated as
empty/vanilla). Use a designated sentinel:
- OOT: `RG_COMBO_FOREIGN` (a new `RandomizerGet` value, or repurpose an existing unused RG).
  `SOH_ApplyRandoPlacements` places this sentinel. At runtime, `GetFinalGIEntry` returns a
  generic "gift" get-item entry when it sees the sentinel.
- MM: `RI_COMBO_FOREIGN` (a new `RandoItemId` value). `MM_InitRandoSaveFile` writes this into
  `RANDO_SAVE_CHECKS[rc].randoItemId`. At runtime, `CheckQueue` / `GiveItem` recognizes the
  sentinel and defers to the foreign map + mailbox.

The sentinel approach keeps each game's existing pickup pipeline intact — it just adds one branch
at the grant point.

---

## 5. Combo Settings Window

Both games' rando options are **CVar-backed in the shared store** (one `libultraship` Context,
one CVar namespace), so a single combo-owned ImGui window can render and edit all of them.

### Data sources

- **MM:** `Rando::StaticData::Options` — `std::map<RandoOptionId, RandoStaticOption>` with `id`,
  `name`, `cvar`, `defaultValue`, plus value-label maps from `Rando/Menu.cpp`.
- **OOT:** `Rando::Settings::GetInstance()->GetAllOptions()` — `Option` objects with CVar name,
  labels, category, hidden state.
- All options read/written via `CVarGetInteger / CVarSetInteger / CVarGetString / CVarSetString`
  (`<libultraship/bridge/consolevariablebridge.h>`). Cross-DLL writes are immediately visible
  (shared CVar store, no per-DLL caching).

### Window design

A `Ship::GuiWindow` subclass registered in `soh.dll`'s `SohGui::SetupGuiElements` (OOT boots
first and owns the menu slot), gated `#ifdef COMBO_BUILD`. Persists across game transitions. Tabs:
- **OOT Settings:** renders OOT's rando options by category.
- **MM Settings:** renders MM's rando options by category.
- **Combo:** master seed input, cross-placement toggle (on by default).
- **Generate button:** runs the combined fill from current CVars, writes the combined spoiler,
  stashes the result. Subsequent save creation consumes it via the existing `gComboGenerateCallback`
  path.

Reachable before file-select (menu bar entry or hotkey). A "Generating cross-world seed..." overlay
displays during the synchronous fill.

### Bypassing per-game settings screens

- **OOT:** `FileChoose_UpdateRandomizerMenu` (`z_file_choose.c:777`) gates proceed-to-name-entry
  on `Randomizer_IsSeedGenerated()`. Under `COMBO_BUILD`, this gate is skipped — the combo Generate
  has already run and set `SeedGenerated(true)`.
- **MM:** MM's `OnFileCreate` normally runs its own seed generation. Under `COMBO_BUILD`, this is
  bypassed — `MM_InitRandoSaveFile` (called from the combo callback) writes the rando save directly.
- **Rando-only saves:** ComboShip always forces `QUEST_RANDOMIZER` (OOT) and `SAVETYPE_RANDO` (MM).
  No vanilla save path exists in combo mode.

---

## 6. Increment Breakdown

Each increment is independently verifiable and gets its own spec->plan->build. Increments 1-2 are
already implemented.

### Increment 1 — Mailbox + cross-game grant plumbing (DONE)

Built: `combo/rando/CrossMailbox.h`, OOT receive drain in `hook_handlers.cpp`, debug `cross_send`
console command. Verified: OOT->MM delivery with placeholder rupee grant.

### Increment 2 Phase 1 — Native-only generator pipeline (DONE)

Built: `SOH_DumpRandoStaticData`, `MM_DumpRandoStaticData`, `SOH_ApplyRandoPlacements`,
`MM_InitRandoSaveFile`, `gComboGenerateCallback`, `CrossWorldRando.h` (native-only assignment),
`Combo_OnGenerate` orchestration. Verified: both saves created as rando, combined spoiler emitted
with native-only placements.

### Increment 3 — MM rando-logic warm-up + oracle foundation

**Goal:** Build `MM_InitRandoLogic()` and confirm MM's region graph + reachability engine work
headlessly after `SOH_Init()`.

**Build:** `MM_InitRandoLogic()` export in `BenPort.cpp`. Call it from `ComboShip.cpp` after
`SOH_Init()` returns. The warm-up itself is validated (no crash, tested 2026-06-04); this increment
builds it as a proper export and extends it with a throwaway reachability probe.

**Verify:** `Regions.size()` is non-zero (expect ~80-100 regions), no corruption of OOT's state.
Call `FindReachableRegions(RR_MAX, ...)` with a zeroed scratch `gSaveContext` and confirm a
non-empty reachable set (proves the graph is evaluable, not just populated).

### Increment 4 — Per-game reachability oracles

**Goal:** Implement and export the full oracle for each game.

**Build:**
- OOT: `SOH_RandoOracle_Reset`, `_SetOwnedItems`, `_GetReachableChecks`, `_PlaceItem` in
  `OTRGlobals.cpp`. Thin wrappers around the existing logic engine.
- MM: `MM_RandoOracle_Reset`, `_SetOwnedItems`, `_GetReachableChecks`, `_PlaceItem`,
  `_Restore` in `BenPort.cpp`. Wraps `FindReachableRegions` + check-condition evaluation,
  with gSaveContext snapshot/restore and time-state management.

**Verify:** each oracle, driven standalone with its game's vanilla settings, produces a
self-completable fill. Sanity-check: starting from empty inventory, repeatedly query reachable
checks, "collect" their vanilla items, query again until all checks are reachable. Should
converge (proves the oracle correctly models the game's logic).

### Increment 5 — Combined multiworld fill

**Goal:** The combo-layer assumed fill over the union, producing cross-placements.

**Build:** Replace `CrossWorldRando.h`'s no-logic permutation with the assumed fill algorithm
(section 2). Add portal-gating logic. Emit combined spoiler with foreign-placement annotations.

**Verify:**
- Generated seeds pass a combined reachability replay: starting from empty inventory, replay
  sphere-by-sphere, confirm 100% of advancement items are reachable across both worlds.
- Foreign placements appear in the spoiler (OOT items in MM checks and vice versa).
- No seed-generation failures (the fill converges within retry limits).

### Increment 6 — Foreign markers + send interception + gift presentation

**Goal:** Wire cross-placements to the delivery channel, replacing placeholder grants with real
items.

**Build:**
- Foreign-item map (`saves/combo/slot{N}.foreign.json`) — written by the generator, read by
  each game at pickup time.
- Sentinel items (`RG_COMBO_FOREIGN`, `RI_COMBO_FOREIGN`) in each game's placement table.
- Send interception at OOT's `hook_handlers.cpp:380` and MM's `CheckQueue.cpp:37` — check
  foreign map, divert to mailbox if foreign.
- Real item grant in each game's receive drain — map `itemName` to the game's give function.
- Gift presentation: generic "gift" model + "Sent to Termina/Hyrule: <item>" text.

**Verify:** full end-to-end loop — open an OOT check holding an MM progression item -> "Sent to
Termina" -> portal to MM -> item arrives, unlocks a gated check -> collect that check. And reverse.

### Increment 7 — Combined settings window + Generate UX

**Goal:** One window to configure both games' rando settings, with a Generate button.

**Build:** `Ship::GuiWindow` subclass in soh.dll (COMBO_BUILD), rendering both option tables,
master seed input, Generate action. "Generating..." overlay. Menu entry. Bypass per-game settings
screens.

**Verify:** toggling an option changes which checks are shuffled in the spoiler. Generate produces
a valid combined seed. Window is reachable before save creation and persists across transitions.

---

## Open Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| ~~MM warm-up crashes after SOH_Init~~ | ~~HIGH~~ | **RETIRED.** Tested 2026-06-04, no crash. |
| MM time logic in headless oracle | MEDIUM | Time is bitmask-based, no real clock needed; oracle wraps existing `FindReachableRegions` which already handles time. Test with Clock Shuffle ON. |
| gSaveContext corruption during MM oracle | MEDIUM | Snapshot/restore around generation; explicit `MM_RandoOracle_Restore` call. |
| Combined fill performance | LOW | ~200 oracle queries * ~1ms each = ~200ms baseline. JSON interchange adds overhead; if over 5s, switch to binary interchange or cache reachability incrementally. |
| Two fill paradigms (OOT assumed vs MM forward) | LOW | The combined fill drives placement externally; each oracle only answers reachability queries. The fill discipline is the combo layer's, not either game's. |
| Portal return-gating | LOW | Initial model: one-way (OOT->MM). No known OOT check requires MM items. Add if discovered. |
| itemPool / advancement classification | LOW | OOT has `Item::IsAdvancement()`; MM derives from pool categorization. Confirm both classify consistently before Increment 5. |

---

## HM64 Compliance

All interception is in port code (hook_handlers / CheckQueue / SaveManager / BenPort / OTRGlobals)
and combo-layer files. No OOT/MM game-source (`soh/src`, `mm/src`) edits except additive
`#ifdef COMBO_BUILD` plumbing:
- `z_sram.c`: `gComboGenerateCallback` hook (already exists).
- `z_file_choose.c`: quest forcing + seed-gate bypass (already exists).
- Sentinel enum values (if added to game headers): `RG_COMBO_FOREIGN`, `RI_COMBO_FOREIGN`.

Each game-source change is documented in `docs/UPSTREAM_MERGES.md` with a `// ComboShip:` comment
and the WHY (per `[[document-post-merge-changes]]`).

## Verification Approach

No unit-test harness exists for combo/game C++. Per-increment verification:
- **Builds:** per-target builds succeed (build individually per `[[comboship-build-targets]]`).
- **In-game manual:** documented click-path; observe behavior; read logs.
- **Artifact inspection:** combined spoiler, foreign map, mailbox file.
- **Reachability replay** (Increment 5): automated or semi-automated sphere-walk confirming
  completability of generated seeds.
- **Crash capture:** `x64/Debug/combo_abort_stack.txt`.

## Reuse Inventory

| Component | Source | Status |
|-----------|--------|--------|
| CrossMailbox.h | combo/rando/ | Done, unchanged |
| SOH_DumpRandoStaticData | OTRGlobals.cpp | Done, reused for pool enumeration |
| MM_DumpRandoStaticData | BenPort.cpp | Done, reused for pool enumeration |
| SOH_ApplyRandoPlacements | OTRGlobals.cpp | Done, extended for sentinel items |
| MM_InitRandoSaveFile | BenPort.cpp | Done, extended for sentinel items |
| gComboGenerateCallback | OTRGlobals.cpp + z_sram.c | Done, reused |
| Combo_OnGenerate | ComboShip.cpp | Done, body replaced with combined fill |
| CrossWorldRando.h | combo/rando/ | Done (no-logic), replaced by combined fill |
| Receive drains | hook_handlers.cpp + MM equivalent | Done (placeholder grant), upgraded in Increment 6 |
| Rando-only enforcement | z_file_choose.c + SaveManager | Done |
