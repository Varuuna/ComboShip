// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include <imgui.h>
#include <memory>
#include <string>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
typedef void (*FnDrawSettings)(const char*, const char*);
FnDrawSettings ResolveDraw(const char* dll, const char* sym) {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA(dll); // already loaded by the exe
    return h ? (FnDrawSettings)GetProcAddress(h, sym) : nullptr;
#else
    return nullptr;
#endif
}
FnDrawSettings sSohDraw = nullptr;
FnDrawSettings sMmDraw  = nullptr;
// Engine sidebars shown ONCE in the Shared tab (rendered by OOT, since they write the shared
// gSettings.* engine CVars) and skipped from the per-game tabs to avoid duplication.
const char* kEngineSidebars = "Settings/Graphics,Settings/General";
} // namespace

namespace ComboRando {

static std::shared_ptr<ComboMenu> sComboMenu;

void ComboMenu::Draw() {
    // Mirror Ship::Menu::Draw — skip GuiWindow::Draw's normal Begin/End wrapper so DrawElement
    // can open its own fullscreen overlay window instead of a floating one.
    if (!IsVisible()) {
        return;
    }
    DrawElement();
    SyncVisibilityConsoleVariable();
}

void ComboMenu::DrawElement() {
    // ImGui's GImGui (current-context) is a per-module global; comboui.dll has its own,
    // separate from libultraship.dll where the context actually lives. Point it at the shared
    // context before any ImGui call here (same pattern soh.dll/2ship.dll use) — otherwise
    // ImGui::BeginTabBar dereferences a null context and crashes.
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }

    // Fullscreen overlay covering the viewport work area, matching the old port menu
    // (NoDecoration/NoMove, sized to the viewport, with a translucent backdrop).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(vp->WorkSize, ImGuiCond_Always);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0, 0, 0, CVarGetFloat("gSettings.Menu.BackgroundOpacity", 0.85f)));
    bool open = ImGui::Begin("Combo Menu", nullptr, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (open) {
        if (ImGui::BeginTabBar("ComboScopeTabs")) {
            if (ImGui::BeginTabItem("Shared")) { DrawSharedPanel(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("OOT"))    { DrawGamePanel("oot"); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("MM"))     { DrawGamePanel("mm");  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Combo"))  { DrawComboPanel(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// Shared tab: render OOT's themed engine sidebars (Graphics/General) via the only-list. They write
// the shared gSettings.* engine CVars, so this one panel controls both games — and it looks native.
void ComboMenu::DrawSharedPanel() {
    if (!sSohDraw) sSohDraw = ResolveDraw("soh.dll", "SOH_DrawSettings");
    if (sSohDraw) sSohDraw(kEngineSidebars, "");
    else ImGui::TextUnformatted("Shared settings unavailable (SOH_DrawSettings not found).");
}

void ComboMenu::DrawGamePanel(const char* gameKey) {
    if (strcmp(gameKey, "oot") == 0) {
        if (!sSohDraw) sSohDraw = ResolveDraw("soh.dll", "SOH_DrawSettings");
        if (sSohDraw) sSohDraw("", kEngineSidebars);
        else ImGui::TextUnformatted("OOT settings unavailable (SOH_DrawSettings not found).");
    } else {
        if (!sMmDraw) sMmDraw = ResolveDraw("2ship.dll", "MM_DrawSettings");
        if (sMmDraw) sMmDraw("", kEngineSidebars);
        else ImGui::TextUnformatted("MM settings unavailable (MM_DrawSettings not found).");
    }
}
void ComboMenu::DrawComboPanel() { ImGui::TextUnformatted("Cross-world Generate (todo Phase 3)"); }

} // namespace ComboRando

#ifdef _WIN32
extern "C" __declspec(dllexport) void ComboUI_Register(void)
#else
extern "C" void ComboUI_Register(void)
#endif
{
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return; // GUI not ready
    }
    auto gui = ctx->GetWindow()->GetGui();
    // Match the existing menu-visibility CVar so the in-game menu hotkey toggles us.
    ComboRando::sComboMenu = std::make_shared<ComboRando::ComboMenu>("gOpenWindows.Menu", "Combo Menu");
    gui->SetMenu(ComboRando::sComboMenu);
}
