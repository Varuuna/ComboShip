// combo/gui/ComboResolutionEditor.cpp — see ComboResolutionEditor.h for the why.
#include "ComboResolutionEditor.h"
#include "ComboWidgetStyle.h" // themed Push/Pop helpers + ComboMenu_ThemeColor

#include <libultraship/libultraship.h> // Ship::Context, CVar bridge, Gui::SaveConsoleVariablesNextFrame
#include <fast/Fast3dWindow.h>         // Fast::Fast3dWindow::GetInterpreterWeak
#include <fast/interpreter.h>          // Fast::Interpreter dims
#include <imgui.h>
#include <cstring>
#include <memory>

namespace ComboRando {

namespace {

// Effective CVar prefix — top-level CMake/lus-cvars.cmake sets this for both soh and libultraship,
// overriding libultraship's "gAdvancedResolution" default. String-literal concatenation builds the
// full names (e.g. CV_ADVRES ".Enabled").
#define CV_ADVRES "gSettings.AdvancedResolution"
#define CV_LOWRES "gSettings.LowResMode"

// Preset tables transcribed from ResolutionEditor.cpp (kept in sync per UPSTREAM_MERGES).
const char* kAspectLabels[] = {
    "Off", "Custom", "Original (4:3)", "Widescreen (16:9)", "Nintendo 3DS (5:3)", "16:10 (8:5)", "Ultrawide (21:9)"
};
const float kAspectX[] = { 0.0f, 16.0f, 4.0f, 16.0f, 5.0f, 16.0f, 21.0f };
const float kAspectY[] = { 0.0f, 9.0f, 3.0f, 9.0f, 3.0f, 10.0f, 9.0f };
const int kAspectCustom = 1; // "Custom" — the index that shows the manual X/Y sliders

const char* kPixelLabels[] = { "Custom",     "Native N64 (240p)", "2x (480p)",       "3x (720p)", "4x (960p)",
                               "5x (1200p)", "6x (1440p)",        "Full HD (1080p)", "4K (2160p)" };
const int kPixelValues[] = { 480, 240, 480, 720, 960, 1200, 1440, 1080, 2160 };
const int kPixelCustom = 0;

const int kMinVert = 240;  // SCREEN_HEIGHT
const int kMaxVert = 4320; // 8K
const int kDefaultMaxScale = 6;

void Save() {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

// Live render dimensions from the Fast3d interpreter (same source SoH's readouts use). Read on the
// overlay's render thread, which is the thread that runs the interpreter (as SoH's MenuUpdate does);
// a torn read would be a one-frame cosmetic readout, so no synchronization is needed.
bool GetDims(uint32_t& vpW, uint32_t& vpH, uint32_t& intW, uint32_t& intH) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow()) {
        return false;
    }
    auto fast = std::dynamic_pointer_cast<Fast::Fast3dWindow>(ctx->GetWindow());
    if (!fast) {
        return false;
    }
    auto interp = fast->GetInterpreterWeak().lock();
    if (!interp) {
        return false;
    }
    vpW = interp->mGameWindowViewport.width;
    vpH = interp->mGameWindowViewport.height;
    intW = interp->mCurDimensions.width;
    intH = interp->mCurDimensions.height;
    return true;
}

// Max integer scale factor for the current viewport/internal ratio (mirrors UpdateResolutionVars).
int ComputeMaxIntegerScale() {
    uint32_t vpW, vpH, inW, inH;
    if (!GetDims(vpW, vpH, inW, inH) || inW == 0 || inH == 0 || vpH == 0) {
        return kDefaultMaxScale;
    }
    int bounds = ((float)vpW / vpH > (float)inW / inH) ? (int)(vpH / inH) : (int)(vpW / inW);
    if (bounds < 1) {
        bounds = 1;
    }
    return kDefaultMaxScale < bounds ? bounds : kDefaultMaxScale;
}

bool AdvancedEnabled() {
    return CVarGetInteger(CV_ADVRES ".Enabled", 0) != 0;
}

void RenderReadout(const char* name) {
    uint32_t vpW, vpH, inW, inH;
    const bool ok = GetDims(vpW, vpH, inW, inH);
    const bool viewport = (std::strncmp(name, "Viewport", 8) == 0);
    if (!ok) {
        ImGui::TextUnformatted(viewport ? "Viewport dimensions: (unavailable)" : "Internal resolution: (unavailable)");
    } else if (viewport) {
        ImGui::Text("Viewport dimensions: %u x %u", vpW, vpH);
    } else {
        ImGui::Text("Internal resolution: %u x %u", inW, inH);
    }
}

void RenderEnable(const char* name) {
    bool en = AdvancedEnabled();
    ComboMenu_PushCheckbox(ComboMenu_ThemeColor());
    if (ImGui::Checkbox((name && name[0]) ? name : "Enable advanced settings.", &en)) {
        CVarSetInteger(CV_ADVRES ".Enabled", en ? 1 : 0);
        Save();
    }
    ComboMenu_PopCheckbox();
}

void RenderAspect() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    ImGui::BeginDisabled(!AdvancedEnabled());

