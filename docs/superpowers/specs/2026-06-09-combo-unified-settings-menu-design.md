# ComboShip — Combo-Owned Unified Settings Menu

**Date:** 2026-06-09
**Branch:** `randomizer`
**Status:** Design complete (brainstormed + approved 2026-06-09); plan to follow.
**Supersedes:** `docs/superpowers/specs/2026-06-05-combo-inc7-settings-window-design.md` and its plan `docs/superpowers/plans/2026-06-09-combo-inc7-settings-window.md`. The cross-world Generate UX from Inc7 is preserved here as the **Combo** tab.
**Depends on (all DONE):** shared `libultraship.dll` + shared CRT across all DLLs; eager-MM-boot (both game DLLs loaded at startup); the cross-world combined fill + oracles + mailbox (Inc1–6); `SOH_ApplyRandoPlacements`, `MM_InitRandoSaveFile`, `g_PendingMMPlacements`.

---

## Goal

Make **ComboShip permanently own the single ImGui menu** through a dedicated combo-owned module, presenting one menu with four scope tabs — **Shared | OOT | MM | Combo** — that surface *all* of each game's settings (not just randomizer): engine-level settings once in **Shared**, each game's specific content in **OOT** / **MM**, and the cross-world randomizer Generate in **Combo**. SoH and 2ship continue to own and draw "their parts" exactly as they do today; Combo owns the chrome and the shared/combo panels. The overriding constraint is to touch port code as little as possible so upstream pulls stay conflict-light.

## Non-goals

- Unifying or refactoring the two ports' menu *frameworks* (they have diverged and are ABI-incompatible — see §2). We do **not** merge widget data.
- Re-skinning or redesigning individual game settings widgets. OOT/MM tabs show each game's existing widgets as-is.
- Per-game independent engine settings. Graphics/Audio/etc. are genuinely shared (one libultraship engine, one CVar store) and are shown once.
- Fill performance, spoiler viewers, multi-seed management (unchanged from Inc7 scope).

---

## Decisions (locked during brainstorming, 2026-06-09)

1. **Pivot now; replace Inc7.** The unified menu is the next thing built. The Inc7 standalone rando window is dropped; its Generate UX becomes the **Combo** tab.
2. **Shared section + per-game tabs.** Engine-level settings (Graphics/Audio/General/Controls) appear once under **Shared**, not duplicated under each game.
3. **Outer scope tabs on top.** A top tab strip (Shared | OOT | MM | Combo); selecting a tab swaps the panel beneath.
4. **Delegation, not merging — forced by ABI.** The menu framework (`Ship::Menu`, `WidgetInfo`, `WidgetType`, `AddWidget`) is **not** in libultraship; each game has its own diverged copy. `WidgetInfo` differs in layout between soh.dll and 2ship.dll (SoH `WidgetFunc` = `std::function` ~32B; MM = raw fn-ptr 8B; differing fields/enums). Therefore widget data must **never** cross a DLL boundary. Each game draws its own widgets; combo never holds them.
5. **Dedicated `comboui.dll`.** The menu shell lives in a new combo-owned DLL (not the exe, not soh.dll). It isolates the libultraship-C++-ABI coupling into a combo module that is expected to track libultraship (like soh/2ship do), keeps the launcher thin, and keeps combo UI out of port binaries.
6. **Combo draws the Shared tab itself.** Engine settings are all libultraship-level; comboui renders them directly against libultraship's CVars/APIs — true dedup, zero port-touch for that panel.
7. **One guarded port-touch is accepted.** The games' own `SetMenu`/`ActivateMenu` calls (boot + transition) are suppressed under `COMBO_BUILD` so comboui's menu stays installed. Additive/guarded; the main merge-conflict surface.

---

## 1. Architecture

