// mm/2s2h/Rando/MiscBehavior/CrossRocsFeather.cpp
// ComboShip (cross-game items): Ship of Harkinian's Roc's Feather in Majora's Mask (RO_CROSS_ROCS_FEATHER).
//
// Used from a C button by human Link (or adult Link under Cross-Games Age) like the other OOT items in MM
// (Combo_CrossItem_TryUse). It is a port of SoH's RocsFeather.cpp: one jump per time in the air, refreshed after Link
// has stood on the ground for 3 frames; SoH's own jump animation (soh.o2r, "@oot:" routed), its jump heights (7.0
// child, 7.5 adult) and forward push, the ripple + splash at Link's feet and the skip sound. The jump goes through
// MM's jump setup (func_80834D50, OOT's func_80838940) in z_player.c.
// See docs/deviations/rando.md "Roc's Feather in MM".
#ifdef COMBO_BUILD

#include "CrossGear.h"
#include "AdultLink.h"
#include "2s2h/Rando/Rando.h"

extern "C" {
#include "variables.h"
#include "functions.h"
#include "macros.h"
#include "align_asset_macro.h"
}

namespace {

constexpr u8 kMaxRocsUses = 1;

// SoH's jump animation (soh.o2r), served by OOT's ResourceManager.
const ALIGN_ASSET(2) char sRocsFeatherJumpAnim[] =
    "__OTR__@oot:objects/gameplay_keep/gPlayerAnim_link_rocs_feather_jump";

u8 sRocsUseCount = 0;
u8 sGroundTimer = 0;

} // namespace

extern "C" void Combo_Rocs_OnPlayerUpdate(PlayState* play, Player* player) {
    // SoH: the use comes back once Link has been on the ground for 3 frames.
    if (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) {
        if (sGroundTimer <= 3) {
            sGroundTimer++;
        }
    } else {
        sGroundTimer = 0;
    }
    if (sGroundTimer >= 3) {
        sRocsUseCount = 0;
    }
}

extern "C" int Combo_Rocs_Use(PlayState* play, Player* player) {
    if (sRocsUseCount >= kMaxRocsUses) {
        return 1; // SoH: nothing happens on a second press in the air
    }
    sRocsUseCount++;

    Combo_Player_Jump(play, player, (void*)sRocsFeatherJumpAnim, 5.8f);

    // SoH: needed to prevent a weird animation morph when the jump lands.
    player->av2.actionVar2 = 1;

    // Move Link forward, facing where he faces.
    player->speedXZ = 5.0f;
    player->actor.world.rot.y = player->yaw = player->actor.shape.rot.y;

    f32 effectsScale;
    if (Combo_AdultLink_Is(player)) {
        player->actor.velocity.y = 7.5f;
        effectsScale = 1.5f;
    } else {
        player->actor.velocity.y = 7.0f;
        effectsScale = 1.0f;
    }

    Vec3f effectsPos = player->actor.home.pos;
    effectsPos.y += 3;
    EffectSsGRipple_Spawn(play, &effectsPos, static_cast<s16>(200 * effectsScale), static_cast<s16>(300 * effectsScale),
                          1);
    EffectSsGSplash_Spawn(play, &effectsPos, NULL, NULL, 0, static_cast<s16>(150 * effectsScale));

    // SoH: drop the hopping state left by a sidehop / backflip so ledges can be grabbed again.
    player->stateFlags2 &= ~PLAYER_STATE2_80000;

    Player_PlaySfx(player, NA_SE_PL_SKIP);
    return 1;
}

#endif // COMBO_BUILD
