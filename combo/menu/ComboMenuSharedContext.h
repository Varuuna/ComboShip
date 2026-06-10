/* combo/menu/ComboMenuSharedContext.h
 * ComboShip-owned helper, compiled into each game DLL. Each DLL has its own per-module ImGui
 * GImGui; when a game is backgrounded it isn't current, so any cross-DLL export that can reach
 * an ImGui call (menu build, callbacks, disable eval, custom draw) must point it at the shared
 * libultraship context first.
 *
 * TU-GLUE HEADER: include from the game's port file AFTER libultraship.h / ImGui are in scope.
 */
#ifndef COMBO_MENU_SHARED_CONTEXT_H
#define COMBO_MENU_SHARED_CONTEXT_H

#ifndef IMGUI_VERSION
#error "ComboMenuSharedContext.h is TU-glue: include imgui.h and libultraship.h before it"
#endif

namespace ComboMenuContext {

inline void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace ComboMenuContext

#endif // COMBO_MENU_SHARED_CONTEXT_H
