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

**Dormant "only while paused" (#127, 2026-08-12):** the in-tracker dormant bypasses stay (stale
pause state); instead `Reconcile` hides a dormant tracker whose game is set to only-while-paused
unless the FOREGROUND game is paused, via new `SOH_IsPausedForCombo` / `MM_IsPausedForCombo`
exports (fail open = paused). Peek-hold still overrides.

**On future merges:** if upstream renames the tracker ImGui windows or the settings windows,
update `combo/gui/ComboTrackerCommon.h` (`kKinds[].imguiWin`) and the inline-widget window names.

## Tracker batch: spawn offset, icon parity, OOT check-tracker reset (2026-08-10)

**Why:** (1) First-show placement latched a bad MM position (OOT's AlwaysAutoResize width isn't
settled the frame both windows appear), so new configs opened the trackers stacked. (2) The two
grids had different density: OOT draws icon + IconSpacing gaps, MM packed 46px cells edge-to-edge.
(3) The OOT Check Tracker could permanently zero: a threaded trackerData save queued before a
reload runs against the freshly recreated (all-UNCHECKED) gRandoContext and persists
`"checkStatus": []`; separately, incremental saves demote COLLECTED->SCUMMED on disk and nothing
ever promotes back (autosave is blocked for the whole Ocarina-without-Song-of-Time stretch).

**Combo-owned:** `ComboTrackerSwap.cpp` placement waits for settled rects; `ComboTrackerBridge.*`
adds canonical `gCombo.Tracker.IconSpacing` (separately seeded) and flips Draggable default to on;
`ComboMenu.cpp` gets the shared "Icon spacing (px)" slider.

**Vendored (`COMBO_BUILD`-guarded):**
- `mm/.../ItemTracker/ItemTracker.cpp` — cell padded around the icon by
  `gSettings.ItemTracker.IconSpacing` (bridge-mirrored) so both grids share icon+gap pitch;
  count text anchored to the icon rect. Vanilla path unchanged (pad 0 / `#else`).
