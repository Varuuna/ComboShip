// combo/gui/ComboExtractScreen.cpp
// ComboShip-owned ROM-extraction screen (see ComboExtractScreen.h).
//
// Why this lives in comboui.dll: the friendly progress UI needs a window + ImGui context, and the
// only window in the process is libultraship's, created by SOH_InitWindowOnly() (the OoT OTRGlobals
// ctor, from soh.o2r) before any ROM exists. comboui already drives ImGui against that shared context
// (ComboMenu.cpp), so it owns the frame loop here. The actual ROM validation + ZAPD work happens in
// soh.dll / 2ship.dll via the function pointers in ComboExtractCallbacks; this file only renders.
#include "ComboExtractScreen.h"
#include "ComboWidgetStyle.h" // ComboShip port style: ComboMenu_ThemeColor / PushButton / PopButton

#include <libultraship/libultraship.h>
#include <fast/Fast3dWindow.h>
#include <imgui.h>

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32") // GetOpenFileNameA — comboui doesn't otherwise link comdlg32
#endif

using ComboRando::ComboMenu_PopButton;
using ComboRando::ComboMenu_PushButton;
using ComboRando::ComboMenu_ThemeColor; // port style helpers (combo/gui/ComboWidgetStyle.h)

namespace {

struct RomSlot {
    const char* label = "";
    bool needed = false; // archive missing -> must extract
    ComboFnValidateRom validate = nullptr;
    ComboFnValidateRom classify = nullptr; // header-only version check for the auto-scan
    ComboFnStartExtraction start = nullptr;
    ComboFnGetProgress progress = nullptr;
    std::string path;
    bool valid = false;
    // extraction runtime state
    bool started = false;
    bool done = false;
    bool success = false;
};

// Native open-file dialog filtered to N64 ROM extensions. Returns "" if cancelled.
std::string PickRomFile() {
#ifdef _WIN32
    char file[MAX_PATH] = { 0 };
    OPENFILENAMEA ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "N64 ROMs (*.z64;*.n64;*.v64)\0*.z64;*.n64;*.v64\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Select ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        return std::string(file);
    }
#endif
    return std::string();
}

// Non-recursive scan of a directory for candidate ROM files.
void ScanForRoms(const std::filesystem::path& dir, std::vector<std::string>& out) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext == ".z64" || ext == ".n64" || ext == ".v64") {
            out.push_back(entry.path().string());
        }
    }
}

// ImGui's GImGui is a per-module global; point comboui's at the shared libultraship context
// (same pattern as ComboMenu.cpp) before any ImGui call.
void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace

