# ComboShip deviations — Randomizer & cross-world fill

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

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

**Invariant (corrected 2026-07-29 — see "Pool/check balance" below):** the combo fill enforces
`items == checks` PER GAME. The dumps deliberately over-supply and the over-supply is **progression**,
not junk, so reconciling it means sacrificing junk to make room. Shuffled shopsanity slots aren't in
`itemPool` (`CountEmptyLocations(false)` excludes shops), so the OOT dump adds each shuffled shop slot's
vanilla buy item to `pool[]`. Link's Pocket is excluded from the dump entirely — it stays owned by
`SOH_GetForcedPlacements`, which reserves its item out of `pool[]`.

## Pool/check balance: only JUNK may ever be discarded (2026-07-29)

**The bug this replaced.** The old invariant above read "*`pool.size() >= checks.size()` … surplus is
junk … that the cross fill drops*". That premise was false, and it made a truncating `for` loop look
safe. A reported seed (masterSeed 1568694522, No Logic both games, ALR on) silently lost three OOT
advancement items — `Volvagia's Soul`, `Nocturne of Shadow`, `Water Temple Boss Key` — leaving
`Volvagia` (Goron's Ruby), `Fire Temple Volvagia Heart Container` and the Water Temple reward
permanently unobtainable while generation reported success.

**Why the pool exceeds the checks.** Both generators add items that have no vanilla location *precisely
because they are special*, and every one is progression:
- MM (`Rando/Logic/GeneratePools.cpp`): progressive sword `:158`, hero shield `:159`, boss souls
  `:164-171`, enemy souls `:174-178`, clock items `:181-196`, swim `:199-201`, progressive wallet `:226`,
  **20x `RI_TRIFORCE_PIECE`** `:230-236`, skeleton key `:238` (+27), minus starting items whose locations
  remain `:281-287` (−18) = **+9**. Excluded checks (`:139-149`) push the item but not the check: +1 each.
  2Ship reconciles in `MiscBehavior/OnFileCreate.cpp:91-137`; the dump only mirrored the pad direction.
- OOT: `ComboFillConfined` runs `FillExcludedLocations()` (`fill.cpp:1565`), which places a *fresh*
  `GetJunkItem()` so the location moves to `fixed[]` while its pool item stays (+1 each); plus
  `RC_LINKS_POCKET`, counted in `locCount` but omitted from `checks[]` (+1). Stock soh discards its
  leftover safely at `fill.cpp:1505` only because `:1497-1499` extracts every `IsAdvancement()` item first.

**The rules.**
1. **Only items whose native category is `JUNK` may be discarded** — never advancement, hearts, masks,
   keys or tokens. If the surplus can't be absorbed by `JUNK` alone, generation fails loudly.
2. **No Logic constrains PLACEMENT, never MEMBERSHIP.** It may put any item on any check, including
   unreachable ones (validation tolerates that outside `ALL_REACHABLE`). It is never licence to omit an
   item. "Anywhere" is not "nowhere".

**Implementation.**
- `pool[]` entries now carry `category` (`OTRGlobals.cpp` `comboCategory`, `BenPort.cpp` `categoryName`)
  — a stable string from each game's own taxonomy (OOT `GetItemCategory`, MM `RandoItemType`), which are
  the same 7 categories modulo MM's `mask`/`strayFairy`. The old single `advancement` bool fused junk with
  hearts and traps, so "only junk" was not expressible; `advancement` is retained as the orthogonal axis
  (it selects the fill *phase*, not discardability). Unknown/absent category => never discardable.
- `CrossWorldRando.h` balances per game after the forced-placement block, before `CwRng rng(masterSeed)`:
  trims surplus `JUNK` most-duplicated-name-first (RNG-free, so the seeded stream can't shift), pads a
  deficit with cloned junk, hard-fails if `JUNK` runs out. Per-game rather than global on purpose: under
  global-only an OOT surplus and an MM deficit cancel and the defect stays invisible.
- Phase B's stream is `fastFillItems` (it holds junk *and* relaxed OOT advancement; the old name
  `junkToPlace` asserted the discarded tail was junk while it discarded a boss key). A `stable_partition`
  puts advancement ahead of junk with a named `mustPlace` boundary, so the truncatable tail is junk by
  construction. This does not narrow No Logic's freedom: `allChecks` is the uniformly shuffled sequence,
  so advancement still takes a uniformly random subset of the free checks.
- Any leftover that isn't `JUNK`, any unfilled check, or `ji < mustPlace` is a hard failure (returns
  `!success` -> the caller's `kFillAttempts` reroll -> a loud user-visible error), not a warning.
- Post-fill backstop: the full pool multiset must appear in `placements`, checked per pass ahead of the
  validation fixpoint. Non-`JUNK` residue is reported by name — this is the assertion that would have
  named the three lost items. Guards key on **category**, never on `advancement`: a Heart Container is
  `advancement == false`, so an advancement-keyed guard would have let a stranded heart pass.
- Deleted the Link's Pocket junk filler: the dump's pool already carries LP's item, so the reservation is
  self-balancing and the filler left a permanent +1 surplus.

**Seed compatibility:** every existing seed string now yields different placements — removing surplus
items changes `cwShuffle`'s draw count, and no correct fix avoids that. Headless <-> in-game parity is
preserved (all logic is in the shared header).

**Known follow-up:** Heart Containers stay `advancement == false` (`comboIsAdv`'s deliberate demotion);
`category == HEALTH` is what protects them from discarding. They can still land on an unreachable check
in relaxed modes. Revisit only if that becomes a real complaint.

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
  `RSM_OPEN_RANDOMIZER_SETTINGS` → `SOH_OpenComboRandoSettings()` → comboui export
  `ComboUI_OpenRandomizerSettings()` (opens the combo menu on its Randomizer tab; the menu's
  visibility is object-state, so setting `gOpenWindows.Menu` no longer works); the
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
  dead, warp to `ENTR_TEMPLE_OF_TIME_WARP_PAD` (adult, no cutscene) instead of the Chamber of the
  Sages credits. The pedestal is the guaranteed route to Child (the Master Sword is in hand after
  Ganon) and from there to the Mask Shop portal.
- `mm/src/overlays/actors/ovl_Boss_07/z_boss_07.c` — Majora's Wrath death: if not both dead, warp to
  `ENTRANCE(SOUTH_CLOCK_TOWN, 0)` (no cutscene) instead of the Termina Field `0xFFF7` credits.

**Revised (2026-08-13, issue #137):** the first-kill warp originally targeted
`ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP` with `linkAgeOnLoad = 0` — but 0 is `LINK_AGE_ADULT`, so
Link stayed adult and the adult scene layer (+2) landed him in Market Ruins with the Mask Shop
boarded up. Now warps to the Temple of Time adult spawn instead of forcing child: the player must
travel to Child via the pedestal anyway to reach the portal.

**Playtest-pending:** both orders (Ganon-first and Majora-first); portal reachable after each warp;
resume-after-first-kill keeps the flag; finale plays on the second kill.

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
(only its own-game repeat-hint pool is protected). ~~Ganondorf's combined-hint phrasing variant isn't
mirrored~~ — fixed 2026-08-29, see below.

## Ganondorf hint variants + foreign item names in hints (2026-08-29)

**Why:** a player's Ganondorf hint showed an EMPTY textbox. `BuildGanondorfHint` (StaticHints.cpp)
picks the message by INDEX from live state — 1 or 2 while Master Sword is shuffled and unowned — but
the combo payload carried a single message, and `Hint::GetHintMessage` returns empty text for an
out-of-range index. The seed had Master Sword shuffled, so the player could never see a hint.

- `combo/rando/CrossHints.h` — new `itemAreaText` resolver (an OOT item's area wherever it landed, in
  either game), shared with the altar block's `rewardArea`. The Ganondorf hint now emits all three
  variants in native's `hintKeys` order (LA_ONLY / MS_ONLY / LA_AND_MS) when the sword is shuffled and
  isn't a starting item. `nullopt` (item in no check at all — starting item, or category not shuffled)
  means the hint is not emitted, so native fills that slot from its own placement instead: a static
  hint never names a location for an item that isn't in the pool.
- `soh/soh/OTRGlobals.cpp` — `SOH_DumpRandoHintData` exports `shuffleMasterSword`/`startingMasterSword`;
  `Combo_IsUsedHintTemplate` allows the two extra Ganondorf templates plus `RHT_YOUR_POCKET`.
- `soh/soh/Enhancements/randomizer/hint.cpp` — `GetHintMessage` falls back to the LAST message when a
  builder indexes past a MESSAGE hint's payload, so an old seed (or any short payload) shows the one
  variant it has instead of a blank box. It has to sit here, not in the apply walk: `LoadRandomizer`
  rebuilds every hint from the save's own `comboMessagesEn` array AFTER `SOH_ApplyComboHints` runs, so
  anything the walk padded is thrown away on a save load.
- `soh/soh/Enhancements/randomizer/hint.cpp` — `GetItemHintText` resolves `RG_COMBO_FOREIGN` through
  the foreign map. The sentinel's own hint key is `RHT_NONE`, which renders as the literal string
  "No Hint", so every item-naming hint pointing at a cross-placed check used to say "I will give you
  No Hint!" (Loach, HBA, Malon, Big Poes, Chickens, Biggoron, Frogs, OoT, Mask Shop). A lookup that
  races the blob push falls back to "something", never to the sentinel.
- `soh/soh/Enhancements/randomizer/hook_handlers.{h,cpp}` — `OOT_LookupForeignByCheck`, the same
  lookup keyed by check for callers with no save/location context.
- `combo/gui/ComboHintTracker.cpp` — shows a multi-message hint's last (most complete) variant.

Verified headlessly: regenerating the reported seed changes only the Ganondorf entry (1 -> 3 messages,
message 0 byte-identical); all 42 other OOT hints, the MM hints and both placement maps are unchanged,
because the new templates have no clarity variants and so draw nothing from the RNG.

**Known limitations (unchanged):** composed area names are English in all three languages, including
for OOT areas that do have translations (`areaText` wraps them in `EnglishOnly`); warp-song hints still
use native's OOT-only area resolution; the 12 area-type NPC static hints (Sheik, boss keys, Dampé,
Greg, Saria, Mido, Fishing Pole) still say "an Isolated Place" for a cross-placed target — that is the
follow-up branch.

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
stays all-reachable (MM reachability must never degrade).

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`):** the dump gains an `"accessibility"` block
(`noLogic`, `allLocationsReachable`, `lockOverworldDoors`) read from the live
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

See "Gate MM on the OOT→MM portal region" below — the portal is no longer ungated, which is what makes
the MM-always-all-reachable rule above sound.

**`combo/rando/ComboPlaythrough.h`:** `MmOnlyMajoraGoal` — under NO_LOGIC the pare-down (WotH) gates
requiredness on MM only, since OOT may be structurally unbeatable from empty. Wired at both
`PareDownPlaythrough` call sites (`ComboShip.cpp`, `ComboRandoHeadless.cpp`).

**`combo/ComboRandoHeadless.cpp` (`--playthrough` verdict):** a No Logic seed that is OOT/Ganon
unbeatable but keeps MM fully reachable + Majora beatable is downgraded from FAIL to PASS (No Logic).

**Hearts:** see the preceding "OOT hearts as junk" section — shrinks the OOT advancement pool.

## Cross-game foreign-draw hardening: cache liveness, colour pinning, recipe validation (2026-07-26)

Review follow-ups on the foreign-item draw path (`combo/menu/ComboForeignDraw{OOT,MM}.h`,
`ComboItemDraw{ABI,OOT,MM}.h`, `ComboForeignAnim.h`, both `z_draw.c` exposures).

**1. Transient vs permanent resolution failures.** The per-check draw cache used to write a sticky
`ok=false` entry on *every* failure path, so a lookup that merely ran too early froze the sentinel
into that check for the whole save slot — and worst on `stateDependent` recipes, which re-resolve
every frame and therefore get thousands of chances to hit a transient failure. Two changes:
- The fill is now a separate function returning `Ok / Unknown / NotReady`, and the recipe is built
  into a **local** that is only copied into the cache after the attempt — a failure can never clobber
  a live cached recipe (this also fixed the animated branch silently dropping `stateDependent`).
  `NotReady` erases the entry and retries next frame; only `Unknown` negative-caches (that path is
  what keeps junk checks from making a cross-DLL call every frame).
- Producers can now say "not ready" over the ABI: `CW_DRAW_NOT_READY (-1)`. `OOT_GetItemDrawInfo`
  returns it while `OTRGlobals::Instance` / `gRandomizer` / `gRandoContext` are null (normal while OOT
  is dormant) instead of the old `0`.
- Both draw caches are additionally keyed on the foreign-map **generation** — MM already had
  `Rando::MiscBehavior::ComboRandoGen()`; OOT gained `OOT_ForeignMapGen()`, bumped by
  `SOH_LoadComboRando` and by the lazy rebuild inside `OOT_LookupForeign`. A negative entry recorded
  before the spoiler blob arrived is therefore discarded when the map is (re)built, without paying a
  per-frame retry for seeds that genuinely have no foreign checks.

**2. Colour pinning on every handler.** MM's scene `AnimatedMaterial` type-4 entries leave a
continuously-interpolated prim colour in the pipeline; a foreign recipe that only sets env inherits
it (playtest bug: an OOT key ring cycling colours in MM). The pin existed on two handlers only. It is
now a macro per consumer (`MM_FOREIGN_PIN_{OPA,XLU}` / `OOT_FOREIGN_PIN_{OPA,XLU}`) emitted
immediately after *every* `Gfx_SetupDL25_*` / `Gfx_SetupDL_25*` and before the handler's own colour
commands, including both streams of the ops interpreter (`CW_OP_SETUP_OPA` / `CW_OP_SETUP_XLU` each
re-pin, so an ops recipe opening on XLU — MM's `DrawDoubleDefense` — is covered).
`MM_DrawForeignMusicNote` is the one exception to "pin the setup's stream": its OOT original sets up
*Opa* state but submits on XLU, so the XLU pin is used.

**3/4. Recipe validation.** soh's `z_draw.c` mirror of `CwDrawKind` now carries explicit `= N` values
(MM's already did), so inserting a kind can't silently re-tag every OOT recipe. And
`CwMinDlistsForKind()` (in the ABI header) gives the lowest `dlists[]` slot each kind's handler
blind-indexes; both resolvers reject a shorter recipe as `Unknown` before caching it, so a handler
can never `gSPDisplayList(disp, NULL)`. `CW_DRAW_KIND_OPS` is exempt — the interpreter already
bounds-checks each `CW_OP_DLIST` index.

**5/6. Misc.** The OOT-host animated branch now copies `anim.stateDependent` (the MM mirror always
did). All four `*_GetItem{,Anim}DrawInfo` exports wrap their **entire** body in `try/catch(...)`:
`itemNameToEnum.find` / `GetItemIdFromDisplayName` construct `std::string`s and `CVarGetInteger`
runs before the fill, and an unwind across the C ABI into the other DLL is unrecoverable.

**Detail moved out of inline comments** (project rule: 1-2 line inline comments):
- *Draw bytecode (`CwDrawOp`)*: for funcs shaped as per-DL transforms/colours rather than one flat
  OPA/XLU submission (MM's `DrawClock`, `DrawOwlStatue`, `DrawTycoonWallet`). The producer folds its
  own live values into the ops; the consumer replays them against its own matrix stack and gbi.
  Anything needing GPU state beyond this (texture scrolls, skeletons) gets a dedicated `CwDrawKind`.
- *`matAnimPath`*: generalizes `xluSeg8TexScroll` for items whose animation lives in a
  `TextureAnimation` resource (MM's Moon's Tear). The consumer loads it from the owning game's RM
  (`ComboForeignTexAnim_Run`) and binds the animated segment before the DLs; the path is the owning
  game's own unrouted `"__OTR__..."` string. The skulltula token has *no* such resource — MM draws it
  with an inline `Gfx_TwoTexScrollEx` — hence the separate hardcoded flag.
- *Handler invariant (`ComboForeignDraw{MM,OOT}.h`)*: every DL that samples a segment is preceded by
  a bind of that segment **in the same stream**, so we never submit a DL against an unbound segment
  (the documented garbage-DL crash class). Scroll/matrix params are constant per func and taken
  verbatim from the owning game's source; only per-instance colours travel as data.
- *No namespace in `ComboForeignAnim.h`*: `OPEN_DISPS`/`CLOSE_DISPS` embed block-scope declarations of
  `FrameInterpolation_Record*Child`. A prior visible `extern "C"` declaration gives the block-scope
  redeclaration C linkage — but only at global scope; inside a namespace MSVC mangles it as C++ and
  the link fails (verified). Hence the `extern "C"` pre-declaration and `Cfa-`/`ComboForeignAnim_`
  name prefixes instead of a namespace.
- *Limb-DL routing*: limb DLs in a foreign loaded skeleton are `"__OTR__<path>"` string pointers
  (`SkeletonLimbFactory` stores `path.c_str()`). The host's GbiWrap resolves plain `"__OTR__"` strings
  at submission time against the *host's* RM (wrong game); `"__OTR__@<game>:"` strings instead go out
  as `G_DL_OTR_FILEPATH` commands the interpreter resolves against the named game's RM with scoped
  inner-reference resolution. `CfaRouteLimbDList` rewrites and interns them (the pointer is emitted
  into the display list and dereferenced later, so it must outlive the frame).
- *Texanim segment hygiene*: OOT re-establishes segments 8-D at the start of each frame's buffers
  (`Scene_Draw` -> scene draw config, `z_scene_table.c sDefaultDisplayList`), so contamination is
  bounded to commands *after* the draw in the current stream; re-pointing at an empty DL makes those
  see a no-op instead of our prim/env-colour DL.
- *MM `GetItem_GetDrawTableEntry` portability*: only self-contained funcs (plain
  `Gfx_SetupDL25 Opa/Xlu` + optional scale) are exposed as `KIND_SIMPLE`. Funcs needing extra MM
  runtime state that *can* be replayed get a `CwDrawKind` and the raw table row; the rest return 0 and
  the other game falls back to its sentinel. Remains ARE portable (their object-segment setup is
  vestigial under OTR extraction) — only the 0.02 scale must carry across.

## Gate MM on the OOT→MM portal region (2026-07-26)

**Why:** the cross-fill never modeled the portal — every call site passed `portalCheckName=""`, so
`portalOpen` was unconditionally true and MM was reachable from sphere 0. The fixpoint then credited an
OOT item placed on an MM check back into `ootOwned` and re-queried OOT with it, "proving" the portal
reachable using an item obtainable only through the portal. Real softlocks (adult start, Door of Time
behind Song of Time, Song of Time behind the portal). `portalCheckName` can never work: `RR_MARKET_MASK_SHOP`
holds no real checks (`RC_MASK_SHOP_HINT` is an `OtherHint`, `RC_MK_MASK_SHOP_SIGN` a sign), and the
oracle only returns `allLocations` names. The portal must be modeled as **region** access.

**`soh/soh/OTRGlobals.cpp`:** `Combo_SOH_Rando_GetReachableChecks` stashes
`RegionTable(RR_MARKET_MASK_SHOP)->Child() || ->Adult()` into a file-static at the end of its existing
`ReachabilitySearch`; new export `Combo_SOH_Rando_GetPortalOpen()` returns it. **Contract:** call it
right after `GetReachableChecks` — it describes that owned-set. Piggybacking is deliberate; a second
traversal per fixpoint iteration would roughly double gen time. Any age, not child-only: the
age/time/key requirement is already in the entrance condition (`market.cpp`), so with vanilla entrances
it collapses to child-day on its own, and under interior entrance shuffle the mask-shop scene can sit
behind a different door.

**`combo/rando/CrossWorldRando.h`:** `OracleFns` gains a nullable `GetPortalOpen` (MM leaves it null).
`CrossWorldCombinedFill` drops `portalCheckName` and reads the gate off the OOT oracle **immediately
after** the OOT query in each fixpoint iteration, before any MM check is credited — that ordering is
what makes it sound. Latched (monotone) for the rest of the fixpoint. Retry/validation conditions are
unchanged: `mmAdvUnreachable > 0` is already fatal in every mode, so a closed portal fails the pass.

- **NO_LOGIC bypass:** the gate is skipped entirely (`portalGated == false`). An impossible seed is that
  mode's point.
- **Hard fail on missing export:** any non-NO_LOGIC mode with a null `GetPortalOpen` returns
  `success=false` naming the export. Silently degrading to ungated would reproduce exactly this bug, and
  stale-DLL mismatches are a known hazard here. Both entry points also refuse up front when the other
  oracle exports resolved but this one didn't: the launcher errors instead of taking its no-logic
  fallback (which would have generated an ungated seed), and headless hard-fails on required exports.

  The gate is bypassed for NO_LOGIC in `ComboPlaythrough.h` too (`portalGated` parameter), so the hint
  pare-down and the fill agree; otherwise a NO_LOGIC seed's `MmOnlyMajoraGoal` could never be met and
  every advancement item would be classified required, flattening WotH/Foolish hints.

**`combo/rando/ComboPlaythrough.h`:** the same latched gate in `RunPlaythrough` (sphere trace + the
full-inventory "ever reachable" pass) and in `PareDownPlaythrough`'s `winsWithout`, or the
`--playthrough` validator would keep certifying these seeds beatable. The reachability memo now caches
the portal bit next to the set (`ReachResult`): a memo hit runs no search, so reading the DLL's bit
afterwards would be stale.

**`soh/.../3drando/fill.cpp` (`ComboFillConfined`):** the Mask Shop Key is filled within
`ctx->allLocations` (OOT-only), which keeps it out of the cross-world pool. The `RSK_COMBO_FORCE_MASK_SHOP_KEY`
setting that used to force it onto a fixed early check is **deleted**: its target was
`RC_KF_BEHIND_MIDOS_RUPEE`, an `RCTYPE_FREESTANDING` location, so with Shuffle Freestanding off (the
default) it was absent from `allLocations` and the force silently degraded to `AssumedFill` — it had
likely never worked for child starts. Measured over 10 seeds on adult + song-only Door of Time +
songsanity, forcing changed the hard-failure rate not at all (1/10 either way); it only cut retry churn
~41%. Not worth a user-facing switch whose "off" position is strictly worse.

**Deliberately NOT done:** hand-enumerating the portal's prerequisites (Ocarina / Song of Time /
`RG_OPEN_CHEST` / stones) anywhere. That would re-encode `market.cpp` + `temple_of_time.cpp` in a second
place and go stale. They are *derived* per seed instead — see below.

### Portal-aware fill (2026-07-26, same change)

The gate alone left a cliff: `AssumedFill` assumes every not-yet-placed item is owned, so a prerequisite
could land late, `portalOpen` flip false, and **every** remaining MM check vanish at once — MM items
dead-end. ~1/10 hard failures on adult + song-only Door of Time + songsanity. Fixed in three parts.

**Mask Shop exclusions.** The scene never runs, so everything in `RR_MARKET_MASK_SHOP` is uncollectable.
`RC_MK_MASK_SHOP_SIGN` is **not registered** under `COMBO_BUILD` (`ShuffleSigns.cpp`), which leaves its
`locationTable` slot at `RC_UNKNOWN_CHECK` → `GenerateLocationPool` and the check tracker both skip it. An
exclusion set or a dump filter would have kept it visible in the tracker. `RSK_MASK_SHOP_HINT` is forced
off in `FinalizeSettings` — `RH_MASK_SHOP_HINT` is delivered inside that scene, so leaving it on silently
burns a hint. `RC_MASK_SHOP_HINT` is an `OtherHint`, never in `allLocations`; nothing to do.

**Derived prerequisite set (`CrossWorldCombinedFill`).** A **sufficient witness**, built forward from an
empty owned set, not a required set: remove-one minimization fails when routes are interchangeable (Song
of Time vs. an entrance-shuffle route — dropping either alone keeps the portal open, so neither looks
required and nothing gets constrained). Each round bisects the canonically-sorted OOT advancement pool for
the item that flips `GetPortalOpen`, adds it to the witness and drops everything after it: O(log n) queries
per witness item. RNG-free so it cannot shift the seeded stream.

The probe is `ootClosedFixpoint`, not a single query, and that detail is the whole correctness argument.
Only `ootForcedOwned` is owned outright; an OOT `fixed[]` item is credited **when its check is reachable**,
which is exactly what `reachableFixpoint` does in the real fill. Owning `fixed[]` items outright instead
looks self-consistent (the Tier-1 check agrees with the derivation) but is optimistic in the same direction
as the derivation, so nothing ever detects the disagreement with the real model — and on default settings
(`RSK_SHUFFLE_SONGS` = Song Locations) Song of Time is a `fixed[]` entry, so the witness would collapse to
`{Ocarina}`, Phase A0 would place the Ocarina in a child-only area that really needs Song of Time first,
Tier 1 would pass, and the seed would deadlock. Where the whole requirement set is `fixed[]`, the witness
would come out empty and Phase A0 would not run at all. Budget `kMaxDeriveQueries = 1500` (queries, not
probes — each probe is a fixpoint); over it, warn and fall through to the old unconstrained behaviour.

**Prerequisites placed first (Phase A0).** Before general placement — mixing them into the normal random
order would let one land at position ~400/460, keeping MM locked for most of the fill so MM receives
almost only junk (a silently bad seed, worse than the failure being fixed). Candidates come from
`ootClosedFixpoint`: an OOT-only fixpoint with the portal shut, nothing assumed beyond forced-owned items,
MM never queried. Each item is chosen **randomly** across that whole valid set — variety comes from the
choice, not the ordering. (A deterministic first-match put the key on the same check five attempts running
and burned the entire retry budget.)

**Same constraint on the soh side**, because that layer places some of the carriers itself:
`ComboFillPortalClosed` (`fill.cpp`, `COMBO_BUILD`) is an assumed-fill variant that assumes **nothing**
from the free pool — candidates are only what's reachable from starting inventory plus already-placed
items. `ComboFillConfined` routes the Mask Shop Key and `PlaceRestrictedSongs` through it,
unconditionally; items with no such check fall through to the normal `AssumedFill`, so it is never worse
than before, and it logs placed-vs-fell-through so the path can't silently become a no-op. The combo layer
cannot fix the key at all: it arrives as a `fixed[]` entry the cross-fill never re-fills or validates.

*Not* wired into `RandomizeDungeonRewards`: its End-of-Dungeon branch fills the 9 boss checks, none of
which are reachable from starting inventory with nothing placed, so every candidate set would be empty —
9 wasted `ReachabilitySearch` calls for no placements. The plan's "spiritual stones" case is Own
Dungeon/Vanilla anyway, which goes through `RandomizeOwnDungeon`/`PlaceVanillaItem`; when rewards are
shuffled Anywhere they land in the cross pool and the derivation picks them up like any other item.

**Failure policy.** Tier 1 — portal still shut after the prerequisites are placed: repick just those,
`kMaxPrereqTries = 4`. (Phase A0 runs before any general placement, so a repick resets `placements` to
`lockedPlacements` and costs nothing but the prerequisite choices.) Tier 2 — budget exhausted: fail the
pass into the retry loop, `kMaxPasses = 3` × `kFillAttempts = 2` (`CrossWorldRando.h:171-172`), down from
10 × 5 — the old ceiling is
what produced the 300-800 s waits, and 15 still covers the observed pre-fix tail (seeds seen succeeding on
attempt 4 and pass 10). Both are pass counts, never wall-clock: a time limit makes success
machine-dependent and breaks seed sharing. All three budgets are `constexpr` in `CrossWorldRando.h`;
`ComboShip.cpp` and `comborando` both read `kFillAttempts` from there, since headless seeds only reproduce
in-game ones while the two loops agree. Tier 3 — the derivation finds the portal unreachable even with
everything owned: **warn loudly and generate anyway**, since a prediction of structural impossibility was
already made once and proved wrong. Terminal failures now name the last pass cause instead of "assumed
fill failed".

## MM rando save always SAVETYPE_RANDO (2026-07-31)

A player reported Song of Time wiping all Stray Fairies and dungeon Small Keys. That wipe
(`z_sram_NES.c:687-691`) is correct *vanilla* MM behaviour; only the rando-only `AfterEndOfCycleSave`
hook (`Rando/MiscBehavior/OnCycleSave.cpp`) restores them, and `COND_HOOK` tests `IS_RANDO` **once, at
registration** (fired from `title_setup.c` on every MM entry). So a `SAVETYPE_VANILLA` MM save silently
disables it — plus every other `IS_RANDO` behaviour, and `BenJsonConversions.hpp` then omits the whole
`rando` block from the save.

ComboShip could reach that state two ways, both now closed:

- **Stale placement cache.** `g_PendingMMPlacements` was set only at generation / seed-reload and
  *cleared after the first file creation*, never repopulated. Creating a second save file (or erase +
  re-create) without re-generating fell through to a vanilla MM save, while `g_ConsolidatedJson` was
  *not* cleared — so the slot still looked like a valid seed (baked `combo.rando`, randomized OOT).
  Fixed by deleting the cache: `Combo_OnOOTSaveInit` re-derives MM's apply payload from the bound
  consolidated seed on every creation, via the new `ComboRando::ApplyPayloadFromConsolidated`
  (`combo/rando/CrossForeign.h`, extracted from the reconstruction `Combo_OnReloadRequest` already
  used). The `MM_InitSaveFile` vanilla fallback and that now-dead export are gone.
- **Silent catch.** `MM_InitRandoSaveFile`'s exception path marked the save `SAVETYPE_VANILLA`. It still
  rebuilds the playable baseline (the rando strips run before the apply, so a bare return would persist
  a soft-locked slot) but keeps `SAVETYPE_RANDO` and now returns nonzero; the launcher logs loudly. Same
  for the empty-placement early return.

Tripwire: `Combo_LoadMMSaveFile` logs an error whenever a loaded MM save isn't `SAVETYPE_RANDO`.
Already-broken saves are not repaired — re-create the file. The legacy population from before this was
caught is retired by the 0.3.0 container gate (see below).

### Residual key-loss gaps closed (2026-08-22)

The 2026-07-31 pass covered every *creation* path. A re-audit found four more ways the same symptom
(Song of Time eating Small Keys) still reached players.

**Loading never creates or persists.** `SaveManager_LoadSaveFile` used to build *and write* a fresh
`SAVETYPE_VANILLA` save whenever the container had no `mm` section — reachable from the read-only dormant
peek (`Combo_OnOOTSaveLoad` → `MM_LoadSaveForCombo`), which permanently poisoned the slot. That block is
gone. The function now returns a code (`0` ok, `-1` missing, `-2` unreadable, `-3` migrate, `-4` no
`newCycleSave`, `-5` parse throw), `Combo_LoadMMSaveFile` adds `-6` for a non-`SAVETYPE_RANDO` save, and
every failure parks `gSaveContext.fileNum` at `0xFF` and clears `saveType`. Both halves of that matter:
`0xFF` is the "no save" sentinel `Combo_MM_GiveDormantResolved`, `MM_MarkForeignObtained` and MMAnchor's
`PumpDormant` all test, so a stray write lands *nowhere* rather than persisting the previous slot's save
(or zeroed vanilla BSS) into the failed slot; and since `fileNum` is signed and `0xFF` still passes the
peek trackers' `>= 0` test, only the cleared `saveType` (→ `IS_RANDO` false) stops them from drawing the
previous slot's save as this one — `ItemTracker`'s peek gate was missing that `IS_RANDO` term and now has
it. The dormant peek only marks a slot resident when the load returned 0, so it is strictly read-only and
fail-closed: it never rebuilds, it just goes blank.
`SaveManager_SysFlashrom_WriteData`'s owl branch also used to emit an `owlSave`-only section when its
pre-read failed, which nothing could ever load again; under `COMBO_BUILD` it now seeds `newCycleSave` from
the owl snapshot (resuming a little late beats a dead slot).

**No rebuild, no repair, no blocked entry.** ComboShip never blocks MM entry *and* never repairs a save.
There is deliberately nothing between those two: a missing or unloadable `mm` section means either the
file was created with **no seed bound** — which `Combo_OnOOTSaveInit` already refuses loudly, writing no
`mm` section at all — or the file is damaged. In both cases the load logs an error, the fail-closed
sentinel above keeps stray writes and tracker draws off the slot, play proceeds, and the remedy is
re-creating the file. `title_setup.c` therefore just calls `Combo_LoadMMSaveFile` and ignores the code;
the return value's only consumers are that log and the dormant peek's success check.

**Legacy broken saves are retired by the `0.3.0` container gate, not by runtime repair.**
`LoadOrCreateContainer` backs up any container whose `comboRelease` differs in `major.minor` and starts
fresh, so the pre-0.3.0 population poisoned by the old load-side auto-create is invalidated wholesale —
there is no migration path for a save whose MM half was silently vanilla for an unknown stretch of play,
and inventing one at runtime would only mask the bug. `SaveManager_InitNewSaveForSlot` does stamp
`SAVETYPE_RANDO` before its write (it persists, and it is a combo-only function), so the legitimate
creation path can never leave a vanilla MM save in the container even transiently.

**Key-mirror safety net.** A small-key check left `shuffled == false` delivers through vanilla `Item_Give`
(`z_parameter.c:4228`), which bumps `inventory.dungeonKeys` but not `rando.foundDungeonKeys`; the cycle
restore then truncates keys down to the stale mirror. A `COND_ID_HOOK(OnItemGive, ITEM_KEY_SMALL, IS_RANDO)`
registered in `Rando::MiscBehavior::OnFileLoad()` (so it re-registers per `OnSaveLoad`, honoring the
`COND_*` re-sample invariant) raises the mirror to the inventory count. It gates on
`Map_IsInDungeonOrBossScene` because outside a dungeon `gSaveContext.mapIndex` is the overworld minimap
index, which aliases key indices. Idempotent, monotonic, and a no-op after a rando grant. The two paths
that could produce such a check are now loud rather than silent: `MM_InitRandoSaveFile` counts unknown
checks/items and substitutes junk for an unknown *item* (keeping the check shuffled, so its vanilla key can
never take the vanilla path), and `Spoiler/Apply.cpp` forces `shuffled = true` + `RI_JUNK` when an absent
check's vanilla item is `RITYPE_SMALL_KEY`. Small keys are always shuffled in MM rando, so that is a
provable anomaly — every other absent check keeps its legitimate vanilla revert.

**Skeleton Key self-heals.** `Rando::GiveItem`'s `RI_SKELETON_KEY` case gated the mirror write on the
*inventory* count, so it could not repair a desync. It now raises both counters independently to
`max(current, N)` from a shared table (`Rando::skeletonKeyCounts`) — never lowering either, healing `-1`
sentinels and drift in both directions. The headless oracle (`GiveItemForOracle`) had no
`RI_SKELETON_KEY` case at all, so key-gated regions stayed unreachable during fill; it now shares the same
table, and its four `RI_*_SMALL_KEY` cases normalize the mirror sentinel before bumping instead of a bare
`++`. Consequence: newly generated skeleton-key seeds may place differently now that fill reachability is
correct. Stored seeds re-apply saved placements and are unaffected.

**Three edits are deliberately un-guarded**, because each is behavior-neutral upstream rather than a
combo deviation: the `z_sram_NES.c:1279` moon-crash `memcpy` now targets `gSaveContext.save` like the
sibling read two lines up (`save` is `SaveContext`'s first member, so the bytes landed identically
before); `GiveItem.cpp`'s `RI_SKELETON_KEY` rewrite and the four `RI_*_SMALL_KEY` cases only ever raise
counters that vanilla MM rando already intended to be equal; and `Rando.h`'s `skeletonKeyCounts` table
plus `Rando::AddSmallKey()` are pure factoring of constants and arithmetic that already existed inline.

**Anchor.** See `anchor.md` — MM's `HandlePacket_UpdateTeamState` wholesale-replaced `saveInfo`/
`shipSaveInfo` (including `saveType` and the key counters), and the cycle-save broadcast could outrun the
restore hook. Hook execution order is *not* registration order (`RegisteredGameHooks<H>::functions` is an
`std::unordered_map`), so the fix defers the broadcast rather than ordering the hooks.

## Per-seed spoiler names + shop-only prices (2026-07-31)

**Spoilers no longer overwrite each other.** Generation wrote a single
`Randomizer/Last-Generated-Randomizer.json` (`ComboRando::PendingPath()`), so every new seed clobbered
the previous one. Replaced with `ComboRando::ComboSpoilerPath(fileHash, stem)` →
`Randomizer/Combo-23-48-56-60-85.json`, built from the same 5 hash-icon indexes SoH names its own
spoilers with (`spoiler_log.cpp`). The newest is remembered in `CVAR_GENERAL("ComboSpoiler")`, mirroring
SoH's `SpoilerLog` CVar, via new `SOH_Set/GetComboSpoilerPath` exports — the launcher has no CVar access
of its own. The file-select auto-reload reads that CVar instead of a fixed path.

Two traps this had to avoid:

- **CVars are main-thread only.** `libultraship`'s `ConsoleVariable` map is completely unlocked, and
  `Save()` iterates it while ImGui touches CVars every frame. `RunComboFill` runs on a worker thread, so
  it only *writes the file* (`WriteComboSpoiler`); `Combo_FinalizeGenerate` sets the CVar
  (`RememberComboSpoiler`). Same hazard the main-thread-only placement apply already documents.
- **The plandomizer export** wrote to the pending path; under per-seed naming that would overwrite the
  source seed with the edited copy, so it writes `Combo-Plando-<hash>.json` instead.

Nothing reads `Last-Generated-Randomizer.json` any more; an install with only that file has no
remembered seed until the next generate or drag-drop.

**`mm.prices` is now shops only.** It carried a price for ~every shuffled check (390 of 392 placements,
against 104 of 1246 on OOT's side). Root cause is upstream: `GeneratePools.cpp`'s tingle-shop guard is
`if (type == RCTYPE_TINGLE_SHOP && shuffle == NO) continue; else roll price;` — the `else` binds to the
whole compound condition, so it rolls for every check that got past the shuffle gates. With tingle
shuffle on, the `continue` is unreachable entirely.

Not fixed locally: `Ship_Random` is the fill's own stream, so dropping ~390 draws per generation would
shift every seed and break reproduction of existing spoilers. Filed as a 2Ship upstream bug. Instead
`MM_DumpRandoStaticData` emits only `RCTYPE_SHOP` / `RCTYPE_TINGLE_SHOP` into the spoiler while
`sMMComboCheckPrices` still captures the full set for the oracle. Safe because all 37 checks using
`CAN_AFFORD` are exactly those 25 + 12, and the only runtime readers of `.price` are the shop/tingle
actors (`EnGirlA`, `EnBal`, `EnIn`, `EnTab`).

**The spoiler's `playthrough` steps are plain strings.** They were objects
(`{check, game, item, foreign}`), which reads as noise for something a player scans. Now each sphere's
`steps` is a string array of `"[OOT] Check --> Item"`; cross-game placements aren't marked, since which
game owns an item isn't actionable. `RunPlaythrough` still emits the structured form — the headless
`--playthrough` validator's affordability canary needs `game`/`check`/`item`/`foreign`/`advancement` —
so the flattening happens in `ComboRando::PlaythroughLines`, applied only where the consolidated spoiler
is assembled. The validator builds its own `pt1` from its own `RunPlaythrough` call and never reads the
spoiler's section, so the two formats don't collide.

## Foreign traps fire on the finder, and use each game's curated trick names (2026-08-03)

**Why (effect):** a foreign trap did nothing to the player who found it. MM's `CheckQueue` foreign
branch showed "You found &lt;disguise&gt;!" and called `SendForeignCheck` — mailing the trap into the
other game's save — so the freeze never happened where it was collected. Native `RI_TRAP` by contrast
shows `GetTrapMessage()` and calls `OfferTrapItem()`.

**Why (names):** both games ship curated near-miss name tables — OOT's `trickNameTable`
(`soh/soh/Enhancements/randomizer/Traps.cpp`, trilingual) and MM's `fakeItemNames`
(`mm/2s2h/Rando/StaticData/Items.cpp`, English) — but the combo layer used neither. `MakeTrickName`
only doubled a letter, so a disguised trap read as "Ganon's Souul" instead of "Rauru's Medallion".
Note upstream MM's own fallback is broken (`fakeItemName` is still empty when the `else` runs, so it
indexes an empty string), which is why the curated table matters rather than the fallback.

**Trap identity** now rides in the seed. Both dumps already emitted a per-item `trap` flag
(`rg == RG_ICE_TRAP` / `id == RI_TRAP`) but only `AssignTrapDisguises` consumed it, to pick disguises.
It now also stamps `fm["trap"] = true` — **before** the no-candidate bail, so a trap that got no
disguise is still flagged — the emitter writes it into each `foreign[]` entry, and `ForeignItem` gained
`bool trap`. Absent in older saves → defaults false → previous behaviour, so this needs a regenerated
seed to take effect.

**Firing:** each game springs its **own** flavour, so an OOT Ice Trap found in MM becomes an MM trap
and an MM trap found in OOT becomes OOT's freeze. Porting trap implementations between engines would
be a lot of work for no player-visible gain.
- MM (`Rando/MiscBehavior/CheckQueue.cpp`): `GetTrapMessage()` + `OfferTrapItem()`, no cross-grant.
  Fires on every collection, matching native — a Song of Time cycle reset re-arms a native trap check,
  so a foreign one should re-arm too.
- OOT (`Enhancements/randomizer/hook_handlers.cpp` `OOT_DeliverForeign`): `FreezePlayer()` directly.
  **Not** `pendingIceTrapCount++` — that counter is consumed by `VB_SHORT_CIRCUIT_GIVE_ITEM_PROCESS`,
  which has already run by grant time, so incrementing it springs the trap on the player's **next**
  pickup and short-circuits that item's presentation instead.
- OOT text (`Messages/ItemMessages.cpp`): a foreign trap taunts with OOT's real ice-trap tables via a
  new `Rando::Traps::BuildIceTrapMessageNamed`, naming what it pretended to be. The stock
  `BuildIceTrapMessage` resolves the name from the draw entry, which for a foreign sentinel would read
  "Combo Foreign Item" — and a foreign disguise may be an item of the *other* game with no local
  `RandomizerGet` to resolve at all.

**Anchor teammates still get trapped.** The grant and the broadcast were bundled in one branch; only
the local cross-grant is skipped. `Anchor_BroadcastCrossItem` / `MMAnchor_BroadcastCrossItem` still
fire, and the receive path springs the trap on teammates (`Packets/GiveItem.cpp`). Verified with two
clients in OOT (2026-08-03): both froze once and the finder was not re-frozen, so the broadcast is not
echoed back to the sender. Note a teammate playing the *other* game still banks the trap and springs
it on switch — that is the item-routing model, not a bug in this change.

**Trick names:** both games expose their tables through small `COMBO_BUILD` accessors
(`Rando::Traps::GetTrickNamesEnglish`, `Rando::StaticData::GetTrickNames`), emitted as `trickNames`
beside the existing `advancement`/`trap` flags and parsed into `ForeignItemMeta`.
`AssignTrapDisguises` prefers a curated name and falls back to letter-doubling only where an item has
no entry. English only — OOT's table is trilingual but the foreign schema carries single strings, so
localisation would mean widening `fakeTrickName` everywhere it is consumed.

**Known nit:** the fallback doubles punctuation (`Tingle''s Clock Town Map`). Upstream skips spaces
but not apostrophes.

## MM oracle: zeroed inventory read as "owns Ocarina" (2026-08-09)

`Combo_MM_Rando_Reset` (`mm/2s2h/BenPort.cpp`) resets the headless oracle save with a whole-struct
`memset(0)`. MM's `ITEM_OCARINA_OF_TIME` is item id **0** and `HAS_ITEM` is an equality compare
(`INV_CONTENT(item) == item`), so a zeroed slot reads as "ocarina owned" — every song became playable
from sphere 0 and the fill placed MM's ocarina arbitrarily late (player-reported unbeatable seed:
Snowhead at sphere 10, ocarina at sphere 23). A real fresh save inits `inventory.items` to
`ITEM_NONE` (0xFF, `z_sram_NES.c`). Masked whenever `RI_OCARINA` was a starting item (the default
kit); exposed by `gRando.StartingItems: []`, which shuffles the kit into the pool. The
`--playthrough` validator shares the same oracle, so it could not catch it.

Fix: after the memset, re-init `inventory.items` (48 slots, items + masks) to `ITEM_NONE`, plus a
one-time sweep asserting no inventory-slot item reads as owned on the empty context.

## Hints never target non-shuffled placements (issue #132, 2026-08-14)

**Why:** Native only hints locations that went through a shuffle fill (`ItemLocation::SetAsHintable`,
enforced at `hints.cpp` `FilterHintability`). The combined fill merges each dump's `fixed[]` (confined)
placements into the flat spoiler maps, and the cross-hint layer filtered only on `advancement` — so
gossip stones hinted own-dungeon keys, dungeon rewards, min-set Buy items, excluded checks and MM's
non-shuffled Boss Remains, and an area could turn "way of the hero" off its own confined boss key.

**`soh/soh/OTRGlobals.cpp` (`SOH_DumpRandoStaticData`):** each `fixed[]` entry now emits
`"hintable": GetItemLocation(rc)->IsHintable()` — captured here, right after `ComboFillConfined()`,
because the oracle's per-query `ItemReset()` wipes the flag long before the hint dump runs.
`SOH_DumpRandoHintData` additionally emits `"hintAccessibleChecks"`, mirroring `CreateStoneHints`'
two `SetHintAccesible` cases (Song from Impa with Zelda's letter unshuffled; ToT Master Sword).

**`soh/.../3drando/fill.cpp`:** the combo-only Mask Shop Key confinement now passes
`setLocationsAsHintable = true` — it's a genuinely shuffled item that native would place hintable.

**MM (`mm/2s2h/BenPort.cpp`):** the dump's `fixed[]` splits — confined pre-placements emit
`"hintable": true` (`PreplaceConfinedItems` sets `shuffled = true`), the ComboShip Boss Remains block
emits `false`. `MM_InitRandoSaveFile` also clears `shuffled` on every `RCTYPE_REMAINS` check when
`RO_SHUFFLE_BOSS_REMAINS` is off (the spoiler apply stamps `shuffled = true` on all payload checks),
restoring native state for MM's own stone draw, tracker, prices and the Saria hint. `randoItemId` is
untouched — remains delivery resolves from it, never from `shuffled`.

**Forced placements:** the fill spoiler now carries `startKnown` (forced checks, e.g. Link's Pocket
— skipped by the dump's `fixed[]`, owned at start); CrossHints folds them into the same set, matching
native's `RA_LINKS_POCKET` exclusion.

**`combo/rando/CrossHints.h`:** `nonHintable` (both dumps' `fixed[]`, `"oot:"/"mm:" + check`) stamps
`HintCandidate::hintable`. Candidates are kept either way — the Ganondorf/altar/always item lookups and
`areaHasMajor` still need the FULL view; only the stone-draw sites filter: Song/Overworld/Dungeon and
NamedItem/Random pools, MM's `gossipPool`, and always-hints (an excluded always-check is junk-filled
non-hintably). WotH now needs a `hintable && required` check in the area; the foolish universe and the
`areaHasMajor` signal stay unfiltered (native counts majors regardless of hintability). Start-known
checks are pre-inserted into `usedCheckKeys` after the always block, matching native's ordering.

**Absent flag = old DLL:** `hintable` defaults to true (previous behavior) and a dump with a non-empty
`fixed[]` carrying no `hintable` key logs one warning naming the DLLs to rebuild. Hint TEXT for a given
seed changes versus older builds (smaller pools -> different draws); placements are untouched and the
same-seed byte-identity protocol still holds.

**Known remaining gap (pre-existing):** native reserves its static-hint targets (`FindItemsAndMarkHinted`
— the Light Arrows check, altar-named rewards) before stone hints, so stones never re-hint them; the
combo distributor doesn't, and MM's cross `gossipPool` doesn't consult `usedCheckKeys` either — a stone
may duplicate a static hint's target. Duplication only, never a non-shuffled target.

## Container Matches Contents dresses foreign checks as the real item (issue #103, 2026-08-10)

Both foreign sentinels are hard-typed junk (`itemTable[RG_COMBO_FOREIGN]` = `ITEM_CATEGORY_JUNK`,
`RI_COMBO_FOREIGN` = `RITYPE_JUNK`), so every CMC surface — OOT chests + the Shuffle* containers, MM
chests/grass/pots/barrels/crates — rendered every cross-game item as junk.

**Seed schema:** `foreign[]` entries gain `category` — `CwCatName(p.item.cat)` stamped by the fill
(the per-item `category` both dumps already emit in `pool[]`), carried by `BuildForeignArray` /
`ForeignItem`. `CwCat::UNKNOWN` is omitted so consumers fall back to
`advancement ? major : junk` (also the rule for pre-category seeds and plando imports, which cannot
compute categories; the plando writer carries `category` over for unchanged rows like the disguise
fields). Traps always classify **major** — OOT-native parity (`RG_ICE_TRAP` is MAJOR; a junk-looking
container never gets opened, so the trap would never fire) — deliberately diverging from MM's native
`RI_TRAP` = LESSER. `advancement`/`trap` are now emitted only when true (loaders already default
false), and `checkArea` is no longer persisted: its only reader was `CrossHints.h::Generate`, which
now derives the area from its own `ootChecks` table (same `sohHintDump` source, same
empty-→checkName fallback, so hint text is unchanged).

**OOT consumption:** `OOT_GetForeignCategory(rc)` (`hook_handlers.cpp`, per-rc cache invalidated by
`g_ootForeignGen`); `GetFinalGIEntry` overrides `giEntry.getItemCategory` for the sentinel inside
the existing `COMBO_BUILD` block — the table entry stays junk. MM-only `mask`/`strayFairy` map to
MAJOR (no OOT textures). `Randomizer_AdjustItemCategory` early-returns for the sentinel so OOT's
Skeleton Key cannot junk a foreign MM small key. The skip-animation classification in
`RandomizerOnPlayerUpdateForRCQueueHandler` is intentionally untouched (animation gate ≠ container
art).

**MM consumption:** `Rando::GetItemTypeForCheck(itemId, checkId)` (`ConvertItem.cpp`) resolves the
sentinel via `MM_LookupForeign` (category → `RITYPE_*`, same rules) and otherwise returns the old
`Items[ConvertItem(...)].randoItemType`; the 8 CMC draw sites in `Rando/ActorBehavior/` now call it.

## Mask Shop exclusion sub-options (#133/#134) (2026-08-15)

**Why:** the Happy Mask Shop scene is the OOT→MM portal, so two stock shuffles gate cross-world
routing: **Lock Overworld Doors** puts `RG_MASK_SHOP_KEY` in the pool, and **Interior Entrances** can
move the portal behind an arbitrary door. Two default-off opt-outs let players keep the portal
predictable: `RSK_EXCLUDE_MASK_SHOP_KEY` ("Exclude Mask Shop Key", CVar `ExcludeMaskShopKey`) and
`RSK_EXCLUDE_MASK_SHOP_ENTRANCE` ("Exclude Mask Shop", CVar `ExcludeMaskShopEntrance`). Both are
children of their parent setting, `defaultHidden` and revealed by the parent's callback.

**Touches** (all `soh/soh/Enhancements/randomizer/`, marked `// ComboShip: (#133)` / `(#134)`):

- `randomizerEnums/RandomizerSettingKey.h` — two keys before `RSK_MAX` (spoiler settings are
  name-keyed, so appending is safe).
- `option_descriptions.cpp` — one description each.
- `settings.cpp` — option creation + parent Hide/Unhide callbacks (`RSK_LOCK_OVERWORLD_DOORS` gained
  its first callback), menu groups (`RSG_MENU_SECTION_AREA_ACCESS`, `RSG_MENU_SECTION_ENTRANCES`, plus
  legacy `RSG_OPEN`/`RSG_WORLD` for consistency), `FinalizeSettings` coupling, `RandomizeAllSettings`
  skip-list.
- `3drando/item_pool.cpp` — the `RG_MASK_SHOP_KEY` pool add is skipped when excluded.
- `3drando/starting_inventory.cpp` — `AddItemToInventory(RG_MASK_SHOP_KEY)` so logic/oracle own it
  from sphere 0; the market entrance condition collapses to child+day.
- `savefile.cpp` — the matching runtime grant in `Randomizer_InitSaveFile`.
- `entrance.cpp` — the mask-shop entrance is erased from the Interior pool.

**`RAND_INF_MASK_SHOP_KEY_OBTAINED` only, not `RAND_INF_MASK_SHOP_UNLOCKED`.** `LockOverworldDoors.cpp`
reads UNLOCKED for door state and KEY_OBTAINED for "have key". Granting only the latter keeps the door
visibly locked until the player opens it once, matching how a found key behaves, and shows the key in
the item tracker.

**Erase ordering.** The `std::erase_if` sits *after* the `SpecialInterior` append (the mask shop is in
the base Interior list, but this keeps the erase applying to the fully assembled pool) and *before* the
decoupled reverse push, so the reverse entrance is never pushed either and decoupled mode needs no
separate handling. Mixed pools are assembled later from these pools, so
they inherit the exclusion. `CreateEntranceOverrides` skips unshuffled entrances, so no override is
emitted and runtime keeps the identity mapping — no save/serialization change.

**Coupling.** `Context::FinalizeSettings` force-clears each child when its parent is off, so a spoiler
never claims an exclusion that had no effect; every consumer additionally `&&`s the parent.

**No `#ifdef COMBO_BUILD`.** The neighbouring `RSK_MASK_SHOP_HINT` ifdef guards a *forced* deviation.
These are default-off opt-ins that are coherent in vanilla SoH too, so they carry `// ComboShip:`
markers instead — which is what identifies combo lines during upstream merges.

**Portal gate unchanged.** Both options only relax constraints, and the gate is region-based
(`RR_MARKET_MASK_SHOP` reachability) — see "Gate MM on the OOT→MM portal region (2026-07-26)".

## Starting Game: OOT / MM / Random (#135) (2026-08-15)

**What:** `gCombo.Rando.StartingGame` (0 = OOT, 1 = MM, 2 = Random) picks which game a new file boots
into. An MM start spawns the player in South Clock Town — the same place the portal arrives at — via
the existing launcher handoff: `Combo_OnOOTSaveInit` sets the slot's lastGame to MM, and
`Combo_ResumeMMIfLastSavedThere` does the rest. Boot order is unchanged (OOT first, MM eager-booted).
The resolved value is written to the spoiler as top-level `startingGame` ("OOT"/"MM") and is
seed-bound; every read defaults to "OOT", so old spoilers, saves and plando files still load.

**Random is resolved per generation attempt** from `ComboHash("startingGame:" + masterSeed) & 1`.
ComboShip.cpp and ComboRandoHeadless.cpp each hold a copy of that derivation; the string must stay
byte-identical or headless seeds stop reproducing in-game ones.

**Forced settings.** `Context::FinalizeSettings` (`#ifdef COMBO_BUILD`, reading `gComboStartingGameMM`)
forces, when the resolved start is MM:

- `RSK_STARTING_AGE` → Child. The Clock Tower door lands the player outside the Happy Mask Shop, and a
  child there can always walk back in.
- `RSK_FOREST` Closed → Closed Deku (Closed Deku / Open untouched), so an itemless child who saved and
  quit in OOT can always leave the forest and reach the Market.
- `RSK_EXCLUDE_MASK_SHOP_KEY` ON when Lock Overworld Doors is on, and `RSK_EXCLUDE_MASK_SHOP_ENTRANCE`
  ON when interior shuffle is on — the portal's own key/door must not sit behind the portal.

Both force-branches are `else`-chained onto #134's force-clears, so parent-off still wins. As with
`RSK_WINCON`, the spoiler's `oot.settings` blob records the player's CVars, not the forced values; the
authoritative record is top-level `startingGame`.

**UI.** `SOH_RefreshComboStartingGameUI` greys out those three options with the tooltip "Starting Game
is Majora's Mask" — only under an *explicit* MM start. Under Random they stay editable and the force is
silent when MM rolls. `HandleStartingAgeUI` owns `RSK_STARTING_AGE` in both directions (it re-applies
the MM disable at its end), so the refresh can't undo its own unbeatable-config disable.

**Fill.** `CrossWorldCombinedFill` takes `GameId startingGame`; the whole reachability change is
`portalOpen = !portalGated || mmStart` in `reachableFixpoint`. MM's logic already roots at South Clock
Town unconditionally — today's fill just suppresses that root until the portal opens. The OOT side
still roots on OOT's own start-of-game regions; the transient "standing in the Market" state right
after the Clock Tower door is deliberately not modelled (an under-approximation, so sound).

The portal-prerequisite derivation and Phase A0 stay active under an MM start: there they are the
*re-entry* guarantee, not the entry one. Because `mmAdvUnreachable` no longer catches a closed portal
under an MM start, each pass additionally asserts `ootClosedFixpoint(placements, {}).portalOpen` — a
player who strays into OOT with nothing must always be able to walk back in. RNG-free, one portal-closed
fixpoint over the placements per pass, MM starts only. `ComboPlaythrough.h` mirrors the seed in
`RunPlaythrough`, `PareDownPlaythrough` and `everPortalOpen`.

**Fail policy.** Explicit MM that exhausts its attempts hard-fails, with an error naming the re-entry
requirement and telling the player to loosen the OOT access settings. Random silently pins the
remaining attempts to an OOT start after any failed attempt that resolved MM, and its last attempt
always resolves OOT — Random can never hard-fail on an MM start.

**Hints are unchanged (strict).** Recovery/witness items stay non-WotH under the strict counterfactual;
keeping the seed escapable is generation's job, not the hint layer's.

**Residual:** NO_LOGIC + MM start has no re-entry guarantee — that mode's point. No MM-side setter
exists (nothing in MM reads the value); add one when a consumer appears.

## Combo-owned unified Hint Tracker (#164) (2026-08-20)

**Why:** the Hint Tracker arrived in the 2026-07-13 soh merge as vendored OOT-only code, reachable
only from the OOT per-game tab. It is a poor fit for combo seeds even for OOT alone: combo injects
every hint as a pre-rendered MESSAGE hint (`SOH_ApplyComboHints`), so native's Journal view,
WotH/Foolish coloring and found-tracking are all dead. MM had no hint tracker at all. Replaced by a
combo-owned window under Settings, directly below Check Tracker, covering both games.

**No generator changes.** The tracker reads the seed's existing `hints` slice
(`combo/rando/CrossHints.h`): `hints.oot[] {checkName, type, messages[{en,de,fr}]}` and
`hints.mm {gossipPool[{weight,text}], itemLocations{item -> text}}`.

**New files (no merge risk):** `combo/gui/ComboHintTracker.h`/`.cpp` — the `"Hint Tracker##Combo"`
GuiWindow (the `##` tail keeps a distinct Gui-map/ImGui identity from OOT's native `"Hint Tracker"`)
plus the Settings panel. It draws only from plain strings the launcher pushes — no ResourceManager or
game-DLL dependency — so it renders under either foreground game with none of the dormant-draw
machinery the icon trackers need. `ComboTracker::ForegroundPaused` was de-`static`ed out of
`ComboTrackerSwap.cpp` so the window's OnlyPaused option reuses the same fail-open pause probe.

**Read-state store.** `.combosav` gains `combo.hintsRead`:

    "hintsRead": {
      "oot":      ["__GANONDORF__", "__STONE__3", ...],   // combo checkName keys
      "mmPool":   [0, 4, 7],                              // hints.mm.gossipPool indices
      "mmNative": [{ "check": "...", "text": "..." }],     // MM's own stone hints
      "mmNpc":    ["Great Fairy Sword", ...]              // hints.mm.itemLocations keys
    }

Absent means empty. Every bucket is set-semantics (insert-if-absent), so a hook that fires repeatedly
per textbox is free, and `FlushContainer` only runs on an actual insert. `mmNative` dedupes on `check`
alone (first write wins): a trap check's disguise name re-rolls per scene init, so comparing the whole
`{check, text}` object would append a fresh entry — and flush the container — on every talk.
`Combo_OnOOTSaveInit` erases `combo.hintsRead` unconditionally, not only on the rebake path: a slot
whose container survived a delete path arrives with no pending seed, and the new file must not inherit
the old one's read marks. `EraseComboContainer` pushes an empty slot -1 payload so a deleted slot's
hints stop showing on the file-select screen. MM-native stone hints have no stable upfront list (they
are composed at talk time from live save state), so they appear only as a "revealed" group built from
`mmNative`.

**Three new ABI points:**
- `soh.dll` `SOH_SetComboHintRevealCb(void (*)(int fileNum, const char* comboKey))` — OOT's
  `OnRandoHintRevealed` subscriber (registered once from `SOH_LoadComboRando`) maps the
  `RandomizerHint` back to the combo `checkName` it was applied from and reports it. Non-combo hints
  (native static hints, warp songs) and disabled hints are dropped.
- `2ship.dll` `MM_SetComboHintRevealCb(void (*)(int fileNum, int kind, int poolIndex, const char* key,
  const char* text))` — kind 0 = cross `gossipPool` pick (by index, from a new report-only out-param on
  `EnGs.cpp GetRandomCheck`), 1 = native MM stone hint (key = combo-spoiler check name, plain text),
  2 = NPC `itemLocations` hint (key = item friendly name, reported where
  `Rando.cpp GetItemLocationHintName`'s COMBO_BUILD branch resolves). `SECOND_GS_MESSAGE` reports only
  in the paid-and-shown branch. `DmStk.cpp`'s Skull Kid taunt now resolves the Oath-to-Order location
  inside the branch that actually has a `{{location}}` placeholder — resolving it for the taunt marked
  the hint read without the player ever seeing it. The out-param changes no draw logic and consumes no RNG, so the stone
  draw stays byte-identical.
- `comboui.dll` `ComboUI_SetHintTrackerData(int slot, const char* hintsJson, const char* readStateJson)`
  — pushed at `Combo_OnOOTSaveInit` (after bake) and `Combo_OnOOTSaveLoad`, and re-pushed after every
  reveal that actually inserted. Push-only; there is no comboui -> launcher direction, so manual
  mark-read from the UI is a later phase. comboui copies both strings under a small mutex (the launcher
  reuses its buffers and the reveal path runs on the reporting game's thread) and re-parses lazily on
  the draw thread via a dirty flag; corrupt JSON degrades to "no seed loaded".

**Shared hint resolver.** `SOH_ApplyComboHints`' resolution loop was extracted into
`Combo_WalkComboHints(hints, isTaken, emit, ...)`, used by two callers. Apply runs it with `isTaken =
ctx->GetHint(rh)->IsEnabled()` and records `RandomizerHint -> checkName` as it goes; the lazy replay
(`Combo_EnsureHintKeyMap`) runs it with a claimed-set predicate over the pushed blob's hints slice.
**The replay is the path that actually serves every real flow**: it is keyed on `OOT_ForeignMapGen()`,
and `SOH_LoadComboRando` bumps that generation unconditionally after apply, so the first reveal of a
session always rebuilds from the blob. Apply-side recording is only a narrow safety net for a process
where the blob was never pushed. That makes the two walks staying one function load-bearing, not
belt-and-braces: the sentinel (`__STONE__n`/`__TRIAL__…`/`__JUNK__n`) to physical-stone mapping
depends on the claiming order. Both callers build into a local map and commit (map + generation stamp)
only past a successful walk, so a throw logs and retries at the next reveal instead of latching an
empty map. Worst case a wrong entry is marked read.

**Native OOT Hint Tracker suppressed.** `"Hint Tracker"` is gone from the OOT per-game tab's rando
allow-list (only Entrance Tracker remains), and `ComboUI_Register` forces
`gOpenWindows.HintTracker`/`…Settings` to 0 and `Hide()`s both windows via the Gui map — a config that
persisted `HintTracker=1` would otherwise keep showing a window with no reachable settings and no
combo hint content. This closes the `docs/merges/2026-07-13.md` follow-up for that window pair.

**New CVars:** `gCombo.HintTracker.{Enabled, WindowType, OnlyPaused, ShowUnrevealed, ShowJunk,
HideRead}`. Unread entries render as `???` unless ShowUnrevealed; the search box only matches text the
player is allowed to see.

**Residuals:** MM's plain-text recomposition for a native stone hint may diverge cosmetically from the
formatted in-game string (colors/line breaks are stripped); MM-native NPC hints outside the combo
`itemLocations` path are not reported; there is no manual mark-read.

## Randomize cosmetics on combo generation + cross-game sync (#169) (2026-08-21)

**Why:** both games offer "randomize all cosmetics when a randomizer seed is generated"
(`gCosmetics.RandomizeCosmeticsGenModes` = On Rando Gen Only; MM's `gCosmetics.RandomizeOnSeedGen`),
but combo owns generation and never reaches either game's vanilla fire site, so neither hook ran. The
same gap silently disabled both Audio Editors' "randomize all on rando gen" — they consume the same
two hooks, and they are the only other consumers, so firing the hooks fixes them too.

**Vendored deltas (all inside existing combo-only functions):**

- `soh/soh/OTRGlobals.cpp` — `SOH_ApplyRandoPlacements` now calls `ctx->SetSeed(sComboRandoSeed)` when
  the launcher supplied a seed. Combo bypasses `Playthrough_Init`/spoiler-load, the only vanilla
  `SetSeed` sites, so the ctx seed sat at 0 and every seed-derived feature degenerated: save
  `finalSeed` was always 0, Anchor's roster seed-mismatch check compared 0 to 0, and the in-game
  seeded derivations (EnemyRandomizer, ExtraTraps, MirroredWorld, Audio) were not per-seed at all.
  Both the fresh-gen and the reload path call `SOH_SetComboRandoSeed(masterSeed)` before the apply, so
  generator and reloader agree; the u64→u32 truncation is consistent across clients.
- `soh/soh/OTRGlobals.cpp` — new export `SOH_FireGenerationCompleteHooks`, an RM-scoped
  `ExecuteHooks<OnGenerationCompletion>()` (same `CrossRMRegistry::Get("oot")` pattern as
  `SOH_MenuApplyCVarChange`; the cosmetic consumers patch OOT gfx). Vanilla fires this from the rando
  worker thread — we fire from the main thread, which is strictly safer. The `ExecuteHooks` call is
  wrapped in `try`/`catch(...)` with `SPDLOG_ERROR`, like the MM side: subscribers reach `std::map::at`
  and allocate, and a throw must not unwind across the C ABI into `ComboShip.exe`.
- `soh/soh/Enhancements/cosmetics/CosmeticsEditor.cpp` and `soh/soh/Enhancements/audio/AudioEditor.cpp` —
  both seeded branches (`RandomizeColor`, `RandomizeGroup`) fold in the `Rando::Context` seed in one
  strictly degenerate case: `!IS_RANDO && fileCreatedAt == 0`. Vanilla seeds from
  `IS_RANDO ? GetSeed() : fileCreatedAt`, but combo fires the gen roll at file select where `IS_RANDO`
  is false and no file is loaded, so both terms are 0 and every seed would get the *same* palette and
  the *same* audio shuffle. With the `SetSeed` above in place the rolls are genuinely seed-derived and
  reproduce identically on the generator and on every recipient. Vanilla is bit-for-bit unchanged in
  every other case (any real save has a nonzero `fileCreatedAt`).
- `mm/2s2h/BenPort.cpp` — `MM_InitRandoSaveFile` now fires `ExecuteHooks<OnRandoSeedGeneration>()`,
  mirroring `OnFileCreate.cpp`'s tail (that function re-implements `OnFileCreate`'s body and had omitted
  only this line). Deliberately placed *after* the placement `try`/`catch` and inside its own
  `try`/`catch`: a throwing cosmetic/audio subscriber must not send the catch path off to rebuild the
  slot as a placement-less vanilla save. `MM_InitRandoSaveFile` intentionally re-implements
  `OnFileCreate`'s tail rather than calling it, so it must be re-audited whenever upstream changes
  `OnFileCreate`; a shared-helper refactor was considered and declined precisely because the hook fire
  has to sit outside the try/catch.
- `mm/2s2h/Enhancements/Audio/AudioEditor.cpp` — `ReplayCurrentBGM` early-returns on
  `NA_BGM_DISABLED`. MM's audio randomize-on-gen subscriber now runs while MM is dormant (combo fires
  the hook during OOT's save init), where the active seq id is `0xFFFF`; the queued
  `SEQCMD_PLAY_SEQUENCE` would index `gSequenceMap[0xFFFF]` and crash on MM's first frame after a
  portal. Replaying a disabled BGM is meaningless in vanilla too, so the guard is behavior-neutral there.

**Where combo fires them.** OOT: end of `Combo_FinalizeGenerate`, after `SOH_ApplyComboHints` and
before the pending-settings bookkeeping. Also on the seed-**reload** path (`Combo_OnReloadRequest`,
after the placement apply + seed hash): a client that merely loads a shared seed never runs
`Combo_FinalizeGenerate`, so without this its OOT colors would never roll at all. Since the roll is
seed-derived, the recipient reproduces the generator's colors exactly. MM: inside
`MM_InitRandoSaveFile`, i.e. per rando save-file creation (fresh or reloaded seed) — exactly vanilla
MM's semantics.

**Once per seed, per machine.** The reload path runs on *every* boot (silent auto-load re-applies the
remembered seed), so an unconditional fire there would re-roll on each launch and wipe cosmetic/audio
edits the user made by hand. A persisted latch CVar (`gCombo.Rando.GenRollSeed`, a comma-separated list
of master seeds as hex strings — the int CVar store is 32-bit) makes the roll happen once per seed:
`ComboUI_ClaimGenRollSeed` returns 1 (and appends the seed) only when the seed is not already in the
list, and `Combo_FireGenRollHooksOnce` fires on that. The list keeps the **last 8** seeds, not one slot:
with a single slot, playing seed A, then B, then A again would re-roll A over the user's manual edits.
Beyond 8 the oldest entry is pruned, so a very long rotation can cost one extra roll — the failure mode
is a re-roll, never a lost seed. A claim also only happens when a subscriber is actually **enabled** to
roll (OOT cosmetics mode == `RANDOMIZE_ON_RANDO_GEN_ONLY`, or `gAudioEditor.RandomizeAudioGenModes` ==
the same); otherwise the default config's every-boot auto-load would silently burn each seed and turning
the options on mid-seed would do nothing. A fresh generation passes `force=true`: it claims the seed
(so subsequent loads leave manual edits alone) but fires regardless, which is vanilla's unconditional
gen-only semantics. The fire is deliberately **not** coupled to the cosmetics sync gate — each hook
subscriber gates on its own CVars, and a recipient who wants audio randomization but not cosmetic sync
must still get its roll. `Combo_OnOOTSaveInit` calls the same helper just before the sync when the sync
gate passes, which is what makes enabling the options plus sync mid-seed roll and sync fresh colors on
the next file creation.

**Sync Randomized Cosmetics** (`gCombo.Rando.SyncCosmetics`, default off, combo settings panel).
`combo/gui/ComboCosmeticsSync.cpp` copies OOT's colors onto MM's semantically-shared elements after
`MM_InitRandoSaveFile` returns 0 — MM has just rolled its own, so this overwrites it. OOT is the source
of truth because its roll is seed-derived and MM's is not. It lives in comboui (not the launcher exe)
because comboui already links libultraship, so the CVar API is called directly instead of through a
hand-cast `GetProcAddress` shim; the launcher drives it through three exports,
`ComboUI_SyncRandomizedCosmetics`, `ComboUI_CosmeticsSyncGateEnabled` (one predicate, so the gate's CVar
reads are never duplicated) and `ComboUI_ClaimGenRollSeed` (the latch above). The CVar store is one
shared instance across the exe and every DLL (shared `libultraship.dll`), so this is plain CVar
reads/writes with no IPC. MM's `MM_MenuApplyCVarChange` comes from `ComboMenuModel`'s cached resolver,
which retries until `2ship.dll` is loaded.

- **Gate:** sync on AND OOT mode == `RANDOMIZE_ON_RANDO_GEN_ONLY` AND MM's `RandomizeOnSeedGen` == 1.
  The File-Load modes deliberately don't count — they re-roll OOT *after* the sync and would drift the
  games apart again. The UI note says so.
- **Keys:** OOT `gCosmetics.<Id>.{Value,Changed,Locked,Rainbow}` vs MM `gCosmetic.<Id>.{Color,Changed,
  Locked,Rainbow}`. The singular MM prefix is what keeps the two games' cosmetic keys from colliding —
  do not "fix" it.
- **Per-pair gate:** copy only when OOT `.Changed`==1 && MM `.Changed`==1 && MM `.Locked`==0. That covers
  OOT's *advanced* rows (not randomized unless AdvancedMode, so they simply don't fire and MM keeps its
  own color), MM's suppressed options (custom model override → randomize skipped → `Changed` stays 0),
  and MM rows the user locked. A **locked OOT** row is deliberately still copied: it is a color the user
  pinned, and sync's job is to make MM match it.
- **Copy:** OOT RGB via `CVarGetColor24` with alpha 255 — every mapped MM option is `supportsAlpha=false`
  and MM's draw path takes alpha from the caller, never from the CVar — plus `.Changed`=1, `.Rainbow`=0,
  then `MM_MenuApplyCVarChange` on each written key, mirroring MM's own `CosmeticEditorRefreshElement`.
  A **rainbow** OOT source has no fixed color to copy, so MM gets `.Rainbow`=1 instead and both keep
  cycling (reachable when the user turned rainbow on for an *advanced* row, which the gen roll then never
  overwrites).
- **Drift tripwire:** each pair carries an `ootAdvanced` flag. When a non-advanced pair copies nothing
  and its OOT row has neither `.Changed` nor `.Locked` (read with a `-1` sentinel), the id no longer
  exists upstream — logged as a warning naming the pair. The warnings only fire when the run copied at
  least one *other* pair: an upstream rename kills one mapping while the rest still copy, whereas an OOT
  "Reset All" or a run where no roll happened kills all 16 non-advanced pairs and must stay silent.
  Cheap, non-fatal, and the only signal we get that an upstream rename has silently broken a mapping.

**Residuals:**

- OOT's *advanced* rows are not rolled for default users, so their MM counterparts keep their own random
  color. That includes the three arrow **Secondary** colors — a default user can get two-tone arrow
  mismatches (OOT rolls the primaries only; MM's secondaries keep their independent roll). The spin
  attack pairs are unaffected: OOT derives its advanced Primaries from the non-advanced Secondary roll.
- Link's tunic is mapped Kokiri → all four MM forms on purpose. OOT's Goron/Zora tunic rolls are
  equipment colors with no MM equivalent and are intentionally not mirrored.
- `ctx->SetSeed` means OOT saves created from this version on persist a real `finalSeed`, while saves
  made before it carry 0. Per project policy save compatibility across versions is not required, so
  `COMBO_RELEASE_VERSION` is unchanged; the practical effect is that a pre-existing save and a new one
  disagree on seeded features and can report an Anchor seed mismatch against each other.
- The fire is the whole `OnGenerationCompletion` hook, so it also rolls OOT's Audio Editor "randomize
  all on rando gen" (the hook's only other subscriber). That is intended — the same combo gap had
  silently disabled it too.
- The sync itself only ever runs from `Combo_OnOOTSaveInit`, so a seed loaded but never started keeps
  MM's own colors until a slot is created.
- A hand-edited seed file with no `masterSeed` key falls back to 0 consistently on every path (latch,
  `SOH_SetComboRandoSeed`, the rolls), so all such seeds share one palette and one audio shuffle — the
  pre-existing behavior of `SOH_SetComboRandoSeed(0)`, not a new deviation.
