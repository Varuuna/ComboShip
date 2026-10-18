// ComboShip (cross-game items): Majora's Mask songs playable in OOT (OoTMM's elegyOot / songHealingOot /
// songAwakeningOot / songGoronOot / songZoraOot / songOrderOot). Like OoTMM they do nothing in Hyrule beyond
// being played, except the Elegy, which leaves a statue of Link with a green flash (ComboElegyShell.cpp); their
// use is as items, and with Shared Items one copy counts for both games.
//
// Recognition mirrors ComboOwlWarp.cpp's Song of Soaring: a note ring fed from OnOcarinaNote (OOT's own song
// table is untouched), and on a match during free play the ocarina session closes with the "correct" chime.
#ifdef COMBO_BUILD
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/randomizer/randomizer.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Demo_Effect/z_demo_effect.h"
extern PlayState* gPlayState;
void Combo_ElegyShell_Place(PlayState* play);
}

namespace {

// MM's button sequences (gOcarinaSongButtons): A = D4, C-down = F4, C-right = A4, C-left = B4, C-up = D5.
struct MmSong {
    RandomizerInf owned;
    u8 length;
    u8 pitches[8];
};
const MmSong kSongs[] = {
    { RAND_INF_OBTAINED_COMBO_SONG_ELEGY,
      7,
      { OCARINA_PITCH_A4, OCARINA_PITCH_B4, OCARINA_PITCH_A4, OCARINA_PITCH_F4, OCARINA_PITCH_A4, OCARINA_PITCH_D5,
        OCARINA_PITCH_B4 } },
    { RAND_INF_OBTAINED_COMBO_SONG_HEALING,
      6,
      { OCARINA_PITCH_B4, OCARINA_PITCH_A4, OCARINA_PITCH_F4, OCARINA_PITCH_B4, OCARINA_PITCH_A4, OCARINA_PITCH_F4 } },
    { RAND_INF_OBTAINED_COMBO_SONG_SONATA,
      7,
      { OCARINA_PITCH_D5, OCARINA_PITCH_B4, OCARINA_PITCH_D5, OCARINA_PITCH_B4, OCARINA_PITCH_D4, OCARINA_PITCH_A4,
        OCARINA_PITCH_D4 } },
    { RAND_INF_OBTAINED_COMBO_SONG_GORON,
      8,
      { OCARINA_PITCH_D4, OCARINA_PITCH_A4, OCARINA_PITCH_B4, OCARINA_PITCH_D4, OCARINA_PITCH_A4, OCARINA_PITCH_B4,
        OCARINA_PITCH_A4, OCARINA_PITCH_D4 } },
    { RAND_INF_OBTAINED_COMBO_SONG_BOSSA,
      7,
      { OCARINA_PITCH_B4, OCARINA_PITCH_D5, OCARINA_PITCH_B4, OCARINA_PITCH_A4, OCARINA_PITCH_F4, OCARINA_PITCH_B4,
        OCARINA_PITCH_A4 } },
    { RAND_INF_OBTAINED_COMBO_SONG_OATH,
      6,
      { OCARINA_PITCH_A4, OCARINA_PITCH_F4, OCARINA_PITCH_D4, OCARINA_PITCH_F4, OCARINA_PITCH_A4, OCARINA_PITCH_D5 } },
};
constexpr int kSongCount = sizeof(kSongs) / sizeof(kSongs[0]);
constexpr int kElegy = 0;

constexpr int kRing = 8;
u8 sRing[kRing];
int sRingLen = 0;
u8 sPrevPitch = OCARINA_PITCH_NONE;
int sMatched = -1; // index into kSongs, set from the audio side, consumed on the game frame

void ResetRing() {
    sRingLen = 0;
    sPrevPitch = OCARINA_PITCH_NONE;
}

void OnOcarinaNote(uint8_t pitch, float modulator, int8_t instrument) {
    (void)modulator;
    if (instrument == OCARINA_INSTRUMENT_OFF) {
        ResetRing();
        return;
    }
    // Same counting rule as the Song of Soaring ring: a held or repeated pitch counts once.
    const bool counts = (pitch != sPrevPitch) && (pitch != OCARINA_PITCH_NONE);
    sPrevPitch = pitch;
    if (!counts) {
        return;
    }
    if (sRingLen == kRing) {
        for (int i = 1; i < kRing; i++) {
            sRing[i - 1] = sRing[i];
        }
        sRingLen--;
    }
    sRing[sRingLen++] = pitch;
    for (int s = 0; s < kSongCount; s++) {
        const MmSong& song = kSongs[s];
        if (sRingLen < song.length) {
            continue;
        }
        bool match = true;
        for (int i = 0; i < song.length && match; i++) {
            match = sRing[sRingLen - song.length + i] == song.pitches[i];
        }
        if (match) {
            sMatched = s;
            sRingLen = 0;
            return;
        }
    }
}

void OnSongPlayed(PlayState* play, int song) {
    MessageContext* msgCtx = &play->msgCtx;
    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
    Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME); // OOT's bank has no MM song fanfares
    msgCtx->ocarinaMode = OCARINA_MODE_04;       // Link puts the ocarina away
    Message_CloseTextbox(play);

    Player* player = GET_PLAYER(play);
    if (song == kElegy) {
        Combo_ElegyShell_Place(play); // ComboElegyShell.cpp: a statue of Link where he stands
        DemoEffect* effect = (DemoEffect*)Actor_Spawn(
            &play->actorCtx, play, ACTOR_DEMO_EFFECT, player->actor.world.pos.x, player->actor.world.pos.y,
            player->actor.world.pos.z, 0, player->actor.shape.rot.y, 0, DEMO_EFFECT_TIMEWARP_TIMEBLOCK_SMALL);
        if (effect != nullptr) {
            effect->envXluColor[0] = 0;
            effect->envXluColor[1] = 100;
            effect->envXluColor[2] = 0;
        }
    } else {
        Actor_Spawn(&play->actorCtx, play, ACTOR_OCEFF_WIPE3, player->actor.world.pos.x, player->actor.world.pos.y,
                    player->actor.world.pos.z, 0, 0, 0, 0);
    }
}

void OnGameFrame() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    MessageContext* msgCtx = &play->msgCtx;
    if (msgCtx->msgMode == MSGMODE_OCARINA_STARTING) {
        ResetRing();
    }
    const int song = sMatched;
    if (song < 0) {
        return;
    }
    sMatched = -1;
    if (!Flags_GetRandomizerInf(kSongs[song].owned) || msgCtx->msgMode != MSGMODE_OCARINA_PLAYING ||
        msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY) {
        return; // not learned, or not free play (a prompt, a scarecrow session): vanilla carries on
    }
    OnSongPlayed(play, song);
}

} // namespace

static void RegisterComboMmSongs() {
    // Registered for every rando save (like ComboOwlWarp.cpp): the seed's options may not be applied yet when
    // this runs, and owning one of the songs (checked per play) already implies the option was on.
    const bool on = IS_RANDO;
    ResetRing();
    sMatched = -1;
    COND_HOOK(OnOcarinaNote, on, OnOcarinaNote);
    COND_HOOK(OnGameFrameUpdate, on, OnGameFrame);
}

static RegisterShipInitFunc initFunc(RegisterComboMmSongs, { "IS_RANDO" });

#endif // COMBO_BUILD
