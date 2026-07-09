// combo/gui/ComboWidgetRender.cpp
//
// ComboShip: render one flat C-ABI widget (CwWidget) with comboui's own ImGui calls over
// the shared libultraship CVar store. Non-declarative slices (preFunc disable-eval,
// custom-draw, post-change callback) are routed BY INDEX into the owning game's exports.
//
// DESIGN NOTE: NO ResourceManager usage here. RM scoping for invoke callbacks is GAME-SIDE
// (inside each game's own invoke/draw/eval export). comboui only calls game.evalDisabled,
// game.invokeCallback and game.drawCustom by index — no shared_ptr crosses the boundary.
//
// KNOWN ISSUE (docs/KNOWN_ISSUES.md): settings whose effect depends on a registered per-page
// MenuUpdate function (e.g. SoH's Advanced Resolution editor — local-var bindings translated to
// effective CVars by UpdateResolutionVars) do NOT apply through this generic CVar-write path.
// Toggling them here writes the UI CVars but not the effective ones, so the viewport never changes.
#include "ComboWidgetRender.h"
#include "ComboMenuModel.h"        // GameMenu (resolved export fn-ptrs)
#include "ComboWidgetStyle.h"      // combo-owned replication of OOT UIWidgets menu styling
#include "ComboAudioBridge.h"      // Shared-tab audio -> MM mirror
#include "ComboResolutionEditor.h" // combo-owned Advanced Resolution editor (intercepts broken widgets)

#include <libultraship/libultraship.h> // ImGui-adjacent + CVar bridge (CVarGet/Set*) + color.h
#include <ship/window/gui/IconsFontAwesome4.h> // ICON_FA_* for the window-toggle button (matches SoH)
#include <imgui.h>
#include <cstring>
#include <string>

