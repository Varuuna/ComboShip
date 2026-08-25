# ComboShip deviations — Menu & UI

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## UIWidgets empty-combobox UB + combo-rendered MM rando menu (2026-06-09)

**`mm/2s2h/BenGui/UIWidgets.hpp` (and `soh/soh/SohGui/UIWidgets.hpp`) — fix to a vendored upstream
bug:** every `UIWidgets::Combobox` template overload declared `const char* longest;` **uninitialized**,
then assigned it only inside the loop that scans the options for the widest entry. If the options
container is empty, `longest` stays garbage and the immediately-following
`CalcComboWidth(longest, ...)` → `ImGui::CalcTextSize` dereferences it → crash. Changed to
`const char* longest = "";` in all overloads, with a `// ComboShip:` comment explaining why. This is a
genuine upstream bug; OOT's `std::vector<std::string>` overload happened to already have the
initializer, but its three other overloads (map / `vector<const char*>` / fixed array) did not — those
were fixed too. WHY it surfaced: MM's rando "Seed" combobox reads `Rando::Spoiler::spoilerOptions`,
which is empty when the combo layer renders MM's always-available rando menu while MM is backgrounded.

**`mm/2s2h/BenPort.cpp` (`Combo_EnsureBenMenu()`):** belt-and-suspenders for the same symptom —
`spoilerOptions` is populated by `Rando::Init()` → `RefreshOptions()` at boot, but in the
combo/backgrounded render path that vector can be empty. `Combo_EnsureBenMenu()` (called by every MM
menu export — ExportMenu / InvokeCallback / EvalDisabled / DrawCustom) now calls
`Rando::Spoiler::RefreshOptions()` when `spoilerOptions.empty()`, so the menu has real options to draw.
`RefreshOptions` is idempotent (clears + repopulates) and only runs when empty. No new include needed
(`2s2h/Rando/Spoiler/Spoiler.h` was already included).

**Note:** other MM rando tabs (Logic / Items / etc.) may have their own backgrounded-live-state crashes;
out of scope here, handled as they surface.

## Menu code extraction to combo-owned headers (2026-06-10)

The ComboShip-written menu glue that lived inside the vendored trees was consolidated into
combo-owned, header-only units under `combo/menu/` (already on both games' include paths, so no
game CMake changes). The headers compile INTO each game DLL — only the SOURCE ownership moved —
so future upstream pulls conflict at most on small per-game glue blocks, never on the algorithms:

- **`combo/menu/ComboMenuExport.h`** — the two-pass count/reserve/fill CwMenu serializer that was
  copy-pasted in `soh/soh/SohGui/SohMenu.cpp` and `mm/2s2h/BenGui/BenMenu.cpp` (~260 lines each).
  Each game now keeps only its `WidgetTypeToCwKind` mapping + a Policy struct
  (`SohExportPolicy` / `BenExportPolicy`) + a one-line delegate. Pointer-stability invariant
  (reserve == fill) is now assert-checked and unit-tested
  (`libultraship/tests/combo_menu_export_tests.cpp`, mock-based, runs in `lus_tests`).
- **`combo/menu/ComboMenuDrawContent.h`** — the shared `Menu::DrawContent` ImGui body (~230 lines
  each) that mirrored the upstream `DrawElement` layout inside comboui's window. Each game keeps a
  Hooks shim (`SohDrawHooks` / `BenDrawHooks`) + a ~30-line wrapper. Upstream `DrawElement` is
  untouched. WHY a TU-glue header (`#error` guard, no own ImGui include): it must resolve each
  game's OWN UIWidgets/draw functions and per-module compile context.
- **`combo/menu/ComboMenuSharedContext.h`** — `ComboMenuContext::UseSharedImGuiContext()`, replacing
  2 static + 3 inline duplicated copies in `soh/soh/OTRGlobals.cpp` / `mm/2s2h/BenPort.cpp`.