```
ComboShip.exe ──LoadLibrary("comboui.dll")──> comboui.dll
                                                  │  links libultraship (C++) + ImGui
                                                  │  ComboUI_Register():  gui->SetMenu(ComboMenu)  [once, forever]
                                                  │
   ComboMenu : Ship::GuiWindow  (drawn each frame by libultraship's shared Gui::DrawMenu loop)
      ├─ scope tab strip: [ Shared ] [ OOT ] [ MM ] [ Combo ]
      ├─ Shared tab  → comboui draws engine settings against libultraship CVars/APIs (combo-owned)
      ├─ OOT tab     → SOH_DrawSettings()   (soh.dll draws its own game-specific widgets)
      ├─ MM  tab     → MM_DrawSettings()    (2ship.dll draws its own game-specific widgets)
      └─ Combo tab   → comboui draws seed field + Generate + progress (combo-owned)
                           Generate → SOH_TriggerComboGenerate(seed,&progress) → exe worker thread
```

libultraship's `Gui::SetMenu(std::shared_ptr<GuiWindow>)` accepts **any** `GuiWindow` from any module and stores it opaquely as `mMenu`; `Gui::DrawMenu` calls `mMenu->Update(); mMenu->Draw();` each frame. So a `comboui.dll`-owned `GuiWindow` subclass is a first-class menu with no special engine support needed.

---

## 2. Why merging is impossible (and delegation is the only correct model)

The menu widget framework is duplicated per game, not shared:

| Item | Location | Status |
|------|----------|--------|
| `GuiElement` / `GuiWindow` / `GuiMenuBar` / `Gui` | `libultraship/include/ship/window/gui/` | shared base — safe |
| `Ship::Menu` (the AddWidget framework) | `soh/soh/SohGui/Menu.{h,cpp}` **and** `mm/2s2h/BenGui/Menu.{h,cpp}` | duplicated per game |
| `WidgetInfo`/`WidgetType`/`MainMenuEntry`/`MenuTypes.h` | `soh/soh/SohGui/MenuTypes.h` **and** `mm/2s2h/BenGui/MenuTypes.h` | duplicated **and diverged** |

Divergence that breaks ABI: SoH's `WidgetFunc` is `std::function<void(WidgetInfo&)>` (~32B) vs MM's raw `void(*)(WidgetInfo&)` (8B); different `WidgetType` enum entries; SoH `WidgetInfo` has a `raceDisable` field MM lacks; different `DisableOption` enums. `WidgetInfo` therefore has a different memory layout in each DLL.

**Consequence:** any attempt to harvest/copy/merge one game's `menuEntries` into another module corrupts memory. The only safe interaction is "module that owns the widgets draws them." Hence the delegation model: combo asks each game to draw, into the shared ImGui context, using the game's own (ABI-local) types.

ImGui note: `GImGui` (current context) is per-module; each DLL already calls `ImGui::SetCurrentContext(gui->GetImGuiContext())`. comboui does the same. A game's `*_DrawSettings()` runs in its own module, so its ImGui calls resolve against the shared context correctly. comboui's `ComboMenu::Draw` opens the menu window (via `GuiWindow::Draw`); the per-game draw-exports must emit **widgets only** (no top-level `ImGui::Begin`/`End` of their own) so they nest inside comboui's window/tab.

---

## 3. Components & Ownership

### 3.1 `comboui.dll` (new, combo-owned)
- **Build:** new CMake target under `combo/` (mirror how soh/2ship link `libultraship` + ImGui + their include dirs). Source under `combo/gui/`. Added to the combo build; copied next to the exe + into `x64/Debug` by the existing combo POST_BUILD pattern.
- **`ComboMenu : Ship::GuiWindow`** — overrides `DrawElement()` to render the scope-tab strip and the active panel.
- **`ComboUI_Register()`** (exported C): obtains the shared `Gui`, `SetCurrentContext`, constructs `ComboMenu`, calls `gui->SetMenu(comboMenu)`. Idempotent; safe to call once GUI exists.
- **Shared panel (combo-drawn):** engine settings against libultraship CVars/APIs — internal resolution, MSAA, texture filter, vsync, windowed/fullscreen, aspect/interpolation FPS, master/music/sfx volumes, audio backend, video backend, controls entry point, menu theme/scale. (Scope = the genuinely libultraship-level settings; the same CVar names both ports already use.)
- **Combo panel (combo-drawn):** seed input (blank = random), Generate button, threaded progress bar + status line. Reuses the Inc7 mechanics: `ComboGenProgress` struct, `SOH_TriggerComboGenerate`, the exe worker.

