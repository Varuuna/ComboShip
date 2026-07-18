# ComboShip deviations — Trackers & notifications

Preserved deviations — keep across upstream merges. See [../UPSTREAM_MERGES.md](../UPSTREAM_MERGES.md) for the merge mechanism.

## Cross-game Check/Item tracker windows (collision fix + active-game gating) (2026-06-21)

**Why:** Both soh and mm register tracker windows named identically — `"Check Tracker"`,
`"Check Tracker Settings"`, `"Item Tracker"`, `"Item Tracker Settings"`. There is ONE shared
libultraship `Gui` across both DLLs, and `Gui::AddGuiWindow` keys by display name and **rejects
duplicates**. OOT boots first, so all four of MM's tracker windows were silently dropped and could
never show. Separately, the shared Gui draws *every* registered window each frame, so the
backgrounded game's tracker `DrawElement` would run against that DLL's stale per-module ImGui
context. The user wants the popout trackers to follow the active game (OOT↔MM).

**Vendored (additive, `COMBO_BUILD`-guarded, minimal):**
- `mm/2s2h/BenGui/BenGui.cpp` — MM's 4 tracker windows are registered with a `COMBO_MM_TRACKER_SUFFIX`
  (`"##MM"`) appended to their names. ImGui renders only the text before `##`, so the visible title
  stays `"Check Tracker"`/`"Item Tracker"`, but the Gui map key (and ImGui ID) is unique → both games
  coexist. Non-combo builds get an empty suffix (unchanged).
- `mm/2s2h/Rando/Menu.cpp` — the two "Popout Settings" buttons resolve their window **by name**
  (`BenGui/Menu.cpp` → `GetGuiWindow(windowName)`), so their `WindowName(...)` carries the same
  `COMBO_MM_TRACKER_SUFFIX`.
- `soh/.../randomizer_item_tracker.cpp`, `soh/.../randomizer_check_tracker.cpp`,
  `mm/.../ItemTracker/ItemTracker.cpp`, `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — each tracker's
  `Draw()` calls `ComboMenuContext::UseSharedImGuiContext()` (from `combo/menu/ComboMenuSharedContext.h`)
  before any ImGui call, the same pattern already used by `SOH_DrawSettings`. This is the leading fix
  for OOT trackers not appearing even foreground; if a build shows they still don't, the next step is
  per-stage logging (registration → visibility → draw-reached → context).

**Combo-owned:**
- `combo/gui/ComboTrackerVisibility.cpp` (in comboui) — `ComboUI_OnForegroundGame(int game)` hides the
  background game's 4 tracker windows (remembering each one's intent) and restores the foreground
  game's; `ComboUI_RestoreTrackerIntent()` puts both games' intent back before shutdown config save so
  a backgrounded-at-exit game doesn't persist its tracker as "off". Uses CVar + `GuiWindow::Show/Hide`
  (no ImGui calls → no context concern in comboui).
- `combo/ComboShip.cpp` — resolves the two exports and calls `ComboUI_OnForegroundGame` after the eager
  MM boot (OOT foreground) and at each portal transition (next to `ComboAnchor::SetActiveGame`);
  `ComboUI_RestoreTrackerIntent` runs at the start of teardown.

The active-game gating is also what keeps the two games' identically-titled inner `ImGui::Begin("Item
Tracker")` / `ImGui::Begin("Check Tracker")` calls from ever running in the same frame.

## Toast/notification window: collision fix + active-game gating (issue #28, 2026-06-22)

**Why:** Same class of bug as the trackers above. Both soh and mm already have a near-identical
notification (toast) system, and both register their window under the identical name
`"Notifications Window"`. The single shared libultraship `Gui::AddGuiWindow` **rejects duplicates**, and
OOT boots first, so MM's notification window was silently dropped — MM toasts (item pickups, the
ComboShip "Sent to Hyrule" cross-world send, Anchor events) never appeared in the combined build, even
though the emitters exist and work in standalone MM. The user wants the active game's toasts to show.

**Vendored (one line, `COMBO_BUILD`-guarded):**
- `mm/2s2h/BenGui/BenGui.cpp` — MM's notification window registration appends `COMBO_MM_TRACKER_SUFFIX`
  (`"##MM"`) to its name, exactly like the tracker windows. Map-key only; the name isn't displayed
  (`Notification::Window::Draw` titles its ImGui windows `notification#<id>`). No change to MM's
  notification behavior. Non-combo builds get the empty suffix (unchanged).

**Combo-owned:**
- `combo/gui/ComboTrackerVisibility.cpp` — `ComboUI_OnForegroundGame` now also gates the notification
  window. Unlike trackers, `Notification::Window` overrides `Draw()` and ignores `IsVisible()` (and
  `GuiElement::Update()` runs `UpdateElement()` unconditionally), so `Show()/Hide()` does NOT suppress
  it. Instead the background game's window is `RemoveGuiWindow`'d (dropped from the draw loop entirely,
  stopping both `Draw` and `UpdateElement`) and the foreground game's is re-added — the SAME
  already-initialized object (a `weak_ptr` handle; the window stays owned by its game DLL's
  `mNotificationWindow` member, so no cross-DLL ownership / shutdown-dtor hazard, and no fresh window
  is created → no 0xCD re-registration crash). No CVar/intent bookkeeping (notifications have no user
  open/close toggle), so `ComboUI_RestoreTrackerIntent` is unchanged.

The "Sent to Hyrule" toast itself was already implemented (`mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp`,
`Rando_SendForeignCheck`); this change only makes MM's window survive registration so it can show.

## Shared Item Tracker: master panel + hold-to-swap dormant peek (2026-07-04)