Intentional micro-deviations from the pre-extraction bodies (do not "fix" back): empty tooltips are
no longer copied into owned-string storage (output still `""`); MM's redundant
`GetVectorIndexOf(sidebarOrder, …)` fallback clause was dropped (subsumed by the `std::find` over
`visibleSidebars`); OOT's widget label width now uses the clamped column count like MM (visible
only below 800px window width).

Net effect: vendored menu diff vs upstream shrank from ~1,630 to ~907 added lines across soh/ + mm/.

## hook_handlers.h re-added (combo-owned) (2026-06-11)

**`soh/soh/Enhancements/randomizer/hook_handlers.h`** — upstream has NO such header at our vendor
tip (functions in hook_handlers.cpp are static/self-registered). ComboShip re-added it to expose
`OOT_LookupForeign` (the per-slot foreign-item map lookup) to `Messages/MerchantMessages.cpp` and
the check tracker for cross-game item display names. The file contains only `#ifdef COMBO_BUILD`
declarations and MUST be preserved on future upstream merges — an upstream pull that "removes the
deleted file" silently breaks the foreign shop/tracker name builds.

## Dual-game title screen logos (2026-06-12)

**`soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c`:** the OOT title screen now shows BOTH games'
logos — OOT's shrunk and shifted left, MM's title logo (mask + Zelda logo + subtitle, ROM-extracted
textures resolved against MM's ResourceManager via the G_COMBO_RM_PUSH/POP interpreter bracket) on
the right. Both games' flame-effect mask grids (OOT 3x3 `gTitleEffectMask*`, MM 2x3
`gTitleScreenDisplayEffectMask*` + per-cell `gTitleScreenFlame0-3`) draw behind their logos, scaled
and repositioned with them; MM's oscillating effect colors are replicated in the combo header
(OOT's EnMag doesn't carry MM state). The early-out therefore sits BEFORE the original effect-grid
combiner setup, so the skipped vendored region spans from `gDPSetCycleType` through the MQ
subtitle. All draw code
is combo-owned (`combo/title/ComboTitleLogos.h`, included after the LOGO_* macros so it can reuse
them and the EnMag_Draw* helpers). The vendored logo block in `EnMag_DrawInner` is left BYTE-INTACT
for upstream merges; a `#ifdef COMBO_BUILD`-guarded early-out (`if (ComboTitle_DrawLogos(...))
goto ...;` + a guarded label after the block) skips it at runtime, so non-combo builds compile the
file unchanged. On future merges, keep the include + the goto wrapper and re-check
that the duplicated OOT-logo draw sequence in ComboTitleLogos.h still matches upstream's (combiner
setup, texture names, MQ subtitle branch).

**`soh/CMakeLists.txt`:** added `${CMAKE_SOURCE_DIR}/combo/title` to the include dirs (next to the
existing `combo/menu` entry).

## File-select seed-hash icons for combo seeds (issue #32, 2026-06-23)

