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

## Cherry-pick: LUS PR #1121 — round interpolated texture tile sizes (2026-07-14)

`libultraship/src/fast/interpreter.cpp`: cherry-picked unmerged upstream PR
Kenix3/libultraship#1121 (commit `c66cebe2f`, fixes #1119 / Shipwright#6666). Interpolated
float tile coords truncated to int made the rendered texture window alternate 32×32/32×31
across interpolation phases → animated water/lava flicker above 20 FPS. New
`GetTileSizeFromCoordinates()` rounds via `lroundf`. Drop this local copy once the PR lands
upstream and the pin passes it.
