// combo/gui/ComboSettingsImportScreen.cpp
// ComboShip-owned first-launch settings-import screen (see ComboSettingsImportScreen.h). Structurally
// a trimmed ComboExtractScreen: one PICK phase, two optional slots (SoH / 2Ship), Browse + auto-detect,
// Import / Skip. The launcher does the JSON parse + merge + apply; this file only renders and reports
// the chosen paths.
#include "ComboSettingsImportScreen.h"
#include "ComboWidgetStyle.h" // ComboMenu_ThemeColor / PushButton / PopButton

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <imgui.h>

#include <string>
#include <filesystem>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32")
#endif

using ComboRando::ComboMenu_PopButton;
using ComboRando::ComboMenu_PushButton;
using ComboRando::ComboMenu_ThemeColor;

namespace {

struct ConfigSlot {
    const char* label = "";
    const char* fileName = ""; // expected filename for auto-detect
    ComboFnValidateConfig validate = nullptr;
    std::string path;
    bool valid = false;
};

// Native open-file dialog filtered to .json. Returns "" if cancelled.
std::string PickConfigFile() {
#ifdef _WIN32
    char file[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Settings (*.json)\0*.json\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select settings file";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        return std::string(file);
    }
#endif
    return std::string();
}

void CopyPath(char* dst, size_t cap, const std::string& src) {
    std::strncpy(dst, src.c_str(), cap - 1);
    dst[cap - 1] = '\0';
}

void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace

extern "C" __declspec(dllexport) int ComboUI_RunSettingsImport(const ComboSettingsImportCallbacks* cb,
                                                               ComboSettingsImportResult* out) {
    if (!out) {
        return 0;
    }
    out->action = 0;
    out->sohPath[0] = '\0';
    out->mmPath[0] = '\0';

    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return 0;
    }
    auto wnd = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow());
    if (!wnd) {
        return 0;
    }
    auto gui = wnd->GetGui();
    UseSharedImGuiContext();

    ConfigSlot slots[2];
    slots[0].label = "Ship of Harkinian (OoT)";
    slots[0].fileName = "shipofharkinian.json";
    slots[0].validate = cb ? cb->sohValidate : nullptr;
    slots[1].label = "2 Ship 2 Harkinian (MM)";
    slots[1].fileName = "2ship2harkinian.json";
    slots[1].validate = cb ? cb->mmValidate : nullptr;

    // Auto-detect each game's config in the working dir (same convenience as the ROM auto-scan).
    for (auto& s : slots) {
        std::error_code ec;
        if (std::filesystem::exists(s.fileName, ec)) {
            s.path = std::filesystem::absolute(s.fileName, ec).string();
            s.valid = !s.validate || s.validate(s.path.c_str()) != 0;
        }
    }

    bool finished = false;
    while (!finished) {
        if (!ctx->GetWindow()->IsRunning()) {
            return 0; // window closed -> launcher treats as Skip
        }
        wnd->HandleEvents();
        if (!wnd->IsFrameReady()) {
            continue;
        }

        UseSharedImGuiContext();
        gui->StartDraw();
        wnd->StartFrame();
        wnd->RunGuiOnly();

        const ImVec4 theme = ComboMenu_ThemeColor();
        const ImVec4 kGreen(0.5f, 1.0f, 0.5f, 1.0f);
        const ImVec4 kRed(1.0f, 0.4f, 0.4f, 1.0f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 210));

        const float kMargin = 20.0f;
        float panelW = vp->WorkSize.x - kMargin * 2.0f;
        if (panelW > 680.0f) {
            panelW = 680.0f;
        }
        float panelMaxH = vp->WorkSize.y - kMargin * 2.0f;
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(panelW, 0.0f), ImVec2(panelW, panelMaxH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09f, 0.09f, 0.11f, 0.98f));
        bool open = ImGui::Begin("ComboShip - Import Settings", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (open) {
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(theme, "ComboShip Settings Import");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();

            ImGui::TextWrapped("Optional: bring over your settings from an existing Ship of Harkinian and/or "
                               "2 Ship 2 Harkinian install. Pick one or both files, or Skip to start fresh. "
                               "Where both set the same option (e.g. graphics or controls), OoT wins.");
            ImGui::Spacing();

            for (int i = 0; i < 2; i++) {
                ConfigSlot& s = slots[i];
                ImGui::PushID(i);
                ImGui::TextColored(theme, "%s", s.label);
                ImGui::TextWrapped("%s", s.path.empty() ? "(none selected)" : s.path.c_str());
                ComboMenu_PushButton(theme);
                if (ImGui::Button(s.path.empty() ? "Browse..." : "Change")) {
                    std::string picked = PickConfigFile();
                    if (!picked.empty()) {
                        s.path = picked;
                        s.valid = !s.validate || s.validate(picked.c_str()) != 0;
                    }
                }
                ComboMenu_PopButton();
                if (!s.path.empty()) {
                    ImGui::SameLine();
                    if (s.valid) {
                        ImGui::TextColored(kGreen, "Looks like a %s config", s.label);
                    } else {
                        ImGui::TextColored(kRed, "May not be a %s config", s.label);
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::PopID();
            }

            ComboMenu_PushButton(theme);
            if (ImGui::Button("Import", ImVec2(150.0f, 0.0f))) {
                out->action = 1;
                CopyPath(out->sohPath, sizeof(out->sohPath), slots[0].path);
                CopyPath(out->mmPath, sizeof(out->mmPath), slots[1].path);
                finished = true;
            }
            ComboMenu_PopButton();
            ImGui::SameLine();
            ComboMenu_PushButton(theme);
            if (ImGui::Button("Skip", ImVec2(150.0f, 0.0f))) {
                out->action = 0;
                finished = true;
            }
            ComboMenu_PopButton();
        }
        ImGui::End();

        gui->EndDraw();
        wnd->EndFrame();
    }

    return 1;
}