**Why:** stock SoH draws five item icons on the file-select as a visual seed hash so players can
match seeds/spoiler logs. The combo build showed five Deku Nuts (or an identical set every seed):
the combo generator owns generation and never runs OOT's `Playthrough_Init`, which is where vanilla
calls `GenerateHash()` to fill `Rando::Context::hashIconIndexes` — so the array stayed all-zero.
Scope is OOT only: combo boots into OOT's file-select and enters MM via transition, so MM's
file-select is never shown (MM's `finalSeed = 0` gap in `MM_InitRandoSaveFile` is a known follow-up).

**Change (vendored `soh`, minimal):** new export `SOH_SetComboSeedHash(uint32_t)` in
`OTRGlobals.cpp` (just after `SOH_ApplyRandoPlacements`) calls `ctx->SetHash(...)` + `GenerateHash()`
— mirrors stock `playthrough.cpp:64-73`. Added `#include ".../3drando/spoiler_log.hpp"` for the
declaration. Persistence/render are stock-wired (`SaveManager` copies into `fileMetaInfo.seedHash`;
`z_file_choose.c` renders it).

**Change (combo-owned, `ComboShip.cpp`):** `RunComboFill` computes a settings-aware display hash
`ComboHash(inputSeed + sohDump + mmDump)` and passes it to the new export *after*
`SOH_ApplyRandoPlacements` (which `ItemReset`s). Folding both static dumps in makes the icons
identify seed **and** settings: each dump is built from its game's live CVars and carries only the
settings-scoped pool, so OOT *and* MM shuffle/starting-item settings both vary the icons. Accepted
limitation (symmetric): a logic-only setting that doesn't change the shuffled pool (e.g. tricks)
won't change the dump, so won't change the icons.

**On future merges:** if upstream restructures `GenerateHash`/`SetHash` or the file-select hash
render, re-point the new export; the combo-side hash derivation is independent.

## File-select quest menu locked to Randomizer (2026-06-27)

**Why:** the combo build drives a randomizer through OOT's normal save flow; showing the Normal /
Master Quest / Boss Rush quest options on file-select is confusing since they're never the intent.

**Change (vendored `soh`, one COMBO_BUILD-guarded block):** in
`soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c`, the `MIN_QUEST`/`MAX_QUEST` macros
(which seed `questType` at init and bound the L/R carousel wrap) are redefined to `QUEST_RANDOMIZER`
under `COMBO_BUILD`. The carousel therefore starts on Randomizer and every L/R wrap lands back on it,
so the other quests are unreachable — no draw/branch code was deleted (the non-combo build is
byte-intact via the `#else`).

**On future merges:** if upstream changes the quest enum or the carousel wrap logic, re-check that
the locked `MIN_QUEST == MAX_QUEST == QUEST_RANDOMIZER` still resolves to Randomizer on every L/R
path (incl. the Master-Quest-absent skip loop).

## Live-apply settings changed from the combo menu (2026-06-28)

**Why:** the games' native UIWidgets call `ShipInit::Init(cvar)` after a widget change to re-run the
enhancement registered for that CVar; comboui only set the CVar, so combo-menu changes didn't apply
until the next `ShipInit::InitAll` (game boot / new save). New exports `SOH_MenuApplyCVarChange` /
`MM_MenuApplyCVarChange` (OTRGlobals.cpp / BenPort.cpp) run `ShipInit::Init(cvar)`; the comboui menu
model + `ComboWidgetRender` call them after each change. Fixes #27. **On future merges:** if the
native UIWidgets stop using `ShipInit::Init`-on-change, revisit.

## Shared-settings consolidation + dev-tool window fixes (issue #22, 2026-06-28)

**Why:** three related issues in the shared menu/window subsystem. (a) #22 — MM ignored the Shared
tab's Graphics/Audio/Controls and the per-game tabs redundantly exposed them. (b) Opening MM's "Save
Editor" showed OOT's: the shared `Gui::AddGuiWindow` keys by display name and rejects duplicates, so
MM's identically-named dev windows were dropped (OOT boots first). (c) Dev-tool windows only offered a
"popout" button, never rendering inline.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `mm/2s2h/BenGui/BenGui.cpp` — the 9 MM dev/debug windows whose names collide with OOT's (Save Editor,
  Actor/Collision/Message Viewer, Audio Editor, Mod Menu, Hook Debugger, Input Viewer (+Settings)) now
  register with `COMBO_MM_TRACKER_SUFFIX` (`"##MM"`), same mechanism as the trackers — map-key/ID only,
  visible title unchanged, empty in standalone. (Cosmetic Editor / Time Splits Window / DL Viewer differ
  from OOT's strings already, so they don't collide.)
- `mm/2s2h/BenGui/BenMenu.cpp` — matching `WindowName(...)` popout refs carry the same suffix
  (`COMBO_MM_WINDOW_SUFFIX`). New `#ifdef COMBO_BUILD` inline `WIDGET_CUSTOM` entries render each dev
  window's `DrawElement()` inline (skipped when popped out, so no double-draw); live-world viewers
  (Actor/Collision/Message/DL/Event Log) gate on `Combo_MmIsForeground()` so they don't read MM's
  dormant/swapped play state when the MM tab is opened while OOT is foreground.