namespace ComboRando {

namespace {
// Human-readable backend names — mirror soh/.../MenuTypes.h windowBackendsMap / audioBackendsMap
// (those live per-DLL and aren't linkable from comboui). Window ids are Fast::WindowBackend values.
const char* WindowBackendName(int32_t id) {
    switch (id) {
        case 1:
            return "DirectX"; // FAST3D_DXGI_DX11
        case 2:
            return "OpenGL"; // FAST3D_SDL_OPENGL
        case 3:
            return "Metal"; // FAST3D_SDL_METAL
        default:
            return "Unknown";
    }
}
const char* AudioBackendName(Ship::AudioBackend b) {
    switch (b) {
        case Ship::AudioBackend::WASAPI:
            return "Windows Audio Session API";
        case Ship::AudioBackend::SDL:
            return "SDL";
        case Ship::AudioBackend::COREAUDIO:
            return "Core Audio";
        case Ship::AudioBackend::NUL:
            return "Null";
        default:
            return "Unknown";
    }
}

// CW_VIDEO_BACKEND: render-API selector. Mirrors SoH's WIDGET_VIDEO_BACKEND — writes the config
// and saves; the change takes effect on relaunch (hence the native "Needs reload" label).
void RenderVideoBackend(const CwWidget& w, const ImVec4& themeColor) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetConfig()) {
        ImGui::TextDisabled("%s", (w.name && w.name[0]) ? w.name : "Renderer API");
        return;
    }
    auto window = ctx->GetWindow();
    auto avail = window->GetAvailableWindowBackends();
    int32_t curId = ctx->GetConfig()->GetInt("Window.Backend.Id", -1);
    if (!window->IsAvailableWindowBackend(curId)) {
        curId = window->GetWindowBackend();
    }
    const char* label = (w.name && w.name[0]) ? w.name : "Renderer API";
    const bool only = !avail || avail->size() <= 1;
    if (only)
        ImGui::BeginDisabled(true);
    // Label above the combo (matches SoH), then a hidden-label combo sized to fit the widest option.
    ImGui::TextUnformatted(label);
    ComboMenu_PushCombobox(themeColor);
    const ImGuiStyle& st = ImGui::GetStyle();
    float longest = ImGui::CalcTextSize(WindowBackendName(curId)).x;
    if (avail) {
        for (int32_t id : *avail) {
            float x = ImGui::CalcTextSize(WindowBackendName(id)).x;
            if (x > longest)
                longest = x;
        }
    }
    ImGui::SetNextItemWidth(longest + ImGui::GetFrameHeight() + st.FramePadding.x * 2.0f);
    if (ImGui::BeginCombo("##RendererAPI", WindowBackendName(curId))) {
        if (avail) {
            for (int32_t id : *avail) {
                const bool sel = (id == curId);
                if (ImGui::Selectable(WindowBackendName(id), sel)) {
                    ctx->GetConfig()->SetInt("Window.Backend.Id", id);
                    ctx->GetConfig()->SetString("Window.Backend.Name", WindowBackendName(id));
                    ctx->GetConfig()->Save();
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ComboMenu_PopCombobox();
    if (only)
        ImGui::EndDisabled();
}

// CW_AUDIO_BACKEND: audio-API selector. Mirrors SoH's WIDGET_AUDIO_BACKEND — applies live.
void RenderAudioBackend(const CwWidget& w, const ImVec4& themeColor) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetAudio()) {
        ImGui::TextDisabled("%s", (w.name && w.name[0]) ? w.name : "Audio API");
        return;
    }
    auto audio = ctx->GetAudio();
    auto avail = audio->GetAvailableAudioBackends();
    const Ship::AudioBackend cur = audio->GetCurrentAudioBackend();
    // SoH's MenuDrawItem hardcodes "Audio API" (ignores the widget's "(Needs reload)" name, since
    // the audio backend applies live) — match that rather than using w.name.
    const char* label = "Audio API";
    const bool only = !avail || avail->size() <= 1;
    if (only)
        ImGui::BeginDisabled(true);
    // Label above the combo (matches SoH), then a hidden-label combo sized to fit the widest option
    // (so "Windows Audio Session API" isn't clipped) — mirrors SoH's CalcComboWidth.
    ImGui::TextUnformatted(label);
    ComboMenu_PushCombobox(themeColor);
    const ImGuiStyle& st = ImGui::GetStyle();
    float longest = ImGui::CalcTextSize(AudioBackendName(cur)).x;
    if (avail) {
        for (Ship::AudioBackend b : *avail) {
            float x = ImGui::CalcTextSize(AudioBackendName(b)).x;
            if (x > longest)
                longest = x;
        }
    }
    ImGui::SetNextItemWidth(longest + ImGui::GetFrameHeight() + st.FramePadding.x * 2.0f);
    if (ImGui::BeginCombo("##AudioAPI", AudioBackendName(cur))) {
        if (avail) {
            for (Ship::AudioBackend b : *avail) {
                const bool sel = (b == cur);
                if (ImGui::Selectable(AudioBackendName(b), sel)) {
                    audio->SetCurrentAudioBackend(b);
                }
                if (sel)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ComboMenu_PopCombobox();
    if (only)
        ImGui::EndDisabled();
}
} // namespace

// Widget kinds whose draw we delegate to the OWNING GAME's real UIWidgets (label-above, +/- steppers,
// themed separators, tooltips) so the menu matches upstream exactly and auto-inherits its changes.
// Buttons / window-buttons / backend selectors / custom stay comboui-owned: they either look correct
// already or can touch live subsystems that aren't safe to drive while the game is backgrounded.
static bool DelegatesToGame(CwKind k) {
    switch (k) {
        case CW_CHECKBOX:
        case CW_SLIDER_INT:
        case CW_SLIDER_FLOAT:
        case CW_COMBOBOX:
        case CW_BTN_SELECTOR:
        // CW_INPUT_TEXT (OOT) and CW_COLOR (MM) are intentionally NOT delegated: the respective
        // game's MenuDrawItem has no case for them, so they'd draw nothing. Left on the raw path.
        case CW_SEPARATOR:
        case CW_SEPARATOR_TEXT:
        case CW_TEXT:
            return true;
        default:
            return false;
    }
}

void RenderWidget(const CwWidget& w, const GameMenu& game, int widthBudget) {
    // Unique id namespace per widget so two widgets sharing a display label don't collide.
    ImGui::PushID(w.index);

    // ComboShip: SoH's Advanced Resolution editor doesn't survive the flat C-ABI (dynamic names,
    // ValuePointer combo, custom-draw, per-page MenuUpdate). Intercept those widgets and render a
    // combo-owned replacement over the effective gSettings.AdvancedResolution.* CVars instead.
    if (TryRenderResolutionWidget(w)) {
        ImGui::PopID();
        return;
    }

    // ComboShip: draw declarative/CVar kinds through the owning game's real MenuDrawItem/UIWidgets.
    // The game applies the change (its own callback + ShipInit) internally, so we must NOT re-invoke
    // those here — only the cross-game audio mirror stays comboui's concern. Falls through to the
    // raw path below when the export is absent (older DLL).
    if (game.drawWidget && DelegatesToGame(w.kind)) {
        // Enforce the widget's preFunc disable at the comboui level (wrap in BeginDisabled) — same as
        // the raw path below. The game's MenuDrawItem also applies it internally, but wrapping here is
        // what reliably makes a preFunc-gated control non-interactive (e.g. "Cull Glitch Useful
        // Actors", gated on "Widescreen Actor Culling").
        const bool dis = w.hasPreFunc && game.evalDisabled && game.evalDisabled(w.index, nullptr) != 0;
        if (dis) {
            ImGui::BeginDisabled(true);
        }
        const bool changed = game.drawWidget(w.index, widthBudget) != 0;
        if (dis) {
            ImGui::EndDisabled();
        }
        if (changed && w.cvar && w.cvar[0]) {
            ComboAudio::MirrorIfVolumeCVar(w.cvar);
        }
        ImGui::PopID();
        return;
    }

    // Same-line flag from the model. Delegated widgets get this inside the game's MenuDrawItem;
    // raw-path widgets (buttons/backends) need it applied here (mirrors MenuDrawItem's sameLine).
    if (w.sameLine) {
        ImGui::SameLine();
    }

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

    // Menu theme accent color (read each frame; clamped). Used to theme each widget kind to
    // match the OOT-rendered Shared tab. See ComboWidgetStyle.{h,cpp}.
    const ImVec4 themeColor = ComboMenu_ThemeColor();

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
            ComboMenu_PushCheckbox(themeColor);
            if (ImGui::Checkbox(label, &v)) {
                CVarSetInteger(w.cvar, v ? 1 : 0);
                changed = true;
            }
            ComboMenu_PopCheckbox();
            break;
        }
        case CW_SLIDER_INT: {
            int v = CVarGetInteger(w.cvar, w.iDefault);
            ComboMenu_PushSlider(themeColor);
            // Some sliders embed a printf format in the NAME (e.g. "Master Volume: %d %%"), meant
            // to be the value overlay. Pass it as ImGui's format with a hidden label so the value
            // renders on the slider (full width) instead of a literal "%d" label overflowing. Match
            // an actual integer specifier (not just any '%') so a literal-percent label isn't fed to
            // vsnprintf as a bogus format.
            if (w.name && (std::strstr(w.name, "%d") || std::strstr(w.name, "%i"))) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderInt("##v", &v, w.iMin, w.iMax, w.name)) {
                    CVarSetInteger(w.cvar, v);
                    changed = true;
                }
            } else if (ImGui::SliderInt(label, &v, w.iMin, w.iMax)) {
                CVarSetInteger(w.cvar, v);
                changed = true;
            }
            ComboMenu_PopSlider();
            break;
        }
        case CW_SLIDER_FLOAT: {
            float v = CVarGetFloat(w.cvar, w.fDefault);
            ComboMenu_PushSlider(themeColor);
            // As CW_SLIDER_INT: a float specifier in the name is the value overlay format.
            if (w.name && (std::strstr(w.name, "%f") || std::strstr(w.name, "%.") || std::strstr(w.name, "%g") ||
                           std::strstr(w.name, "%e"))) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##v", &v, w.fMin, w.fMax, w.name)) {
                    CVarSetFloat(w.cvar, v);
                    changed = true;
                }
            } else if (ImGui::SliderFloat(label, &v, w.fMin, w.fMax)) {
                CVarSetFloat(w.cvar, v);
                changed = true;
            }
            ComboMenu_PopSlider();
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
            ComboMenu_PushCombobox(themeColor);
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
            ComboMenu_PopCombobox();
            break;
        }
        case CW_BTN_SELECTOR: {
            // Simplified rendering: the game owns the exact cycle semantics (wrap-around,
            // label set). Here we expose the backing CVar as an overflow-safe int stepper —
            // functional and clamped at >= 0. The post-change callback (routed below) lets
            // the game react if it needs to. Themed with the frame-input style so the field +
            // +/- steppers pick up the menu accent color.
            int v = CVarGetInteger(w.cvar, w.iDefault);
            ComboMenu_PushInput(themeColor);
            if (ImGui::InputInt(label, &v)) {
                if (v < 0) {
                    v = 0;
                }
                CVarSetInteger(w.cvar, v);
                changed = true;
            }
            ComboMenu_PopInput();
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
        case CW_BUTTON: {
            ComboMenu_PushButton(themeColor);
            // These span the full column width (matches SoH); other buttons keep their natural (text)
            // width so e.g. Generate/Randomize stay side-by-side.
            const bool fullWidth = w.name && (std::strcmp(w.name, "Open App Files Folder") == 0 ||
                                              std::strcmp(w.name, "Toggle Fullscreen") == 0 ||
                                              std::strcmp(w.name, "Test Notification") == 0);
            if (ImGui::Button(label, fullWidth ? ImVec2(-FLT_MIN, 0.0f) : ImVec2(0.0f, 0.0f))) {
                changed = true; // button => fire callback (below)
            }
            ComboMenu_PopButton();
            break;
        }
        case CW_WINDOW_BUTTON: {
            // Button with an open/close icon (matches SoH's WindowButton), NOT a checkbox. Toggle via
            // the window OBJECT: GuiWindow gates Draw() on mIsVisible and only reads the CVar at
            // construction, so a CVar-only toggle never shows/hides at runtime. ToggleVisibility() is a
            // libultraship method (no game-DLL ImGui context needed).
            if (w.cvar && w.cvar[0]) {
                const bool open = CVarGetInteger(w.cvar, 0) != 0;
                std::string txt = std::string(open ? ICON_FA_WINDOW_CLOSE : ICON_FA_EXTERNAL_LINK_SQUARE) + " " +
                                  ((w.name && w.name[0]) ? w.name : "");
                ComboMenu_PushButton(themeColor);
                if (ImGui::Button(txt.c_str())) {
                    std::shared_ptr<Ship::GuiWindow> win;
                    if (w.windowName && w.windowName[0]) {
                        auto ctx = Ship::Context::GetInstance();
                        if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui())
                            win = ctx->GetWindow()->GetGui()->GetGuiWindow(w.windowName);
                    }
                    if (win) {
                        win->ToggleVisibility();
                    } else {
                        CVarSetInteger(w.cvar, open ? 0 : 1); // fallback: no window object to drive
                    }
                    changed = true;
                }
                ComboMenu_PopButton();
            } else {
                ImGui::TextDisabled("%s", w.name ? w.name : "");
            }
            break;
        }
        case CW_AUDIO_BACKEND:
            RenderAudioBackend(w, themeColor);
            break;
        case CW_VIDEO_BACKEND:
            RenderVideoBackend(w, themeColor);
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

    // 5) re-apply the changed CVar live: re-run the owning game's ShipInit func(s) registered for it,
    // mirroring what the games' native UIWidgets do. Without this, enhancements/settings toggled in
    // the combo menu only take effect on the next ShipInit::InitAll (game boot / new save).
    if (changed && w.cvar && w.cvar[0] && game.applyCVarChange) {
        game.applyCVarChange(w.cvar);
    }

    // 6) ComboShip: the Shared tab's audio sliders are OOT's (gSettings.Volume.*); mirror them into
    // MM's gSettings.Audio.* so a single Shared Audio section drives both games (see ComboAudioBridge).
    if (changed && w.cvar && w.cvar[0]) {
        ComboAudio::MirrorIfVolumeCVar(w.cvar);
    }

    if (disabled) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}

