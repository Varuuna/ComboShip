// combo/gui/ComboTimersWindow.cpp — see ComboTimersWindow.h
#include "ComboTimersWindow.h"
#include "ComboForeground.h"    // foreground game gates the OOT-only rows
#include "ComboTrackerCommon.h" // OotActiveSlot + the SetTracker CVar/window lever
#include "ComboWidgetStyle.h"
#include <fast/Fast3dGui.h> // the games' digit/icon textures live in the shared Fast3d cache
#include <imgui.h>
#include <libultraship/libultraship.h> // CVar bridge
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/IconsFontAwesome4.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h> // GetModuleHandleA/GetProcAddress for the SOH_/MM_ timer exports
#endif

namespace ComboRando {

namespace {

constexpr const char* kCvarEnabled = "gCombo.Timers.Enabled";
constexpr const char* kCvarTotal = "gCombo.Timers.TotalPlayTime";
constexpr const char* kCvarPerGame = "gCombo.Timers.PerGame";
constexpr const char* kCvarTimeOfDay = "gCombo.Timers.TimeOfDay";
constexpr const char* kCvarNavi = "gCombo.Timers.Navi";
constexpr const char* kCvarConditional = "gCombo.Timers.Conditional";
constexpr const char* kCvarScale = "gCombo.Timers.Scale";
constexpr const char* kCvarHideBg = "gCombo.Timers.HideBackground";
constexpr const char* kCvarSeeded = "gCombo.Timers.Seeded";
constexpr const char* kWindowName = "Timers##Combo";

// Both games' overlays, retired in combo builds. OOT's Draw reads its CVar directly and MM's reads
// IsVisible(), so SetTracker's write-both is what actually covers the pair.
constexpr ComboTracker::Win kNativeOverlays[2] = {
    { "gOpenWindows.TimeDisplayEnabled", "Additional Timers" },
    { "gWindows.DisplayOverlay", "Display Overlay" },
};

const ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
const ImVec4 kRed(1.0f, 0.05f, 0.0f, 1.0f);
const ImVec4 kBlue(0.0f, 0.88f, 1.0f, 1.0f);
const ImVec4 kGreen(0.52f, 1.0f, 0.23f, 1.0f);
const ImVec4 kGrey(0.78f, 0.78f, 0.78f, 1.0f);

// Loaded by OOT's TimeDisplayWindow::InitElement into the SHARED Fast3d texture cache, which is why
// that window stays registered even though its draw is suppressed. Index 10 is the colon/dot.
constexpr const char* kDigits[11] = { "DIGIT_0_TEXTURE", "DIGIT_1_TEXTURE", "DIGIT_2_TEXTURE", "DIGIT_3_TEXTURE",
                                      "DIGIT_4_TEXTURE", "DIGIT_5_TEXTURE", "DIGIT_6_TEXTURE", "DIGIT_7_TEXTURE",
                                      "DIGIT_8_TEXTURE", "DIGIT_9_TEXTURE", "COLON_TEXTURE" };

// Set by the launcher when both games are beaten; tints the total green, like MM's own overlay.
std::atomic<int> sRunComplete{ 0 };

struct Row {
    const char* icon = nullptr;  // Fast3d texture key, or null to use label
    const char* label = nullptr; // column-1 text for rows with no icon of their own
    char text[24] = {};
    ImVec4 color = kWhite;
};

std::vector<Row> sRows;
float sScale = 1.0f;

// Lazily resolved once per process; a missing symbol (stale DLL) just zeroes that game's half.
struct GameFns {
    uint64_t (*ootPlaytimeDs)(void) = nullptr;
    int (*ootOverlay)(uint32_t*, int32_t*, int32_t*, uint32_t*, int32_t*, int32_t*) = nullptr;
    uint64_t (*mmPlaytimeMs)(void) = nullptr;
};

const GameFns& Fns() {
    static GameFns fns;
    static bool tried = false;
    if (!tried) {
        tried = true;
#ifdef _WIN32
        if (HMODULE soh = GetModuleHandleA("soh.dll")) {
            fns.ootPlaytimeDs = (uint64_t(*)(void))GetProcAddress(soh, "SOH_GetPlaytimeDeciseconds");
            fns.ootOverlay = (int (*)(uint32_t*, int32_t*, int32_t*, uint32_t*, int32_t*, int32_t*))GetProcAddress(
                soh, "SOH_GetOverlayTimers");
        }
        if (HMODULE mm = GetModuleHandleA("2ship.dll")) {
            fns.mmPlaytimeMs = (uint64_t(*)(void))GetProcAddress(mm, "MM_GetPlaytimeMs");
        }
#endif
    }
    return fns;
}

std::shared_ptr<Fast::Fast3dGui> Gui3d() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<Fast::Fast3dGui>(ctx->GetWindow()->GetGui());
}

// H:MM:SS.d — same shape as OOT's formatTimeDisplay and MM's Ship_FormatTimeDisplay.
void FormatDeciseconds(uint64_t ds, char* out, size_t n) {
    uint64_t sec = ds / 10;
    snprintf(out, n, "%llu:%02llu:%02llu.%llu", (unsigned long long)(sec / 3600), (unsigned long long)((sec / 60) % 60),
             (unsigned long long)(sec % 60), (unsigned long long)(ds % 10));
}

void FormatMinSec(uint32_t seconds, char* out, size_t n) {
    snprintf(out, n, "%02u:%02u", seconds / 60, seconds % 60);
}

// Takes the text by value rather than handing back a reference — sRows can reallocate on the next add.
void AddRow(const char* icon, const char* label, const ImVec4& color, const char* text) {
    sRows.emplace_back();
    Row& r = sRows.back();
    r.icon = icon;
    r.label = label;
    r.color = color;
    snprintf(r.text, sizeof(r.text), "%s", text);
}

// Combined play time in deciseconds: each game's own saved value, and only one of them ever
// advances at a time. OOT keeps its truncating frame math so the number matches its own displays.
uint64_t CombinedDeciseconds(uint64_t* ootOut, uint64_t* mmOut) {
    const GameFns& fns = Fns();
    uint64_t oot = fns.ootPlaytimeDs ? fns.ootPlaytimeDs() : 0;
    uint64_t mm = fns.mmPlaytimeMs ? fns.mmPlaytimeMs() / 100 : 0;
    *ootOut = oot;
    *mmOut = mm;
    return oot + mm;
}

void BuildRows() {
    sRows.clear();

    const bool finished = sRunComplete.load(std::memory_order_relaxed) != 0;
    uint64_t ootDs = 0;
    uint64_t mmDs = 0;
    const uint64_t totalDs = CombinedDeciseconds(&ootDs, &mmDs);

    char buf[24];
    if (CVarGetInteger(kCvarTotal, 1) != 0) {
        FormatDeciseconds(totalDs, buf, sizeof(buf));
        AddRow("GAMEPLAY_TIMER", nullptr, finished ? kGreen : kWhite, buf);
    }
    if (CVarGetInteger(kCvarPerGame, 0) != 0) {
        FormatDeciseconds(ootDs, buf, sizeof(buf));
        AddRow(nullptr, "OOT", kGrey, buf);
        FormatDeciseconds(mmDs, buf, sizeof(buf));
        AddRow(nullptr, "MM", kGrey, buf);
    }

    // OOT-only rows: MM has no equivalent, and the values would be stale while OOT is dormant.
    const bool wantOot = CVarGetInteger(kCvarTimeOfDay, 0) != 0 || CVarGetInteger(kCvarNavi, 0) != 0 ||
                         CVarGetInteger(kCvarConditional, 0) != 0;
    if (!wantOot || ComboUI::GetForegroundGame() != 0) {
        return;
    }
    const GameFns& fns = Fns();
    uint32_t dayTime = 0;
    int32_t isDay = 0;
    int32_t naviPhase = 0;
    uint32_t naviTicks = 0;
    int32_t timerKind = 0;
    int32_t timerSeconds = 0;
    if (!fns.ootOverlay || !fns.ootOverlay(&dayTime, &isDay, &naviPhase, &naviTicks, &timerKind, &timerSeconds)) {
        return; // no PlayState (title/file select, or mid-transition)
    }

    if (CVarGetInteger(kCvarTimeOfDay, 0) != 0) {
        const uint32_t ss = (uint32_t)((uint64_t)dayTime * (24 * 60 * 60 - 1) / 65535);
        snprintf(buf, sizeof(buf), "%02u:%02u", ss / 3600, (ss % 3600) / 60);
        AddRow(isDay ? "DAY_TIME_TIMER" : "NIGHT_TIME_TIMER", nullptr, kWhite, buf);
    }
    if (CVarGetInteger(kCvarNavi, 0) != 0) {
        const ImVec4 col = (naviPhase == 1) ? kGreen : (naviPhase == 2 ? kGrey : kWhite);
        FormatMinSec(naviTicks / 20, buf, sizeof(buf));
        AddRow("NAVI_TIMER", nullptr, col, buf);
    }
    if (CVarGetInteger(kCvarConditional, 0) != 0) {
        const char* icon = nullptr;
        ImVec4 col = kWhite;
        switch (timerKind) {
            case 1:
                icon = "ITEM_TUNIC_GORON";
                col = kRed;
                break;
            case 2:
                icon = "ITEM_TUNIC_ZORA";
                col = kBlue;
                break;
            case 3:
                icon = "ITEM_SWORD_MASTER";
                break;
            case 4:
                break;
            default:
                icon = "ITEM_TUNIC_KOKIRI";
                break;
        }
        if (timerKind == 0) {
            snprintf(buf, sizeof(buf), "-:--");
        } else {
            FormatMinSec((uint32_t)timerSeconds, buf, sizeof(buf));
        }
        AddRow(icon, nullptr, col, buf);
    }
}

int GlyphIndex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return (c == ':' || c == '.') ? 10 : -1;
}

