# Upstream merge — mm — 2026-09-28

### mm — `d35196ad7` → `e8757c14a` (33 merges)

<details><summary>new commits</summary>

```
e8757c14a Update build doc and readme
853156049 Disable object dependency for rando time reset NPC (#1953)
818e7a6bb 16 Bit Sound Font Ids (#1863)
6bfd6a35a Fast Transformation: After First Time option (#1949)
5c1243c38 [Enhancement] Difficulty options for Fisherman's Jumping Game (#1947)
fd42de862 Add SoT reset NPC to clock tower rooftop (#1945)
69d3f8ac8 [Enhancement] Always find Rock Sirloin (#1944)
3f51ada63 [Enhancement] Alien speed modifier (#1940)
c821e4a22 Add 'Dash After Roll' enhancement (#1938)
bc3fbeec5 Reorganize Difficulty Options (#1942)
2e211bf0a Save Persistent Bunny Hood state in file info (#1933)
e26240d6f Curated preset updates (#1931)
14cdcf679 Ensure branch names show up for PR builds (#1932)
4c91dcf78 Add option for shuffling remains in it's own dungeon (#1930)
e3a0f2853 Add option to skip Alien Invasion mini-game (#1920)
528eed442 Shuffle songs on song locations (#1928)
b4b4299e4 Merge pull request #1927 from HarbourMasters/develop-battler
f56c7939e Moar Cosmetics (#1891)
337ec608d Bump LUS (#1925)
6bf849276 Add checkbox to change Z-Targeting mode (#1919)
b011f16bc Add vanilla option for moon trials (#1896)
966bff624 Option for moon gossip stones to hint their mask (from vanilla) (#1903)
56d96dc6b Merge pull request #1909 from HarbourMasters/develop-battler
10bfaa0f0 Wire up background input toggle (#1898)
431dea3c6 develop-battler -> develop (#1892)
7e6907f96 Allow freelook in more situations (#1877)
0aebaf297 Fix Circle Shadow Streaks (#1888)
9afe75860 Limit Ocarina dive fix to hovering only (#1847)
525c1bc62 Toggle for screen distortion (#1880)
5d96fd84f Add Dungeon Specific Keys (#1881)
236aa75ba Allow dungeon items to be set to vanilla locations (#1883)
dabc3aadc zora swim Y invert (#1887)
4c128e4cd Merge develop-keiichi->develop
```
</details>

**Conflict surface** (our customized files upstream touched):

```
2s2h/BenGui/BenMenu.cpp
2s2h/BenGui/CosmeticEditor.cpp
2s2h/BenGui/Menu.cpp
2s2h/BenGui/MenuTypes.h
2s2h/BenJsonConversions.hpp
2s2h/BenPort.cpp
2s2h/DeveloperTools/SaveEditor.cpp
2s2h/DeveloperTools/SaveEditor.h
2s2h/Enhancements/Trackers/ItemTracker/ItemTracker.cpp
2s2h/Enhancements/Trackers/ItemTracker/ItemTracker.h
2s2h/Enhancements/Trackers/ItemTracker/ItemTrackerSettings.cpp
2s2h/PresetManager/PresetManager.cpp
2s2h/Rando/ActorBehavior/EnBal.cpp
2s2h/Rando/ActorBehavior/EnGs.cpp
2s2h/Rando/CheckTracker/CheckTracker.cpp
2s2h/Rando/DrawItem.cpp
2s2h/Rando/Logic/GeneratePools.cpp
2s2h/Rando/Logic/Logic.h
2s2h/Rando/Logic/Regions/BeneathTheWell.cpp
2s2h/Rando/Logic/Regions/Central.cpp
2s2h/Rando/Logic/Regions/East.cpp
2s2h/Rando/Logic/Regions/GreatBayTemple.cpp
2s2h/Rando/Logic/Regions/Moon.cpp
2s2h/Rando/Logic/Regions/SnowheadTemple.cpp
2s2h/Rando/Logic/Regions/South.cpp
2s2h/Rando/Logic/Regions/West.cpp
2s2h/Rando/Menu.cpp
2s2h/Rando/StaticData/Checks.cpp
2s2h/Rando/Types.h
2s2h/SaveManager/SaveManager.cpp
2s2h/resource/importer/AudioSequenceFactory.cpp
include/z64save.h
src/code/z_parameter.c
src/code/z_play.c
src/code/z_sram_NES.c
src/overlays/actors/ovl_player_actor/z_player.c
src/overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope_NES.c
```

