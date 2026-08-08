# ComboShip deviations — Resource manager & rendering

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## MM cross-RM display lists must not be eagerly resolved (foreign-draw crash fix) (2026-06-17)

**Why:** entering MM with a cross-world seed that placed a foreign OOT item at an MM check crashed on
the first MM draw: `Rando::DrawItem → MM_DrawComboForeign → gSPDisplayList → ResourceMgr_LoadGfxByName
→ std::vector<Gfx>::operator[]` out of bounds. `MM_DrawComboForeign` submits the OOT model's display
lists as `__OTR__@oot:…` routed paths. MM's `gSPDisplayList` stub eagerly resolves any `__OTR__`
pointer via `ResourceMgr_LoadGfxByName` (→ MM's own RM), but a cross-game `@oot:` path isn't in MM's
archives, so it returned a DisplayList with an empty `Instructions` vector and `&Instructions[0]`
crashed. Cross-RM-routed paths must instead reach the Fast3D interpreter
(`gfx_dl_otr_filepath_handler_custom`, `libultraship/src/fast/interpreter.cpp`), which parses
`@<game>:`, routes via `CrossRMRegistry`, and already null-guards bad routes. This is also the likely
cause of the "corrupted MM save" reports (a crash mid-MM leaves a half-written save).

**`mm/src/code/stubs.c` (vendored, COMBO_BUILD-guarded — preserve on future mm merges):** in
`gSPDisplayList`, when the OTR-signature path is a route marker (`imgData[7] == '@'`), emit it as a
`G_DL_OTR_FILEPATH` command (`gDma1p(pkt, G_DL_OTR_FILEPATH, imgData, 0, G_DL_PUSH)`) and return,
instead of eager-resolving. This is the **exact mirror** of the OOT-side guard already present in
`soh/soh/GbiWrap.cpp:gSPDisplayList` (commit for the OOT-foreign-in-MM feature) — MM was simply
missing the symmetric half. No original lines deleted; non-combo builds keep eager resolution.

## Fast3D guard: never branch into an unbound N64 segment (foreign-draw crash class) (2026-07-11)

**Why:** a cross-game foreign item can submit a raw `G_DL` that references a segment the host game
never bound (e.g. an MM item's animated-material segment 8 that OOT's foreign draw didn't set up).
`SegAddr` returns the raw `0x0S……` address as-is, and `gfx_dl_handler_common` branched into it with
no validity check → the interpreter ran garbage as GBI (tell: an impossible `G_PUSH_SHADER` no asset
emits) → null-shader deref in `GfxSpTri1`. Hit by the foreign Moon's Tear (`gGiMoonsTearItemDL`);
the skull-token flame was the same class, patched ad-hoc earlier.

**`libultraship/src/fast/interpreter.cpp` (COMBO_BUILD-guarded — preserve on future lus merges):**
`gfx_dl_handler_common` **and** `gfx_dl_index_handler` now reject a resolved target still in the
unresolved segment range (`ComboIsUnresolvedSegmentTarget`: null, or `<= 0x0FFFFFFF` and not a real
module address on Windows) — log once per target and skip the command (same `return false` skip the
OTR-filepath handler uses for a bad route). The predicate mirrors the existing timg validation in
`gfx_set_timg_handler_rdp`. A legit native DL always binds its segment first, so this only ever fires
on cross-game garbage.

## Cross-game Moon's Tear render: replicate the animated material + billboard (2026-07-11)

**Why:** the foreign MM Moon's Tear draws a two-tex-scroll animated material on segment 8 (item body
+ glow) via `AnimatedMat_Draw(gGiMoonsTearTexAnim)` and a billboard on the glow. The cross-game
export carried neither, so the item drew wrong (and, before the guard above, crashed on the unbound
segment). Generalizes the ad-hoc skull-token `xluSeg8TexScroll` flame path to resource-driven
animated materials.

**Combo-owned (no vendored churn):** `combo/menu/ComboForeignAnim.h` gains type-1 (DualScroll)
support + `ComboForeignTexAnim_Run`/`_Restore` (loads the owning game's `TextureAnimation` via
`CrossRMRegistry`, binds seg 8 on OPA+XLU, restores after the DLs). `combo/menu/ComboItemDrawABI.h`
+ `ComboItemDrawMM.h` add `matAnimPath`/`matAnimBindOpa`/`matAnimBillboard` (MM matches the tear by
its DL string). `soh/.../randomizer/draw.cpp` binds/billboards/restores around the DL submission.

**`mm/src/code/z_draw.c` (comment only):** the Moon's Tear branch comment in the combo-owned
`GetItem_GetDrawTableEntry` no longer claims the scroll/billboard "are dropped" (the consumer now
replicates them).

## MM shader assets synced to merged libultraship (OpenGL transition crash, 2026-07-02)

**Why:** The 2026-06-29 upstream merge moved the shared libultraship OpenGL backend to the single
prism shader (`shaders/opengl/default.shader.glsl`), but `mm/assets/custom/shaders/` still shipped
the pre-merge split `default.shader.fs`/`.vs`. Shader sources load from the ACTIVE game's
ResourceManager, so with MM active any new shader-variant compile missed in `2ship.o2r` and hit the
`abort()` in `gfx_opengl.cpp` ("Failed to load default fragment shader, missing f3d.o2r?") — every
OOT→MM transition crashed on the OpenGL backend. D3D11/Metal masked it (their filenames didn't
change), which is why Windows runs never saw it.

**Vendored (assets only, no code):** `mm/assets/custom/shaders/` replaced with byte-identical copies
of `libultraship/src/fast/shaders/` (add `.glsl`, drop dead `.fs`/`.vs`, refresh stale `.hlsl` /
`.metal`) — now matching `soh/assets/custom/shaders/`. Requires regenerating `2ship.o2r`
(`Generate2ShipOtr`). Rule for future merges: whenever the shared LUS shader sources change, both
ports' `assets/custom/shaders/` must be re-synced and their `.o2r` regenerated.

## Cosmetics editors: scope the owning game's RM (cross-game open crash, 2026-07-02)

**Why:** `Context::GetInstance()->GetResourceManager()` returns the FOREGROUND game's RM. Opening
MM's Cosmetic Editor from the combo menu while OOT is foreground made its palette patchers
(`ShadePaletteNewBase` etc.) `LoadResource` MM paths through OOT's RM → null → crash on
`GetRawPointer()` (mirror case for OOT's editor while MM is foreground). The
`combo/gui/ComboWidgetRender.h` design note places RM scoping GAME-SIDE in the menu exports, but it
was never implemented.

**Port code (BenPort.cpp / OTRGlobals.cpp):** the four menu exports per game
(`*_MenuInvokeCallback`, `*_MenuApplyCVarChange`, `*_MenuEvalDisabled`, `*_MenuDrawCustom`) now wrap
their body in `Ship::ResourceManagerScope(CrossRMRegistry::Get("mm"/"oot"))` — no-op when that game
is foreground or not yet registered. Covers every inline widget/window draw and CVar-change apply.

**Vendored (`COMBO_BUILD`-guarded, +22/-0 lines):** `mm/2s2h/BenGui/CosmeticEditor.cpp` and
`soh/soh/Enhancements/cosmetics/CosmeticsEditor.cpp` — same scope at the top of
`DrawElement()`. Needed because the POPOUT window is drawn by the shared Gui loop directly,
bypassing the menu exports.

## Audio resource loads pinned to the owning game's RM (audio-thread race, 2026-07-02)

**Why:** each game's audio thread loads fonts/sequences/samples through the swappable ACTIVE RM
(`ResourceGetDataByName` → `Context::GetResourceManager()`). Any `ResourceManagerScope` swap on
another thread (combo-menu exports above, foreign draws) races it: during MM Cosmetic Editor
"Randomize All" (long scope, global RM = MM's), OOT's audio thread resolved an OOT soundfont
against MM's RM → null → unguarded `sf->numDrums` crash in `Audio_GetDrum`
(`soh/src/code/audio_playback.c:366`). Audio semantically always wants its OWN game's assets, so
its loads now bypass the global via `CrossRMRegistry::GetOrActive("oot"/"mm")` — falls back to the
active RM pre-registration / non-combo. Game source (`audio_*.c`) untouched.

**libultraship (combo-owned):** `CrossRMRegistry` gains `GetOrActive()` and a `std::shared_mutex`
(Get() is now called from audio threads, not just the interpreter — the old no-mutex comment's
"add one when multi-threaded" condition triggered).

**Port code:** `soh/soh/ResourceManagerHelpers.cpp` + `mm/2s2h/BenPort.cpp` — the audio bridge fns
(`ResourceMgr_LoadSeqByName`/`LoadSeqPtrByName`/`LoadAudioSample`/`LoadAudioSoundFontByName`/
`...ByCRC`) load via the pinned RM (soh side behind a `COMBO_OWN_RM()` macro with an `#else`
preserving upstream behavior).

**Vendored (`COMBO_BUILD`-guarded):** both games' `resource/importer/AudioSoundFontFactory.cpp`
(nested sample loads, 11 sites each via the `COMBO_OWN_RM()` macro), `AudioSampleFactory.cpp` and
`AudioSequenceFactory.cpp` (one archive `LoadFile` each) — these factories run on the RM worker
pool and read the global mid-load, so they need the same pinning. `#else` keeps upstream lines.

**Residual (known, pre-existing):** non-audio factories (Skeleton/Animation/etc.) still nested-load
via the global — that's load-bearing for `ResourceManagerScope` (ComboForeignAnim's scoped foreign
loads rely on it). Their exposure is limited to async loads in flight during a scope; unchanged.

## ShipInit::Init scoped to the owning game's RM (HUD Editor preset crash, 2026-07-03)

**Why:** `ShipInit` init funcs include the cosmetic patchers (`ShadePaletteRevert` → `LoadResource`),
and they fire from UI paths outside the scoped menu exports — e.g. MM's POPOUT HUD Editor
(`HudEditor.cpp` preset/color handlers call `ShipInit::Init`), drawn by the shared Gui loop where
the ACTIVE RM is the foreground game's. Selecting a HUD preset while OOT was foreground loaded MM
palettes through OOT's RM → null → crash. Scoping `Init` itself fixes every caller at once instead
of scoping window-by-window.

**Vendored (`COMBO_BUILD`-guarded):** `mm/2s2h/ShipInit.hpp` + `soh/soh/ShipInit.hpp` —
`ShipInit::Init` pins the owning game's RM via `Ship::OwnRMScope` (no-op when that game is
foreground or pre-registration). `OwnRMScope` lives in `CrossRMRegistry` (out-of-line) because
including `ResourceManagerScope.h`/`Context.h` from these widely-included headers drags in SDL,
whose `#define main SDL_main` breaks `GameState::main` users (e.g. `CustomLogoTitle.cpp`). Makes
the `*_MenuApplyCVarChange` export scopes redundant but they stay as documentation of the
export-boundary rule.

## RETIRED (2026-08-01): LUS PR #1121 cherry-pick — round interpolated texture tile sizes

Was a local cherry-pick of unmerged Kenix3/libultraship#1121 in
`libultraship/src/fast/interpreter.cpp` (interpolated float tile coords truncated to int made the
texture window alternate 32×32/32×31 across phases → animated water/lava flicker above 20 FPS).

Dropped in the 2026-08-01 `port-maintenance` switch: upstream landed it as **#1164**, and **#1135**
then refined `GetTileSizeFromCoordinates()` to treat an unset tile (`high <= low`) as zero size
rather than the phantom 1-texel our copy produced. We now carry upstream's version verbatim.

## Foreign animated-item segment binds must resolve against the owning RM (2026-08-03)

**Why:** `CfaBindSeg`'s `CW_ANIM_SEG_PATH` case put a raw FOREIGN resource path into the HOST's
`gSPSegment`. On both hosts that is a real function (`soh/soh/GbiWrap.cpp:34`,
`mm/src/code/stubs.c:149`) which probes the path at **record time** against
`Context::GetRawInstance()->GetResourceManager()` — the ACTIVE (host) RM. A foreign path misses,
`ResourceManager::LoadResource` returns nullptr, and both hosts' `ResourceMgr_LoadIfDListByName`
dereferenced it unchecked → access violation reading `mInitData`. Reproduced from a player crash
report: MM drawing a foreign OOT boss soul. Volvagia/Twinrova/Ganon are the only OOT items with a
PATH segment — plus any Ice Trap *disguised* as one, which is how the player actually hit it, so the
reach is far wider than the three items suggest.

The old comment on that line claimed the draw-time bracket covered this. It does not: the
`gSPComboRMPush` calls are GBI commands the interpreter consumes at **playback**
(`interpreter.cpp` `gfx_combo_rm_push_handler_custom`) and cannot affect a record-time CPU lookup —
and they are emitted after the bind anyway.

**`combo/menu/ComboForeignAnim.h` (combo-owned):** the emit is split into `CfaEmitSeg` so a
`Ship::ResourceManagerScope` wraps ONLY the two `gSPSegment` calls. `CfaBindSeg` now takes the owning
`game` explicitly — never the `sCfaCurrentGame` static, which unlike `sCfaInfo` is never cleared and
so can carry a stale value across items and across host/foreign direction changes — and returns
`bool`. The value left in the segment is still the raw path: playback resolves it via `ActiveResMgr()`
under the bracket, and for a Texture `LoadIfDListByName` correctly returns null so `target` is
untouched. Failure propagates to the caller's sentinel: an early bail when
`CrossRMRegistry::Get(game)` is null (before any Gfx is emitted), `CfaRestoreSegs` + `return 0`
mid-recipe, and `CfaDrawFlame` skipping only its flame DL (the flame is `flameSeg`'s sole consumer,
so the model still draws).

**The scope MUST stay narrow.** Both archives share one resource path namespace, so a HOST lookup
performed under the FOREIGN RM does not fail loudly — it silently returns the WRONG asset. soh's
helper also mangles paths on MQ state, i.e. host state applied to a foreign archive. Wrapping
`SkelAnime_Draw*`/`Matrix_*`/`OPEN_DISPS` would be worse than the original bug and buys nothing,
since playback is already covered by the bracket.

Two alternatives were rejected and should stay rejected:
- **Route the path** as `__OTR__@<game>:` like limb DLs — impossible. Only the DL FILEPATH handler
  parses the marker; `gfx_set_timg_handler_rdp` does not, so a routed string in a segment base breaks
  playback.
- **Bypass with `__gSPSegment`** (skipping the probe entirely) — smallest change and provably
  crash-free, but only because every `CW_ANIM_SEG_PATH` is a Texture *today*. `CfaValidateSegBind`
  only checks for an `__OTR__` prefix, so a future foreign *DisplayList* path would leave a heap
  string in the segment; `ComboIsUnresolvedSegmentTarget` won't reject a real heap address and the
  interpreter would run the string as GBI — the very class the guard above exists to close.

**`mm/2s2h/BenPort.cpp` + `soh/soh/ResourceManagerHelpers.cpp` (COMBO_BUILD-guarded):**
`ResourceMgr_LoadIfDListByName` returns null on a miss instead of dereferencing. Both callers already
test the return (`stubs.c:163`, `GbiWrap.cpp:48`), so the function's contract already admitted null —
it just failed to produce it. The sibling `ResourceMgr_LoadTexOrDListByName` has the identical
unguarded deref but is left alone: no `Cfa*` path reaches it via `gSPSegmentLoadRes` today. The other
~11 unchecked `GetResourceByName` sites in `BenPort.cpp` are a vendored 2Ship pattern whose callers
deref unconditionally, so guarding them would relocate the crash rather than fix it.

## MM transition-actor ids re-normalized on scene load (Woodfall door fix) (2026-08-05)

**Why:** MM's `play->transitionActors.list` aliases the cached LUS scene resource, so the negated
"already spawned" ids written by `Actor_SpawnTransitionActors` persist across scene loads whenever a
door's Destroy doesn't restore them. ComboShip's MM→OOT reload (Ctrl+R) cuts the frame loop
without `Play_Destroy`, skipping every live door's restore, so a door left alive at handoff
(e.g. Woodfall Temple room 0) never respawns for the rest of the process. `Door_Shutter`
variants with `room = -1` leak the same way even in stock 2ship. SoH has carried the equivalent fix for
years (`z_scene_otr.cpp`, `Scene_CommandTransitionActorList`); MM never received it.

**`mm/2s2h/z_scene_2SH.cpp` (COMBO_BUILD-guarded — preserve on future mm merges unless upstreamed):**
in `Scene_CommandTransiActorList`, `ABS()`-normalize every transition actor id before
`MapDisp_InitTransitionActorData`. Same code as SoH's fix, wrapped in `#ifdef COMBO_BUILD` with a
`// ComboShip:` comment.
