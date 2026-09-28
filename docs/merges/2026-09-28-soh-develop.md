# Upstream merge — soh — 2026-09-28

### soh — `5a57a0cbc` → `576b30c64` (167 merges)

<details><summary>new commits</summary>

```
576b30c64 Better Save Menu: hard code Return to Spawn in vanilla (#7273)
faed544b2 rename Gerudo Fighter to Gerudo Thief (#7272)
04d0ebd38 Build soh.o2r as part of ALL, only when its inputs change (#7262)
b939163c3 publish appimage unzipped, store to ~/.local/share/soh when SHIP_HOME unset (#7252)
ca1e4c225 Fix Skip Forced Text softlock (#7254)
cd38af795 Fix enemy rando issues in MQ spirit (#7257)
eccbc72cb Fix and hookify full health on spawn (#7239)
2e300d3ac force empty bottle OI to use the milk effect to avoid UB (#7247)
ea4cccc6d fix Ice Cavern Blades Silver Rupee door not opening when silvers are randomised (#7248)
479aa26ef Fix the seeding for Dark Link's spot in enemy rando. (#7249)
8fc1fc869 fix preplanted beans without souls being seen as planted on the tracker (#7251)
33b82dab3 Reduce log spam at start due to invalid RandomizerTrick entries (#7238)
d30fc192f Include <version> before checking __cpp_lib_source_location (#7233)
d54a69833 avoid big poe collector not giving reward when handing bottle after collecting 1000 points worth of big poes on field (#7230)
39a8d0401 Use --no-pager for the no-tabs check's git grep (#7232)
04ccd608b Fix equipping sword over swordless while fishing/riding (#7222)
5fee87c23 Replace tabs with spaces (#7223)
27a66925b Add restoration for hookshot as child to softlock (#7219)
2bc50f605 Fix deep slope vs double cell carpenter mix up (#7220)
a353b6e7c Shuffle Scarecrow Song (#7205)
02c7632a3 Fix boolean logic error breaking Talon cutscene skip (#7211)
01a4f3693 Refactor file select quest visibility, fix single quest handling (#7210)
c9e3ef765 Interrupt espeak on a new utterance (#7207)
e628cf057 refactor blue fire arrows (#7209)
ae6204891 Hookify three Glitch Restoration options (#7200)
aff03cd7b Hookify Mweep cutscene options (#7199)
4bd2356b1 QuickBossDeaths.cpp (#7192)
4c04c7615 Save editor flag improvements (#7198)
034bf5f3c Swap Logic combobox for No Logic checkbox. (#7196)
31ba9e206 fix fire boss typo (#7195)
060787a23 clean up headers (#7194)
43ea33287 [Enhancement] Time Splits v2 (#5839)
97fb13cbe Hookify FP movement (#7189)
09ca2ad38 Maintain correct position/camera for Master Sword cutscene skip (#7184)
53f688df5 Fix logic for fire near boss (#7147)
ea348bc9f a11y: let F9 toggle TTS on Linux (#7191)
11fd0e0e7 Fix bow body cosmetic color (#7190)
8b3d91506 Fix title card margins (#7167)
78dc6d970 Don't erase __attribute__ under clang-cl (#7186)
f00f5b625 Optimise the 64-bit Windows Release build (#7188)
b81187db3 Keep /EHsc when clearing the MSVC default flags (#7185)
3cfe4ddff Prevent despawning shuffled gerudo guard drops (#7183)
f04b570d2 Fix logic typo (#7181)
01166a975 Fix discord invite (#7177)
18f8770ea item tracker: don't render items for disabled settings (#7151)
36d03b9bc Fix save editor v2 (#7170)
d37a37846 Don't apply inverted X axis aiming to first person movement (#7163)
f3830ee8a Fix megadives with jump clamping (#7164)
60dabb21e Add Skip Warp Cutscene option to All/None cutscene skip buttons (#7160)
e2a82213c Reduce big octo cutscene skip (#7158)
a5f590395 water temple: quadruple water level change when skipping cutscene (#7162)
b4c41c9bf Jabu fish cutscene skip: immediately transition (#7159)
371ef6f84 Leave Start Randomizer disabled after generation fails (#7157)
86a55cb1f render stick/nut bag for upgrades past first (#7156)
97f4fd592 Ice Trap Name Setting (#6588)
61523391e Fix flare dancer crash (#7122)
17a4c2cf8 Reopen the OPUS decoder when a note moves to another sample (#7137)
e44bd8d98 prefer spdlog over luslog in C++ code (#7149)
6d65db617 Fix missing entries  in RC to randinf and new identity pattern to make future misses more obvious (#7141)
495a2b17d revise randomizerEnumStrings, include fmt compatibility (#7148)
73a739b7a Fix various trick issues (#7145)
eca767dcb Logical bunny hood (#7032)
0364f7076 Fix seed generation crash (#7140)
acdbc651d Fix compat issues loading pre-9.1 saves on 9.2+ (#7132)
8ca5ca60a Lang Improvements (#7130)
3175ea9c2 Remove completed mask quest option, add all masks to starting items menu (#7104)
a7cd6c811 Avoid randomizer generation races with file select (#7136)
2dd51f128 [Enhancement] Hide quests in File Select (#7093)
2ba5c40da Bombchu logic fix: don't allow bombchus in single bag without bag, eg arms dealer (#7134)
896bf97a4 Move Tracker namespace to its own file (#7135)
8bf26365b Honour the loop end when reading streamed music (#7129)
2ab81a9ba Fix King Zora talkstate not updating (#7125)
6694b5644 Fix crash when dying during remote bombchu (#7108)
a5a182618 Fix check tracker chest game key logic (#7133)
bb6d1d3b4 Cleanup mExcludeLocationsOptionsArea (#7131)
b043cdf40 Fix loop condition in WriteExcludedLocations function (#7127)
808a112b9 Bump libultraship for the WASAPI shutdown hang (#7128)
6d1a6527d Bugfix for AlwaysOnFixes.cpp as well as a fallback to prevent crashes when attempting to display an textId 0 (#7123)
1413ceb33 Fixed carpenter guard hylian jabbernut requirement (#7121)
4868b0712 Prevent double give by treasure chest game shopkeeper (#7118)
e09e15e7e Gamepad Mapper (#7116)
cc63cc4ab fix logic bugs with Bombchus (#7117)
78e3683ef Side room pot 5 and 6 logic fix (#7107)
ce969cab9 clean up includes (#7114)
b341e02cd Remove unused field `progressive` from Item (#7113)
07414fd72 Clear fire timer when blue warp skip bypasses chamber of sage (#7112)
47d138302 MQ DC: standing in corner slingshot can hit through eye through boulder (#7109)
945f70222 Restore the realId assignment in AudioLoad_AsyncLoadInner (#7105)
7a7f942b8 Fix item tracker fishing pole & roc's feather (#7103)
5f52af7ef Enemy rando, make Keese not fall if not dead + fall downscale (#7101)
e8b943968 Disable2DBackgrounds Fixes (#7102)
27b71b1a5 Drop the seqLoadStatus bounds checks made redundant by #6932 (#7100)
22426a812 Resolve the Fast3dGui cast once per scope instead of per call (#7095)
17c57c64c Try fix mac build flakiness in CI with mdutil -a -i off (#7094)
9baa114f6 Hookify fish, refactor fishsanity (#7091)
401605532 Set ToD in non-rando LACS & MSCS skips (#7092)
bd8a5825f Hookify masks / timeless equipment (#7088)
172b373fb Adjust fishing prize threshold when minigame weight adjusted (#7089)
4c43ef030 Fix crash when loading unversioned oot_save.sav (#7090)
6a3c9b760 Add a Skip Warp Cutscenes enhancement (#7059)
```
</details>