**Why:** with both games in one process the item tracker should be controllable from one place and
able to show the OTHER (dormant) game's inventory. A dormant game has no gPlayState/input, so its
tracker draw needs save-context-only gating and RM-pinned texture loads; MM's save must be
headless-loaded before its first visit so a peek shows real items instead of boot defaults.

**Combo-owned (comboui):** `combo/gui/ComboTrackerBridge.{h,cpp}` — canonical `gCombo.Tracker.*`
appearance (icon size, opacity, window type, draggable) mirrored into both games' CVars, seeded
from OOT on first run. `ComboTrackerCommon.h` — per-game window/CVar table + `SetTracker`.
`ComboTrackerSwap.{h,cpp}` — per-frame reconcile of `gCombo.Tracker.ShownGame` (-1 follow / 0 OOT /
1 MM), the click-hold gesture (drag cancels; `HoldMomentary` = peek), true-intent stash CVars for
crash recovery. Shared > Item Tracker panel in `ComboMenu.cpp`.

**Port code:** `soh/soh/OTRGlobals.cpp` — `SOH_SetOnLoadSaveCallback` (launcher mirrors the active
OOT slot for the MM-side load) + `Combo_OotIsForeground` helper; `mm/2s2h/BenPort.cpp` —
`MM_LoadSaveForCombo` (headless SaveManager load, same path as the resume shortcut); launcher
wiring in `combo/ComboShip.cpp`.

**Vendored (`COMBO_BUILD`-guarded):** `soh/.../randomizer_item_tracker.cpp` — dormant draw: OOT RM
scope, `gComboTrackerPeekSaveLoaded` signal, stale pause/combo-button override.
`soh/.../GameInteractor.cpp` — `IsSaveLoaded` judges by save context alone while the peek flag is
set. `mm/.../ItemTracker/ItemTracker.cpp` — MM RM scope, visibility-gate skip while dormant,
shared Draggable toggle, and a main-viewport pin (a window given its own viewport is force-rendered
opaque — showed as a black tracker background during the hold gesture).

## Shared Check Tracker: hold-to-swap peek + shared window type (2026-07-04)

**Why:** the same dormant-peek feature for the check tracker. Extra wrinkles vs the item tracker:
MM's tracker hard-requires `gPlayState`/`IS_RANDO`, its in-logic refresh throttles on the game
frame counter (frozen while dormant), a headless save load never fires `OnFileLoad` (scene-check
map stays empty), and the current-scene filter derefs `gPlayState`. MM also has no native
window-type CVar, so the shared Floating/Window switch is read directly from
`gCombo.CheckTracker.WindowType` in its seam (OOT's is mirrored by the bridge as usual).

**Combo-owned (comboui):** `ComboTrackerSwap.{h,cpp}` generalized to per-tracker instances (item +
check) with independent ShownGame/HoldMomentary/true-intent CVars; at most one tracker arms per
press (topmost wins on overlap); the logic window key gained a leading space (" Combo Tracker
Swap") so it sorts before BOTH tracker windows in the name-ordered Gui map. `ComboTrackerBridge`
mirrors the check window type into OOT. Shared > Check Tracker panel in `ComboMenu.cpp`
(visibility, window type, shown game, peek mode — colors/filters stay per-game).

**Vendored (`COMBO_BUILD`-guarded):** `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — MM RM scope +
visibility-gate skip while dormant; save-gate bypass with lazy `initializeSceneChecks`; ImGui-frame
throttle branch in `RefreshChecksInLogic` so availability refreshes during the peek; `gPlayState`
guard on the current-scene filter; main-viewport pin + window-type flags at `Begin` (original call
kept under `#else`). `soh/.../randomizer_check_tracker.cpp` — OOT RM scope +
`gComboTrackerPeekSaveLoaded` around `DrawElement`; display-gate skip (stale pause/combo-button
state while dormant).

## Both-games tracker model (2026-07-14)

**Why:** Trackers were foreground-follow with a per-tracker game-swap; the user wants both games'
trackers visible together, one master enable, and a single customization home in the Combo menu.
The combo layer carries the model (`ComboTrackerSwap` rewritten to a per-frame reconcile of derived
per-game CVars: master enable + per-kind HideBackground + hold-to-peek; `ComboTrackerVisibility`
now follows only the settings popouts; both Shared tracker panels inline the games' own settings
sidebars with a divider). The old ShownGame/TrueIntent/HoldMomentary CVars are migrated + cleared
on boot.

**Vendored (`COMBO_BUILD`-guarded, additive):**
- `mm/2s2h/Enhancements/Trackers/ItemTracker/ItemTracker.cpp` + `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp`
  — MM's trackers `Begin()` distinct ImGui identities (`"Item Tracker##MM"`, split-group `##<n>MM`,
  `"Check Tracker##MM"`) so both games' windows can exist simultaneously with their own rects
  (previously both games drew into the SAME ImGui window; only the Gui-map key carried `##MM`).
- `mm/2s2h/Rando/Menu.cpp` — "Item/Check Tracker Settings Inline" `WIDGET_CUSTOM` widgets (the
  SoH issue-#22 inline-window mechanism) so the Shared panels can embed MM's settings; skipped
  while popped out.
- `mm/2s2h/Enhancements/Trackers/ItemTracker/ItemTrackerSettings.cpp` +
  `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp` — the Enable/Disable window buttons inside the
  settings content are hidden in combo builds (`gWindows.ItemTracker`/`.CheckTracker` are derived
  from the combo master toggle every frame; the buttons would fight it).

**On future merges:** if upstream renames the tracker ImGui windows or the settings windows,
update `combo/gui/ComboTrackerCommon.h` (`kKinds[].imguiWin`) and the inline-widget window names.