## Conflict resolutions

Same 5.0.1 range as main's PR #222 ([2026-09-28-mm.md](2026-09-28-mm.md) on `main`); the code
resolutions match main's text so a later develop→main merge does not re-conflict.

- `ItemTracker.cpp` / `ItemTrackerSettings.cpp` — took upstream's `GetVanillaItemIdForSlot` at both
  call sites and moved our two needs into the helper (`COMBO_BUILD`): the dormant-MM peek gate
  (develop's version, which also checks `IS_RANDO`) and the empty safe-item-list guard. The slot
  icon lookup skips `ITEM_NONE`, which is past the end of `gItemIcons`.
- `EnBal.cpp` (Tingle) — took upstream's `ConvertItem` wrap and kept our `livePreview` argument, so
  a foreign item still shows its live name.
- `EnGs.cpp` — kept upstream's new `GetGossipStone()` / `ShouldHintMoonMask()` and our
  `GetRandomCheck` signature (foreign-hint out-params) on top of upstream's body.
- `CosmeticEditor.cpp` — both include blocks.

## Post-merge changes

- **Shared Context kept alive (`BenPort.cpp` `DeinitOTR`).** #1879's
  `Ship::Context::DestroyInstance()` is `#ifndef COMBO_BUILD`, same as main. After `MM_Deinit()`
  the Context is still alive and `SOH_Deinit()` is the only place that destroys it. See
  [deviations/boot-shutdown.md](../deviations/boot-shutdown.md).
- **Fill parity (`MM_DumpRandoStaticData`).** `GeneratePools` now drops more checks from
  `checkPool` after marking them `shuffled` in the discarded local save: vanilla dungeon items
  (#1883), surplus song locations under "Songs on Song Locations" (#1928, junk). The 5.0.0
  skulltula-only block became one loop that emits every such check as `fixed[]` with the item
  native would have (`hintable=true`, like native). This also covers MM excluded checks, which
  now get junk like native instead of keeping their vanilla item. Surplus song checks show as
  junk, not "skipped", in MM's check tracker. The two remains checks now read
  `RO_REMAINS_SHUFFLE_VANILLA` (same value as before). See `docs/COMBO_FILL_PARITY.md` 10b–10d.
- **Known gap (not fixed here):** MM own-dungeon and song-location confinement is not honoured in
  combo seeds (pre-existing for keys; see `docs/COMBO_FILL_PARITY.md` GAP-9). Fix parked on
  `wip/mm220-dump-confinement` for a follow-up PR.
- **Moon gossip-stone mask hints (#1903, `EnGs.cpp`).** Upstream only searches MM checks, so a
  mask placed in OOT read "in an Unknown Location". Under `COMBO_BUILD` the hook uses
  `Rando::GetItemLocationHintName` (the helper other MM NPC hints already use), which also finds
  OOT placements and reports them to the Hint Tracker. MM placements are reported through
  `ComboReportStoneHint`.
- Merge log renamed to `2026-09-28-mm-develop.md`: main already has `2026-09-28-mm.md`.

Absorbed without changes:
- Save version 7→8 (Migration 8, persistent Bunny Hood): `.combosav` MM halves migrate through the
  same version map. Existing MM rando saves go stale from the new options; accepted.
- New `RO_`/`RR_` values (remains own-dungeon, song locations, Alien skip, ...) are not referenced
  by `combo/`. No `RI_` changes.
- Moar Cosmetics (#1891): every id `ComboCosmeticsSync` targets still exists; the new
  `HUD.MagicChateau` is not synced (follow-up).
- Stray fairies: upstream now sets `RO_STRAY_FAIRIES_MAX = 15` under VANILLA and starts with the
  Clock Town fairy under START_WITH. The dump/apply do not copy the REQUIRED/MAX normalisation
  (follow-up; REQUIRED ≤ 15 is always reachable).
- 15 new custom assets (dungeon-key DLs, `flatavg` shaders): regenerate `2ship.o2r`. The asset
  collision check passes.

Headless matrix (`comborando` gen + `--playthrough`): 7/7 PASS — defaults, dungeon items Vanilla,
remains Own Dungeon, songs on Song Locations, stray fairies Start With, those combined, keys/fairies
Own Dungeon.

## Post-merge build fixes

None: `2ship`, `ComboShip`, `comboui` built clean in Debug with no changes beyond the above.