**Conflict surface** (our customized files upstream touched):

```
CMakeLists.txt
include/z64audio.h
soh/Enhancements/AlwaysOnFixes.cpp
soh/Enhancements/ExtraModes/EnemyRandomizer.cpp
soh/Enhancements/FileSelectEnhancements.cpp
soh/Enhancements/FileSelectEnhancements.h
soh/Enhancements/Fixes/DarkLinkFixes.cpp
soh/Enhancements/Fixes/FixFlexDrops.cpp
soh/Enhancements/Graphics/Disable2DBackgrounds.cpp
soh/Enhancements/Graphics/VisualAgony.cpp
soh/Enhancements/Lang/Lang.cpp
soh/Enhancements/Minigames/DampeBothPrizes.cpp
soh/Enhancements/Presets/Presets.cpp
soh/Enhancements/Restorations/N64WeirdFrames/N64WeirdFrames.cpp
soh/Enhancements/TimeDisplay/TimeDisplay.cpp
soh/Enhancements/TimeSavers/FasterShadowShip.cpp
soh/Enhancements/audio/AudioEditor.cpp
soh/Enhancements/cosmetics/CosmeticsEditor.cpp
soh/Enhancements/debugconsole.cpp
soh/Enhancements/debugger/colViewer.cpp
soh/Enhancements/debugger/debugSaveEditor.cpp
soh/Enhancements/game-interactor/vanilla-behavior/GIVanillaBehavior.h
soh/Enhancements/kaleido.cpp
soh/Enhancements/kaleido.h
soh/Enhancements/mod_menu.cpp
soh/Enhancements/randomizer/3drando/fill.cpp
soh/Enhancements/randomizer/3drando/hint_list.cpp
soh/Enhancements/randomizer/3drando/hint_list/hint_list_exclude_dungeon.cpp
soh/Enhancements/randomizer/3drando/hint_list/hint_list_exclude_overworld.cpp
soh/Enhancements/randomizer/3drando/hint_list/hint_list_item.cpp
soh/Enhancements/randomizer/3drando/hints.cpp
soh/Enhancements/randomizer/3drando/item_pool.cpp
soh/Enhancements/randomizer/3drando/spoiler_log.cpp
soh/Enhancements/randomizer/3drando/starting_inventory.cpp
soh/Enhancements/randomizer/Messages/ItemMessages.cpp
soh/Enhancements/randomizer/Messages/MerchantMessages.cpp
soh/Enhancements/randomizer/Messages/StaticHints.cpp
soh/Enhancements/randomizer/Plandomizer.cpp
soh/Enhancements/randomizer/SeedContext.cpp
soh/Enhancements/randomizer/ShuffleSigns.cpp
soh/Enhancements/randomizer/Traps.cpp
soh/Enhancements/randomizer/Traps.h
soh/Enhancements/randomizer/draw.cpp
soh/Enhancements/randomizer/draw.h
soh/Enhancements/randomizer/dungeon.cpp
soh/Enhancements/randomizer/entrance.cpp
soh/Enhancements/randomizer/fishsanity.h
soh/Enhancements/randomizer/hint.cpp
soh/Enhancements/randomizer/hook_handlers.cpp
soh/Enhancements/randomizer/item.cpp
soh/Enhancements/randomizer/item.h
soh/Enhancements/randomizer/item_list.cpp
soh/Enhancements/randomizer/item_override.cpp
soh/Enhancements/randomizer/item_override.h
soh/Enhancements/randomizer/location_access/dungeons/bottom_of_the_well.cpp
soh/Enhancements/randomizer/location_access/dungeons/deku_tree.cpp
soh/Enhancements/randomizer/location_access/dungeons/fire_temple.cpp
soh/Enhancements/randomizer/location_access/dungeons/shadow_temple.cpp
soh/Enhancements/randomizer/location_access/dungeons/spirit_temple.cpp
soh/Enhancements/randomizer/logic.cpp
soh/Enhancements/randomizer/option_descriptions.cpp
soh/Enhancements/randomizer/randomizer.cpp
soh/Enhancements/randomizer/randomizerEnums/RandomizerGet.h
soh/Enhancements/randomizer/randomizerEnums/RandomizerSettingKey.h
soh/Enhancements/randomizer/randomizer_check_tracker.cpp
soh/Enhancements/randomizer/randomizer_entrance_tracker.cpp
soh/Enhancements/randomizer/randomizer_item_tracker.cpp
soh/Enhancements/randomizer/savefile.cpp
soh/Enhancements/randomizer/settings.cpp
soh/Extractor/Extract.cpp
soh/Extractor/Extract.h
soh/GbiWrap.cpp
soh/Network/Anchor/Anchor.cpp
soh/Network/Anchor/Anchor.h
soh/Network/Anchor/AnchorRoomWindow.cpp
soh/Network/Anchor/HookHandlers.cpp
soh/Network/Anchor/Menu.cpp
soh/Network/Anchor/Packets/GiveItem.cpp
soh/Network/Anchor/Packets/UpdateTeamState.cpp
soh/Network/CrowdControl/CrowdControl.cpp
soh/Network/CrowdControl/CrowdControl.h
soh/Network/Network.h
soh/Network/Sail/Sail.cpp
soh/Network/Sail/Sail.h
soh/Notification/Notification.cpp
soh/OTRGlobals.cpp
soh/OTRGlobals.h
soh/ResourceManagerHelpers.cpp
soh/SaveManager.cpp
soh/ShipInit.hpp
soh/ShipUtils.h
soh/SohGui/Menu.cpp
soh/SohGui/Menu.h
soh/SohGui/MenuTypes.h
soh/SohGui/ResolutionEditor.cpp
soh/SohGui/SohGui.cpp
soh/SohGui/SohMenu.cpp
soh/SohGui/SohMenu.h
soh/SohGui/SohMenuDevTools.cpp
soh/SohGui/SohMenuEnhancements.cpp
soh/SohGui/SohMenuRandomizer.cpp
soh/SohGui/SohMenuSettings.cpp
soh/SohGui/UIWidgets.hpp
soh/cvar_prefixes.h
soh/resource/importer/AudioSampleFactory.cpp
soh/resource/importer/AudioSequenceFactory.cpp
soh/resource/type/Scene.cpp
soh/z_message_OTR.cpp
soh/z_play_otr.cpp
src/code/audio_heap.c
src/code/audio_load.c
src/code/graph.c
src/code/z_actor.c
src/code/z_en_item00.c
src/code/z_parameter.c
src/code/z_sram.c
src/code/z_vr_box.c
src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.c
src/overlays/actors/ovl_En_Mag/z_en_mag.c
src/overlays/actors/ovl_En_Peehat/z_en_peehat.c
src/overlays/actors/ovl_En_Peehat/z_en_peehat.h
src/overlays/actors/ovl_En_Po_Relay/z_en_po_relay.c
src/overlays/actors/ovl_Mir_Ray/z_mir_ray.c
src/overlays/actors/ovl_player_actor/z_player.c
src/overlays/gamestates/ovl_file_choose/z_file_choose.c
src/overlays/misc/ovl_kaleido_scope/z_kaleido_scope_PAL.c
```