### 3.2 `soh.dll` / `2ship.dll` seams (additive, `COMBO_BUILD`-guarded)
- **`SOH_DrawSettings()` / `MM_DrawSettings()`** (new exported C): draw that game's **game-specific** headers (Enhancements, Cheats, Cosmetics, MM Modes/Fixes, Randomizer, Dev Tools, Network) into the current ImGui window, **excluding** the engine "Settings" header. Implemented by reusing the game's existing menu render; source may be combo-owned files compiled into each game DLL to minimize edits to vendored menu code.
- **Suppress self-installed menu under `COMBO_BUILD`:** remove/guard the `gui->SetMenu(...)`/`ActivateMenu()` calls at boot and on transition so comboui's menu stays installed:
  - `soh/soh/SohGui/SohGui.cpp` `SetupMenu` (the `gui->SetMenu(mSohMenu)`),
  - `soh/soh/OTRGlobals.cpp` `SOH_ReinitForResume` (the `SetMenu(GetSohMenu())` restore),
  - `mm/2s2h/BenGui/BenGui.cpp` `SetupMenu`/`ActivateMenu`,
  - `mm/2s2h/BenPort.cpp` `MM_ResumeGame` (the `BenGui::ActivateMenu()` call).
  - The games still **build** `SohMenu`/`BenMenu` (the draw-exports render from them); they just don't install them.

### 3.3 `ComboShip.exe`
- After the first game's GUI is up, `LoadLibrary("comboui.dll")` + call `ComboUI_Register()`.
- Keeps the existing cross-world generate worker (threaded fill, seed + progress) and the `g_PendingMMPlacements` stash; the Combo panel's Generate triggers it via `SOH_TriggerComboGenerate`.
- Decouple save-time generation exactly as the Inc7 spec described (§3/§5 of the Inc7 spec): the worker applies OOT placements + sets seed-generated before file create; `z_sram.c`'s combo block keeps forcing `QUEST_RANDOMIZER` but no longer runs the fill.

All vendored seams documented in `docs/UPSTREAM_MERGES.md` with `// ComboShip:` comments per `document-post-merge-changes`.

---

## 4. Data Flow

- **Shared (engine) settings:** comboui ↔ shared libultraship CVar store directly. Both games read those same CVars live each frame, so a change applies to whichever game is foregrounded with no extra plumbing.
- **Game-specific settings:** comboui calls the active game's `*_DrawSettings()` each frame; the game reads/writes its own CVars as always.
- **Generate (Combo tab):** comboui Generate → `SOH_TriggerComboGenerate(inputSeed, &progress)` → exe worker thread → dump static data → `CrossWorldCombinedFill(..., &progress)` → write spoiler/foreign map → `SOH_ApplyRandoPlacements` (+ seed-generated) → stash MM slice → set progress.done. comboui polls `progress` each frame for the bar + status.

---

## 5. Window Layout

