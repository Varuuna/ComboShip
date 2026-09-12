# Cross-game items

Last updated 2026-09-10. Current state: Phase 1 code is in (shared Lens of Truth), builds on Windows,
passes the headless generator and validator, and works in game: pickup in either direction, the Lens
surviving the portal handoff, and a quit-without-saving reverting both games. The file map lives in
the 2026-09-09 entry of `deviations/rando.md`.

## Goal

An item found in one game should be usable in the other. Today a foreign item is delivered into its
home game's save and drawn with the right model at the pickup, but it never does anything in the
game it was found in. There is no notion of a shared item in the fill, the settings, or either DLL.

We follow OoTMM. It splits the feature in two:

- **Shared items.** Both games already have the item (bow, lens, magic, wallet...). The two copies
  become one logical item: picking it up in either game grants it in both, and logic counts it for
  both. No new gameplay code.
- **Extensions.** One game gains an item it never had (Blast Mask in OOT, hammer in MM). New item id,
  inventory slot, icon, equipped model, player behaviour. OoTMM gates most `shared*` options on the
  matching extension.

Order of work: shared items, then the cross-game teleport songs, then MM items in OOT, then OOT
items in MM.

## Where OoTMM does it

Useful when porting a specific item:

- Pool merge: `packages/logic/src/world/transform.ts`, `setupSharedItems`. Both copies are replaced by
  `SHARED_X`, then the surplus is removed to an explicit count per item (bows 3+3 -> 3, hookshot
  2+1 -> 2, lens 1+1 -> 1).
- Logic: macros in `data/macros/macros_*.yml` of the form `has(X) || has(SHARED_X)`.
- Runtime grant: `src/common/item/item_add.c`, `kSimpleSharedItems` and `comboAddItemRaw`. Granting
  one half grants the other in the same call.
- Consumable sync: `comboSyncItems` in `src/common/item/item.c`, run on every save write, gated per
  shared flag (arrows under `sharedBows`, bombs under `sharedBombBags`, magic under `sharedMagic`, and
  so on). Not adopted here; see Design rules.
- Which id a shared item becomes at a given check: `lib/combo/randomizer/checks.ts`.
- Extensions: `include/combo/data/items.h`, `src/oot/actors/Player.c`, `gi.yml` `object: [mm, ...]`.
- Teleport songs: `src/oot/ocarina.c` (Song of Soaring in OOT: `SetupSoaring`, `PrepareSoaringScreen`,
  `HandleSoaring`) and `src/mm/ocarina.c` (OOT warp songs in MM: `Ocarina_HandleLastPlayedSong`,
  `warpTexts`). Both end in a cross-game transition to an entrance in the other game.

## Design rules

### Where each OoTMM piece lands in ComboShip

| OoTMM | ComboShip |
|---|---|
| `shared*` settings | `gCombo.Rando.Shared.<key>` CVars, one checkbox per pair on the "Shared Items" page of the combo menu. The launcher and the headless tool read them through a soh.dll export and bake them into the consolidated seed as `sharedItems`; the validator reads the spoiler only. |
| Pool merge | A pre-pass in the fill right after the dumps are parsed. MM copies leave the pool, OOT copies are trimmed or cloned to the merged count. The existing per-game balancer junk-pads MM. |
| Union macro | When a shared item enters one game's owned set, its pair name is pushed into the other's. Native logic graphs are untouched. Applies to the fill and the validator. |
| Per-check id | A shared item placed at an MM check is emitted under MM's native name. It is not a foreign marker and is not game-suffixed in the spoiler. |
| Give both | A hook at each game's local-pickup seam hands the pair to the existing cross-deliver callback. Dormant grants and Anchor receives are excluded. |
| `comboSyncItems` | Not adopted. Capacity is shared, counts stay per game; see "Counts" below. |

### Dedupe

Three separate problems:

- **Pool.** The pairing table carries the merged count. That is the only place the count is decided.
- **Grants.** The launcher's cross-deliver dedupe is keyed by check plus item. Pair grants for
  progressive items must set a level, not increment one: the hook passes the source game's resulting
  level and the target raises its copy to match. Re-collecting after a reload then does nothing on
  the far side. Lens is one-shot, so this reduces to "grant if missing".
- **Levels.** Every enabled pair is max-reconciled at both transitions and on OOT save load. Shared
  progression only goes up, so the higher level is always right. This also covers one-sided grants:
  starting items, forced placements, co-op backfill.

