#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
<<<<<<< HEAD
=======
#include "soh/cvar_prefixes.h"
>>>>>>> vendor-soh

static void RegisterFireproofDekuShield() {
    COND_VB_SHOULD(VB_BURN_SHIELD, CVarGetInteger(CVAR_CHEAT("FireproofDekuShield"), 0), { *should = false; });
}

static RegisterShipInitFunc initFunc(RegisterFireproofDekuShield, { CVAR_CHEAT("FireproofDekuShield") });
