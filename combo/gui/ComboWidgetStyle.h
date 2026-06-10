// combo/gui/ComboWidgetStyle.h
//
// ComboShip: combo-owned replication of OOT's UIWidgets menu styling (theme accent color +
// frame rounding/padding + per-widget color pushes). comboui CANNOT call the games' UIWidgets
// (those live per-DLL and are not linkable across the boundary), so the OOT look is mirrored
// here against the shared ImGui context + shared CVar store.
//
// Reference: soh/soh/SohGui/UIWidgets.cpp (PushStyleCheckbox/Combobox/Slider/Button +
// PopStyle* counterparts) and the ColorValues palette in UIWidgets.hpp. OOT is the reference
// look because the Shared tab is OOT-rendered.
#pragma once

#include <imgui.h>

namespace ComboRando {

// Current menu theme accent color, read from the shared CVar each call (clamped to range).
// Mirrors UIWidgets::ColorValues.at(gSettings.Menu.Theme).
ImVec4 ComboMenu_ThemeColor();

// Push/pop pairs mirroring OOT's UIWidgets style functions EXACTLY (same colors/vars + matching
// pop counts). Always call the matching Pop, including on early-return paths.
void ComboMenu_PushCheckbox(const ImVec4& color);
void ComboMenu_PopCheckbox();

void ComboMenu_PushCombobox(const ImVec4& color);
void ComboMenu_PopCombobox();

void ComboMenu_PushSlider(const ImVec4& color);
void ComboMenu_PopSlider();

void ComboMenu_PushButton(const ImVec4& color);
void ComboMenu_PopButton();

// Frame-input style (themed FrameBg + Button) used for InputInt steppers so they match.
// Mirrors UIWidgets::PushStyleInput.
void ComboMenu_PushInput(const ImVec4& color);
void ComboMenu_PopInput();

} // namespace ComboRando
