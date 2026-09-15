// ComboShip (cross-game teleport songs): Ocarina of Time's six warp songs as MM rando items.
//
// Playing one in Termina shows "You played the <song>." then asks "Soar to <place>?"; Yes persists MM and
// hands off to OOT arriving at that warp pad (Combo_RequestCrossSwitch, the entrance-targeted handoff).
// Refused wherever MM refuses its own Song of Soaring (restriction flag, dungeons, the Secret Shrine).
//
// The songs live past MM's 24-bit availability mask, so this file is the only thing that ever makes them
// playable (VB_SONG_AVAILABLE_TO_PLAY). Both prompts reuse text id 0x1B95 with a local state machine,
// exactly like SariasSongHint.cpp, so no new message-table ids are needed and ClockShuffle's unguarded
// hooks on 0x1B8A..0x1B92 (where 0x1B72 + id would have landed) stay untouched.
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
// OOT's own "Warp to X?" prompts tint the destination in the song's color. MM's color bytes:
// 0x02 green, 0x01 red (MM's red is orange-red), 0x03 blue, 0x04 yellow, 0x06 purple, 0x05 light blue.
const char* const kPlaceColors[6] = { "%g", "%r", "%b", "%y", "%p", "\x05" };
const char* const kPlaceNames[6] = {
    "the Sacred Forest Meadow", "Death Mountain Crater", "Lake Hylia",
    "the Desert Colossus",      "the Graveyard",         "the Temple of Time",
};
// OOT's warp-pad entrances (soh/include/tables/entrance_table.h), same order as z_player.c's
// sWarpSongEntrances. OOT's TitleSetup routes the value through Entrance_OverrideNextIndex, so entrance
// rando is honoured on that side.
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

// Mirrors the Song of Soaring branch in z_message.c (MSGMODE_TEXT_CLOSING): the restriction flag, any
// room with a dungeon map, and the Secret Shrine.
bool SoaringForbiddenHere() {
    PlayState* play = gPlayState;
    return play->interfaceCtx.restrictions.songOfSoaring != 0 || Map_CurRoomHasMapI(play) ||
           play->sceneId == SCENE_SECOM;
}

} // namespace

void Rando::MiscBehavior::WarpSongs() {
    bool shouldRegister = IS_RANDO && RANDO_SAVE_OPTIONS[RO_SHUFFLE_SONG_WARP_SONGS];
    sState = WS_IDLE;

    // Ownership. Both the free-play recogniser (code_8019AF00.c) and the song-played gate (z_message.c)
    // ask here; the vanilla condition is always false for these ids (no quest bit, past the mask).
    COND_VB_SHOULD(VB_SONG_AVAILABLE_TO_PLAY, shouldRegister, {
        uint8_t* songIndex = va_arg(args, uint8_t*);
        if (IsWarpSongId(*songIndex)) {
            *should =
                Flags_GetRandoInf((RandoInf)(RANDO_INF_OBTAINED_SONG_MINUET + (*songIndex - OCARINA_SONG_MINUET)));
        }
    });

    // The song's name box has closed: decide between the confirm prompt and the refusal.
    COND_VB_SHOULD(VB_MSG_CAPTURE_MSGMODE_TEXT_CLOSING_OCARINA_ACTION, shouldRegister, {
        if (IsWarpSongId(sLastPlayedSong)) {
            *should = true;
            sWarpIndex = (u8)(sLastPlayedSong - OCARINA_SONG_MINUET);
            sLastPlayedSong = 0xFF;
            // Refused: the vanilla 0x1B95 text loads (sState stays IDLE, the OnOpenText hook below passes)
            // and the restricted-song mode dismisses it. Otherwise our prompt replaces the same id.
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
            // A fresh press only (see SariasSongHint.cpp): a button still held from the name box would
            // otherwise confirm the default "Yes" before the player has seen the prompt.
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
