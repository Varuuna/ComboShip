# ComboShip Owns the Menus — Design

Date: 2026-06-09
Branch: feat/combo-unified-menu
Status: Approved design, pre-implementation
Supersedes the render approach in: `2026-06-09-comboship-owns-menu-render-direction.md`
Related (out of scope): `comboship-crossgame-randomizer` (CrossMailbox item delivery)

## Problem

The unified menu currently delegates each game's tab to that game's own draw
function (`SOH_DrawSettings`, `MM_DrawSettings`). This works for the foreground
game but fails for the backgrounded game: `BenMenu::DrawContent` binds to live MM
state that isn't available when MM sits behind OOT — `fontStandardLargest`, the
per-module ImGui `GImGui` (not current for a backgrounded DLL), the per-widget
disable-map evaluation lambdas, and `mBenMenu` (torn down post eager-boot in
`DeinitOTR`). The interim guard (`dcba542a1`) shows a "reworking" note instead of
crashing. The in-port `COMBO_BUILD` guards scattered across `BenGui.cpp`,
`BenPort.cpp`, `OTRGlobals.cpp`, and `SohGui.cpp` are the symptom of menu logic
living in the wrong layer.

## Goal

ComboShip owns the menus outright. Both OOT and MM settings render and function
from the combo layer regardless of which game is foreground, with no dependency on
a backgrounded game's live render state. Port code shrinks to declarative data plus
narrow callback entry points, keeping the vendored source close to upstream
(HM64 / combo-ownership principles).

## Key facts established during design

These are load-bearing; the design depends on them.

- **Both game DLLs stay resident** for the whole process. `soh.dll` and `2ship.dll`
  are `LoadLibrary`'d once at launch and only freed at exit
  (`combo/ComboShip.cpp`). No `FreeLibrary` between transitions.
- **A backgrounded game is "frozen in ice":** DLL code, heap, `gPlayState`, its
  `ResourceManager` object, fonts, and the shared CVar store stay allocated.
  Execution is halted and its RM is not the Context's *active* RM.
- **One active ResourceManager.** `Ship::Context` holds a single active RM; each
  game has its own (`sOOTResourceManager` / `sMMResourceManager`) swapped via
  `context->SetResourceManager()` (bare pointer assignment, no lock). Resource
  lookups resolve against whichever RM is active.
- **Render loop and game loop are sequential on one thread**
  (`Fast3dWindow::DrawFrame` → `gui->StartDraw()` [DrawMenu] → interpreter →
  `EndDraw`). There is no concurrent access to the active-RM pointer, so a scoped
  RM swap around a menu callback is race-free.
- **CVars are a single process-wide store** in shared libultraship. A write is
  visible to both games; config persists to one shared file.
- **Resume reloads `gSaveContext` from disk**, it does not continue the frozen
  in-memory save (OOT: `Sram_OpenSave`→`Save_LoadFile`; MM:
  `Combo_LoadMMSaveFile`→`SaveManager_LoadSaveFile`). Therefore the menu must never
  mutate a dormant game's in-memory save — that write would be discarded.
- **Menu content is ~94% declarative.** Of ~519 OOT and ~332 MM `AddWidget` calls,
  the large majority are CVar/option rows. The exceptions:
  - ~14 `WIDGET_CUSTOM` imperative widgets (OOT: rando Seed / Excluded Locations /
    Tricks / Spoiler, Warp Points, Sail & Crowd-Control host fields, Resolution
    editor; MM: About, Presets, Warp Point, Resolution editor).
  - ~18% of widgets carry a `preFunc` that evaluates disable-state each frame by
    reading `gPlayState`/CVars.
- **The two widget frameworks have diverged ABI:** OOT uses
  `std::function<void(WidgetInfo&)>`; MM uses raw `void(*)(WidgetInfo&)`. Enums and
  fields differ (`WIDGET_INPUT`/color-picker vs `WIDGET_COLOR_24/32`, `raceDisable`,
  differing `DisableOption` sets). Widget structs cannot be shared across the DLL
  boundary as-is.

## Scope refinement (2026-06-10, from runtime testing)

