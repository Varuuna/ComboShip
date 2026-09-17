// combo/gui/ComboMenuModel.cpp
#include "ComboMenuModel.h"
#include "ComboResolve.h"

namespace ComboRando {

ComboMenuModel& ComboMenuModel::Get() {
    static ComboMenuModel sInstance;
    return sInstance;
}

void ComboMenuModel::LoadGame(GameMenu& g, const char* moduleName, const char* exportSym, const char* invokeSym,
                              const char* evalSym, const char* drawSym, const char* applySym,
                              const char* drawWidgetSym) {
    // Resolution is process-wide via Combo_ResolveSym; a module not yet resident just resolves to
    // nulls, leaving g.loaded=false for a later retry.
    auto exportFn = (Fn_ExportMenu)Combo_ResolveSym(moduleName, exportSym);
    g.invokeCallback = (Fn_MenuInvokeCallback)Combo_ResolveSym(moduleName, invokeSym);
    g.evalDisabled = (Fn_MenuEvalDisabled)Combo_ResolveSym(moduleName, evalSym);
    g.drawCustom = (Fn_MenuDrawCustom)Combo_ResolveSym(moduleName, drawSym);
    g.applyCVarChange = (Fn_MenuApplyCVar)Combo_ResolveSym(moduleName, applySym);  // optional — not in loaded check
    g.drawWidget = (Fn_MenuDrawWidget)Combo_ResolveSym(moduleName, drawWidgetSym); // optional — not in loaded check

    // ExportMenu may build the menu lazily (MM does so on ActivateMenu), so it can return
    // null until the game has eager-booted. Re-call it each retry until it yields a menu.
    g.menu = exportFn ? exportFn() : nullptr;

    g.loaded =
        (g.menu != nullptr && g.invokeCallback != nullptr && g.evalDisabled != nullptr && g.drawCustom != nullptr);
}

void ComboMenuModel::EnsureLoaded() {
    if (mLoaded) {
        return; // both games already resolved — cheap no-op for the per-frame caller
    }

    // Retry each game independently. A game whose menu isn't yet buildable stays loaded=false
    // so a later frame can pick it up; we only latch mLoaded once BOTH have loaded.
    if (!mOot.loaded) {
        LoadGame(mOot, "soh", "SOH_ExportMenu", "SOH_MenuInvokeCallback", "SOH_MenuEvalDisabled", "SOH_MenuDrawCustom",
                 "SOH_MenuApplyCVarChange", "SOH_MenuDrawWidget");
    }
    if (!mMm.loaded) {
        LoadGame(mMm, "2ship", "MM_ExportMenu", "MM_MenuInvokeCallback", "MM_MenuEvalDisabled", "MM_MenuDrawCustom",
                 "MM_MenuApplyCVarChange", "MM_MenuDrawWidget");
    }

    if (mOot.loaded && mMm.loaded) {
        mLoaded = true;
    }
}

} // namespace ComboRando
