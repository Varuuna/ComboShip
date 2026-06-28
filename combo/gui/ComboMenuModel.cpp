// combo/gui/ComboMenuModel.cpp
#include "ComboMenuModel.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace ComboRando {

ComboMenuModel& ComboMenuModel::Get() {
    static ComboMenuModel sInstance;
    return sInstance;
}

void ComboMenuModel::LoadGame(GameMenu& g, const char* dll, const char* exportSym, const char* invokeSym,
                              const char* evalSym, const char* drawSym, const char* applySym) {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA(dll); // already LoadLibrary'd by the exe — mirror ResolveDraw
    if (!h) {
        return; // DLL not yet present in the process — leave g.loaded=false for a later retry
    }
    auto exportFn = (Fn_ExportMenu)GetProcAddress(h, exportSym);
    g.invokeCallback = (Fn_MenuInvokeCallback)GetProcAddress(h, invokeSym);
    g.evalDisabled = (Fn_MenuEvalDisabled)GetProcAddress(h, evalSym);
    g.drawCustom = (Fn_MenuDrawCustom)GetProcAddress(h, drawSym);
    g.applyCVarChange = (Fn_MenuApplyCVar)GetProcAddress(h, applySym); // optional — not in loaded check

    // ExportMenu may build the menu lazily (MM does so on ActivateMenu), so it can return
    // null until the game has eager-booted. Re-call it each retry until it yields a menu.
    g.menu = exportFn ? exportFn() : nullptr;

    g.loaded =
        (g.menu != nullptr && g.invokeCallback != nullptr && g.evalDisabled != nullptr && g.drawCustom != nullptr);
#else
    (void)g;
    (void)dll;
    (void)exportSym;
    (void)invokeSym;
    (void)evalSym;
    (void)drawSym;
    (void)applySym;
    // Non-Windows: exports are resolved via the OS dynamic loader elsewhere; leave unresolved.
#endif
}

void ComboMenuModel::EnsureLoaded() {
    if (mLoaded) {
        return; // both games already resolved — cheap no-op for the per-frame caller
    }

    // Retry each game independently. A game whose menu isn't yet buildable stays loaded=false
    // so a later frame can pick it up; we only latch mLoaded once BOTH have loaded.
    if (!mOot.loaded) {
        LoadGame(mOot, "soh.dll", "SOH_ExportMenu", "SOH_MenuInvokeCallback", "SOH_MenuEvalDisabled",
                 "SOH_MenuDrawCustom", "SOH_MenuApplyCVarChange");
    }
    if (!mMm.loaded) {
        LoadGame(mMm, "2ship.dll", "MM_ExportMenu", "MM_MenuInvokeCallback", "MM_MenuEvalDisabled",
                 "MM_MenuDrawCustom", "MM_MenuApplyCVarChange");
    }

    if (mOot.loaded && mMm.loaded) {
        mLoaded = true;
    }
}

} // namespace ComboRando
