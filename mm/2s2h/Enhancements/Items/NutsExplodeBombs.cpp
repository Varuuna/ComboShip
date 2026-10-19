// ComboShip: Ship of Harkinian's "Deku Nuts Explode Bombs" (soh's z_en_bom.c) for Majora's Mask. A Deku Nut that
// hits a bomb sets it off, the way nuts set off Bombchus. Powder Kegs are left alone, like SoH leaves Bomb Flowers.
#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "overlays/actors/ovl_En_Bom/z_en_bom.h"
}

#define CVAR_NAME "gEnhancements.Items.NutsExplodeBombs"
#define CVAR CVarGetInteger(CVAR_NAME, 0)

void RegisterNutsExplodeBombs() {
    COND_ID_HOOK(OnActorInit, ACTOR_EN_BOM, CVAR, [](Actor* actor) {
        EnBom* bomb = (EnBom*)actor;
        if (!bomb->isPowderKeg) {
            bomb->collider1.elem.acDmgInfo.dmgFlags |= DMG_DEKU_NUT;
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterNutsExplodeBombs, { CVAR_NAME });