void RenderSidebarWidgets(const CwSidebar& side, const GameMenu& game) {
    const uint32_t cols = side.columnCount > 0 ? side.columnCount : 1;
    const int widthBudget = 90 / (int)cols; // label WrappedText budget, matching the native menu
    if (cols <= 1) {
        for (int i = 0; i < side.widgetCount; ++i) {
            RenderWidget(side.widgets[i], game, widthBudget);
        }
        return;
    }
    // Per-column child windows (mirrors the native menu): each column scrolls INDEPENDENTLY within
    // the section height, so a long column (e.g. Menu Settings) gets its own scrollbar while a short
    // one (About) does not — instead of one scrollbar for the whole section. Widgets are flattened
    // column-major (ComboMenuExport.h) and carry CwWidget.column.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float sectionW = ImGui::GetContentRegionAvail().x;
    const float colH = ImGui::GetContentRegionAvail().y;
    const float colW = (sectionW - st.ItemSpacing.x * (cols - 1)) / (float)cols;
    for (uint32_t c = 0; c < cols; ++c) {
        ImGui::PushID((int)c);
        ImGui::BeginChild("##seccol", ImVec2(colW, colH), 0, ImGuiWindowFlags_NoTitleBar);
        for (int i = 0; i < side.widgetCount; ++i) {
            int wc = side.widgets[i].column;
            if (wc < 0) {
                wc = 0;
            } else if ((uint32_t)wc >= cols) {
                wc = (int)cols - 1; // clamp stray indices into the last column
            }
            if ((uint32_t)wc == c) {
                RenderWidget(side.widgets[i], game, widthBudget);
            }
        }
        ImGui::EndChild();
        ImGui::PopID();
        if (c + 1 < cols) {
            ImGui::SameLine();
        }
    }
}

} // namespace ComboRando