- `soh/soh/SaveManager.cpp` — `Save_LoadFile`: `ThreadPoolWait()` before recreating gRandoContext
  (reload paths reach here before OnExitGame's drain), and early-return keeping the current
  context when the container has no OOT block (previously reset-then-bail left it all-unchecked).
- `soh/.../randomizer_check_tracker.cpp` — `CheckTrackerLoadGame` promotes a persisted status to
  SAVED when the loaded save's own flags prove the check was obtained (`ComboCheckFlagObtained`);
  genuine save-scums stay SCUMMED (their flags are absent). Also self-heals already-wiped saves.
  Tripwire WARN when persisting an empty checkStatus for a rando file.
- `soh/soh/OTRGlobals.cpp` — `SOH_MarkForeignObtained` persists via full `SaveFile` (a foreign
  check has no OOT flag to promote it back from SCUMMED).
- `soh/.../SeedContext.cpp` — WARN tripwires in `ItemReset`/`ClearItemLocations` when called with
  a save loaded (suspected mid-session wipe path; not yet root-caused).

## Item Tracker settings: flat layout, icon-size parity, shared paused-only (2026-08-18)

**Why:** (1) MM's `ItemTrackerSettingsWindow::DrawElement` wrapped everything in auto-sized
`BeginChild`s. Embedded inline in the combo Settings hub the outer child only gets the parent's
leftover height, so the group Available/Active lists (and the reorder chevrons / per-group options)
were unreachable even though the hub scrolls. (2) Both games still exposed their own icon
size/spacing editors, which the bridge silently overwrites at the next `SyncAppearance`. (3) MM's
split-window-group mode used the per-group `scale` *instead of* the global one, so MM icons jumped
to their 46px base while OOT sat at the shared 36px.

**Combo-owned:** `ComboMenu.cpp` — shared "Icon size" max widened 64 → 128 (covers OOT's old
range), new "Only enable while paused" checkbox + a hint when the window type is Window.
`ComboTrackerBridge.{h,cpp}` — canonical `gCombo.Tracker.OnlyPaused` (default 0) mirrored into
`gTrackers.ItemTracker.ShowOnlyPaused` and `gSettings.ItemTracker.VisibilityMode` (0/1).

**Vendored — guard-free (equivalent-or-better standalone):**
- `mm/.../ItemTrackerSettings.cpp` — `DrawElement` / `DrawTrackerAvailableGroups` /
  `DrawTrackerActiveGroups` lay out flat, like SoH's item-tracker settings: the outer, per-column
  and per-list `BeginChild`s are gone, so content extends the parent and the *window* scrolls when
  popped out. The two lists regained ID scoping via `PushID` (they were previously separated by
  their child windows, and ImGui table cells do not scope IDs). The right-aligned group buttons
  now use a cursor-relative helper — `SameLine(absolute_x)` is cell-relative while
  `GetContentRegionMax()` is window-relative, so the old form would have flown off-screen in the
  second table column. The dead `SetNextWindowSize` before the removed outer child went with it
  (`BeginChildEx` always overrides it; the popout's size comes from its `GuiWindow` registration).
- `mm/.../ItemTracker.cpp` — `DrawItemCounts` restores `SetWindowFontScale(1.0f)`; it is
  window-wide state and leaked into every widget drawn after a counted icon.

**Vendored (`COMBO_BUILD`-guarded):**
- `mm/.../ItemTracker.cpp` — new `GetItemTrackerGroupScale()`; in combo the effective scale is
  `global × group.scale`, making the per-group scale a relative multiplier (1.0 = parity with OOT).
  Standalone keeps the either/or.
- `mm/.../ItemTrackerSettings.cpp` — global `Scale` slider and `Visibility` combobox hidden (combo
  owns both); per-group scale slider relabelled "Group scale (x global)" with a 0.05 step (0.5
  steps would snap the bridge-derived 0.7826 to 0.5/1.0); the Available/Active preview grids draw
  at `min(effective scale, 1.0)` instead of a hardcoded 1.0.
- `soh/.../randomizer_item_tracker.cpp` — "Icon size"/"Icon margins" sliders and the "Only Enable
  While Paused" checkbox hidden (all three are bridge-derived).

**Residuals (accepted):**
- MM's Button Toggle / Button Hold visibility modes (2/3) are unreachable in combo builds — the
  shared toggle only expresses Always (0) and Only-on-pause (1), and `SyncAppearance` forces one of
  those every sync.
- OOT's `ShowOnlyPaused` is a no-op when the tracker window type is Window rather than Floating
  (pre-existing upstream nesting); MM honors paused-only in both. The shared panel says so inline.
- The per-group scale is still MM-only; OOT has no equivalent, so a non-1.0 group scale breaks
  cross-game icon parity by design.

## Cross-game Personal Notes (issue #165)

**Why:** SoH's item-tracker notes live in the OOT save's `itemTrackerData` section, so a note taken
in OOT was invisible in MM and the MM-side tracker had no notes at all. Worse, the notes widget is
reachable during the dormant-game peek, and its save path (`SaveSection(gSaveContext.fileNum, ...)`)
would flush a stale OOT saveBlock while MM held the foreground. The note is per *slot*, not per game,
so the launcher — the owner of the merged container — is the right place for it.

**Vendored (`COMBO_BUILD`-guarded, all in `soh/.../randomizer_item_tracker.cpp`):**
- The `DisplayType.Notes` clause in the main-window OR-condition (`ItemTrackerWindow::DrawElement`).
- The main-window `DrawNotes()` call.
- The separate "Personal Notes" floating-window block.
- The `personalNotesWiget` `MenuDrawItem` call in `ItemTrackerSettingsWindow::DrawElement`.
- The `personalNotesWiget` definition + `AddSearchWidget` registration.
- `ItemTrackerSaveFile` writes an empty `personalNotes` (nothing in a combo build can edit or clear
  the loaded buffer, so the pre-migration text would otherwise round-trip forever).

`DrawNotes`, `itemTrackerNotes`, and the `itemTrackerData` load/init functions are untouched: the
section keeps round-tripping so a combo container can be migrated (and standalone SoH is unaffected).
Migration runs at container load, before any save could scrub the legacy field.

**Combo-owned:**
- `combo/gui/ComboNotesWindow.{h,cpp}` — `ComboNotesWindow` (registered in `ComboUI_Register`,
  visibility CVar `gCombo.Tracker.NotesWindow`, ImGui identity `Personal Notes##Combo`). Buffer is a
  `std::string` edited through the vendored `misc/cpp/imgui_stdlib` overload (compiled into comboui)
  — never an `ImVector` (see `boot-shutdown.md`). Hidden entirely when no save is loaded. Toggled
  from Shared > Item Tracker.
- `combo/ComboShip.cpp` — `Combo_GetNotes`/`Combo_SetNotes` over `combo.notes` (find()-based so the
  raw-fn-ptr calls can't throw and never deep-copy `combo.rando`; unchanged-value short-circuit so
  the debounce doesn't rewrite the container); `LoadOrCreateContainer` one-time migration of
  `oot.sections.itemTrackerData.data.personalNotes` into `combo.notes`, gated on the key being
  *absent* so a deliberately cleared note is never resurrected; `Combo_OnOOTSaveInit` writes an empty
  `combo.notes` so a fresh file starts blank (written, never erased — erasing would re-arm the
  migration gate; outside the seed branch, so it runs even when seed parsing fails).
- Seam: `ComboUI_SetNotesStore(getter, setter)`, resolved next to `ComboUI_SetAnchorRosterProvider`.
- Flush points: window close, slot change, `IsItemDeactivatedAfterEdit`, a 2s wall-clock debounce
  (SoH's 40-idle-frame equivalent), `ComboUI_OnForegroundGame` (OOT<->MM switch), and
  `ComboUI_RestoreTrackerIntent` (launcher pre-shutdown). Every reload of the buffer flushes first,
  so the pending text always belongs to `sDirtySlot`.