- `soh/soh/SohGui/SohMenuDevTools.cpp` + `OTRGlobals.cpp` — the same inline `WIDGET_CUSTOM` treatment for
  OOT's dev tools, gating live viewers on `Combo_OotIsForeground()`. Both `Combo_*IsForeground` helpers
  resolve comboui's `ComboUI_GetForegroundGame` once.
- `mm/2s2h/BenPort.cpp` — exports `MM_ApplyAudioVolume(seqPlayer, vol)` (→ `AudioSeq_SetPortVolumeScale`,
  the apply path MM uses instead of ShipInit) and `MM_ReloadControls()` (reloads MM's ControlDeck ports
  from the shared `gSettings.Controllers.*` CVars); `Combo_MmIsForeground()` queries comboui's
  foreground game. All inside the existing `COMBO_BUILD` export block.

**Combo-owned:**
- `combo/gui/ComboAudioBridge.{h,cpp}` — one-way mirror of OOT's canonical `gSettings.Volume.*` (int
  0-100) into MM's `gSettings.Audio.*` (float 0-1). `MirrorIfVolumeCVar` fires from the
  `ComboWidgetRender` apply-step; `SyncAllToMM` runs on MM entry. Graphics needs no bridge (one shared
  window) and Controls' data is already shared CVars.
- `combo/gui/ComboForeground.h` + `ComboTrackerVisibility.cpp` — cache the foreground game from the
  existing `ComboUI_OnForegroundGame` callback; expose `ComboUI::GetForegroundGame()` (C++) and
  `ComboUI_GetForegroundGame()` (C ABI, for MM's gating). On MM entry the callback also runs
  `ComboAudio::SyncAllToMM()` + `MM_ReloadControls` so changes made while MM was dormant take effect.
- `combo/gui/ComboMenu.cpp` — the per-game tab sidebar filter now also hides MM's
  Graphics/Audio/Controls/General (they live only in Shared, on shared CVars). **On future merges:** if
  MM's audio CVar names/scale change, update `kMap` in `ComboAudioBridge.cpp` (note: the OOT `Volume.*`
  defaults — Master 40, rest 100 — are duplicated in `kMap`'s `defaultPct` and must match
  SohMenuSettings.cpp, else an untouched slider reads as 0 and silences MM).

## Combo-side Advanced Resolution editor (issue #26 #3, 2026-06-29)

**Why:** SoH's Advanced Resolution editor (`soh/soh/SohGui/ResolutionEditor.cpp`) is built from
machinery the flat C-ABI menu snapshot can't carry — dynamic `WIDGET_TEXT` names set per-frame by a
`PreFunc`, an aspect-ratio combobox bound to a C++ static via `ValuePointer` (no CVar), two
`WIDGET_CUSTOM` draw lambdas, and a per-page `UpdateResolutionVars` MenuUpdate func. In the overlay
those widgets render broken (empty `{} x {}` readouts, an empty-CVar combobox, placeholder customs,
dead aspect/enable controls). User chose a combo-side reimplementation (works regardless of which
game is foreground) over a game-side custom-draw dependency.

**Combo-owned (no vendored edits):**
- `combo/gui/ComboResolutionEditor.{h,cpp}` — `TryRenderResolutionWidget(w)`, called at the top of
  `ComboWidgetRender::RenderWidget`. Intercepts the resolution widgets **by name/cvar** and renders
  combo controls over the effective `gSettings.AdvancedResolution.*` CVars (libultraship's shared
  `Fast3dGui::ApplyResolutionChanges` reads them live each frame). Stateless per-frame: the CVars are
  the source of truth. Live dims read from `Fast::Interpreter` via `Fast3dWindow::GetInterpreterWeak`.
  Owns the Enable checkbox + calls `SaveConsoleVariablesNextFrame` after writes so changes persist.
  Scope = core controls; niche extras (horizontal-res-field alt, NeverExceedBounds/ExceedBoundsBy,
  IgnoreAspectCorrection) intentionally omitted.

