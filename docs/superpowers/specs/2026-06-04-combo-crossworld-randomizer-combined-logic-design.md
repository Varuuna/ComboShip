# ComboShip Cross-World Randomizer — Combined-Logic Multiworld (Design)

**Date:** 2026-06-04
**Branch:** `randomizer`
**Status:** Design approved (overall shape); per-increment specs/plans to follow.
**Supersedes (generation model):** `2026-06-04-combo-crossworld-randomizer-scope-a.md` and the
no-logic dump+permute approach (Increment 2 Phase 1). That work was a **de-risking stepping stone**,
not the destination. The mailbox channel (Increment 1) and the combo lifecycle plumbing it established
are reused unchanged.

## Goal

A **feature-complete cross-world randomizer** for ComboShip (OOT "soh" + MM "2ship" in one process):

- The player configures the **full option set of BOTH games** before any save exists.
- One **Generate** produces a single combined seed.
- Items — **including progression items** — are placed **across both worlds**. An OOT chest can hold an
  MM progression item and vice-versa.
- **Logic is respected**: the combined seed is guaranteed completable. Cross-world progression works —
  reaching an MM item sitting in an OOT chest is gated by *OOT* logic; collecting it delivers it to MM
  and unlocks further MM checks.

### Locked decisions (user, 2026-06-04)

- **Cross-items MUST include real progression.** "Restrict cross-placement to non-progression items" is
  explicitly rejected. The fill must place progression across worlds and still guarantee completability.
- **No swap-on-top.** Generating two independent per-game seeds and swapping items afterward cannot
  preserve logic and is rejected.
- **Combined-logic fill is the only acceptable model.** A single fill that understands both worlds at
  once, driving each game's real logic engine.
- **Combo-level settings UI**, available before save creation (not each game's in-game menu).
- **Foreign-item presentation = generic "gift" model + text** (carried over from Scope A; no foreign
  model rendering).

## Non-goals

- Merging the two games' logic *codebases* into one engine. We orchestrate two intact engines.
- Shared item identity (an OOT Bow ≠ an MM Bow mechanically). The key model: **an item always belongs to,
  and functions fully in, its OWN game.** A check in the *other* world merely *holds* it; collecting that
  check sends the item home, where it is granted as the real, functional item. So an MM Bow placed in an
  OOT chest is collected in OOT and delivered to MM as a working MM Bow — there is no "OOT analog" problem
  and no trophy/placeholder for progression. Each engine only ever reasons about its own items. (The
  earlier Increment-1 placeholder-rupee grant was a channel-proving stepping stone, not the model.)
- New foreign-item art/models. The "gift" treatment is **presentation only** — the brief get in the world
  where the check was found shows generic gift text ("Sent to Termina: X") instead of the foreign model;
  the item itself is fully functional once delivered home.

## Architecture

### 1. The reachability oracle (per game)

Both engines already answer "given the items I own, which of my checks are reachable?" — they just
don't expose it headlessly. Each game exposes a small, uniform, **serialized** interface (both engines
read global state and run single-threaded, so set → query → place → repeat is exactly their model):

```
Reset()                      // clear logic/inventory scratch state
SetOwnedItems(itemSet)       // load an owned-item multiset into the engine's logic state
GetReachableChecks() -> set  // checks reachable under the current owned set (+ portal state)
PlaceItem(check, item)       // commit a final placement (what the save will consume)
```

**OOT backing** (`soh/soh/Enhancements/randomizer/3drando/`):
- Item state: `Logic::SetInventory/SetAmmo/SetQuestItem/ApplyItemEffect` (`logic.cpp`), backed by a
  fill-time scratch `SaveContext` (separate from gameplay `gSaveContext`).
- Reachability: `ReachabilitySearch(allowedLocations)` (`fill.cpp:512`).
- Init order (strict): `FinalizeSettings` → `RegionTable_Init` (sets the file-global `ctx`/`logic`) →
  `GenerateLocationPool`. The prior headless crash came from calling `GenerateLocationPool` *without*
  `RegionTable_Init`; doing them in order is the supported path.
- Place: `Context::PlaceItemInLocation(rc, rg, ...)` (`SeedContext.cpp:138`).

**MM backing** (`mm/2s2h/Rando/Logic/`):
- Region graph: `Rando::Logic::Regions` (`Logic.cpp:10`), region conditions are pure `std::function<bool()>`
  reading only global state (`gSaveContext`, `RANDO_SAVE_OPTIONS`, `gCurrentRegionTime`, CVars).