    int item = CVarGetInteger(CV_ADVRES ".UIComboItem.AspectRatio", 3); // SoH default: Widescreen
    if (item < 0 || item >= IM_ARRAYSIZE(kAspectLabels)) {
        item = kAspectCustom;
    }
    ComboMenu_PushCombobox(theme);
    if (ImGui::BeginCombo("Aspect Ratio", kAspectLabels[item])) {
        for (int i = 0; i < IM_ARRAYSIZE(kAspectLabels); ++i) {
            const bool sel = (i == item);
            if (ImGui::Selectable(kAspectLabels[i], sel)) {
                CVarSetInteger(CV_ADVRES ".UIComboItem.AspectRatio", i);
                if (i != kAspectCustom) { // presets write X/Y directly (Off => 0,0); Custom uses sliders
                    CVarSetFloat(CV_ADVRES ".AspectRatioX", kAspectX[i]);
                    CVarSetFloat(CV_ADVRES ".AspectRatioY", kAspectY[i]);
                }
                Save();
            }
            if (sel) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ComboMenu_PopCombobox();

    if (item == kAspectCustom) { // manual aspect: X/Y sliders (folds in the AspectRatioCustom widget)
        float x = CVarGetFloat(CV_ADVRES ".AspectRatioX", 16.0f);
        float y = CVarGetFloat(CV_ADVRES ".AspectRatioY", 9.0f);
        ComboMenu_PushSlider(theme);
        if (ImGui::SliderFloat("X", &x, 0.1f, 32.0f, "%.3f")) {
            CVarSetFloat(CV_ADVRES ".AspectRatioX", x);
            Save();
        }
        if (ImGui::SliderFloat("Y", &y, 0.1f, 24.0f, "%.3f")) {
            CVarSetFloat(CV_ADVRES ".AspectRatioY", y);
            Save();
        }
        ComboMenu_PopSlider();
    }
    ImGui::EndDisabled();
}

void RenderMore() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    ImGui::BeginDisabled(!AdvancedEnabled());

    bool vToggle = CVarGetInteger(CV_ADVRES ".VerticalResolutionToggle", 0) != 0;
    ComboMenu_PushCheckbox(theme);
    if (ImGui::Checkbox("Set fixed vertical resolution (disables resolution slider)", &vToggle)) {
        CVarSetInteger(CV_ADVRES ".VerticalResolutionToggle", vToggle ? 1 : 0);
        Save();
    }
    ComboMenu_PopCheckbox();

    const bool pixelDisabled = !vToggle;
    ImGui::BeginDisabled(pixelDisabled);

    int pItem = CVarGetInteger(CV_ADVRES ".UIComboItem.PixelCount", kPixelCustom);
    if (pItem < 0 || pItem >= IM_ARRAYSIZE(kPixelLabels)) {
        pItem = kPixelCustom;
    }
    ComboMenu_PushCombobox(theme);
    if (ImGui::BeginCombo("Pixel Count Presets", kPixelLabels[pItem])) {
        for (int i = 0; i < IM_ARRAYSIZE(kPixelLabels); ++i) {
            const bool sel = (i == pItem);
            if (ImGui::Selectable(kPixelLabels[i], sel)) {
                CVarSetInteger(CV_ADVRES ".UIComboItem.PixelCount", i);
                if (i != kPixelCustom) {
                    CVarSetInteger(CV_ADVRES ".VerticalPixelCount", kPixelValues[i]);
                }
                Save();
            }
            if (sel) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ComboMenu_PopCombobox();

    int vpc = CVarGetInteger(CV_ADVRES ".VerticalPixelCount", 480);
    ComboMenu_PushInput(theme);
    if (ImGui::InputInt("Vertical Pixel Count", &vpc, 8, 240)) {
        if (vpc < kMinVert) {
            vpc = kMinVert;
        } else if (vpc > kMaxVert) {
            vpc = kMaxVert;
        }
        CVarSetInteger(CV_ADVRES ".VerticalPixelCount", vpc);
        CVarSetInteger(CV_ADVRES ".UIComboItem.PixelCount", kPixelCustom);
        Save();
    }
    ComboMenu_PopInput();
    ImGui::EndDisabled(); // pixelDisabled

    if (ImGui::CollapsingHeader("Integer Scaling Settings")) {
        const bool ppm = CVarGetInteger(CV_ADVRES ".PixelPerfectMode", 0) != 0;
        const bool fitAuto = CVarGetInteger(CV_ADVRES ".IntegerScale.FitAutomatically", 0) != 0;

        bool ppmV = ppm;
        ComboMenu_PushCheckbox(theme);
        ImGui::BeginDisabled(pixelDisabled); // Pixel Perfect needs the fixed-vertical-resolution toggle
        if (ImGui::Checkbox("Pixel Perfect Mode", &ppmV)) {
            CVarSetInteger(CV_ADVRES ".PixelPerfectMode", ppmV ? 1 : 0);
            Save();
        }
        ImGui::EndDisabled();
        ComboMenu_PopCheckbox();

        int factor = CVarGetInteger(CV_ADVRES ".IntegerScale.Factor", 1);
        const int maxFactor = ComputeMaxIntegerScale();
        if (factor > maxFactor) {
            factor = maxFactor;
        }
        ComboMenu_PushSlider(theme);
        ImGui::BeginDisabled(!ppm || fitAuto);
        if (ImGui::SliderInt("Integer scale factor", &factor, 1, maxFactor)) {
            CVarSetInteger(CV_ADVRES ".IntegerScale.Factor", factor);
            Save();
        }
        ImGui::EndDisabled();
        ComboMenu_PopSlider();

        bool fitAutoV = fitAuto;
        ComboMenu_PushCheckbox(theme);
        ImGui::BeginDisabled(!ppm);
        if (ImGui::Checkbox("Automatically scale image to fit viewport", &fitAutoV)) {
            CVarSetInteger(CV_ADVRES ".IntegerScale.FitAutomatically", fitAutoV ? 1 : 0);
            Save();
        }
        ImGui::EndDisabled();
        ComboMenu_PopCheckbox();
    }
    ImGui::EndDisabled(); // !enabled
}

} // namespace

bool TryRenderResolutionWidget(const CwWidget& w) {
    const char* name = w.name ? w.name : "";
    const char* cvar = w.cvar ? w.cvar : "";

    // Enable checkbox — owned so its write persists (SaveConsoleVariablesNextFrame).
    if (w.kind == CW_CHECKBOX && std::strcmp(cvar, CV_ADVRES ".Enabled") == 0) {
        RenderEnable(name);
        return true;
    }
    // Live dimension readouts (SoH sets these names per-frame via PreFunc; the snapshot is a template).
    // Kind-guarded so an unrelated control whose label merely contains these words isn't swallowed.
    if (w.kind == CW_TEXT && (std::strstr(name, "Viewport dimensions") || std::strstr(name, "Internal resolution"))) {
        RenderReadout(name);
        return true;
    }
    // Aspect ratio combobox — the broken ValuePointer combo (CW_COMBOBOX with no backing CVar).
    if (w.kind == CW_COMBOBOX && cvar[0] == '\0' && std::strcmp(name, "Aspect Ratio") == 0) {
        RenderAspect();
        return true;
    }
    // Custom X/Y are folded into RenderAspect; suppress the standalone custom widget.
    if (std::strcmp(name, "AspectRatioCustom") == 0) {
        return true;
    }
    // The vertical-resolution + integer-scaling block.
    if (std::strcmp(name, "MoreResolutionSettings") == 0) {
        RenderMore();
        return true;
    }
    // Advisory widgets whose PreFunc-driven visibility never runs in the overlay:
    //  - FPS-drop notice: hidden (combo can't cheaply compute the interpolation target FPS).
    //  - N64-mode notice + disable button: shown only while N64 (low-res) mode is active.
    if (w.kind == CW_TEXT && std::strstr(name, "frame rate (FPS) drops")) {
        return true; // hide
    }
    if ((w.kind == CW_TEXT && std::strstr(name, "is overriding these settings")) ||
        (w.kind == CW_BUTTON && std::strcmp(name, "Click to disable N64 mode") == 0)) {
        if (CVarGetInteger(CV_LOWRES, 0)) {
            if (w.kind == CW_BUTTON) {
                ComboMenu_PushButton(ComboMenu_ThemeColor());
                if (ImGui::Button(name)) {
                    CVarSetInteger(CV_LOWRES, 0);
                    Save();
                }
                ComboMenu_PopButton();
            } else {
                ImGui::TextUnformatted(name);
            }
        }
        return true;
    }
    return false;
}

} // namespace ComboRando