// N64 digit strip, matching OOT's geometry (8x16 per glyph, the dot drawn half-height and dropped).
// All-or-nothing: any missing key falls the whole row back to text rather than half-rendering it.
void DrawTimeText(const std::shared_ptr<Fast::Fast3dGui>& gui, const Row& row) {
    bool ok = gui != nullptr;
    for (const char* p = row.text; ok && *p != '\0'; ++p) {
        const int idx = GlyphIndex(*p);
        ok = idx >= 0 && gui->HasTextureByName(kDigits[idx]);
    }
    if (!ok) {
        ImGui::TextColored(row.color, "%s", row.text);
        return;
    }
    for (const char* p = row.text; *p != '\0'; ++p) {
        const int idx = GlyphIndex(*p);
        ImTextureID tex = gui->GetTextureByName(kDigits[idx]);
        if (*p == '.') {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (8.0f * sScale));
            ImGui::Image(tex, ImVec2(8.0f * sScale, 8.0f * sScale), ImVec2(0, 0.5f), ImVec2(1, 1), row.color,
                         ImVec4(0, 0, 0, 0));
        } else {
            ImGui::Image(tex, ImVec2(8.0f * sScale, 16.0f * sScale), ImVec2(0, 0), ImVec2(1, 1), row.color,
                         ImVec4(0, 0, 0, 0));
        }
        ImGui::SameLine(0, 0);
    }
}