### Save timing

OoTMM writes both saves in one flash write, so a pickup either persists in both games or in neither.
ComboShip cannot do that: the OOT copy of a cross-grant is written to disk at once, MM writes only on
owl save, Song of Time, portal return, or an autosave-on reset.

| Pickup in | Local copy persists | Far copy persists | Quit without saving |
|---|---|---|---|
| MM | next MM save | immediately | MM loses it, OOT keeps it, the MM check can be collected again |
| OOT | next OOT save | immediately | OOT loses it, MM keeps it |

Foreign items write the far side immediately, and that is fine for them: if the finder quits without
saving, the far game keeps the item and the check is simply open to collect again.

Shared pairs do not get the immediate write. The far half goes into the dormant game's memory and
reaches disk when that game saves on its own or at the next portal handoff, which writes the arriving
game's resident save before it resumes (both resume paths reload the slot from disk, so without this
the memory-only half would be dropped on arrival). A quit-without-saving never reaches that write, so
it reverts both halves: the far game reloads from disk on the way back to the title, and a reload has
nothing to re-propagate. The first attempt at this had the immediate write, and the reconcile re-seeded the Lens
from whichever side still held it on every reload, so the reopened chest handed out junk. The reconcile
stays as the safety net for the remaining one-sided cases (the far game saved on its own before the
source did, co-op backfill), and it runs at handoffs and on OOT save load.

Two supporting rules make the reopen behave like a first pickup: a shared half MM already owns is
given as itself rather than converted to junk (and a dormant grant of an already-owned half is a no-op
rather than a consolation rupee), and the launcher's delivery dedupe is cleared on every slot bind and
load instead of living for the whole session.

### Counts

OoTMM shares the consumable along with the item: one bow means one quiver level and one arrow count,
copied between the saves on every write. We share only the capacity. Both games receive the quiver,
bomb bag, or magic-meter upgrade; each keeps its own arrows, bombs, magic, and rupees.

Reasons: logic only checks ownership, so nothing in the fill or validator changes; there is no
transition-time copy and therefore no last-writer-wins ordering to get right; a Song of Time reset
cannot zero the arrows OOT sees next; and the save-asymmetry surface shrinks to progression, which the
reconcile already covers. Refill packs stay per game, as they are today. If OoTMM parity is wanted
later, a consumable copy can be added as its own option at the transition seam without touching the
pairing table.

## Phase 1: shared items

Pairing table. Each row is one line in `combo/rando/CrossShared.h`; the Shared Items menu page,
settings bit, fill, validator and hooks all derive from it.

| Pair | OOT pool | MM pool | Merged | Needs |
|---|---|---|---|---|
| Lens of Truth | 1 | 1 | 1 | nothing; first pair |
| Progressive Bow | 4 | 3 | 3 | level-set grant |
| Progressive Magic Meter / Progressive Magic | 3 | 2 | 2 | level-set grant |
| Progressive Bomb Bag | 4 | 3 | 3 | level-set grant |

Tasks:

| # | Task | State |
|---|---|---|
| 1 | Pairing table, settings bitmask, name lookup | done |
| 2 | Menu checkboxes, soh.dll read export, launcher and headless plumbing, `sharedItems` in the seed | done |
| 3 | Fill pre-pass, owned-set pairing, native-name emission, `shared[]`, suffix bypass | done |
| 4 | Give-both hooks in OOT and MM, check+item dedupe key | done; the OOT hook had to move once, see below |
| 5 | Level probes per DLL and the launcher reconcile at transitions and OOT save load | done |
| 6 | Validator pairing; the headless tool reads the menu CVars like the launcher | done |
| 7 | Deviation record | done |
| 8 | Level-set grant for progressive pairs | todo, before bows |
| 9 | Add bow, magic, bomb bag rows | todo, after 8 |

Lesson from the first in-game run: SoH chests and shops do not go through the queued-check path, and a
vanilla-table item reaches the receive hook with no check identity. The OOT hook now runs before the
queued-check gate and takes the check from the player's held get-item entry, guarded by a dormant-give
flag that mirrors MM's.

## Phase 2: cross-game teleport songs

The songs that move you between games. In OoTMM these are the two extensions `songSoaringOot` and
`songMinuetMm` through `songPreludeMm`, plus their `sharedSong*` pairs once the extension is on.

What they do there:

