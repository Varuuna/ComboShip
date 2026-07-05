# Entrance Randomizer Prep

Design doc for absorbing entrance randomization into ComboShip's cross-game randomizer.
SoH (OOT) already ships a mature entrance rando; 2Ship (MM) has one incoming upstream
(PR [HarbourMasters/2ship2harkinian#1329](https://github.com/HarbourMasters/2ship2harkinian/pull/1329)).
Investigation as of 2026-07-05. **No code has been changed yet** — this doc is the plan for when it lands.

## 1. SoH's entrance rando (reference model)

Heavyweight, ported from OOTR's 3drando. Three layers:

**Seed time (C++)** — `EntranceShuffler::ShuffleAllEntrances()`
(`soh/soh/Enhancements/randomizer/entrance.cpp`): builds pools per `EntranceType`
(28 types: Dungeon, Boss, Interior, GrottoGrave, Overworld, OwlDrop, Spawn, WarpSong, BlueWarp…),
priority-places one-ways first (Bolero/Nocturne/Requiem must stay reachable in glitchless),
optionally mixes pools, supports decoupled two-ways, validates reachability after each placement
via `ValidateEntrances()` (`3drando/fill.cpp`), retries up to 20× per pool.

**Artifact** — `EntranceOverride[296]` of `{type, index, destination, override, overrideDestination}`
(`randomizer_entrance.h`). **Serialized** into the save (`SaveManager.cpp`) and the spoiler JSON
under an `"entrances"` key (round-trips via `ParseJson()`).

**Runtime (C)** — `Entrance_Init()` (`randomizer_entrance.c`) expands the array into an O(1)
`entranceOverrideTable[~2080]`. Vanilla code touches the system through ~10 functions:
`Entrance_OverrideNextIndex` (the main hook, from z_player/z_demo/shops),
`Entrance_OverrideDynamicExit`, `Entrance_SetGameOverEntrance` / `SetSavewarpEntrance` /
`SetWarpSongEntrance` / `OverrideBlueWarp`, plus grotto handling (`randomizer_grotto.c`,
synthetic 0x0700 load / 0x0800 exit index spaces) and epona/weather fixups.
An entrance tracker UI keeps a discovered-bitfield in the save.

## 2. 2Ship PR #1329 (incoming model)

By garrettjoecox (lead maintainer), out of draft 2026-07-01, actively updated. +521/−1 across
9 files, **all in the `2s2h/Rando/` port layer, zero game-source lines** — ideal for our
HM64/combo-ownership principles. Deliberately minimal compared to SoH:

- **Model**: one global `std::map<s32, s32> sEntranceMap` (vanilla entrance → shuffled entrance),
  plain MM entrance ints (`(scene<<9)|(spawn<<4)`). No entrance structs, no override table.
- **Pools**: interiors (~50) and dungeons (4, Stone Tower counted once) are hardcoded sets
  (author TODO: derive from logic maps); overworld pairs are **derived from the logic region
  graph's** two-way exits; grottos punted entirely (empty pool). Coupled two-way shuffle only.
- **Shuffle**: swap-shuffle with `Ship_Random`, then **rejection sampling** — up to 256 attempts
  reseeded `finalSeed + attempt` until a structural BFS (`ReachableRegions()`, item/time-gate-blind)
  reports all regions connected. ⚠️ On 256 failures it only logs a warning and *keeps the last
  (possibly disconnected) map*.
- **Logic seam**: one line in `Logic.cpp FindReachableRegions` — exits resolve through
  `GetShuffledEntrance()` — so item-placement logic is fully entrance-aware, because the shuffle
  runs in `OnFileCreate` *before* placement and again on every `OnFileLoad`.
- **Runtime**: a single `COND_HOOK(OnPlayDestroy)` rewrites `gSaveContext.save.entrance` just
  before scene transition. Guards: skip when `respawnFlag != 0` (respawn entrances are already
  shuffled-resolved) and skip grotto scene exits; forces `nextCutsceneIndex = 0xFFEF` to avoid a
  stale-cutscene-layer crash.
- **Persistence: none.** The map is deterministically re-derived from
  `shipSaveInfo.rando.finalSeed` on every load; only the three option flags
  (`RO_SHUFFLE_ENTRANCES_DUNGEONS/_INTERIORS/_OVERWORLD`) are saved. No spoiler log, no tracker.
- **Maturity** (author's words): interiors + dungeons confirmed working; overworld "use at your
  own risk" (grotto/swim interactions untested); no external review yet.

### Delta vs SoH that matters for us

| | SoH (OOT) | 2Ship PR #1329 (MM) |
|---|---|---|
| State | serialized `EntranceOverride[]` in save + spoiler | seed-derived `map<s32,s32>`, nothing saved |
| Shuffle | assumed-placement w/ per-step validation | rejection sampling, structural-connectivity check |
| One-ways | shuffled pools (spawns, warps, owls) | excluded (respawn guard only) |
| Runtime | ~10 C API call sites in game code | 1 port-layer hook |
| Spoiler/tracker | yes / yes | no / no |

## 3. ComboShip integration design (when #1329 merges)

Today's combined fill (`combo/rando/CrossWorldRando.h`, `CrossWorldCombinedFill()`) is
oracle-based: each game exposes `Reset / SetOwnedItems / GetReachableChecks / PlaceItem`.
**Entrance shuffle stays invisible to the fill as long as each oracle reflects its own shuffled
world.** That makes the integration surface small:

### 3.1 Oracle entrance-state contract (the critical piece)

The PR shuffles in `OnFileCreate`/`OnFileLoad`; our MM oracle (`Combo_MM_Rando_Reset` etc. in
`mm/2s2h/BenPort.cpp`) runs headless during combined generation and hits neither hook. Contract:

- `Combo_MM_Rando_Reset` must run `ShuffleEntrances(finalSeed)` (with the entrance options that
  will be active in-game) before any `GetReachableChecks` query.
- `Combo_MM_Rando_Restore` must clear `sEntranceMap` along with the rest of the snapshot.
- **Seed identity is the invariant**: the `finalSeed` used at generation must equal the one the
  game derives at file create/load, or logic and reality silently diverge (uncompletable seeds).
  Since we already drive MM's seed from the master seed, this is an assert-worthy invariant, not
  new plumbing. Determinism means no serialization is needed.
- OOT side is already correct by construction: SoH shuffles entrances inside its own seed gen
  before `SOH_DumpRandoStaticData()`, and its logic/oracle operate on the connected region graph.

### 3.2 Portal gating

The OOT↔MM portal is scene-triggered (Happy Mask Shop → MM; Clock Tower → OOT) with no entrance
involvement. Entrance shuffle breaks the current assumption that MM is reachable from OOT start:

- With OOT **interior shuffle**, the Happy Mask Shop entrance can be anywhere. The portal gate is
  a **scene-access predicate**: "can we access scene X" (Happy Mask Shop) given the current owned
  set, evaluated each `reachableFixpoint()` iteration to gate when MM checks join the fixpoint.
  This needs a scene-reachability query on the OOT oracle (checks live *in* scenes, so a check-name
  proxy is fragile — e.g. the mask shop scene itself holds no early check).
- Symmetrically, MM **overworld shuffle** could complicate returning to South Clock Town / Clock
  Tower; MM→OOT return gating needs the same treatment once MM checks can hold OOT-progression.
- The scene-entry *triggers* themselves keep working under entrance shuffle (they fire on scene
  init regardless of which entrance led there) — only the *logic* gate is missing.

### 3.3 Generation-failure handling

2Ship's 256-attempt rejection sampler can fail and still proceed with a disconnected map. Our
fill's final validation would eventually flag unreachable advancement items, but the failure
should be explicit: surface the shuffle-failed condition through the oracle (e.g. `Reset` returns
status / a queryable flag) and abort combined generation with a clear error instead of a
confusing fill dead-end.

### 3.4 Consolidated spoiler schema

Add a game-namespaced `"entrances"` section next to `"foreign"` (`combo/rando/CrossForeign.h`):

```json
"entrances": {
  "oot": [ { "index": 411, "override": 1234, ... } ],   // SoH's existing array, verbatim
  "mm":  { "finalSeed": 123456789, "flags": ["dungeons", "interiors"] }  // derivable → store inputs
}
```

MM's layout is reproducible from seed + flags, so storing inputs (plus optionally the resolved
map for human-readable spoilers) is enough. This also future-proofs the shared trackers.

### 3.5 Upstream-merge exposure

The PR touches `2s2h/Rando/Logic/Logic.cpp`, `MiscBehavior/OnFileCreate.cpp`, and
`MiscBehavior/MiscBehavior.cpp` — files we already deviate in for the oracle exports and combo
behavior. Expect conflicts there on the next upstream pull (vendor-branch 3-way per
`docs/UPSTREAM_MERGES.md`); the new files (`EntranceShuffle.cpp/h`, `EntranceHooks.cpp`) come in
clean.

## 4. Cross-game entrances — interiors first (investigated 2026-07-05)

Scope decision: **interiors only** for the first cut; overworld ignored. Interiors are near-leaf
nodes (enter → interior → exit back through the same door), coupled pairs are natural, and neither
game's overworld topology changes. Investigation of the concrete seams says this is **feasible with
small, well-localized changes** — and notably, it does **not depend on PR #1329**: the combo layer
owns the cross mapping end to end (2Ship's entrance rando is only needed for MM-internal shuffling).

### 4.1 Runtime design

**Portal table** — combo-owned map of coupled pairs:
`{OOT exterior door ↔ MM interior}` and `{MM exterior door ↔ OOT interior}`, generated at seed
time, stored in the consolidated spoiler. Never inside either game's native shuffle.

**Sentinel entrance values** — both games have free index space for "foreign entrance" markers:
- OOT: entrance table ends at `ENTR_MAX 0x614`; grottos already use synthetic 0x0700/0x0800 ranges;
  **0x0900+ is free**. `Entrance_GetOverride` passes indices ≥ table size straight through.
- MM: entrance is `(scene<<9)|(spawn<<4)|layer`; scene ids stop at ~0x62 of a 0x7F field, so
  **scene 0x63–0x7F make usable sentinel encodings** before `Entrance_GetTableEntry` would index
  out of bounds.

**Interception** — one seam per game, at the point where the pending entrance is consumed:
- OOT: after `Entrance_OverrideNextIndex()` in the z_player exit path (`z_player.c:~5128`), before
  `Scene_SetTransitionForNextEntrance()`: if `nextEntranceIndex` is in the foreign range, suppress
  the local transition and raise the existing switch-pending flow with the mapped MM target.
- MM: before the scene load consumes `gPlayState->nextEntrance` (same site class as #1329's
  `OnPlayDestroy` hook): foreign value → suppress + raise switch with the mapped OOT target.

**Arrival at a chosen entrance** — the missing piece is small. Both resume paths hardcode arrival
today: `soh/src/code/title_setup.c:23` (`entranceIndex = outside Mask Shop`) and
`mm/src/code/title_setup.c:69` (`entrance = South Clock Town`). Make these parametric:
a static target-entrance variable + exported setters (`SOH_SetTargetEntrance` /
`MM_SetTargetEntrance`), called by the launcher before `*_ResumeGame`. ~4 lines per game plus two
exports; MM also needs `cutsceneIndex = 0` sanitation (the 0xFFEF stale-layer lesson from #1329).

**Cost** — a resume-path switch is ~50–150 ms (dominated by save flush I/O). Acceptable as a
door-transition; comparable to a normal scene load with fade.

**Return trip** — coupled by construction: the foreign interior's exit entrance is also in the
portal table, mapping back to the source game's exterior-side spawn (OOT reverse indices like
`ENTR_KOKIRI_FOREST_OUTSIDE_MIDOS_HOUSE 0x443`; MM exterior spawns like `EAST_CLOCK_TOWN,9`).

**Save/respawn semantics** (mostly benign, verify in playtest):
- Save-and-quit inside a foreign interior: the active game's `save.entrance` holds a *native*
  interior value → reload boots that game inside that interior. Correct, no work needed.
- Death/void inside a foreign interior: native respawn data was set on arrival (door entry sets
  `RESPAWN_MODE_DOWN` to the interior's own entrance) → respawns inside the interior, native.
- One-ways (song warps, owl statues, save warps) always resolve in-game — never through the
  portal table.

### 4.2 Pool construction (which interiors qualify)

Only pure leaf interiors go in the cross pool. Exclude multi-exit/special cases:
- OOT: everything SoH classifies `SpecialInterior` (Windmill, Kak Potion Shop front+back,
  Link's House, Temple of Time); the plain `Interior`-typed pairs (~43) are the candidate set.
  Examples: Mido's House (0x433↔0x443), Market Bazaar (0x4EB↔0x1D4), Lakeside Lab (0x538↔0x1CE).
- MM: interiors are `RandoRegion`s with a single exit. Exclude Stock Pot Inn (two doors) and
  Curiosity Shop (back passage ↔ Laundry Pool hideout). Examples: Milk Bar
  (`ENTRANCE(MILK_BAR,0)`), Mayor's Residence, Post Office, Honey & Darling.

**Time is safe for MM interiors**: the shuffleable Clock Town-style interiors all have
`timeSpeed = 0` (RESTRICTIONS_INDOORS) — the 3-day clock does not advance inside them, so
"player is off in OOT while conceptually inside an MM doorway" has no clock cost.

### 4.3 Fill/logic design

The leaf property collapses the "portal edges" problem: a foreign interior's checks become
reachable exactly when the source-side exterior door is reachable. Concretely in
`reachableFixpoint()` (`combo/rando/CrossWorldRando.h`):
- For each cross pair, query the *source* oracle "can we access the exterior door's scene/region"
  (the same scene-access predicate as the §3.2 portal gate).
- If yes, mark the target interior's scene externally-reachable in the *target* oracle — a small
  oracle extension (e.g. `SetExternallyReachableScenes(json)`), letting the target game's own
  logic evaluate the checks *inside* the interior (time gates, item conditions) natively.
- Check↔scene mapping already exists on both sides: MM `RandoStaticCheck.sceneId`
  (`mm/2s2h/Rando/StaticData`), OOT `Location.scene` / region `areaTable` locations.

Interiors chosen for cross pairing are removed from (or forced vanilla in) each game's native
interior shuffle pool so the two systems never fight over the same door.

### 4.4 Decisions (grilled 2026-07-05)

- **Phasing (revised 2026-07-05)**: no hardcoded-pair PoC — game traversal was already proven by
  the Mask Shop portal. Phase A shipped only the generic **cross-entrance plumbing**: parametric
  portal arrival (`gComboTargetEntrance` + `SOH_/MM_SetTargetEntrance`), arrival latch, and the
  launcher's stage/drain carry (`SOH_/MM_GetPendingCrossTarget`). Phase B = seed-time **union
  shuffle** (interiors permute freely across the selected doors, cross- and same-game outcomes)
  → portal table pushed into both games' door hooks + fill integration per §4.3. NOTE: same-game
  reassignments require an in-game interior remap (no native mechanism in MM until #1329) — decide
  full-swap-only vs union at Phase B planning.
- **Form/age**: no restriction — each game resumes its own save's Link; mismatch is narrative only.
- **Deferred to Phase B planning**: oracle extension shape (§4.3), composition with each game's
  native interior shuffle (partition proposal: cross-selected doors leave the native pools),
  Anchor/co-op interactions.
- **Verify during Phase A implementation**: MM sentinel safety — nothing else masks/switches on
  the scene field of `nextEntrance` before our hook (owl-save encoding, `Entrance_Create` callers).
- **Delivery**: two branches/PRs — `feat/entrances-tab` first, then `feat/cross-interiors-poc`.
- **Status**: Phase A implemented on `feat/cross-interiors-poc` (toggle in Shared > Entrances >
  Cross-Game; deviations logged in `docs/UPSTREAM_MERGES.md`). Awaiting playtest.

## 5. Shared menu: "Entrances" tab (design)

Extract entrance-shuffle options out of the per-game menus into a combo-owned tab in the Shared
section, below "MM Randomizer".

**How the shared menu works** (`combo/gui/ComboMenu.cpp:440-556`): games export their menus as
flat `CwMenu` POD data (`combo/menu/ComboMenuABI.h`); `AppendSectionEntries()` pulls OOT's
"Randomizer" and MM's "Rando" sections into Shared hub entries; combo renders the widgets itself
(`RenderSidebarWidgets`) — no C++ types cross the DLL boundary.

**Key enabler**: all 18 menu-visible SoH entrance options are plain CVars
(`gRandoSettings.ShuffleInteriorsEntrances`, `gRandoSettings.ShuffleDungeonsEntrances`,
`gRandoSettings.MixedEntrances`, `gRandoSettings.DecoupleEntrances`, … — definitions at
`soh/soh/Enhancements/randomizer/settings.cpp:292-427`), so a combo-side panel can read/write them
directly with `CVarGet/SetInteger`. (A 19th option, the `RSK_SHUFFLE_ENTRANCES` master toggle, is
CVar-less and never appears in the menu.) 2Ship's incoming options (#1329) are likewise CVar-backed
(`Rando::StaticData::Options` reads CVars).

**Design (fully combo-side, zero vendored changes; decided 2026-07-05)**:
1. New `HubEntry` "Entrances" in `DrawSharedPanel()`, placed after the MM Randomizer group.
2. Three sections: **OOT / MM / Cross-Game**. OOT claims **all 18** entrance options; MM is a
   placeholder until #1329; Cross-Game hosts ComboShip's own options (first occupant: the
   cross-interiors PoC toggle, §4.4).
3. `DrawEntrancesPanel()`: renders the OOT entrance widgets by **filtering the exported CwWidget
   list by CVar name** (the entrance CVar set) — reusing the game's own labels/tooltips/option
   lists so upstream changes flow through merges automatically. Styled with the existing
   `ComboMenu_Push*/Pop*` helpers (`combo/gui/ComboWidgetStyle.h`), narrow-column table layout
   like `DrawTrackerSharedPanel()`.
4. The same CVar-name filter **excludes** those widgets when rendering the OOT Randomizer
   sidebars in Shared (widget-level analog of the existing `sidebarShown()`/`skipTrackers`
   filters) — the options appear only in the Entrances tab.
5. When #1329 lands, MM's three entrance CVars join the same claim/exclude filter.

## 6. Checklist for when #1329 merges

1. Upstream pull via the vendor-branch playbook; resolve expected conflicts in
   `Logic.cpp` / `OnFileCreate.cpp` / `MiscBehavior.cpp`.
2. Wire the oracle contract (§3.1): shuffle-in-`Reset`, clear-in-`Restore`, seed-identity assert.
3. Wire the scene-access portal gate into `CrossWorldCombinedFill()` (§3.2).
4. Surface shuffle-failure from the MM oracle; abort generation explicitly (§3.3).
5. Add the `"entrances"` spoiler section (§3.4).
6. Verify headless: `COMBO_AUTOGEN_SEED` generation with each entrance-shuffle flag combination;
   confirm validation passes and the spoiler records the layout.
7. Playtest: portal round-trip with OOT interior shuffle + MM dungeon shuffle enabled.
