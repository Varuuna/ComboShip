# Cross-World Randomizer — Combined-Logic Implementation Progress

**Branch:** `randomizer`
**Date:** 2026-06-04
**Spec:** `docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-combined-logic-design.md`

---

## Completed

### Increment 1 — Mailbox (prior work)
- `combo/rando/CrossMailbox.h`: header-only mailbox, JSON-backed, atomic writes
- OOT receive drain in `hook_handlers.cpp` (placeholder blue rupee grant)
- Debug `cross_send` console command in OOT

### Increment 2 Phase 1 — Native-only generator (prior work)
- `SOH_DumpRandoStaticData` / `MM_DumpRandoStaticData` exports
- `SOH_ApplyRandoPlacements` / `MM_InitRandoSaveFile` exports
- `gComboGenerateCallback` hook in `z_sram.c`
- `CrossWorldRando.h` no-logic permutation generator
- `Combo_OnGenerate` / `Combo_OnOOTSaveInit` orchestration in `ComboShip.cpp`
- Rando-only save enforcement

### Increment 3 — MM warm-up (`b4ba2cb8e`)
- **`mm/2s2h/BenPort.cpp`**: `MM_InitRandoLogic` export — calls `ShipInit::InitAll()` +
  `Rando::StaticData::PopulateCheckNames()` after `SOH_Init()`. Idempotent.
- **`combo/ComboShip.cpp`**: resolves + calls `MM_InitRandoLogic` at startup after `SOH_Init()`.
- Added `#include "2s2h/Rando/Logic/Logic.h"` and `#include "2s2h/Rando/MiscBehavior/ClockShuffle.h"` to BenPort.cpp.

### Increment 4 — Per-game reachability oracles (`b4ba2cb8e`)

**Naming convention:** `Combo_<GAME>_Rando_<Function>` (e.g. `Combo_SOH_Rando_Reset`).

**OOT oracle** (`soh/soh/OTRGlobals.cpp`):
- `Combo_SOH_Rando_Reset` — `Logic::Reset()` + `Regions::AccessReset()` + `LocationReset()` + `ItemReset()` + `ApplyStartingInventory()`
- `Combo_SOH_Rando_SetOwnedItems(json)` — parses item name array, calls `Item::ApplyEffect()` per item
- `Combo_SOH_Rando_GetReachableChecks()` — calls `ReachabilitySearch(ctx->allLocations)`, returns JSON array of check names
- `Combo_SOH_Rando_PlaceItem(check, item)` — `PlaceItemInLocation(rc, rg, false, false)`
- `EnsureOracleInit()` — one-time init: `RegionTable_Init` + `GenerateLocationPool` + `GenerateItemPool` + `GenerateStartingInventory`
- Added includes: `fill.hpp`, `location_access.h`, `item_pool.hpp`, `starting_inventory.hpp`

**MM oracle** (`mm/2s2h/BenPort.cpp`):
- `Combo_MM_Rando_Reset` — snapshots `gSaveContext` + `gCurrentRegionTime`, zeroes save context
- `Combo_MM_Rando_SetOwnedItems(json)` — parses item name array, calls `GiveItemForOracle(ri)` per item
- `Combo_MM_Rando_GetReachableChecks()` — runs `FindReachableRegions` fixpoint from `RR_MAX`, evaluates check conditions per reachable region, returns JSON array
- `Combo_MM_Rando_PlaceItem(check, item)` — sets `RANDO_SAVE_CHECKS[rc].randoItemId` + `.shuffled`
- `Combo_MM_Rando_Restore` — restores snapshotted `gSaveContext` + `gCurrentRegionTime`
- `GiveItemForOracle(ri)` — **direct save-context writes** (bypasses `Rando::GiveItem` / `Item_Give` to avoid `gPlayState` dependency). Covers: magic, swords, bomb bags, wallets, hearts, bottles, dungeon items, small keys, stray fairies, rando-flag items (deeds, keys, letters), abilities, ocarina buttons, songs, clock items, souls, frogs, GS tokens, progressive items. Default branch: `INV_CONTENT(itemId) = itemId` + song quest-item bits.
- Uses `using Rando::Logic::gCurrentRegionTime` for namespace resolution.

