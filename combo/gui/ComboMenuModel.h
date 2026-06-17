// combo/gui/ComboMenuModel.h
//
// ComboShip: comboui-side model that resolves each game's flat C-ABI menu exports
// (SOH_*/MM_* in soh.dll/2ship.dll, see combo/menu/ComboMenuABI.h) and caches the
// returned CwMenu pointer + invoke/eval/draw fn-ptrs. The renderer draws from these.
//
// DESIGN NOTE: this model deliberately does NOT touch ResourceManagers. RM scoping for
// the invoke callbacks lives GAME-SIDE, inside each game's own invoke export — so no
// shared_ptr<ResourceManager> ever crosses the DLL boundary here.
#pragma once
#include "ComboMenuABI.h"

namespace ComboRando {

struct GameMenu {
    const CwMenu*          menu = nullptr;
    Fn_MenuInvokeCallback  invokeCallback = nullptr;
    Fn_MenuEvalDisabled    evalDisabled = nullptr;
    Fn_MenuDrawCustom      drawCustom = nullptr;
    bool                   loaded = false;
};

class ComboMenuModel {
  public:
    static ComboMenuModel& Get();
    // Idempotent + cheap after success: resolves the exports and caches the CwMenu pointers.
    // Safe to call every frame — each game is retried independently until it loads (MM's
    // menu builds lazily once MM has eager-booted), and mLoaded only latches once both load.
    void EnsureLoaded();
    const GameMenu& Oot() const { return mOot; }
    const GameMenu& Mm() const { return mMm; }

  private:
    void LoadGame(GameMenu& g, const char* dll, const char* exportSym,
                  const char* invokeSym, const char* evalSym, const char* drawSym);
    GameMenu mOot, mMm;
    bool mLoaded = false;
};

} // namespace ComboRando