// A preset or a restored config can turn the native overlays back on, and MM's window does not even
// exist yet at ComboUI_Register time, so this re-asserts every frame (write only on change).
void SuppressNativeOverlays() {
    for (const auto& w : kNativeOverlays) {
        if (CVarGetInteger(w.cvar, 0) != 0) {
            ComboTracker::SetTracker(w, false);
        }
    }
}

void SeedFromNativeCvars() {
    if (CVarGetInteger(kCvarSeeded, 0) != 0) {
        return;
    }
    CVarSetInteger(kCvarSeeded, 1);
    // 0 off, 1 real-time, 2 in-game. MM's config migration runs after this, and a config from before
    // the mode/visibility split still keeps the mode in gWindows.DisplayOverlay itself.
    int mmMode = CVarGetInteger("gDisplayOverlay.Mode", 0);
    if (mmMode == 0) {
        mmMode = CVarGetInteger("gWindows.DisplayOverlay", 0);
    }
    const bool nativeShown = CVarGetInteger("gOpenWindows.TimeDisplayEnabled", 0) != 0 || mmMode != 0;
    CVarSetInteger(kCvarEnabled, nativeShown ? 1 : 0);
    CVarSetInteger(kCvarTotal, 1);
    CVarSetInteger(kCvarTimeOfDay, CVarGetInteger("gTimeDisplay.Timers.TimeofDay", 0));
    CVarSetInteger(kCvarNavi, CVarGetInteger("gTimeDisplay.Timers.NaviTimer", 0));
    CVarSetInteger(kCvarConditional, CVarGetInteger("gTimeDisplay.Timers.HotWater", 0));
    CVarSetFloat(kCvarScale, CVarGetFloat("gTimeDisplay.FontScale", 1.0f));
    CVarSetInteger(kCvarHideBg, CVarGetInteger("gTimeDisplay.ShowWindowBG", 0));
    if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
}

