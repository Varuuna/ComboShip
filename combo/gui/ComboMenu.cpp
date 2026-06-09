// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include <imgui.h>
#include <memory>

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

// Placeholder bodies until later tasks.
void ComboMenu::DrawSharedPanel() { ImGui::TextUnformatted("Shared engine settings (todo Phase 2)"); }
void ComboMenu::DrawGamePanel(const char* gameKey) { ImGui::Text("%s settings (todo Phase 1)", gameKey); }
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