- **Song of Soaring in OOT.** Child only by default. Playing it opens MM's owl-statue map, showing only
  the statues already activated in the MM save (with none activated you get a "you have yet to leave
  your mark" message). Picking one is a cross-game transition into MM at that statue.
- **OOT warp songs in MM.** Playing one asks "Soar to X?" and transitions into OOT at that warp pad.
  Refused wherever MM itself forbids the Song of Soaring (dungeons, boss rooms).
- Cross-game Farore's Wind is a separate OoTMM option and is out of scope here.

Work, in order:

1. **Entrance-targeted handoff.** Today the portal always lands in South Clock Town and the return
   resumes OOT where it left off. Generalise the launcher's switch to take a target game, entrance
   and spawn, have MM's boot and resume paths honour a requested entrance, and have OOT's resume
   load a requested entrance instead of resuming in place. Decide the age rule for arriving in OOT
   (proposal: the age OOT was last in, as OoTMM does without `crossAge`). A song warp is a
   transition like the portal: it runs the shared-item reconcile and persists MM the way the portal
   return does. Useful on its own for debugging, so it comes first.
2. **Song of Soaring in OOT.** One new OOT quest item: RG id, quest bit in a free save field, pause
   quest icon and staff text through the foreign draw headers, MM's note sequence added to SoH's
   ocarina recognition. A destination chooser listing the MM statues activated in the dormant save
   (one export to read the owl flags), then the handoff from step 1. Child only to start.
3. **OOT warp songs in MM.** Six new MM quest items with the same shape on the 2Ship side (RI ids,
   quest bits, icons from OOT's resource manager, ocarina recognition), the "Soar to X?" confirm,
   MM's existing soaring restrictions, then the handoff to the warp pad.
4. **Shared rows.** Once both extensions exist, Song of Soaring and the six warp songs join the Phase 1
   table as one-shot pairs gated on their extension, exactly like OoTMM's `sharedSong*`.

Logic: the two oracles cannot express an OOT song granting access to an MM region, or the reverse.
Leave the warps out of logic at first. A route logic does not know about can only make a seed more
open, never less beatable, so seeds stay valid; it only costs hint accuracy. Revisit once the
reconcile and handoff are stable.

## Phase 3: MM items usable in OOT

Follow OoTMM's `*Oot` options and SoH's own Roc's Feather, which is already a foreign-origin item wired
end to end (item id, get-item entry, draw, logic, tracker, behaviour). Rough order by cost:

1. Stone Mask. One enemy-awareness check, no button action.
2. Blast Mask. B-button action.
3. Kamaro Mask.
4. Skeleton Key. SoH already has one, so this is really a share.
5. Powder Keg.
6. MM songs.

Per item: RG id, save flag, pause-menu slot, icon loaded through MM's resource manager, behaviour.
Get-item models already render through the foreign draw headers. Each extension then earns a row in
the Phase 1 table.

## Phase 4: OOT items usable in MM

Hammer, boots, tunics, hookshot length, magic arrows. This is the heavy Player-code side in 2Ship and
is deferred.

## Testing

- Headless: turn the pair on in the combo menu (or set `gCombo.Rando.Shared.lensOfTruth` in
  `comboship.json`), run `comborando --seed <s>`, check the fill log for
  `oot 1 + mm 1 copies -> 1 shared`, then `--playthrough` on the spoiler. Run a handful of seeds so
  the Lens lands at an MM check at least once.
- In game: plando the single Lens at an early OOT chest, collect it, expect the "Shared with Termina"
  toast and the Lens in MM's inventory. Then the reverse with the Lens at an MM check. Finally a
  quit-without-saving in MM after a pickup: neither game has the Lens afterwards, nothing is granted
  on the reload, and reopening the chest gives the Lens again, not junk.
- Before bows land: the same three runs with a progressive pair. Both games must end on the same
  quiver level after each run, while the arrow counts stay independent.
- Teleport songs: warp from OOT to each activated MM statue and back through the Clock Tower; warp
  from MM to each OOT pad as child and as adult; play a warp song in an MM dungeon and expect the
  refusal; quit without saving in MM after a song warp and check the reconcile on the next handoff.

## Open questions

- Shared items live in the OOT name namespace inside the fill. That is an internal choice; emitted
  placements always use the check game's native name. Revisit if a pair ever needs MM-side counting.
- Starting items are applied inside each oracle and are not mirrored into the other game's owned set
  during the fill. The fill is slightly conservative as a result; the reconcile grants the pair at
  runtime. Fine for now.