std::shared_ptr<ComboTimersWindow> sWindow;

} // namespace

void ComboTimersWindow::DrawElement() {
    auto gui = Gui3d();
    if (!ImGui::BeginTable("##combotimers", 2, ImGuiTableFlags_NoClip | ImGuiTableFlags_NoSavedSettings)) {
        return;
    }
    for (const Row& row : sRows) {
        ImGui::TableNextColumn();
        if (row.icon != nullptr && gui != nullptr && gui->HasTextureByName(row.icon)) {
            ImGui::Image(gui->GetTextureByName(row.icon), ImVec2(16.0f * sScale, 16.0f * sScale));
        } else {
            ImGui::TextColored(row.color, "%s", row.label != nullptr ? row.label : "");
        }
        ImGui::TableNextColumn();
        DrawTimeText(gui, row);
    }
    ImGui::EndTable();
}

void ComboTimersWindow::Draw() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    // comboui's per-module GImGui must point at the shared context (same pattern as ComboMenu).
    ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    SuppressNativeOverlays(); // before the visibility gate — it must run with the overlay off too

    if (!IsVisible()) {
        return;
    }
    if (ComboTracker::OotActiveSlot() < 0) {
        SyncVisibilityConsoleVariable(); // title/file select: no slot, nothing to time
        return;
    }

    sScale = std::clamp(CVarGetFloat(kCvarScale, 1.0f), 1.0f, 5.0f);
    try {
        BuildRows();
    } catch (...) { // never let a game-DLL call unwind into the Gui draw loop
        sRows.clear();
    }
    if (sRows.empty()) {
        SyncVisibilityConsoleVariable();
        return;
    }

    // Explicit black backdrop, not the menu theme's WindowBg — matches both games' overlays.
    const float alpha = CVarGetInteger(kCvarHideBg, 0) != 0 ? 0.0f : 0.5f;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::SetWindowFontScale(sScale); // scale the text rows with the digit images
        DrawElement();
    }
    ImGui::End();
    ImGui::PopStyleVar(1);
    ImGui::PopStyleColor(2);
    SyncVisibilityConsoleVariable();
}

