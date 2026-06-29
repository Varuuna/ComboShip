// combo/gui/ComboResolutionEditor.h
//
// ComboShip: combo-owned reimplementation of SoH's Advanced Resolution editor
// (soh/soh/SohGui/ResolutionEditor.cpp). That editor is built from machinery the flat C-ABI
// menu snapshot can't carry — dynamic PreFunc-updated names, a combobox bound to a C++ static
// via ValuePointer (no CVar), WIDGET_CUSTOM draw lambdas, and a per-page MenuUpdate func that
// syncs statics to CVars. So those widgets render broken in the overlay (empty "{}" readouts,
// an empty-CVar combobox, placeholder customs, dead controls).
//
// We intercept those specific widgets by name/cvar and render combo-owned controls that
// read/write the EFFECTIVE gSettings.AdvancedResolution.* CVars directly. libultraship's shared
// Fast3dGui reads those CVars every frame (Fast3dGui.cpp:367) and applies them live, so no
// game-side callback is needed and this works regardless of which game is foregrounded.
//
// UPSTREAM COUPLING (see docs/UPSTREAM_MERGES.md): this shadows specific vendored SoH widget
// names and transcribes SoH's preset tables; a SoH menu rename must be re-synced here.
#pragma once
#include "ComboMenuABI.h"

namespace ComboRando {

// Returns true if `w` is an advanced-resolution widget handled here — in which case this has
// already rendered the combo-owned replacement and the caller must skip its generic render.
bool TryRenderResolutionWidget(const CwWidget& w);

} // namespace ComboRando
