// ComboShip (teleport songs): OOT's warp songs in MM; playing one offers a warp to its OOT pad.
#ifdef COMBO_BUILD

#include "2s2h/CustomMessage/CustomMessage.h"
#include "MiscBehavior.h"
#include "2s2h/BenPort.h"
#include <libultraship/libultraship.h>
#include <string>

extern "C" {
#include <variables.h>
#include <z64ocarina.h>
extern s16 sLastPlayedSong;
extern u32 sOcarinaAvailableSongFlags; // code_8019AF00.c: bitmask of songs the recogniser currently accepts
s32 Map_CurRoomHasMapI(PlayState* play);
}

namespace {

enum WarpSongState { WS_IDLE, WS_CONFIRM };
WarpSongState sState = WS_IDLE;
u8 sWarpIndex = 0; // 0..5 = Minuet..Prelude, the OOT warp index

const char* const kSongNames[6] = {
    "Minuet of Forest",  "Bolero of Fire",     "Serenade of Water",
    "Requiem of Spirit", "Nocturne of Shadow", "Prelude of Light",
};
// Destination tint per song, like OOT's own warp prompts.
const char* const kPlaceColors[6] = { "%g", "%r", "%b", "%y", "%p", "\x05" };
const char* const kPlaceNames[6] = {
    "the Sacred Forest Meadow", "Death Mountain Crater", "Lake Hylia",
    "the Desert Colossus",      "the Graveyard",         "the Temple of Time",
};
// OOT warp-pad entrances in warp-index order.
const int kWarpPadEntrances[6] = {
    0x0600, // ENTR_SACRED_FOREST_MEADOW_WARP_PAD
    0x04F6, // ENTR_DEATH_MOUNTAIN_CRATER_WARP_PAD
    0x0604, // ENTR_LAKE_HYLIA_WARP_PAD
    0x01F1, // ENTR_DESERT_COLOSSUS_WARP_PAD
    0x0568, // ENTR_GRAVEYARD_WARP_PAD
    0x05F4, // ENTR_TEMPLE_OF_TIME_WARP_PAD
};

bool IsWarpSongId(int songId) {
    return songId >= OCARINA_SONG_MINUET && songId <= OCARINA_SONG_PRELUDE;
}

// Same refusals as MM's own Song of Soaring (z_message.c).
bool SoaringForbiddenHere() {
    PlayState* play = gPlayState;
    return play->interfaceCtx.restrictions.songOfSoaring != 0 || Map_CurRoomHasMapI(play) ||
           play->sceneId == SCENE_SECOM;
}

} // namespace

void Rando::MiscBehavior::WarpSongs() {
    bool shouldRegister = IS_RANDO && RANDO_SAVE_OPTIONS[RO_SHUFFLE_SONG_WARP_SONGS];
    sState = WS_IDLE;

    // Only path that makes these songs playable (no quest bit, past the 24-bit mask).
    COND_VB_SHOULD(VB_SONG_AVAILABLE_TO_PLAY, shouldRegister, {
        uint8_t* songIndex = va_arg(args, uint8_t*);
        if (IsWarpSongId(*songIndex)) {
            // Let the Termina wall (En_Gakufu) win while it listens for its tune.
            if (sOcarinaAvailableSongFlags & (1 << OCARINA_SONG_TERMINA_WALL)) {
                *should = false;
            } else {
                *should =
                    Flags_GetRandoInf((RandoInf)(RANDO_INF_OBTAINED_SONG_MINUET + (*songIndex - OCARINA_SONG_MINUET)));
            }
        }
    });

    // The song's name box has closed: decide between the confirm prompt and the refusal.
    COND_VB_SHOULD(VB_MSG_CAPTURE_MSGMODE_TEXT_CLOSING_OCARINA_ACTION, shouldRegister, {
        if (IsWarpSongId(sLastPlayedSong)) {
            *should = true;
            sWarpIndex = (u8)(sLastPlayedSong - OCARINA_SONG_MINUET);
            sLastPlayedSong = 0xFF;
            // Refused: vanilla 0x1B95 loads and is dismissed. Otherwise our prompt replaces it.
            sState = SoaringForbiddenHere() ? WS_IDLE : WS_CONFIRM;
            Message_StartTextbox(gPlayState, 0x1B95, NULL);
            gPlayState->msgCtx.ocarinaMode = OCARINA_MODE_PROCESS_RESTRICTED_SONG;
        }
    });

    // Yes / No on the confirm prompt.
    COND_VB_SHOULD(VB_MSG_CAPTURE_MSGMODE_TEXT_DONE, shouldRegister, {
        if (sState == WS_CONFIRM && gPlayState->msgCtx.ocarinaMode == OCARINA_MODE_PROCESS_RESTRICTED_SONG) {
            *should = true;
            Input* input = CONTROLLER1(&gPlayState->state);
            // Fresh press only, so a held button can't confirm Yes unseen (see SariasSongHint.cpp).
            const bool pressedA = CHECK_BTN_ALL(input->press.button, BTN_A);
            const bool pressedB = CHECK_BTN_ALL(input->press.button, BTN_B);
            if (pressedA || pressedB) {
                const bool yes = pressedA && gPlayState->msgCtx.choiceIndex == 0;
                sState = WS_IDLE;
                Audio_PlaySfx(NA_SE_SY_DECIDE);
                Message_CloseTextbox(gPlayState);
                gPlayState->msgCtx.ocarinaMode = OCARINA_MODE_END;
                if (yes) {
                    // Persist + switch happen on the next OnGameStateMainStart, like the Clock Tower portal.
                    Combo_RequestCrossSwitch(kWarpPadEntrances[sWarpIndex]);
                }
            }
        }
    });

    COND_ID_HOOK(OnOpenText, 0x1B95, shouldRegister, [](u16* textId, bool* loadFromMessageTable) {
        MessageContext* msgCtx = &gPlayState->msgCtx;
        CustomMessage::Entry entry;
        if (msgCtx->msgMode == MSGMODE_DISPLAY_SONG_PLAYED_TEXT_BEGIN && IsWarpSongId(msgCtx->songPlayed)) {
            // The "You played ..." name box, routed to this id by z_message.c for the OOT songs.
            entry.textboxType = TEXTBOX_TYPE_3;
            entry.msg = std::string("You played the ") + kSongNames[msgCtx->songPlayed - OCARINA_SONG_MINUET] + ".";
        } else if (sState == WS_CONFIRM) {
            entry.nextMessageID = 0x1B95;
            entry.msg = std::string("Warp to ") + kPlaceColors[sWarpIndex] + kPlaceNames[sWarpIndex] +
                        "%w?\x11\x02\x11\xC2Yes\x11No"; // blank line before the choices, like the vanilla warp prompt
        } else {
            return; // not ours: vanilla text (or another feature's hook, e.g. Saria's Song)
        }
        CustomMessage::LoadCustomMessageIntoFont(entry);
        *loadFromMessageTable = false;
    });
}

#endif // COMBO_BUILD