void RegisterTimersWindow() {
    auto ctx = Ship::Context::GetRawInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return;
    }
    SeedFromNativeCvars(); // before construction — the window reads its visibility CVar in the ctor
    if (!sWindow) {
        sWindow = std::make_shared<ComboTimersWindow>(kCvarEnabled, kWindowName);
    }
    ctx->GetWindow()->GetGui()->AddGuiWindow(sWindow);
}

void DrawTimersSharedPanel() {
    const ImVec4 theme = ComboMenu_ThemeColor();
    bool changed = false;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##combotimerscols", 2, tableFlags)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        const bool shown = CVarGetInteger(kCvarEnabled, 0) != 0;
        std::string txt = std::string(shown ? ICON_FA_WINDOW_CLOSE : ICON_FA_EXTERNAL_LINK_SQUARE) + " Toggle Timers";
        ComboMenu_PushButton(theme);
        if (ImGui::Button(txt.c_str())) {
            ComboTracker::SetTracker({ kCvarEnabled, kWindowName }, !shown);
            changed = true;
        }
        ComboMenu_PopButton();
        ImGui::TextDisabled("One overlay for both games. Play time is each game's own saved time, added up.");

        ImGui::SeparatorText("Rows");
        ComboMenu_PushCheckbox(theme);
        bool total = CVarGetInteger(kCvarTotal, 1) != 0;
        if (ImGui::Checkbox("Total play time", &total)) {
            CVarSetInteger(kCvarTotal, total ? 1 : 0);
            changed = true;
        }
        bool perGame = CVarGetInteger(kCvarPerGame, 0) != 0;
        if (ImGui::Checkbox("Per-game breakdown", &perGame)) {
            CVarSetInteger(kCvarPerGame, perGame ? 1 : 0);
            changed = true;
        }

        ImGui::SeparatorText("Ocarina of Time only");
        bool tod = CVarGetInteger(kCvarTimeOfDay, 0) != 0;
        if (ImGui::Checkbox("Time of day", &tod)) {
            CVarSetInteger(kCvarTimeOfDay, tod ? 1 : 0);
            changed = true;
        }
        bool navi = CVarGetInteger(kCvarNavi, 0) != 0;
        if (ImGui::Checkbox("Navi timer", &navi)) {
            CVarSetInteger(kCvarNavi, navi ? 1 : 0);
            changed = true;
        }
        bool cond = CVarGetInteger(kCvarConditional, 0) != 0;
        if (ImGui::Checkbox("Conditional timer", &cond)) {
            CVarSetInteger(kCvarConditional, cond ? 1 : 0);
            changed = true;
        }
        ComboMenu_PopCheckbox();
        ImGui::TextDisabled("Hidden while Majora's Mask is the active game.");

        ImGui::TableSetColumnIndex(1);
        ImGui::SeparatorText("Appearance");
        float scale = CVarGetFloat(kCvarScale, 1.0f);
        ComboMenu_PushSlider(theme);
        if (ImGui::SliderFloat("Scale", &scale, 1.0f, 5.0f, "%.1fx")) {
            CVarSetFloat(kCvarScale, std::clamp(scale, 1.0f, 5.0f));
            changed = true;
        }
        ComboMenu_PopSlider();

        ComboMenu_PushCheckbox(theme);
        bool hideBg = CVarGetInteger(kCvarHideBg, 0) != 0;
        if (ImGui::Checkbox("Hide background", &hideBg)) {
            CVarSetInteger(kCvarHideBg, hideBg ? 1 : 0);
            changed = true;
        }
        ComboMenu_PopCheckbox();

        ImGui::EndTable();
    }

    if (changed) {
        if (auto ctx = Ship::Context::GetRawInstance(); ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
            ctx->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    }
}

} // namespace ComboRando

// ComboShip: the launcher pushes whether both games are beaten, on slot load and when it latches.
#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_SetComboComplete(int complete)
#else
extern "C" void ComboUI_SetComboComplete(int complete)
#endif
{
    ComboRando::sRunComplete.store(complete, std::memory_order_relaxed);
}
