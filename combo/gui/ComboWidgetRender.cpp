// combo/gui/ComboWidgetRender.cpp
//
// ComboShip: render one flat C-ABI widget (CwWidget) with comboui's own ImGui calls over
// the shared libultraship CVar store. Non-declarative slices (preFunc disable-eval,
// custom-draw, post-change callback) are routed BY INDEX into the owning game's exports.
//
// DESIGN NOTE: NO ResourceManager usage here. RM scoping for invoke callbacks is GAME-SIDE
// (inside each game's own invoke/draw/eval export). comboui only calls game.evalDisabled,
// game.invokeCallback and game.drawCustom by index — no shared_ptr crosses the boundary.
#include "ComboWidgetRender.h"
#include "ComboMenuModel.h" // GameMenu (resolved export fn-ptrs)

#include <libultraship/libultraship.h> // ImGui-adjacent + CVar bridge (CVarGet/Set*) + color.h
#include <imgui.h>
#include <cstring>

namespace ComboRando {

void RenderWidget(const CwWidget& w, const GameMenu& game) {
    // Unique id namespace per widget so two widgets sharing a display label don't collide.
    ImGui::PushID(w.index);

    // 1) preFunc disable eval (per frame): the game owns the predicate; we call it by index.
    bool disabled = false;
    const char* reason = "";
    if (w.hasPreFunc && game.evalDisabled) {
        disabled = game.evalDisabled(w.index, &reason) != 0;
    }
    if (disabled) {
        ImGui::BeginDisabled(true);
    }

    // 2) render by kind. `changed` => the backing CVar (or button) changed this frame.
    bool changed = false;
    const char* label = (w.name && w.name[0]) ? w.name : "###unnamed";

    switch (w.kind) {
        case CW_SEPARATOR:
            ImGui::Separator();
            break;
        case CW_SEPARATOR_TEXT:
            ImGui::SeparatorText(w.name ? w.name : "");
            break;
        case CW_TEXT:
            ImGui::TextUnformatted(w.name ? w.name : "");
            break;
        case CW_CHECKBOX: {
            bool v = CVarGetInteger(w.cvar, w.bDefault) != 0;
            if (ImGui::Checkbox(label, &v)) {
                CVarSetInteger(w.cvar, v ? 1 : 0);
                changed = true;
            }
            break;
        }
        case CW_SLIDER_INT: {
            int v = CVarGetInteger(w.cvar, w.iDefault);
            if (ImGui::SliderInt(label, &v, w.iMin, w.iMax)) {
                CVarSetInteger(w.cvar, v);
                changed = true;
            }
            break;
        }
        case CW_SLIDER_FLOAT: {
            float v = CVarGetFloat(w.cvar, w.fDefault);
            if (ImGui::SliderFloat(label, &v, w.fMin, w.fMax)) {
                CVarSetFloat(w.cvar, v);
                changed = true;
            }
            break;
        }
        case CW_COMBOBOX: {
            int v = CVarGetInteger(w.cvar, 0);
            const char* curLabel = "";
            for (int i = 0; i < w.choiceCount; ++i) {
                if (w.choices[i].value == v) {
                    curLabel = w.choices[i].label;
                    break;
                }
            }
            if (ImGui::BeginCombo(label, curLabel)) {
                for (int i = 0; i < w.choiceCount; ++i) {
                    const CwChoice& c = w.choices[i];
                    bool sel = (c.value == v);
                    if (ImGui::Selectable(c.label, sel)) {
                        CVarSetInteger(w.cvar, c.value);
                        changed = true;
                    }
                    if (sel) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
        case CW_BTN_SELECTOR: {
            // Simplified rendering: the game owns the exact cycle semantics (wrap-around,
            // label set). Here we expose the backing CVar as an overflow-safe int stepper —
            // functional and clamped at >= 0. The post-change callback (routed below) lets
            // the game react if it needs to.
            int v = CVarGetInteger(w.cvar, w.iDefault);
            if (ImGui::InputInt(label, &v)) {
                if (v < 0) {
                    v = 0;
                }
                CVarSetInteger(w.cvar, v);
                changed = true;
            }
            break;
        }
        case CW_INPUT_TEXT: {
            // CVarGetString/SetString are available in the bridge, so this round-trips with
            // the game. Seed a local buffer from the CVar; write back on edit. (MM never emits
            // this kind; OOT has a few.)
            char buf[256];
            const char* cur = CVarGetString(w.cvar, "");
            std::strncpy(buf, cur ? cur : "", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText(label, buf, sizeof(buf))) {
                CVarSetString(w.cvar, buf);
                changed = true;
            }
            break;
        }
        case CW_COLOR: {
            // Use the bridge color API so values round-trip with the game's CVar storage.
            if (w.useAlpha) {
                Color_RGBA8 def = { 255, 255, 255, 255 };
                Color_RGBA8 c = CVarGetColor(w.cvar, def);
                float col[4] = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
                if (ImGui::ColorEdit4(label, col)) {
                    c.r = (uint8_t)(col[0] * 255.0f + 0.5f);
                    c.g = (uint8_t)(col[1] * 255.0f + 0.5f);
                    c.b = (uint8_t)(col[2] * 255.0f + 0.5f);
                    c.a = (uint8_t)(col[3] * 255.0f + 0.5f);
                    CVarSetColor(w.cvar, c);
                    changed = true;
                }
            } else {
                Color_RGB8 def = { 255, 255, 255 };
                Color_RGB8 c = CVarGetColor24(w.cvar, def);
                float col[3] = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f };
                if (ImGui::ColorEdit3(label, col)) {
                    c.r = (uint8_t)(col[0] * 255.0f + 0.5f);
                    c.g = (uint8_t)(col[1] * 255.0f + 0.5f);
                    c.b = (uint8_t)(col[2] * 255.0f + 0.5f);
                    CVarSetColor24(w.cvar, c);
                    changed = true;
                }
            }
            break;
        }
        case CW_BUTTON:
            if (ImGui::Button(label)) {
                changed = true; // button => fire callback (below)
            }
            break;
        case CW_WINDOW_BUTTON: {
            // Simplified: toggle the backing window CVar as a checkbox. The full menu uses a
            // styled toggle button; this is functionally equivalent for show/hide. If no CVar
            // is wired we can't toggle anything, so just show the label disabled.
            if (w.cvar && w.cvar[0]) {
                bool v = CVarGetInteger(w.cvar, 0) != 0;
                if (ImGui::Checkbox(label, &v)) {
                    CVarSetInteger(w.cvar, v ? 1 : 0);
                    changed = true;
                }
            } else {
                ImGui::TextDisabled("%s", w.name ? w.name : "");
            }
            break;
        }
        case CW_AUDIO_BACKEND:
            // TODO: needs engine audio-backend enumeration (WindowBackend list). Few of these
            // exist and they don't block the render milestone; wire later.
            ImGui::TextDisabled("%s (audio backend selector — TODO)", w.name ? w.name : "");
            break;
        case CW_VIDEO_BACKEND:
            // TODO: needs engine video-backend enumeration. See CW_AUDIO_BACKEND note.
            ImGui::TextDisabled("%s (video backend selector — TODO)", w.name ? w.name : "");
            break;
        case CW_CUSTOM:
            // The game draws this entirely under its own context/RM, addressed by index.
            if (game.drawCustom) {
                game.drawCustom(w.index);
            }
            break;
        default:
            ImGui::TextDisabled("%s", w.name ? w.name : "");
            break;
    }

    // 3) tooltip on hover (applies to the widget item just drawn).
    if (w.tooltip && w.tooltip[0] && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", w.tooltip);
    }

    // 4) post-change callback, routed by index into the owning game.
    if (changed && w.hasCallback && game.invokeCallback) {
        game.invokeCallback(w.index);
    }

    if (disabled) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

} // namespace ComboRando
