# Direction: ComboShip Owns the Menu Render

**Date:** 2026-06-09
**Branch:** `feat/combo-unified-menu`
**Status:** Direction decided (user, 2026-06-09). This is a decision/direction record to seed a full brainstorm → spec → plan. NOT yet a complete spec.
**Revises:** `docs/superpowers/specs/2026-06-09-combo-unified-settings-menu-design.md` — the *goal* (combo-owned unified menu, Shared/OOT/MM/Combo scope tabs, all settings exposed) is unchanged; the **render mechanism** changes from "delegate to each game's live menu" to "ComboShip owns the render."

---

## Decision

**ComboShip owns the menu render.** Do **not** remove or rewrite port logic. Instead, under `COMBO_BUILD`:
1. **Disable** each port's local menu implementation (its own `SetMenu`/render path) — already partly done (the games' `gui->SetMenu`/`ActivateMenu` are suppressed so comboui owns the single menu slot).
2. **Copy** the code ComboShip needs (the menu framework + the widget definitions/tables) into the **combo layer** (comboui).
3. ComboShip **renders the menu from combo-owned state** (its own font, ImGui context, themed widget library) — completely **independent of which game (OOT/MM) is live/foreground**.

This supersedes the delegation model (each game's `*_DrawSettings` → its own `Ship::Menu::DrawContent`) for the per-game tabs.

---

## Why (the delegation dead-end)

The unified-menu work delegated each game tab to that game's own `Ship::Menu::DrawContent` (rendered by the game DLL, using the game's themed widgets). This **works only for the foreground game**:
- **OOT tab works** — OOT is the foreground game; all its runtime state is live.
- **MM tab cannot** — after the eager MM boot, MM sits **backgrounded** behind OOT. `BenMenu::DrawContent` faults because it touches MM's **live runtime state** that isn't alive when MM is backgrounded:
  - `OTRGlobals::Instance->fontStandardLargest` (MM's font),
  - per-widget `disabledMap` **evaluation lambdas** (probe live game state),
  - `mBenMenu` is even torn down post-eager-boot (`DeinitOTR` → `BenGui::Destroy()`),
  - 2ship's per-module ImGui `GImGui` isn't current when backgrounded.

We patched several of these (context set in both draw-exports; lazy `mBenMenu` rebuild) but the font/disable-evaluation dependence on a *live* MM is the wall. Only a combo-owned render — not dependent on either game being foreground — resolves it. (Current interim: `MM_DrawSettings` shows a "reworking" note instead of crashing; the build is usable with Shared/OOT/Combo working.)

---

## Approach (sketch — to be detailed in the full spec)

- **Combo-owned menu framework**: comboui carries its own copy of the menu render (the `Ship::Menu`-style header/sidebar/widget-column layout + the themed widget primitives `UIWidgets`), so it can draw a native-looking menu using **combo-owned font + the shared ImGui context**.
- **Widget data, copied/owned by combo**: the per-game widget *tables* (label, CVar, widget type, value labels, category/sidebar) are copied into / surfaced to the combo layer. Most settings are pure **CVar checkbox/combo/slider** — comboui reads/writes the CVar directly; the game reads that CVar at runtime, no game call needed. (The shared `gSettings.*` engine CVars + each game's `gEnhancements.*`/`gRando.*` CVars are all in the one shared CVar store — readable/writable regardless of foreground game.)
- **Behavior widgets (the minority)**: settings whose effect needs a game-side action *now* (e.g. apply resolution, grant starting item, backend switch) get either a **thin per-game export** comboui calls, or are rendered CVar-only / "needs reload." Disable-state evaluations that probe live game state are evaluated against **safe defaults / skipped** in the combo render.
- **Ports' local menu disabled under `COMBO_BUILD`**: keep the port menu code intact for standalone builds; the combo build doesn't install or drive it.

Net: the combo menu is **CVar-driven and combo-rendered**, so both games' settings are editable at any time, independent of lifecycle. (This is close to the original Inc7 "uniform option model / `OptionDesc`" idea, which we pivoted away from for native theming — the synthesis is: combo-owned themed render + combo-owned widget tables.)

---

## Hard constraints the full spec MUST address (learned this session)

1. **Menu framework + widgets are per-game and ABI-diverged** (`soh/soh/SohGui/` vs `mm/2s2h/BenGui/`; `WidgetInfo`/`WidgetType` differ — `std::function` vs raw fn-ptr, differing fields/enums). Combo cannot share widget structs across the DLL boundary → it needs its **own** framework copy + a neutral widget-data representation.
2. **Widget callbacks reference game internals** (`Item_Give`, `SetResolutionMultiplier`, game fns) → comboui can't link them. CVar-only widgets are fine; behavior widgets need a thin export or CVar-only treatment.
3. **Disable evaluations + fonts depend on live game state** → combo must use a combo-owned font and evaluate/skip disables safely.
4. **Engine CVars are `gSettings.*` and shared across DLLs** (verified) — this is what makes a CVar-driven combo-owned menu viable. (And recall the migration gotcha: verify CVar names against the **generated** build defines, not `cmake/cvars.cmake`.)

---

## What exists today (on `feat/combo-unified-menu`)

Working + committed: comboui.dll owns the single menu (fullscreen overlay, Shared/OOT/MM/Combo scope tabs); **Shared** tab (combo... currently delegates to OOT's themed engine sidebars); **OOT** tab (themed, delegated, settings honored — KF Open, starting items); **Combo/Generate** (settings-scoped, synchronous, no crash, deterministic; OOT + MM save options persisted). **MM tab**: interim note (delegation can't render MM backgrounded). Known limitation: combo fill is wholesale → within-category count settings (shopsanity count, etc.) not honored.

This direction's rework will replace the per-game-delegated render with the combo-owned render described above; much of the scope-tab shell, the Combo/Generate panel, the generate pipeline, and the settings-application fixes carry over.

---

## Next steps

1. Brainstorm the "ComboShip owns the menu render" architecture (resolve: how widget tables get into combo — copy vs game-exported neutral dump; how the themed renderer is shared; the behavior-widget export surface; whether the Shared tab also moves to combo-owned render).
2. Write the spec, then the plan.
3. Execute (the delegation-based per-game tabs get replaced; interim MM note removed).
