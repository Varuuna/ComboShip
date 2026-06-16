// combo/gui/ComboWidgetRender.h
//
// ComboShip: comboui-side renderer for ONE flat C-ABI widget (CwWidget). Declarative
// kinds are drawn here with comboui's own ImGui calls over the shared libultraship CVar
// store; the non-declarative slices (preFunc disable-eval, custom-draw, post-change
// callback) are routed BY INDEX back into the owning game via the resolved export fn-ptrs.
//
// DESIGN NOTE: this file deliberately does NOT construct a ResourceManagerScope and does
// NOT touch any ResourceManager. RM scoping for the invoke callbacks lives GAME-SIDE,
// inside each game's own invoke/draw/eval export — comboui just calls them by index.
#pragma once
#include "ComboMenuABI.h"

namespace ComboRando {

struct GameMenu; // from ComboMenuModel.h

// Render one widget (declarative kinds drawn here; custom/preFunc/callback routed by index).
void RenderWidget(const CwWidget& w, const GameMenu& game);

// Render all of a sidebar's widgets honoring CwSidebar::columnCount — the faithful multi-column
// layout of the native menu (1 column => linear). Widgets carry their source column (CwWidget.column,
// ABI v2). Use this instead of looping RenderWidget so every section gets the right column layout.
void RenderSidebarWidgets(const CwSidebar& side, const GameMenu& game);

} // namespace ComboRando
