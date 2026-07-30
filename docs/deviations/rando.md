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
  dead, warp to `ENTR_MARKET_DAY_OUTSIDE_HAPPY_MASK_SHOP` (child, no cutscene) instead of the Chamber
  of the Sages credits. Reuses the existing MM→OOT portal arrival point (see title_setup.c).
- `mm/src/overlays/actors/ovl_Boss_07/z_boss_07.c` — Majora's Wrath death: if not both dead, warp to
  `ENTRANCE(SOUTH_CLOCK_TOWN, 0)` (no cutscene) instead of the Termina Field `0xFFF7` credits.

**Deviation from plan:** the OOT first-kill warp targets the Happy Mask Shop area (not Temple of Time)
because the OOT→MM portal is the Happy Mask Shop, only reachable as child in the Market — adult Link at
Temple of Time couldn't reach it.

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