**On future merges (coupling — re-verify):** this **shadows specific SoH widget names** —
`"Aspect Ratio"` (empty-cvar combo), `"AspectRatioCustom"`, `"MoreResolutionSettings"`, the
`"Viewport dimensions"`/`"Internal resolution"` readout prefixes, the `"...is overriding these
settings"` / `"Click to disable N64 mode"` advisories — and **transcribes SoH's preset tables**
(aspect labels + X/Y, pixel-count labels + values, clamps). If SoH renames those widgets or changes
the tables, re-sync `ComboResolutionEditor.cpp` (else the widgets silently fall back to the broken
generic render). CVar prefix `gSettings.AdvancedResolution` comes from `CMake/lus-cvars.cmake`.

## Inline controller bindings on Shared → Controls (issue #26 #1, 2026-06-29)

**Why:** SoH's Controls page is popout-only (no inline bindings widget), so the combo overlay showed
just a "Popout Bindings Window" button — the user had to detach a floating window to rebind.

**Vendored (additive, `COMBO_BUILD`-guarded):**
- `soh/soh/SohGui/SohMenuSettings.cpp` — a new "Controller Bindings Inline" `WIDGET_CUSTOM` in the
  Controls section draws the "Configure Controller" (`SohInputEditorWindow`) inline, reusing the
  issue #22 inline-window mechanism (get the registered `GuiWindow`; skip when `IsVisible()`/popped
  out to avoid double-draw; `Update()` then `DrawElement()`). No foreground gate — the input editor
  only touches the shared `ControlDeck`, not OOT play state.