```
┌ Combo Menu ───────────────────────────────────────────────┐
│ [ Shared ]  [ OOT ]  [ MM ]  [ Combo ]      ← scope tabs   │
├────────────────────────────────────────────────────────────┤
│  (active scope's content)                                  │
│   Shared : Graphics / Audio / General / Controls (combo)   │
│   OOT    : Enhancements / Cheats / Cosmetics / Rando (soh) │
│   MM     : Enhancements / Modes / Fixes / Rando (2ship)    │
│   Combo  : Seed [______(blank=random)] [Generate]  status  │
└────────────────────────────────────────────────────────────┘
```
- The tab strip is always visible; the panel beneath swaps with the active tab.
- The OOT/MM panels render each game's own header-row + sidebar + widgets via the delegate export (content only; comboui owns the window).
- Opened/toggled with the existing menu hotkey (comboui's `ComboMenu` is the single registered menu).

---

## 6. Transition Behavior

comboui's `ComboMenu` is installed once and is the menu for the whole process; nothing swaps it on an OOT↔MM transition (the swap calls are the suppressed port-touch in §3.2). Both games' tabs are reachable at any time regardless of which game is foreground.

**Checkpoint to validate:** a game-specific widget whose callback reads/writes *live* game state may be inert while that game is backgrounded. Settings/cosmetics/CVar-bound options are fine (CVars are shared + persistent). During implementation, note any widget that misbehaves when its game isn't foreground; if found, disable-with-tooltip in that game's `*_DrawSettings()` when backgrounded (combo can pass a "foreground" flag to the export).

---

## 7. Error Handling

- comboui resolves `SOH_DrawSettings`/`MM_DrawSettings` defensively; a missing export → that tab shows an "unavailable" message, no crash.
- `ComboUI_Register()` no-ops (and is retried by the exe's boot sequence) if the shared Gui/context isn't ready yet.
- Generate errors surface in the Combo panel status line; the worker thread is joined on the next request (SoH's pattern).
- comboui never holds game widget structs → no ABI-mismatch corruption is possible by construction.

---

## 8. HM64 / Combo-Ownership Compliance

- New logic is combo-owned: `comboui.dll` (`combo/gui/*`) + the existing combo exe worker.
- Port-touch is confined to: two additive draw-exports (source may be combo-owned files compiled into each game DLL), and the `COMBO_BUILD`-guarded suppression of the games' self-`SetMenu` calls. No `soh/src` or `mm/src` game-source logic changes beyond the already-present combo blocks (e.g. `z_sram.c`).
- Each seam carries a `// ComboShip:` comment and a `docs/UPSTREAM_MERGES.md` entry with the WHY.

---

## 9. Build & Verification (manual; project norm)

Build order: `libultraship → soh → 2ship → comboui → ComboShip` (soh and 2ship still serialized, never parallel). New `scripts/build-comboui.ps1` to mirror the other per-target scripts. comboui.dll copied next to the exe + into `x64/Debug` by the combo POST_BUILD.

Verify in the running exe:
- Menu opens with four tabs (Shared | OOT | MM | Combo).
- **Shared:** changing resolution/vsync/volume/etc. takes effect in the active game.
- **OOT / MM:** each tab shows that game's specific settings (Enhancements/Cheats/Rando/…), and engine settings are **not** duplicated there.
- Toggling a game-specific CVar in a tab persists and affects that game.
- **Combo:** seed + Generate runs with a moving progress bar; status line shows seed + foreign count; fixed seed twice → identical spoiler; blank → differs.
- "Start Game"/file create is gated until Generate has run; allowed after.
- The menu persists and both tabs keep working **across an OOT↔MM transition**.
- No crash; capture `x64/Debug/combo_abort_stack.txt` if one occurs.

---

## 10. Open Risks / Checkpoints

| Risk | Severity | Mitigation |
|------|----------|------------|
| comboui linking libultraship's C++ ABI breaks on an upstream lus pull | MEDIUM | Isolated to one combo-owned DLL (rebuild it, like soh/2ship). Keep comboui's lus usage minimal (Gui/GuiWindow/ImGui only). |
| Per-game `*_DrawSettings()` needs to render a header *subset* (exclude engine "Settings") — may require a small refactor of the game's `Menu` draw to expose "draw header X content" | MEDIUM | Prefer reusing existing per-header render via a thin combo-owned wrapper compiled into the game DLL. Fallback: draw the game's whole menu (engine settings reappear under the game tab) — a plan-time checkpoint, only if subset-draw proves too invasive. |
| A game-specific widget callback touches live game state while backgrounded | LOW–MED | §6 checkpoint; disable-with-tooltip when not foreground. |
| ImGui nesting: a game export opening its own window instead of widgets-only | LOW | Export draws content only; verified visually (no detached window). |
| OOT rando Context reset between Generate and save create | MEDIUM | Carried from Inc7: verify; re-apply stashed OOT placements at save-init if needed. |
| MM `OnFileCreate` self-generation double-runs | LOW | Carried from Inc7: verify; gate under `COMBO_BUILD` if it does. |

---

## Out of Scope (future)

- Merging the two ports' menu frameworks into a shared libultraship one (would eliminate the ABI divergence and allow true merging — large, upstream-coupled, deferred).
- Fill performance (incremental reachability), spoiler-log viewer, multi-seed management.
- Moving auxiliary popout windows (Console/Stats/etc.) under Combo ownership — they remain per-game `AddGuiWindow` registrations.
