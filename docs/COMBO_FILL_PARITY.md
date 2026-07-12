# Fill-parity audit — SoH / 2Ship native generation vs the combo cross-world pipeline

The combo cross-world fill reuses each game's pools, logic graphs, settings, and apply paths, but
replaces the two native fill algorithms with one combo-owned assumed fill
(`combo/rando/CrossWorldRando.h`). Every side effect the native fills perform *around* placement
must be reproduced (or consciously declared N/A) — historically, every missed one has surfaced as a
seed bug (vanilla shop slots, Link's Pocket, boss remains, oracle grant fields, shop prices).

This document is the authoritative checklist. **Statuses:**

- `replicated` — the combo pipeline performs an equivalent step (column says where).
- `intentional` — deliberate divergence; the reason is stated. Cross-game awareness is the only
  sanctioned reason to diverge (ComboShip does not "improve" the generators).
- `n/a` — the step has no meaning in the combo pipeline; the reason is stated.
- `GAP` — missing/incorrect reproduction; numbered in the GAP register below.

**Maintenance:** re-walk this table on every upstream merge (see `UPSTREAM_MERGES.md`) and whenever
`Fill()` (soh) or `OnFileCreate` (mm) changes shape. Line numbers are as of 2026-07-13.

## Combo pipeline map (for reference)

| Stage | Where |
|---|---|
| OOT dump (confined placement + pool export) | `SOH_DumpRandoStaticData` → `SOH_PrepRandoContext` + `ComboFillConfined` (`OTRGlobals.cpp:3377`, `fill.cpp:1513`) |
| MM dump | `MM_DumpRandoStaticData` (`BenPort.cpp:2914`) — `GeneratePools` + `PreplaceConfinedItems` |
| Cross-world fill | `CrossWorldCombinedFill` (`CrossWorldRando.h:91`) — batched assumed fill + reachability fixpoint over both oracles |
| OOT oracle | `Combo_SOH_Rando_*` (`OTRGlobals.cpp:3793-3841`), init via `EnsureOracleInit` (`:3775`) |
| MM oracle | `Combo_MM_Rando_*` (`BenPort.cpp:3439-3689`) |
| OOT apply | `SOH_ApplyRandoPlacements` (`OTRGlobals.cpp:3556`) + `Combo_SetupOOTShops` (`:3253`) |
| MM apply | `MM_InitRandoSaveFile` (`BenPort.cpp:2673`) → `Rando::Spoiler::ApplyToSaveContext` |
| Validator | `comborando --playthrough` (`combo/ComboRandoHeadless.cpp`) |

## Table 1 — SoH `Fill()` (`soh/soh/Enhancements/randomizer/3drando/fill.cpp:1301`)

| # | Native step (file:line) | Purpose | Combo equivalent | Status |
|---|---|---|---|---|
| 1 | Per-attempt reset (`fill.cpp:1307`) | clear playthrough vectors, `placementFailure` | fill state lives in `CrossWorldCombinedFill` locals | replicated |
| 2 | `RegionTable_Init()` (`:1311`) | rebuild region graph, refresh global `ctx`/`logic` | `SOH_PrepRandoContext:3365`, `EnsureOracleInit:3783`, `ComboFillConfined:1515` | replicated |
| 3 | `ItemReset()` (`:1312`) | clear placements **and custom prices** (`item_location.cpp:235`) | `ComboFillConfined:1516`, apply `:3565`, oracle reset `:3799` | replicated — but see GAP-1: nothing re-rolls prices after the oracle-side resets |
| 4 | `GenerateLocationPool()` (`:1313`) | settings-scoped `allLocations` | `ComboFillConfined:1517`, prep `:3366`, oracle `:3784` | replicated |
| 5 | `GenerateItemPool()` (`:1314`) | item pool incl. fixed `PlaceItemInLocation` placements, starting-item removal | `ComboFillConfined:1518`; fixed placements exported as `fixed[]` in the dump (`:3435`) | replicated |
| 6 | `GenerateStartingInventory()` (`:1315`) | starting items from settings | `ComboFillConfined:1519`, oracle `:3789` + `ApplyStartingInventory` per reset (`:3800`) | replicated |
| 7 | `FillExcludedLocations()` (`:1316`) | junk-fill user-excluded checks | `ComboFillConfined:1520`; travels via `fixed[]` | replicated |
| 8 | Temp `GetMinVanillaShopItems(8)` inject + later erase (`:1321`, `:1333`) | shield-gated reachability during confined placement | `ComboFillConfined:1523`, erase `:1549` | replicated |
| 9 | Entrance shuffle (`:1322`) | `ShuffleAllEntrances` + retry on failure | cross-entrance work lives on its own feature branch | n/a here — tracked by feat/cross-entrances |
| 10 | `SetAreas()` (`:1331`) | area metadata for hints/barren | `ComboFillConfined:1524` | replicated |
| 11 | Shopsanity slots + **shop prices** (`:1342-1386`) | pick shuffled slots, `SetCustomPrice(GetRandomPrice(...))`, vanilla items elsewhere | `Combo_SetupOOTShops` (`:3253`) — **apply-time only**; slot set replicated in dump via `Combo_ShuffledShopSlots` (`:3227`) | **GAP-1** (prices absent during fill/oracle) |
| 12 | Scrub prices (`:1389`) | random or vanilla per `RSK_SHUFFLE_SCRUBS` | `Combo_SetupOOTShops:3274` — apply-time only | **GAP-1** |
| 13 | Bean salesman + merchant prices (`:1403-1426`) | random or vanilla per `RSK_SHUFFLE_MERCHANTS` | `Combo_SetupOOTShops:3285` — apply-time only | **GAP-1** |
| 14 | `PricesAffordable` clamp inside `GetRandomPrice` (`shops.cpp:162`) | clamp rolls to wallet-tier minima | flows through the same `GetRandomPrice` at apply | replicated at apply; inherits GAP-1 during fill |
| 15 | `RandomizeDungeonRewards()` (`:1432`) | rewards incl. Link's Pocket reward mode | `ComboFillConfined:1525` | replicated |
| 16 | `RandomizeOwnDungeon()` loop (`:1435`) | own-dungeon keys/BK/map/compass | `ComboFillConfined:1526` | replicated |
| 17 | `PlaceRestrictedSongs()` (`:1442`) | song-shuffle confinement | `ComboFillConfined:1529` | replicated |
| 18 | `RandomizeDungeonItems()` (`:1445`) | any-dungeon / overworld confinement pools | `ComboFillConfined:1530` | replicated |
| 19 | `RandomizeLinksPocket()` (`:1449`) | guarantee advancement/junk at Link's Pocket | `SOH_GetForcedPlacements` (`:3892`) + forced-placement handling in `CrossWorldRando.h:157` | replicated (different mechanism, same contract) |
| 20 | Advancement `AssumedFill` (`:1457`) | reverse assumed fill over own world | `CrossWorldCombinedFill` phase A — batched assumed fill + cross-game fixpoint | intentional — the one combo-owned replacement (cross-game awareness) |
| 21 | `FastFill` remainder (`:1463`) | junk into empty checks | `CrossWorldCombinedFill` phase B (`CrossWorldRando.h:420`) | replicated |
| 22 | `GeneratePlaythrough` + beatable gate (`:1467-1470`) | in-fill beatability proof | full-pool validation fixpoint (`CrossWorldRando.h:436`) + `comborando --playthrough` | replicated (different mechanism) |
| 23 | `PareDownPlaythrough` / `CalculateWotH` / `CalculateBarren` (`:1473-1482`) | playthrough minimization + hint categories | not run | **GAP-2** (only matters if gossip hints are surfaced; see GAP-3) |
| 24 | `CreateItemOverrides()` (`:1486`) | override table, ice-trap disguises | apply path `:3630` (+ disguise candidates `:3620`) | replicated |
| 25 | `CreateEntranceOverrides()` (`:1487`) | entrance override table | entrance feature branch | n/a here — tracked by feat/cross-entrances |
| 26 | `CreateAllHints()` / `CreateWarpSongTexts()` (`:1491-1492`) | gossip-stone hints, warp texts | not run by `SOH_ApplyRandoPlacements` | **GAP-3** (decide: generate combo-aware hints or explicitly disable stones) |
| 27 | Retry loop: 5 attempts, `Regions::ResetAllLocations` + `logic->Reset` between (`:1305`, `:1498-1505`) | whole-fill retry with fresh RNG state | `kMaxPasses = 10` inside `CrossWorldCombinedFill` (`CrossWorldRando.h:318`); no outer retry in `RunComboFill` (`ComboShip.cpp:686`) | **GAP-4** (converge on SoH's policy per fidelity rule) |

## Table 2 — 2Ship native generation (`mm/2s2h/Rando/MiscBehavior/OnFileCreate.cpp:20`)

| # | Native step (file:line) | Purpose | Combo equivalent | Status |
|---|---|---|---|---|
| 1 | Seed: `Ship_Hash(input)` → `Ship_Random_Seed` (`OnFileCreate.cpp:52`) | deterministic RNG per seed | `MM_SetComboRandoSeed` → seeded in `MM_DumpRandoStaticData` (`BenPort.cpp:2923`) | replicated |
| 2 | `GeneratePools()` (`OnFileCreate.cpp:78`, `GeneratePools.cpp:15`) | check/item pools per settings | `MM_DumpRandoStaticData:2936` | replicated |
| 3 | **Price rolls** in `GeneratePools` (`GeneratePools.cpp:117`, `:131`) | `Ship_Random(0,200)` per shop/Tingle slot, consumed by `CAN_AFFORD` (`Logic.h:239`, 38 sites across 6 region files) | rolls happen into a **discarded local** `RandoSaveInfo`; oracle save is `memset` (`BenPort.cpp:3447`) and apply (`MM_InitRandoSaveFile:2673`) never sets prices → oracle **and** in-game combo save both see `price==0` | **GAP-5** (MM shops effectively free in combo seeds; diverges from native) |
| 4 | Pool balancing (junk pad / HP collapse) (`OnFileCreate.cpp:88`) | `checkPool.size()==itemPool.size()` | junk pad in `MM_DumpRandoStaticData:2945`; balance handled by combo fill | replicated |
| 5 | Glitchless forward fill + junk-swap (`GlitchlessLogic.cpp:18`) | reachability-driven placement | replaced by `CrossWorldCombinedFill` | intentional — cross-game awareness |
| 6 | Retry: 10 attempts on pool copies (`OnFileCreate.cpp:164`) | whole-fill retry | `kMaxPasses = 10` (shared with OOT side) | see GAP-4 (one policy for the combined fill) |
| 7 | Spoiler emit: shop/Tingle checks as `{randoItemId, price}` objects (`Spoiler/Generate.cpp:32`) | price round-trip | consolidated spoiler stores name→name pairs only (`CrossWorldRando.h:525`) | **GAP-6** (no MM prices in consolidated spoiler; native object shape + `Apply.cpp:54` price branch already exist to reuse) |
| 8 | Spoiler apply restores price for object-shaped checks (`Apply.cpp:54`) | in-game prices from spoiler | combo placements are bare strings → string branch, price never set | **GAP-5/6** (same fix: emit object shape) |
| 9 | Confined pre-placement (`PreplaceConfinedItems`, `PlacementConstraints.cpp:106`) | keys/remains confinement → `fixed[]` | `MM_DumpRandoStaticData:2940` | replicated |
| 10 | Boss remains emitted as fixed when unshuffled | oracle models Remains gates | dump `fixed[]` (`BenPort.cpp:2952`) | replicated (fixed 2026-07-08, `--playthrough` PR #54) |
| 11 | Oracle starting state (`Combo_MM_Rando_Reset:3439`) | snapshot + rebuild save, grant via `GiveItemForOracle` | grant-field parity (powder keg / keys / mirror shield) fixed | replicated (see MM-oracle fix, 2310/2311 reachable) |

## GAP register

| GAP | Summary | Fix phase |
|---|---|---|
| GAP-1 | OOT shop/scrub/merchant prices exist only at apply (`Combo_SetupOOTShops`); fill + oracle + validator evaluate `GetCheckPrice()==0`, so wallet gates vanish during generation. Prices must be rolled at the native position (post-`ItemReset`, pre-placement) in the oracle prep, re-established after every oracle `ItemReset`, and byte-identical to apply (seeded `Combo_SeedShopRng`). | Phase 1 |
| GAP-2 | `PareDownPlaythrough`/WotH/barren not computed; needed only if hint generation (GAP-3) is adopted. | Phase 2 (blocked on GAP-3 decision) |
| GAP-3 | `CreateAllHints`/`CreateWarpSongTexts` never run in combo seeds — gossip stone / warp text behavior is undefined rather than decided. | Phase 2 (decision) |
| GAP-4 | Retry policy diverges from both natives (no outer whole-fill retry; single 10-pass inner loop). Converge per fidelity rule. | Phase 2 |
| GAP-5 | MM prices are 0 in both the combo oracle (memset, no `GeneratePools`) and the combo in-game save (string-only apply) — shops/Tingle/`CAN_AFFORD` gates are free. Roll per native (`Ship_Random(0,200)`, seeded) and persist into the save. | Phase 1 |
| GAP-6 | Consolidated spoiler carries no prices for either game. Emit OOT prices (SoH spoiler shape, parsed by `SeedContext.cpp:442`) and MM prices (2Ship object shape, parsed by `Apply.cpp:54`); `--playthrough` consumes spoiler prices only and hard-fails on price-less spoilers. | Phase 1 |
| GAP-7 | Excluded-locations state leaks from local `comboship.json` into replayed spoilers: it lives in the `ExcludedLocations` CSV **string** CVar (`randomizer.cpp:931`), which `SOH_DumpRandoSettings` doesn't capture and `SOH_RestoreRandoSettings` (integer-only) couldn't restore. Per-location `Option`s have empty CVar names, so they can't be dumped generically. Likely the remaining source of the sphere-19-vs-24 validation divergence. | Phase 2 |
| GAP-8 | Validator forces `LogicRules=0` per pass, but `GetShopsanityReplaceAmount` shuffles up to 8 slots under No Logic vs 7 otherwise — a No-Logic seed with shopsanity random/8 validates against a different shuffled-slot set. Pre-existing; escalated now that the slot set drives per-reset shop state. | Phase 2 |

Related validator requirement (settled in planning): `--playthrough` must be hermetic — settings
and prices come from the spoiler it reads, never from the neighboring `comboship.json`.

## Resolved-gap ledger (for merge regression checks)

| Formerly | Resolution |
|---|---|
| Non-shuffled OOT shop slots missing from dump → owned-set under-modeling (Deku Shield / closed forest) | dump emits them as `fixed[]` vanilla buy items (`OTRGlobals.cpp:3423`), apply skips them (`:3612`) |
| Link's Pocket double-place / wrong category | `SOH_GetForcedPlacements` (`OTRGlobals.cpp:3892`) + reserved-item handling in `CrossWorldRando.h:157` |
| MM non-shuffled boss remains invisible to oracle | emitted as `fixed[]` (`BenPort.cpp:2952`) |
| MM oracle grant-field mismatches (powder keg → `INV_CONTENT`, small keys → `foundDungeonKeys`, mirror shield value) | `GiveItemForOracle` (`BenPort.cpp:3077`) writes the fields the checks read |
| Red ice / fountain fairies dropped by `GetVanillaItem()!=RG_NONE` dump guard | guard removed; RI_NONE placements are intended (24109cfe) |
