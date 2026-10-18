// ComboShip (cross-game items): Ocarina of Time songs playable in Termina (OoTMM's "Zelda's Lullaby (MM)").
// MM's song table still carries Zelda's Lullaby (OCARINA_SONG_ZELDAS_LULLABY, an OOT leftover the vanilla
// recognizer never accepts). Owning RI_SONG_ZELDA makes it recognizable in free play; like OoTMM it has no
// effect in Termina, and with Shared Items one copy counts for both games.
#ifdef COMBO_BUILD

#include "2s2h/CustomMessage/CustomMessage.h"
#include "MiscBehavior.h"
#include "2s2h/BenPort.h"
#include <libultraship/libultraship.h>
#include <string>

extern "C" {
#include <variables.h>
#include <z64ocarina.h>
extern u32 sOcarinaAvailableSongFlags; // code_8019AF00.c: bitmask of songs the recogniser currently accepts
}

void Rando::MiscBehavior::CrossSongs() {
    const bool shouldRegister = IS_RANDO && RANDO_SAVE_OPTIONS[RO_CROSS_SONG_ZELDA];

    // Recognizer (code_8019AF00.c) and the played-song check (z_message.c) both ask this.
    COND_VB_SHOULD(VB_SONG_AVAILABLE_TO_PLAY, shouldRegister, {
        uint8_t* songIndex = va_arg(args, uint8_t*);
        if (*songIndex != OCARINA_SONG_ZELDAS_LULLABY) {
            return;
        }
        // Let the Termina wall (En_Gakufu) win while it listens for its tune, like the warp songs do.
        if (sOcarinaAvailableSongFlags & (1 << OCARINA_SONG_TERMINA_WALL)) {
            *should = false;
        } else {
            *should = Flags_GetRandoInf(RANDO_INF_OBTAINED_SONG_ZELDA);
        }
    });

    // The "You played ..." name box: z_message.c asks for 0x1B72 + songPlayed.
    COND_ID_HOOK(OnOpenText, 0x1B72 + OCARINA_SONG_ZELDAS_LULLABY, shouldRegister,
                 [](u16* textId, bool* loadFromMessageTable) {
                     MessageContext* msgCtx = &gPlayState->msgCtx;
                     if (msgCtx->msgMode != MSGMODE_DISPLAY_SONG_PLAYED_TEXT_BEGIN ||
                         msgCtx->songPlayed != OCARINA_SONG_ZELDAS_LULLABY) {
                         return; // not ours: vanilla text
                     }
                     CustomMessage::Entry entry;
                     entry.textboxType = TEXTBOX_TYPE_3;
                     entry.msg = "You played Zelda's Lullaby.";
                     CustomMessage::LoadCustomMessageIntoFont(entry);
                     *loadFromMessageTable = false;
                 });
}

#endif // COMBO_BUILD