One inline editor (OOT's) covers both games: controls are shared `gSettings.Controllers.*` CVars and
`MM_ReloadControls` reloads MM from them, and MM's Controls sidebar is hidden in the overlay (above),
so no MM-side change is needed. **On future merges:** if SoH renames the "Configure Controller"
window, update the name string here.

## Link's Voice Pitch Multiplier CVar collision (2026-08-04)

**Why:** SoH stores its pitch slider as a float **leaf** `gAudioEditor.LinkVoiceFreqMultiplier`
while 2Ship stores **children** of that exact path (`.Enable`/`.Scale`). Upstream never meet; in the
shared ComboShip CVar table + single config both coexist, and `Config::Save`'s `unflatten()` throws
(json can't make one key both scalar and object) — an uncaught crash that also truncated the config
file (the `ofstream` opened before `unflatten`).

**Vendored (`COMBO_BUILD`-guarded):**
- `soh/soh/cvar_prefixes.h` — `CVAR_LINK_VOICE_FREQ_MULTIPLIER` resolves to the `.Scale` child in
  combo builds (upstream leaf otherwise); used at the 3 SoH sites (widget, reset button, z_actor.c
  read). Side effect: both games share one pitch value; MM still gates behind `.Enable`.
- `soh/soh/OTRGlobals.cpp` (`Combo_FinishInit`, after `RunVersionUpdates()`) — one-shot migration of
  the old leaf to `.Scale` (covers old combo configs, the legacy `gLinkVoiceFreqMultiplier` updater
  target, and launcher-imported standalone SoH configs, which all still produce the leaf). Gated on
  table presence (not float type — a legacy int leaf must still be cleared) and followed by
  `CVarSave()` so a mid-session `ConsoleVariable::Load` can't resurrect the leaf from disk.
- `libultraship/src/ship/config/Config.{h,cpp}` — all four `unflatten()` sites (`Save`, `Nested`,
  `SetBlock`, `EraseBlock`) route through a `TryUnflatten` helper that logs instead of throwing
  (the exception would unwind across the game-DLL boundary); `Save()` unflattens **before**
  opening/truncating the file; `Nested()` falls back to the last-good nested state on failure.

**On future merges:** if upstream SoH renames the CVar or grows its own `.Enable`, drop the macro
seam and re-check the migration. Audited 2026-08-04: this was the only cross-game leaf-vs-subtree
CVar pair (other candidates were Color-CVar bases, never stored as leaves).

Follow-up (same day): the checkbox still didn't enable the slider — MM's `disabledMap` per-frame
refresh lives only in `Menu::DrawElement`, which never runs under comboui, so popout windows
(Audio Editor, Mod Menu) that draw via `MenuDrawItem` read a frozen disable flag.
`mm/2s2h/BenGui/Menu.cpp` (`Menu::MenuDrawItem`, `COMBO_BUILD`-guarded) now refreshes the map once
per ImGui frame. SoH needs no parity fix: its popout-drawn widgets have no disable-map preFuncs.

## Config::SetBlock drops writes when intermediate keys are missing (2026-08-10)

**Why:** `SetBlock`'s dot-walk only descended into keys that already existed — a missing
intermediate (e.g. `CVars.gRando` on a config that never saved a gRando key) made the whole write a
silent no-op. Surfaced as `MM_RestoreRandoSettings` failing to restore an empty
`gRando.StartingItems: []` kit: `SetStartingItemsInConfig` never landed, exact-seed repro fell back
to the default kit. (Related pre-existing quirk, handled by callers: nlohmann `flatten()` turns an
empty array into null; `GetStartingItemsFromConfig` treats null as empty.)

**Vendored:** `libultraship/src/ship/config/Config.cpp` (`SetBlock`) — create missing intermediate
objects and descend; a non-object in the path keeps the old silent no-op (`ComboShip:` comment at
site).

## Mod ordering never survived a restart (2026-08-24)

**Why:** two independent defects, both combo-only.

(a) Both mod menus stored the user's mod order in the SAME CVar, `gSettings.EnabledMods` — soh via
`CVAR_SETTING("EnabledMods")`, MM via a hardcoded literal, and `CVAR_PREFIX_SETTING` is `gSettings`
in both games. In combo MM reuses OOT's Context and skips `InitConsoleVariables()`, so
ConsoleVariables is shared. `UpdateModFiles(init=true)` erases every listed name it can't find in
*its own* mods dir and rewrites the key; OOT scans `./mods/soh`, MM scans `./mods/2ship`, so each
wiped the other's names. OOT boots first and MM writes last, so after every launch the key held only
MM's mods (or `""`, since `./mods/2ship` is auto-created even when empty) and the order was rebuilt
path-sorted on the next boot. Same shared key also crashed OOT's Mod Menu: Edit -> Cancel re-reads
the CVar without pruning (`init == false`), then `DrawMods` hit `filePaths.at()` on an MM name.

(b) MM's `BenModalWindow` registered as `"Modal Window"`, which OOT already owns, so
`Gui::AddGuiWindow` rejected it and it was never drawn. MM's Mod Menu "Clear List" and "Apply &
Close" queue into a `BenModals` global vector that only that window drains — both buttons did nothing
at all, so an MM order could never be saved.

**Vendored (additive, `COMBO_BUILD`-guarded unless noted):**
- `mm/2s2h/Enhancements/ModMenu/ModMenu.cpp` — MM's key becomes `gSettings.EnabledModsMM`. A SIBLING
  leaf, never a child: `gSettings.EnabledMods.MM` would put a leaf and a subtree at one path, the
  failure behind `CVAR_LINK_VOICE_FREQ_MULTIPLIER`. Since `Config::TryUnflatten` now logs instead of
  throwing, that would silently drop every config write rather than crash.
- `mm/2s2h/BenGui/BenGui.cpp` — `"Modal Window" COMBO_MM_TRACKER_SUFFIX`. Nothing resolves MM's modal
  by name (MM uses the `BenGui::mModalWindow` static), and the visibility CVars already differed
  (`gWindows.ModalWindow` vs OOT's `gOpenWindows.ModalWindow`).
- `mm/2s2h/BenGui/Menu.cpp` — seed `menuThemeIndex` in the ctor (mirroring
  `soh/soh/SohGui/Menu.cpp`) **and** clamp it in `GetMenuThemeColor()`. Required: registering the
  modal makes it draw every frame from the Gui loop, and `THEME_COLOR` resolves to a member that only
  `UpdateElement()` sets — which under comboui runs only once MM's tab is drawn — so `ColorValues.at()`
  would throw out of 2ship.dll across the DLL boundary. The ctor seed alone is not enough:
  `MM_MenuDrawCustom` calls `Update()`, which re-reads the CVar unclamped, so the clamp belongs at the
  `GetMenuThemeColor()` funnel where every `THEME_COLOR` read passes. Also fixes a live crash for
  anyone with `gWindows.InputViewerSettings=1` (`InputViewerSettingsWindow::DrawElement` uses
  `THEME_COLOR`; `InputViewer::DrawElement` does not).
- `mm/2s2h/BenGui/BenModals.cpp` — `ImGuiPopupFlags_NoOpenOverExistingPopup` plus a `PushID("MM")`
  scope. Neither modal window calls `Begin()`, so both open at popup level 0 and hash their popup ids
  against the same window. Both games use the exact titles "Clear List" and "Apply & Close", so
  without the `PushID` MM would draw its message and buttons into OOT's popup with colliding button
  ids — one click could run the other game's callback. The flag is the level tie-break: it is
  one-way (OOT's `OpenPopup` stays unflagged), so OOT wins and MM retries next frame.
- `soh/soh/Enhancements/mod_menu.cpp` + `mm/.../ModMenu.cpp` — a plain "Apply" that writes the CVar
  and leaves edit mode without closing. `SetEnabledModsCVarValue` already calls
  `SaveConsoleVariablesNextFrame()`, so no explicit `Save()`; "Apply & Close" needs its own only
  because it closes before the next frame. Upstream's "Apply & Close" is unchanged, and in combo it
  takes both games down.
- Both mod menus, **not** guarded — a `filePaths.find()` skip at the top of `DrawMods`' loop. A
  genuine upstream bug (a file deleted mid-session throws out of the inline-menu draw path, and
  neither `SOH_MenuDrawCustom` nor `MM_MenuDrawCustom` has a try/catch); correct in standalone too,
  and an `#ifdef` around one `continue` would be worse than the line. The doc entry is the only
  record, since the line carries no `ComboShip:` marker — re-check it after upstream merges.

**Residuals, accepted:** OOT's Presets "Settings" block bulk-clears the whole `gSettings` subtree, so
an OOT settings-preset apply clears both mod lists — benign, they rebuild from disk. The
`GetMenuThemeColor()` clamp covers every `THEME_COLOR` read, but a dozen MM sites read
`gSettings.Menu.Theme` directly with `CVarGetInteger` and stay unclamped; soh's `Menu` is unclamped
throughout, unchanged here because OOT's modal was always registered. If a game's mods dir doesn't
exist, `UpdateModFiles` skips its whole body and leaves the list unpruned and unwritten. When a mod
file vanishes mid-session the skipped row leaves the up/down arrows working on raw vector indices, so
a swap with a skipped neighbour changes the list without changing what is drawn (and a shift-range
selection can pick up the invisible name) — cosmetic, in an already-degraded state.

**On future merges:** the theme seeding relies on soh's and MM's `UIWidgets::Colors` enums and
`ColorValues` maps being identical (`soh/soh/SohGui/UIWidgetOptions.hpp` vs
`mm/2s2h/BenGui/UIWidgets.hpp`) because `gSettings.Menu.Theme` is shared. If a merge diverges them,
the shared key becomes unsafe. Also re-check the `Modal Window` suffix and the "Apply" button if
upstream reworks the mod menu.

## Combo-owned overlay timers (issue #173, 2026-08-24)

**Why:** OOT's "Additional Timers" and MM's "Display Overlay" each showed only their own game's play
time, so a combo run never had a number covering the whole run. MM also had a live bug: its play time
is wall clock between flushes (`filePlaytime += now - lastTimeLog`) and `lastTimeLog` was never
advanced when combo swapped away, so hours spent in OOT were folded into MM's save at its next flush.

**Combo-owned:** `combo/gui/ComboTimersWindow.{h,cpp}` — window `Timers##Combo`, CVars `gCombo.Timers.*`,
settings under ComboShip Settings → Timers. The total is `OOT playTimer/2 + pauseTimer/3` (deciseconds)
plus MM's `filePlaytime/100`; both already live in the `.combosav`, and exactly one advances at a time.
Time of day / Navi / conditional draw only while OOT is foreground. `Combo_SetForegroundGame` pauses
and resumes MM's accumulator across every swap. The total tints green once both games are beaten
(`ComboUI_SetComboComplete`, pushed from the existing `combo.completion` flags — no new save key).

**No real-time (RTA) row, deliberately.** MM's `GetUnixTimestamp` (`mm/2s2h/BenPort.cpp`) assigns
`millis.count()` to a `long`, which is 32-bit on Windows, so every MM timestamp is truncated modulo
2^32 — `fileCreatedAt` lands around 8.6e8 where soh's untruncated ms is around 1.79e12. MM's own
timestamps stay self-consistent (differences survive the truncation until it wraps, every ~49.7 days),
so this is invisible inside 2Ship, but it makes MM and OOT timestamps incomparable. A first attempt at
an RTA row took `min(OOT firstInput, MM fileCreatedAt)` and displayed 496307 hours. **Never compare a
soh timestamp with an MM one** without fixing the truncation first.

**Vendored (all `COMBO_BUILD`-guarded):**
- `soh/soh/OTRGlobals.cpp` — `SOH_GetPlaytimeDeciseconds`, `SOH_GetOverlayTimers` (classification
  stays in soh so comboui hardcodes no vanilla enum values).
- `mm/2s2h/BenPort.cpp` — `MM_GetPlaytimeMs`, `MM_ComboPausePlaytime`,
  `MM_ComboResumePlaytime` + a running latch. The advance is implemented locally rather than calling
  `SavingEnhancements_AdvancePlaytime` so `SavingEnhancements.cpp` stays untouched. The
  `lastTimeLog != 0` guard matters because `z_sram_NES.c` zeroes it on a new file — treating 0 as a
  timestamp would add a whole Unix epoch (~57 years).
- `soh/soh/SohGui/SohMenuEnhancements.cpp` and `mm/2s2h/BenGui/BenMenu.cpp` — the native timer menu
  entries are `#ifndef COMBO_BUILD`. A sidebar allow-list is not enough: `ComboMenu::DrawSearchResults`
  walks both games' menu models unfiltered, so a search for "timer" would re-open the native window.

**Do not remove** the `TimeDisplayWindow` registration at `soh/soh/SohGui/SohGui.cpp:203` — its
`InitElement` is what loads the digit and icon textures the combo overlay draws with. Only its draw
is suppressed.

**Residuals:** the pause only flushes into memory — nothing persists MM's save on the way out — so
time played in MM without an owl/auto save is lost when the slot's MM blob is re-read (reset or
owl-save quit, `g_MmSaveInMemorySlot = -1`), and the total steps back to MM's last saved value. Same
as vanilla 2ship.

**Separate bug found while playtesting this, NOT fixed here:** MM's play time reads 0 after an owl
save because `SaveManager_LoadSaveFile` (`mm/2s2h/SaveManager/SaveManager.cpp`) reads only the
`newCycleSave` key and never `owlSave`. ComboShip's resume shortcut (`mm/src/code/title_setup.c`,
`COMBO_BUILD` block) goes through that function instead of vanilla's `Sram_OpenSave`, which picks the
owl page when `isOwlSave` is set and then deletes it on continue (`VB_DELETE_OWL_SAVE`). So an owl
save's whole state — not just play time — is discarded on a combo resume. Note that a new-cycle save
deliberately preserves `owlSave`, so a read-priority fix alone would let a stale owl save shadow a
newer cycle save; the delete-on-continue half is required too.