- Item state: `gSaveContext` inventory/quest/`RANDO_*` + `Rando::GiveItem`. Reachability mutates
  `gSaveContext` (scratch during generation).
- Reachability: the sphere-search core of `ApplyGlitchlessLogicToSaveContext` /`FindReachableRegions`
  (`GlitchlessLogic.cpp`, `Logic.cpp:162`), including the 3-day-clock time logic (45 TimeSlices,
  uint64 bitmask, `gCurrentRegionTime`).

The oracle implementations live in **port/enhancement code** in each game and are exposed as
`extern "C"` exports (`SOH_RandoOracle_*`, `MM_RandoOracle_*`). The interchange across the DLL boundary
is by **string item/check names** (both games already string-name items/checks in spoilers), plus a
small grant-mapping table where needed. No enum merge.

### 2. The combined multiworld fill (combo layer)

Lives in the combo layer (the only code linked against both DLLs). It runs an **assumed/forward fill
over the union** of both games' pools, joined by the portal:

- Combined inventory is partitioned: OOT-items advance only the OOT oracle; MM-items only the MM oracle.
- **Portal model:** OOT reachable from start. MM checks become reachable only once the OOT→MM portal
  location (the combo transition point, today SCENE_MIDOS_HOUSE) is reachable in OOT. (Return-gating —
  OOT checks that require having been to MM — is modeled if/when needed; initial model is OOT⊇start,
  MM unlocked by portal.)
- **Fixpoint sphere search:** repeatedly (a) push currently-owned OOT items into the OOT oracle and read
  its reachable set, (b) push owned MM items into the MM oracle (gated by portal) and read its reachable
  set, (c) place items from the combined pool into newly-reachable empty checks in *either* world; a
  foreign item placed in a check yields the *other* world's inventory an item, which feeds the next
  iteration. Continue until the pool is placed and the seed is beatable.
- Completability is guaranteed by the same assumed-fill discipline each game uses today, lifted to the
  union (place progression only into currently-reachable locations; retry/undo on dead ends).

**Junk / filler items carry no logic constraint.** Only *progression* (advancement) items must be placed
into currently-reachable locations to preserve completability. Junk — green/blue rupees, ammo, recovery
hearts, and anything else non-advancement — is free filler: after progression is placed, every remaining
empty check (in either world) is filled with junk with no reachability check. Junk is also freely
**interchangeable across worlds and substitutable** — an MM check's junk slot may hold an OOT green rupee
(or any junk), and vice-versa, since junk is inert for progression and each game can render/grant a
sensible local junk equivalent. This is what keeps pool-balancing trivial: junk is the flexible padding
that absorbs any size mismatch between the two games' check/item counts (no forced 1:1). Junk delivered
cross-world still routes through the normal delivery path but never gates anything.

Output: a **combined spoiler** with each game's placement slice **plus per-check foreign markers**
(`{srcGame, itemName}`) for checks that hold the other world's item.

### 3. The MM warm-up constraint (THE central risk)

`Rando::Logic::Regions` is populated only when `ShipInit::InitAll()` runs during MM's boot
(`BenPort.cpp` `InitOTR`). It is **empty at OOT save-creation time**, and calling `InitAll()` early was
exactly the prior crash (it runs *all* registered init funcs, not just region builders; they all live in
the `"*"` category with no selective invocation API). OOT's engine, by contrast, is live whenever OOT
runs.

Therefore the combined fill can only run when **both engines are simultaneously initialized**. Plan:
a **one-time MM rando-runtime warm-up at combo startup** — after the shared libultraship Context exists
(post-`SOH_Init`) and MM archives are loaded — that builds `Regions` once and leaves them live for the
process lifetime (the graph never changes after construction). MM's oracle is then callable for the rest
of the session, including at Generate time.

**Whether that warm-up runs cleanly is the make-or-break unknown.** Increment 1 is a narrow spike to
prove it before anything is built on top.

### 4. Combo settings window

Both option sets are **data-enumerable and CVar-backed in the shared store**, so one window can render
both:
- MM: `Rando::StaticData::Options` (id, name, `cvar`, default) + value-label maps from `Rando/Menu.cpp`.
- OOT: `Rando::Settings::GetInstance()->GetAllOptions()` (`Option` with CVar name, labels, category,
  hidden state).
