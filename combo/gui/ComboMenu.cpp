// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include <imgui.h>
#include <memory>

namespace ComboRando {

static std::shared_ptr<ComboMenu> sComboMenu;

void ComboMenu::DrawElement() {
    if (ImGui::BeginTabBar("ComboScopeTabs")) {
        if (ImGui::BeginTabItem("Shared")) { DrawSharedPanel(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("OOT"))    { DrawGamePanel("oot"); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("MM"))     { DrawGamePanel("mm");  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Combo"))  { DrawComboPanel(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
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
