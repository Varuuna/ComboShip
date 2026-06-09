// combo/gui/ComboSharedPanel.cpp
// ComboShip: combo-owned engine-settings panel drawn directly against libultraship.
// Audio volume and controller mapping are per-game and live under the OOT and MM tabs.
#include "ComboMenu.h"
#include <libultraship/libultraship.h>
#include <imgui.h>

namespace ComboRando {

static void SaveCVars() {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void ComboMenu::DrawSharedPanel() {
    auto window = Ship::Context::GetInstance()->GetWindow();

    // ── Graphics ──────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Graphics");

    // Internal resolution multiplier.
    // Window::SetResolutionMultiplier(float multiplier) — confirmed in Window.h.
    float res = CVarGetFloat("gInternalResolution", 1.0f);
    if (ImGui::SliderFloat("Internal Resolution", &res, 0.5f, 2.0f, "%.2fx")) {
        CVarSetFloat("gInternalResolution", res);
        window->SetResolutionMultiplier(res);
        SaveCVars();
    }

    // MSAA sample count.
    // Window::SetMsaaLevel(uint32_t value) — confirmed in Window.h.
    int msaa = static_cast<int>(CVarGetInteger("gMSAAValue", 1));
    if (ImGui::SliderInt("Anti-aliasing (MSAA)", &msaa, 1, 8)) {
        if (msaa < 1) msaa = 1;
        CVarSetInteger("gMSAAValue", msaa);
        window->SetMsaaLevel(static_cast<uint32_t>(msaa));
        SaveCVars();
    }

    // Texture filter — CVar only; engine reads gTextureFilter directly each frame.
    {
        const char* labels[] = { "Three-Point", "Bilinear", "None" };
        int tf = CVarGetInteger("gTextureFilter", 0);
        if (tf < 0 || tf > 2) tf = 0;
        if (ImGui::BeginCombo("Texture Filter", labels[tf])) {
            for (int i = 0; i < 3; ++i) {
                if (ImGui::Selectable(labels[i], i == tf)) {
                    CVarSetInteger("gTextureFilter", i);
                    SaveCVars();
                }
            }
            ImGui::EndCombo();
        }
    }

    // VSync — CVar only; engine applies on next frame.
    bool vsync = CVarGetInteger("gVsyncEnabled", 1) != 0;
    if (ImGui::Checkbox("VSync", &vsync)) {
        CVarSetInteger("gVsyncEnabled", vsync ? 1 : 0);
        SaveCVars();
    }

    // Windowed fullscreen.
    // Window::SetFullscreen(bool isFullscreen) — confirmed in Window.h.
    bool fs = CVarGetInteger("gSdlWindowedFullscreen", 0) != 0;
    if (ImGui::Checkbox("Windowed Fullscreen", &fs)) {
        CVarSetInteger("gSdlWindowedFullscreen", fs ? 1 : 0);
        window->SetFullscreen(fs);
        SaveCVars();
    }

    // Renderer backend: read-only display with restart note.
    // GetWindowBackendName() returns the current backend's human-readable name (no arg) — confirmed in Window.h.
    // GetAvailableWindowBackends() returns shared_ptr<vector<int32_t>> — confirmed in Window.h.
    // There is no public API to get the name of an arbitrary backend ID or to switch backends at runtime,
    // so we display the current backend name read-only and note that changes require a restart.
    {
        std::string backendName = window->GetWindowBackendName();
        ImGui::LabelText("Renderer Backend", "%s", backendName.empty() ? "(unknown)" : backendName.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(applies on restart)");
    }

    // Interpolation FPS — CVar only; engine reads it each frame.
    int fps = CVarGetInteger("gInterpolationFPS", 20);
    if (ImGui::SliderInt("Interpolation FPS", &fps, 20, 360)) {
        CVarSetInteger("gInterpolationFPS", fps);
        SaveCVars();
    }

    // Match refresh rate — CVar only.
    bool matchRefresh = CVarGetInteger("gMatchRefreshRate", 0) != 0;
    if (ImGui::Checkbox("Match Refresh Rate", &matchRefresh)) {
        CVarSetInteger("gMatchRefreshRate", matchRefresh ? 1 : 0);
        SaveCVars();
    }

    // ── Interface ─────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Interface");

    // Menu theme — CVar only; UI reads it on next draw.
    int theme = CVarGetInteger("gSettings.Menu.Theme", 0);
    if (ImGui::SliderInt("Menu Theme", &theme, 0, 7)) {
        CVarSetInteger("gSettings.Menu.Theme", theme);
        SaveCVars();
    }

    // UI scale — CVar only.
    int scale = CVarGetInteger("gSettings.ImGuiScale", 1);
    if (ImGui::SliderInt("UI Scale", &scale, 0, 3)) {
        CVarSetInteger("gSettings.ImGuiScale", scale);
        SaveCVars();
    }

    // Cursor visibility.
    // Window::SetForceCursorVisibility(bool visible) — confirmed as non-virtual on Window base in Window.h.
    bool cursor = CVarGetInteger("gSettings.CursorVisibility", 0) != 0;
    if (ImGui::Checkbox("Always Show Cursor", &cursor)) {
        CVarSetInteger("gSettings.CursorVisibility", cursor ? 1 : 0);
        window->SetForceCursorVisibility(cursor);
        SaveCVars();
    }

    // Menu background opacity — CVar only; ComboMenu::DrawElement() reads it live.
    float opacity = CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.85f);
    if (ImGui::SliderFloat("Menu Background Opacity", &opacity, 0.0f, 1.0f)) {
        CVarSetFloat("gSettings.Menu.BackgroundOpacity", opacity);
        SaveCVars();
    }

    // ── Audio / Controls ──────────────────────────────────────────────────────
    ImGui::SeparatorText("Audio / Controls");
    ImGui::TextWrapped(
        "Audio volume and controller mapping are per-game and live under the OOT and MM tabs.");
}

} // namespace ComboRando