**ComboShip.cpp resolution:**
- Typedefs: `FnOracleVoid`, `FnOracleSetItems`, `FnOracleGetChecks`, `FnOraclePlaceItem`
- 9 function pointers + `GetSym` calls for all oracle exports

### Increment 5 — Combined assumed fill (`77dcd86e6`)

**`combo/rando/CrossWorldRando.h`** — rewrote with:
- `OracleFns` struct (Reset/SetOwnedItems/GetReachableChecks/PlaceItem function pointers)
- `CwItem`, `CwCheck`, `CwPlacement` data types (reusing `GameId` from `CrossMailbox.h`)
- `CrossWorldCombinedFill()` — assumed fill over the union:
  1. Parses both dumps into a combined item + check pool
  2. Shuffles deterministically (master seed)
  3. For each item: removes from assumed set, queries both oracles (MM gated by portal), places in random reachable empty check
  4. Produces spoiler with same shape (`"oot"` / `"mm"` = `{check: item}`) + `"foreign"` annotations
  5. Commits placements via oracle `PlaceItem` calls
- Legacy `CrossWorldGenerateSpoiler()` kept as fallback

**`combo/ComboShip.cpp`** — `Combo_OnGenerate` updated:
- Tries combined fill first when all oracles are resolved
- Falls back to no-logic on failure
- Calls `Combo_MM_Rando_Restore()` after generation

---

## Remaining

### Increment 6 — Foreign markers + send interception + real grants
- Write `saves/combo/slot{N}.foreign.json` from the combined fill's `"foreign"` annotations
- Add sentinel items (`RG_COMBO_FOREIGN` / `RI_COMBO_FOREIGN`) to each game's enum
- Send interception at OOT `hook_handlers.cpp:380` and MM `CheckQueue.cpp:37` — check foreign map, divert to mailbox
- Replace placeholder blue-rupee grant in receive drains with real item grant via item-name mapping
- Gift presentation: "Sent to Termina/Hyrule: <item>" text
- See spec section 3 (Cross-Game Delivery) and section 4 (Foreign-Item Marker Schema)

### Increment 7 — Combined settings window + Generate UX
- `Ship::GuiWindow` subclass in `soh.dll` (COMBO_BUILD)
- Renders both OOT + MM rando option tables (CVar-backed)
- Master seed input, Generate button
- Bypass per-game settings screens
- See spec section 5 (Combo Settings Window)

---

## Key files modified (Inc3-5)

| File | Changes |
|------|---------|
| `mm/2s2h/BenPort.cpp` | +`MM_InitRandoLogic`, +`GiveItemForOracle`, +`Combo_MM_Rando_*` (5 exports), +2 includes |
| `soh/soh/OTRGlobals.cpp` | +`EnsureOracleInit`, +`Combo_SOH_Rando_*` (4 exports), +4 includes |
| `combo/ComboShip.cpp` | +oracle typedefs/pointers/resolution, +warm-up call, updated `Combo_OnGenerate` |
| `combo/rando/CrossWorldRando.h` | +`OracleFns`/`CwItem`/`CwCheck`, +`CrossWorldCombinedFill`, kept legacy fallback |
| `docs/superpowers/specs/...combined-logic-design.md` | Completed spec (supersedes Scope A) |

## Design decisions

- **MM oracle item-giving**: direct save-context writes (`GiveItemForOracle`) — bypasses `Rando::GiveItem` / `Item_Give` to avoid `gPlayState` null crash. User choice.
- **Naming convention**: `Combo_<GAME>_Rando_<Function>` for all combo-specific exports.
- **MM warm-up**: `ShipInit::InitAll()` after `SOH_Init()` — validated, no crash.
- **Combined fill**: assumed fill over the union, portal-gated MM access, no-logic fallback.
