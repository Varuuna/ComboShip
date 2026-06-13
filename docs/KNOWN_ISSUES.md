# ComboShip — Known Issues

Tracked, non-blocking limitations. Each entry: symptom, root cause, workaround, fix direction.

---

## Combo menu: Advanced Resolution (and other update-func-driven settings) don't apply

**Symptom:** In the combo menu's **Shared → Settings → Graphics** "Advanced Resolution" section,
changing options (aspect ratio, vertical resolution, integer scaling, etc.) has **no effect on the
viewport**. Often paired with the game view appearing oversized / forced to a fixed aspect, because
the effective resolution CVars are stuck at whatever was last written.

**Root cause (combo-menu architecture, not the renderer):**
The render path is fine — `Fast3dGui::CalculateGameViewport()` calls `ApplyResolutionChanges()` every
frame (`libultraship/src/fast/Fast3dGui.cpp:344,369,462`), which reads the *effective* CVars
(`gSettings.AdvancedResolution.AspectRatioX/Y`, `.VerticalPixelCount`, …) and resizes the viewport.
Whatever those CVars hold *is* applied.

The gap is that the **combo menu never produces those effective CVars** when you change a setting.
SoH's Advanced Resolution editor (`soh/soh/SohGui/ResolutionEditor.cpp`) is not a set of plain CVar
widgets:
- Its controls bind to **local C++ variables** + set dirty flags, not directly to the effective CVars.
- A per-page **menu update function**, `UpdateResolutionVars()` (`ResolutionEditor.cpp:518`, registered
  via `RegisterMenuUpdateFunc(..., "Settings", "Graphics")`), is what translates those selections into
  the effective `AspectRatioX/Y` / `VerticalPixelCount` CVars and does the dependent bookkeeping.

The combo build replaced SoH's native menu with **comboui** (`combo/gui/ComboMenu.cpp`,
`combo/gui/ComboWidgetRender.cpp`), which renders a flattened C-ABI snapshot (`CwMenu`,
`combo/menu/ComboMenuABI.h`) and writes plain CVars generically (`RenderWidget`). That path does not
faithfully reproduce the resolution editor's local-variable bindings + `UpdateResolutionVars`
translation, so the UI-only CVars (e.g. `UIComboItem.AspectRatio`) get written while the effective
ones (`AspectRatioX/Y`) stay at `0:0`, and `ApplyResolutionChanges` does nothing.

This affects any setting that depends on a registered per-page MenuUpdate function rather than a
direct CVar binding; Advanced Resolution is the most visible case.

**Not caused by:** the cross-game foreign-item rendering work or the `experimental-title → develop`
branch move (the resolution path in `Fast3dGui.cpp` is identical on both branches; the split-title
commit is guarded and doesn't touch it).

**Workaround:** set the *effective* keys directly in `x64/Debug/shipofharkinian.json` and relaunch,
e.g. `gSettings.AdvancedResolution`: `Enabled=1`, `AspectRatioX=16`, `AspectRatioY=9`,
`VerticalResolutionToggle=1`, `VerticalPixelCount=480`. These are read by libultraship directly and
take effect regardless of the menu.

**Fix direction:** make comboui run SoH's per-page MenuUpdate functions for the rendered section
(comboui already has a `RunUpdateFuncs` hook — `combo/menu/ComboMenuDrawContent.h:267` — verify it
actually runs for the Shared/Graphics page and that the snapshot captures the resolution editor's
bindings), or special-case the Advanced Resolution editor. The broader fix (driving update-funcs)
would also cover other settings with the same pattern.