Runtime testing rescoped the goal. The hard requirement is narrower than "render every
menu from combo": **only the Randomizer options must be accessible/editable at any
time, in either game, regardless of which game is foreground** (because cross-game
rando generation needs both games' rando options set before Generate). Every other
menu surface (Enhancements, Settings, Dev Tools, etc.) rendering only while its game
is foreground is acceptable.

Consequences:
- Custom-draw widgets and disable-evaluation are **foreground-only by default** — a
  backgrounded game shows a placeholder for its custom widgets, because those probe
  live game state (e.g. MM's rando `spoilerOptions`, `gPlayState`) that isn't alive
  while backgrounded. Declarative CVar widgets still render and edit fine in any state.
- **Exception: the Randomizer section is always rendered**, regardless of foreground.
  Section labels (the `menuEntries`/CwSection key) are **"Randomizer"** in OOT
  (soh/soh/SohGui/SohMenuRandomizer.cpp) and **"Rando"** in MM
  (mm/2s2h/Rando/Menu.cpp `RegisterMenu`). The custom-draw guard exempts these
  sections; their underlying init (rando subsystem, fonts) must be made available
  independent of the owning game being foreground.
- This aligns with the cross-game randomizer's combined settings + Generate flow.

## Architecture

### Ownership split

- **comboui.dll owns the menu outright.** It holds the single `Gui::mMenu` slot for
  the whole process, renders with its own framework, its own font, and the shared
  ImGui context. It never depends on either game being foreground.
- **Each game becomes a declarative data source + an action target.** A game
  contributes (a) its widget table as neutral data, emitted once at eager-boot while
  it is alive, and (b) a small registry of callbacks the combo menu invokes by id.
  No game installs or renders a menu.
- **Three responsibilities, cleanly split:**
  - *Definitions* — authored in-port (low upstream drift), captured by combo at
    boot, owned by combo thereafter.
  - *Render* — combo-owned, game-foreground-independent.
  - *Effects* — routed by id into the owning DLL under an RM scope, with a defined
    policy per effect category.

### Definition capture (neutral data)

While each game is alive during eager-boot, it emits its widget tree as ABI-stable
neutral data through one export (e.g. `SOH_ExportMenu` / `MM_ExportMenu`). The
neutral record carries only serializable fields:

- hierarchy (menu entry → sidebar → widget rows)
- `WidgetType` (normalized to a combo-side enum that unions both games' types)
- CVar name, label, tooltip, options lists, layout hints (`sameLine`, columns,
  `hideInSearch`), and disable-reason ids (enum, not the lambda)
- for non-declarative rows: a stable `customId` / `callbackId` referencing a
  per-game callback registered at boot (no function pointer crosses the boundary)

Combo ingests this once and owns it for the rest of the process. Render uses
combo's own framework/font/context and the shared CVar store; it does not call the
game for declarative rows.

### Per-frame game callbacks (the non-declarative slice)

Two slices cannot be static data and require a thin per-frame call into the owning
(resident) DLL:

1. **`WIDGET_CUSTOM` imperative widgets** (~14). Rendered by invoking the game's
   registered custom-draw callback.
2. **`preFunc` disable evaluation** (~18%). Evaluated by invoking the game's
   registered predicate, which only *reads* `gPlayState`/CVars.

Both are safe under the new model where they previously crashed, because:
- both DLLs stay resident (the callback code and the state it reads are alive),
- combo sets the shared ImGui context before invoking (no per-module `GImGui`
  problem), and combo owns the font atlas (no `fontStandardLargest` fault),
- the predicate reads are side-effect-free; a dormant game with `gPlayState == NULL`
  correctly evaluates to its "save not loaded" disabled state,
- any callback that can touch resources runs inside an RM scope (below).

**Validated by the Phase 0 spike (2026-06-09).** comboui successfully rendered a
backgrounded MM `WIDGET_CUSTOM` widget and evaluated a `preFunc` under the shared
ImGui context with no fault. The spike also established the **mandatory per-game
custom-draw contract** the pipeline must honor before invoking any custom widget or
theme-styled UIWidgets call, because comboui owns the menu slot so the Gui loop
never drives the game's own menu lifecycle:
1. the game's menu instance must be **constructed** (MM: `BenGui::ActivateMenu()` /
   `SetupMenu()`; OOT: the `SohMenu` instance), then
2. **`Init()`** (idempotent; runs `InitElement`), then
3. **`Update()`** — critical: the theme member (`menuThemeIndex`) is assigned in
   `UpdateElement()`, not `InitElement()`. Without `Update()` it is uninitialized and
   `THEME_COLOR`/`UIWidgets::ColorValues.at(...)` throws `out_of_range`.

**Cross-game theme-CVar hazard.** `gSettings.Menu.Theme` is a single shared CVar, but
each game indexes it against its **own** `Colors` enum + `ColorValues` map (the maps
differ per game). A value valid for one game can be out of range for the other and
throw in `.at()`. The per-game `Update()` resolves it against that game's
`defaultThemeIndex`; the combo render must clamp/validate the theme index against the
target game's map rather than assume the shared value is in range.

### ResourceManagerScope (new primitive)

There is no scoped RM helper today — only bare swaps in the resume paths. Add an
RAII guard in shared libultraship:

```
{ ResourceManagerScope scope(gameRM);  // context->SetResourceManager(gameRM)
  ...invoke the game callback...
}                                        // restores previous active RM
```

Race-free because render and game loop are sequential on one thread. Used to wrap
any per-game callback that may touch that game's resources.

### Behavior-callback policy (three buckets)

The menu never pushes live state to a dormant game. Behavior callbacks sort into:

1. **CVar-backed settings (the bulk):** write the shared CVar store. Foreground
   game sees it immediately; the dormant game reads the same CVar when it next
   loads/resumes. No cross-DLL call.
2. **Resource-touching callbacks (e.g. authentic-gfx patches):** invoked into the
   owning DLL under `ResourceManagerScope`.
3. **Game-loop-dependent actions (Warp/execute, Switch Age, Hyper-Bosses hook):**
   meaningful only on the *active* game (they queue work the frame loop must run).
   Combo scopes/disables these to the foreground game; it does not queue them onto a
   dormant game.

### Port-code refactor

Replace the scattered `COMBO_BUILD` menu guards with two narrow, declarative exports
per game (export-menu, invoke-callback-by-id) plus per-game callback registration at
boot. Remove the in-port menu install and the `MM_DrawSettings`/`SOH_DrawSettings`
delegation path and its interim guard once combo render is in place. Menu logic
moves to combo.

## Out of scope

- **Cross-game item delivery.** Already implemented and correct via `CrossMailbox`
  (`combo/rando/CrossMailbox.h`): enqueue on find, drain on the receiver's player
  update (covers both active and resume), persist before mark-delivered. The menu
  never delivers items. Retained as-is.
- Cosmetics/preset *content* changes beyond rendering their existing custom widgets.
- Any change to the boot/transition flow beyond menu ownership.

## Risks & validation

- **Spike first (highest risk):** prove one MM `WIDGET_CUSTOM` widget and one MM
  `preFunc`-gated widget render and evaluate correctly via the per-frame callback
  while MM is backgrounded, under combo's context + `ResourceManagerScope`, with no
  fault. This validates the core bet before building the capture pipeline.
- **ABI-neutral export format** must cover both games' `WidgetType` unions and
  options structs; verify no game-specific field is silently dropped.
- **customId stability:** custom-draw and callback ids must be stable across a game
  session so combo's captured tree stays valid for the process lifetime.
- **Disable-reason coverage:** combo must render the union of OOT (13) and MM (26)
  `DisableOption` reasons; evaluation stays per-game via the predicate callback.

## Success criteria

- Both OOT and MM settings render and operate from the combo menu with either game
  foreground or backgrounded; no MM-backgrounded fault.
- CVar settings apply to the foreground game immediately and to the other game on
  its next load/resume.
- Resource-touching and game-loop-dependent callbacks behave per the bucket policy.
- In-port `COMBO_BUILD` menu guards and the draw-delegation path are removed; each
  game's menu contribution is declarative data + a callback registry.