- All via `CVarGetInteger/SetInteger/...` (`<libultraship/bridge/consolevariablebridge.h>`); cross-DLL
  writes are immediately visible (no per-DLL caching that breaks this).

The window is a combo-owned **overlay GuiWindow** added at OOT boot (OOT boots first and owns the menu
slot), persists across transitions, and is reachable before file-select. A **Generate** action runs the
combined fill from the current CVars and stashes the result; save creation then consumes it (extending
the existing `gComboGenerateCallback` path rather than auto-generating with defaults).

### 5. Cross-game delivery (reuses Increment 1)

The Increment-1 mailbox channel already delivers items between games (OOT→MM verified). This increment
wires the generator's foreign markers to it:
- At **pickup** of a check whose marker says "foreign", divert to the mailbox (send) instead of granting
  locally — OOT send branch `hook_handlers.cpp:380`, MM send branch `CheckQueue.cpp:37`.
- The **receiving** game drains the mailbox (already implemented) and grants the **real, functional item**
  in its home game (replacing the Increment-1 placeholder rupee), shown with "Received from Hyrule/Termina:
  X". This real grant is what makes cross-world progression actually work.

## Increment roadmap

Each increment is independently verifiable and gets its own spec→plan→build.

1. **Feasibility spike — MM headless warm-up.** Build `Regions` once at startup; expose a throwaway
   `MM_RandoOracle_GetReachableChecks` for a hardcoded item set; log the reachable count with OOT also
   live. **Gate:** non-empty, sane, no crash/shared-state corruption. If it fails, revisit the model
   (e.g. run the fill at a different lifecycle point) before proceeding.
2. **Per-game reachability oracles.** Implement and export the full oracle for each game. **Verify:**
   each oracle, driven standalone with that game's vanilla options, reproduces a self-completable fill
   (sanity-check against the game's own generator on a known seed).
3. **Combined multiworld fill.** Combo-layer orchestrator + portal model; emits the combined spoiler with
   cross-placements + foreign markers. **Verify:** generated seeds pass a combined reachability replay
   (a check-the-fill pass proving 100% of progression is reachable across both worlds from an empty start).
4. **Combo settings window + Generate UX.** One window over both option tables; Generate consumes CVars.
   **Verify:** toggling an option changes which checks are shuffled in the resulting spoiler; the window is
   reachable before save creation and persists across a transition.
5. **Cross-game delivery.** Foreign markers → mailbox send at pickup; gift presentation. **Verify:**
   in-game, a foreign progression item picked up in world A appears in world B and unlocks a B check that
   was gated on it (the headline end-to-end test).

## Open risks / things to watch

- **MM warm-up safety** (Increment 1 gate) — primary risk.
- **MM time logic in a headless fill** — reachability depends on `gCurrentRegionTime` and clock ownership;
  the oracle must drive MM's time expansion correctly outside a running game.
- **Two fill paradigms** — OOT assumed (backward) vs MM forward fill. The combined orchestrator imposes a
  single discipline (assumed fill over the union) using each oracle only for reachability + commit, rather
  than reusing either game's top-level fill loop.
- **gSaveContext scratch use during MM generation** — generation mutates MM's `gSaveContext`; must be done
  on a scratch/restored context so it can't corrupt a real save or the warmed-up state.
- **Portal return-gating** — initial model ignores "OOT checks that require having visited MM"; revisit if
  any real OOT logic needs it.
- **Performance** — combined fixpoint over ~2700 OOT + ~2300 MM checks; each game's own fill is sub-second,
  but the union with cross-delivery iterations needs to stay reasonable (target: a few seconds, synchronous
  with a "Generating…" frame).

## Reuse / unchanged

- Increment-1 mailbox (`combo/rando/CrossMailbox.h`, `saves/combo/slot{N}.mailbox.json`, receive drains).
- Combo lifecycle plumbing: `gComboGenerateCallback` (z_sram.c), `Combo_OnGenerate`/`Combo_OnOOTSaveInit`,
  `SOH_*`/`MM_*` export + callback pattern, the menu-swap fix.
- Per-game save application: OOT `Randomizer_InitSaveFile` consuming `itemLocationTable`; MM
  `MM_InitRandoSaveFile` consuming a spoiler slice via `Spoiler::ApplyToSaveContext`.