## Headline

- **Torch replaces ZAPD for soh.** Upstream (#6989/#7063/#7082) extracts OOT with Torch and packs
  `soh.o2r` with `soh-o2r-packer`. mm keeps ZAPD/OTRExporter. Torch is vendored at `torch/`
  (`2ab12fe96` = soh's gitlink; manual pin in `upstream-pins.json`).
- **Every existing combosave and consolidated spoiler is retired.** RSK/RG/RC/RandomizerInf gained
  mid-enum entries; old saves are gated out by the version check and old spoilers fail to replay.
  No repair machinery, by policy. `COMBO_RELEASE_VERSION` stays `0.3.0`.

## Build wiring (root `CMakeLists.txt`, `combo/CMakeLists.txt`)

- Torch block inserted after libultraship/ZAPD/OTRExporter and before `soh`, same shape as upstream's
  root. No spdlog pre-declare (Torch#233): libultraship already found spdlog. `ZLIB` and `tinyxml2` are
  found at root so soh's libzip/tinyxml2 users get vcpkg's copies, not Torch's `OVERRIDE_FIND_PACKAGE`
  fetches.
- **CRT:** Torch forces the static CRT and its `cmake_minimum_required(3.12)` leaves CMP0091 unset.
  `CMAKE_POLICY_DEFAULT_CMP0091 NEW` + `combo_dynamic_crt_tree` set `MultiThreaded[Debug]DLL` on every
  torch-tree target (torch, BinaryTools, N64Graphics, tinyxml2, yaml-cpp, zlibstatic) and rewrite
  Torch's explicit `/MT(d)` options. Verified in the generated projects.
- Linux: `torch yaml-cpp BinaryTools N64Graphics` join the hidden-visibility list (they carry their own
  `StringHelper`/CRC64/`stbi_*` copies).
- `GenerateSohOtr` runs `soh-o2r-packer` (no Python/ZAPD); still manual, not `ALL`. Output is
  `soh/soh.o2r`, then `copy-existing-otrs.cmake` deploys it as before.
- soh's extractor assets are now `soh/assets/yml` (config.yml + one dir per ROM version). Install
  rules and ComboShip's POST_BUILD copy them into the flat `assets/` next to mm's ZAPD configs; no
  names overlap. Wipe an old `x64/Debug/assets` once (stale OOT xml there is harmless).
- CI: `torch/**` in the path filters and the build cache key; comments updated.

## Conflict resolutions (58 hunks in 40 files)

- Took upstream where our side had no combo code: AlwaysOnFixes, DarkLink/FlexDrops fixes, save
  editor, GI behaviour header, hint_list, ItemMessages, Plandomizer/SeedContext/Traps (new ice-trap
  names API), dungeon.cpp/logic.cpp (our null guards are obsolete: `std::span` + upstream null
  checks), fishsanity.h (namespace refactor), Sail, SohMenuRandomizer (custom keys always on),
  UIWidgets (upstream now initializes `longest`), Extract.cpp (see below).
- Both sides: CosmeticsEditor, StaticHints, MerchantMessages includes, hint.cpp, randomizer.cpp,
  draw.h, settings/menus includes, Menu.h (`MenuDrawItem` lost its width) + our `DrawContent`.
- Re-applied on upstream's new code: MerchantMessages foreign-item name (now a small
  `ComboForeignMerchantName` helper checked before the `inShop` split), hook_handlers empty-sentinel
  guard, item tracker notes hiding, settings.cpp (our two Mask Shop options on the new `OPT_BOOL`
  arity, win-condition ownership, Mask Shop hint hidden), Lang (headless bypass now in `TryTranslate` +
  `Translate`, returning a cached reference), SaveManager (container parse, no legacy rewrite, no `.bak`
  eviction or popup on the container path), OTRGlobals (four duplicate `strdup` defines dropped),
  file select, Anchor includes.
- `option_descriptions.cpp` removed (upstream deleted it; names/descriptions come from
  `assets/custom/lang/en_US.json`).

## Post-merge changes (unmarked breaks + silent regressions)

- **Extraction:** `CallZapd` → `CallTorch` in `SOH_Extract`/`SOH_StartExtraction`. Upstream's
  Extract.cpp no longer stages assets in a tempdir or `chdir`s and returns real success, so our symlink
  and success-override deviations are retired. `SOH_StartExtraction` writes `soh_extract_error.log` on
  failure; a failed archive copy removes the partial file.
- **Combo generation** (#7136 replaced the `RandoGenerating` CVar with a native flag): a combo flag
  is folded into `IsRandoGenerating()`, set by `SOH_TriggerComboGenerate`, cleared when
  `SOH_PollComboFinalize` reports the seed resolved. Nothing is persisted, so a crash mid-generation
  leaves no stuck state.
- **`audio_load.c`:** git kept our #6917 cherry-pick over upstream's #7100 revert; took upstream.
- **Renamed options:** `RSK_LOGIC_RULES`/`RSK_ALL_LOCATIONS_REACHABLE`/`RSK_MASK_QUEST` →
  `RSK_NO_LOGIC`/`RSK_ALL_CHECKS_REACHABLE`/`RSK_SHUFFLE_MASKS` in the fill and the dump (JSON keys
  unchanged). The dump also records `iceTrapNames`. `SOH_RestoreRandoSettings` applies upstream's
  ConfigVersion7 rename table (upstream appended it to an updater that existing configs already ran).
  Existing `comboship.json` files keep their old keys, so those settings fall back to defaults.
- **Lang keys:** `exclude_mask_shop_key`/`exclude_mask_shop_entrance` added to `en_US.json`
  (otherwise both labels read "[ERROR]" and collide).
- **Headless:** `SOH_InitRandoHeadless` registers silver rupee locations; `comborando` accepts the
  `NoLogic` key.
- **API drift:** `EnumToString` returns `string_view`; `MenuDrawItem` has no width.
- **Keyrings:** the per-dungeon keyring DLs and the Custom Key Models CVar are gone. Foreign keyrings
  draw ring + emblem + one key (the native draw repeats the key with per-key matrices); the Treasure
  Chest Game small key now uses its emblem.
- **Time Splits** (upstream ported 2Ship's): same window names, same `ImGui::Begin("Timesplits")`, and
  soh's button CVar `gWindows.Timesplits.Settings` clashed with MM's leaf `gWindows.Timesplits`
  (unflatten failure = config stops saving). soh's button uses its own `gWindows.TimeSplitSettings`
  and gets an inline settings shim. MM's windows get the `##MM` suffix, its overlay ID becomes
  `Timesplits##MM`, and its settings window CVar moves to `gWindows.TimesplitsSettings` (the old key is
  cleared on init). `ComboTrackerVisibility` keeps only the foreground game's split overlay.
- **Check tracker:** `SetAreaSpoiled` still refreshes the item tracker when the combo bulk load
  suppresses the save.
- **RM scope:** Gamepad Mapper diagram and mod menu Cancel read/mount through OOT's resource manager.

## Deferred (follow-up issues)

- Hide-quest options that are no-ops in combo (`SohMenuEnhancements.cpp` quest hiding).
- Foreign draws for silver rupees, Scarecrow's Song, nut/stick bags (fall back to the item-table
  model today).
- Foreign ice traps honour only the "Similar" name style (`iceTrapNames` is now in the dump).
- `SetSpoilerLoaded(false)` on combo generation failure (#7157); upstream `IdentifyCheck` null deref
  (#7141, not on combo paths).