extern "C" __declspec(dllexport) int ComboUI_RunExtraction(const ComboExtractCallbacks* cb) {
    if (!cb) {
        return 0;
    }
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

    RomSlot slots[2];
    slots[0].label = "Ocarina of Time";
    slots[0].needed = cb->sohNeeded != 0;
    slots[0].validate = cb->sohValidate;
    slots[0].classify = cb->sohClassify;
    slots[0].start = cb->sohStart;
    slots[0].progress = cb->sohProgress;
    slots[1].label = "Majora's Mask";
    slots[1].needed = cb->mmNeeded != 0;
    slots[1].validate = cb->mmValidate;
    slots[1].classify = cb->mmClassify;
    slots[1].start = cb->mmStart;
    slots[1].progress = cb->mmProgress;

    // Auto-scan the working directory and classify any ROMs found into the needed slots.
    {
        std::vector<std::string> candidates;
        ScanForRoms(std::filesystem::current_path(), candidates);
        for (const auto& c : candidates) {
            for (auto& s : slots) {
                if (!s.needed || s.valid) {
                    continue;
                }
                // Route by the header-only classify (fast); fall back to full validate if unavailable.
                ComboFnValidateRom check = s.classify ? s.classify : s.validate;
                if (check && check(c.c_str())) {
                    s.path = c;
                    s.valid = true;
                    break;
                }
            }
        }
    }

    enum Phase { PICK, EXTRACTING, FAILED };
    Phase phase = PICK;
    int activeSlot = -1; // slot currently extracting, -1 = pick next
    int result = 0;
    bool finished = false;

    while (!finished) {
        if (!ctx->GetWindow()->IsRunning()) {
            result = 0;
            break;
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
        // Dim the game behind, mirroring the menu's translucent fullscreen overlay (ComboMenu.cpp).
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                                                      IM_COL32(0, 0, 0, 210));

        // Preferred 680px, but clamp to the main window's work area so we never bleed outside it
        // (a new player's window can launch small/square before any resolution is persisted).
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
        bool open = ImGui::Begin("ComboShip - ROM Setup", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (open) {
            ImGui::SetWindowFontScale(1.4f);
            ImGui::TextColored(theme, "ComboShip ROM Setup");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Separator();

            if (phase == PICK) {
                ImGui::TextWrapped("ComboShip needs both an Ocarina of Time ROM and a Majora's Mask ROM. "
                                   "Select each ROM below, then click Extract. Both are required.");
                ImGui::Spacing();

                for (int i = 0; i < 2; i++) {
                    RomSlot& s = slots[i];
                    ImGui::PushID(i);
                    ImGui::TextColored(theme, "%s", s.label);
                    if (!s.needed) {
                        ImGui::SameLine();
                        ImGui::TextColored(kGreen, "(already extracted)");
                    } else {
                        ImGui::TextWrapped("%s", s.path.empty() ? "(no ROM selected)" : s.path.c_str());
                        ComboMenu_PushButton(theme);
                        if (ImGui::Button(s.path.empty() ? "Browse..." : "Change")) {
                            std::string picked = PickRomFile();
                            if (!picked.empty()) {
                                s.path = picked;
                                s.valid = s.validate && s.validate(picked.c_str());
                            }
                        }
                        ComboMenu_PopButton();
                        ImGui::SameLine();
                        if (s.path.empty()) {
                            ImGui::TextDisabled("required");
                        } else if (s.valid) {
                            ImGui::TextColored(kGreen, "Valid %s ROM", s.label);
                        } else {
                            ImGui::TextColored(kRed, "Not a valid %s ROM", s.label);
                        }
                    }
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::PopID();
                }

                bool ready = true;
                for (const auto& s : slots) {
                    if (s.needed && !s.valid) {
                        ready = false;
                    }
                }

                if (!ready) {
                    ImGui::BeginDisabled();
                }
                ComboMenu_PushButton(theme);
                if (ImGui::Button("Extract", ImVec2(150.0f, 0.0f))) {
                    phase = EXTRACTING;
                    activeSlot = -1;
                }
                ComboMenu_PopButton();
                if (!ready) {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                ComboMenu_PushButton(theme);
                if (ImGui::Button("Quit", ImVec2(150.0f, 0.0f))) {
                    result = 0;
                    finished = true;
                }
                ComboMenu_PopButton();
            } else if (phase == EXTRACTING) {
                if (activeSlot < 0) {
                    // Pick the next needed-but-unfinished slot, or finish.
                    int next = -1;
                    for (int i = 0; i < 2; i++) {
                        if (slots[i].needed && !slots[i].done) {
                            next = i;
                            break;
                        }
                    }
                    if (next < 0) {
                        bool allOk = true;
                        for (const auto& s : slots) {
                            if (s.needed && !s.success) {
                                allOk = false;
                            }
                        }
                        if (allOk) {
                            result = 1;
                            finished = true;
                        } else {
                            phase = FAILED;
                        }
                    } else {
                        activeSlot = next;
                        slots[next].started = slots[next].start && slots[next].start(slots[next].path.c_str());
                        if (!slots[next].started) {
                            slots[next].done = true;
                            slots[next].success = false;
                            activeSlot = -1;
                        }
                    }
                } else {
                    RomSlot& s = slots[activeSlot];
                    unsigned long long count = 0, total = 0;
                    int done = 0, success = 0;
                    if (s.progress) {
                        s.progress(&count, &total, &done, &success);
                    }
                    float pct = total > 0 ? (float)count / (float)total : 0.0f;
                    ImGui::Text("Extracting %s...", s.label);
                    ImGui::Spacing();
                    char overlay[32];
                    if (count > 0) {
                        snprintf(overlay, sizeof(overlay), "%.0f%%", pct * 100.0f);
                    } else {
                        snprintf(overlay, sizeof(overlay), "Starting Up");
                    }
                    // Themed progress bar (matches OTRGlobals::RunExtract's "ROM Extraction" modal).
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(theme.x, theme.y, theme.z, 0.5f));
                    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(theme.x, theme.y, theme.z, 1.0f));
                    ImGui::ProgressBar(pct, ImVec2(ImGui::GetContentRegionAvail().x, 38.0f), overlay);
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar(1);
                    if (done) {
                        s.done = true;
                        s.success = (success != 0);
                        activeSlot = -1;
                    }
                }
            } else { // FAILED
                ImGui::TextColored(kRed, "Extraction failed. Check that your ROM is a supported, uncompressed "
                                         "copy, then relaunch.");
                ImGui::Spacing();
                ComboMenu_PushButton(theme);
                if (ImGui::Button("Quit", ImVec2(150.0f, 0.0f))) {
                    result = 0;
                    finished = true;
                }
                ComboMenu_PopButton();
            }
        }
        ImGui::End();

        gui->EndDraw();
        wnd->EndFrame();
    }

    return result;
}
