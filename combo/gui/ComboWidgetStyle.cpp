// combo/gui/ComboWidgetStyle.cpp
//
// ComboShip: see ComboWidgetStyle.h. Each Push*/Pop* below is a 1:1 mirror of the OOT
// UIWidgets counterpart in soh/soh/SohGui/UIWidgets.cpp — same ImGuiCol_/ImGuiStyleVar_
// pushes, same alpha multipliers, and POP COUNTS THAT MATCH. If you change one side, change
// both. (MM's 2s2h/BenGui/UIWidgets.cpp matches these; OOT is the canonical reference because
// the Shared tab is OOT-rendered.)
#include "ComboWidgetStyle.h"

#include <libultraship/libultraship.h> // CVarGetInteger
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

namespace ComboRando {

// Menu theme palette in UIWidgets::Colors ENUM ORDER (the CVar stores the enum index).
// Copied from UIWidgets.hpp ColorValues (RGBA, alpha 1.0 unless noted). Index = enum value:
//   0 Red, 1 DarkRed, 2 Orange, 3 Green, 4 DarkGreen, 5 LightBlue, 6 Blue, 7 DarkBlue,
//   8 Indigo, 9 Violet, 10 Purple, 11 Brown, 12 Gray, 13 DarkGray, 14 Pink, 15 Yellow,
//   16 Cyan, 17 Black, 18 LightGray, 19 White, 20 NoColor.
static const ImVec4 kThemePalette[] = {
    ImVec4(0.55f, 0.0f, 0.0f, 1.0f),   // Red
    ImVec4(0.3f, 0.0f, 0.0f, 1.0f),    // DarkRed
    ImVec4(0.85f, 0.55f, 0.0f, 1.0f),  // Orange
    ImVec4(0.0f, 0.55f, 0.0f, 1.0f),   // Green
    ImVec4(0.0f, 0.3f, 0.0f, 1.0f),    // DarkGreen
    ImVec4(0.0f, 0.24f, 0.8f, 1.0f),   // LightBlue (default)
    ImVec4(0.08f, 0.03f, 0.65f, 1.0f), // Blue
    ImVec4(0.03f, 0.0f, 0.5f, 1.0f),   // DarkBlue
    ImVec4(0.35f, 0.0f, 0.87f, 1.0f),  // Indigo
    ImVec4(0.5f, 0.0f, 0.9f, 1.0f),    // Violet
    ImVec4(0.31f, 0.0f, 0.67f, 1.0f),  // Purple
    ImVec4(0.37f, 0.18f, 0.0f, 1.0f),  // Brown
    ImVec4(0.45f, 0.45f, 0.45f, 1.0f), // Gray
    ImVec4(0.15f, 0.15f, 0.15f, 1.0f), // DarkGray
    ImVec4(0.87f, 0.3f, 0.87f, 1.0f),  // Pink
    ImVec4(0.95f, 0.95f, 0.0f, 1.0f),  // Yellow
    ImVec4(0.0f, 0.9f, 0.9f, 1.0f),    // Cyan
    ImVec4(0.0f, 0.0f, 0.0f, 1.0f),    // Black
    ImVec4(0.75f, 0.75f, 0.75f, 1.0f), // LightGray
    ImVec4(1.0f, 1.0f, 1.0f, 1.0f),    // White
    ImVec4(0.0f, 0.0f, 0.0f, 0.0f),    // NoColor
};
static const int kThemePaletteCount = (int)(sizeof(kThemePalette) / sizeof(kThemePalette[0]));

ImVec4 ComboMenu_ThemeColor() {
    int idx = CVarGetInteger("gSettings.Menu.Theme", 5 /* LightBlue */);
    if (idx < 0) {
        idx = 0;
    } else if (idx >= kThemePaletteCount) {
        idx = kThemePaletteCount - 1;
    }
    return kThemePalette[idx];
}

// --- Checkbox: mirrors UIWidgets::PushStyleCheckbox (5 colors + 3 vars). Default pad (10,6). ---
void ComboMenu_PushCheckbox(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(1.0f, 1.0f, 1.0f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void ComboMenu_PopCheckbox() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
}

// --- Combobox: mirrors UIWidgets::PushStyleCombobox (9 colors + 4 vars). ---
void ComboMenu_PushCombobox(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(color.x, color.y, color.z, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
}

void ComboMenu_PopCombobox() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(9);
}

// --- Slider: mirrors UIWidgets::PushStyleSlider (6 colors + 4 vars). ---
void ComboMenu_PushSlider(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(1.0f, 1.0f, 1.0f, 0.4f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void ComboMenu_PopSlider() {
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(6);
}

// --- Button: mirrors UIWidgets::PushStyleButton (4 colors + 3 vars). Default pad (10,8). ---
void ComboMenu_PushButton(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void ComboMenu_PopButton() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
}

// --- Input (stepper): mirrors UIWidgets::PushStyleInput (7 colors + 3 vars). ---
void ComboMenu_PushInput(const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x, color.y, color.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x, color.y, color.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x, color.y, color.z, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.3f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 5.0f);
}

void ComboMenu_PopInput() {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(7);
}

// --- Stylized nav buttons (see header). Selected → solid theme fill; unselected → transparent
//     button (so only hover/active show the accent), matching SOH's transparent-inactive idiom
//     (Menu.cpp:723/850). No frame border: SOH's manual renderer draws a borderless filled rect. ---
static bool StyledNavButton(const char* label, bool selected, const ImVec2& size, const ImVec2& pad) {
    const ImVec4 theme = ComboMenu_ThemeColor();
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? theme : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(theme.x, theme.y, theme.z, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(theme.x, theme.y, theme.z, 0.6f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pad);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool ComboMenu_StyledHeaderEntry(const char* label, bool selected) {
    return StyledNavButton(label, selected, ImVec2(0.0f, 0.0f), ImVec2(10.0f, 8.0f));
}

bool ComboMenu_StyledSidebarEntry(const char* label, bool selected, float width) {
    return StyledNavButton(label, selected, ImVec2(width, 0.0f), ImVec2(10.0f, 6.0f));
}

} // namespace ComboRando
