// ComboShip (cross-game teleport songs): Majora's Mask's Song of Soaring as an OOT randomizer item.
//
// Playing MM's Song of Soaring (C-down, C-left, C-up, twice) in free play opens a flat port of MM's
// owl-statue warp map, drawn with MM's own textures through the "mm" ResourceManager bracket, showing
// the statues already activated in the dormant MM save. Confirming one hands off to MM at that statue
// (Combo_RequestCrossSwitch, the entrance-targeted handoff). Refused where OOT refuses its own warp
// songs, and with "you have yet to leave your mark" when no statue is activated (MM's own text).
//
// Recognition never touches OOT's ocarina tables: the 12-song u16 availability word has no free bit
// and every per-song table is 12 wide. The note stream is watched from OnOcarinaNote and its tail
// compared against the six-pitch pattern (no vanilla song is a suffix of it). On a match the ocarina
// session is closed the way the vanilla B-cancel closes it, and only once the message mode is clean
// again does anything else start (PauseWarp's shape): the refusal textbox, or the chooser, which freezes
// the world with a real pauseCtx->state (so the engine keeps drawing its Start "Return" prompt) and holds Link with
// PLAYER_STATE1_IN_CUTSCENE, stepping Message_Update itself for the Yes/No prompt so the freeze holds. Starting a
// textbox while the session is still in MSGMODE_OCARINA_PLAYING corrupts the message context (it spilled into
// interfaceCtx->view). The map is drawn from OnPlayDrawEnd into OVERLAY_DISP, under the HUD and any textbox, like MM's.
#ifdef COMBO_BUILD
#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include <string>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "align_asset_macro.h"
#include "textures/icon_item_static/icon_item_static.h"
extern PlayState* gPlayState;
// OTRGlobals.cpp: launcher-provided views into MM's dormant save (-1 = unavailable / bad id).
extern int (*gComboOwlFlagsProvider)(void);
extern int (*gComboOwlWarpEntranceProvider)(int owlId);
void Combo_RequestCrossSwitch(int mmEntrance);
// OPEN_DISPS / CLOSE_DISPS re-declare these at block scope; that declaration only keeps C linkage when
// the enclosing function is extern "C" (draw.cpp), so declare them here first (frame_interpolation.cpp).
void FrameInterpolation_RecordOpenChild(const void* a, int b);
void FrameInterpolation_RecordCloseChild(void);
}

namespace {

// MM assets (in mm.o2r). Redeclared here: soh cannot include mm headers. Loaded inside the "mm" bracket.
#define COW_MM_ASSET(sym, path) static const ALIGN_ASSET(2) char sym[] = path
COW_MM_ASSET(sCowMapTex, "__OTR__icon_item_field_static/gWorldMapImageTex");   // CI8 216x128
COW_MM_ASSET(sCowMapTlut, "__OTR__icon_item_field_static/gWorldMapImageTLUT"); // RGBA16 x256
COW_MM_ASSET(sCowOwlTex, "__OTR__icon_item_field_static/gWorldMapOwlFaceTex"); // RGBA32 24x12
// Location plates, IA4 128x16, one per OwlWarpId (MM: sOwlWarpPauseItems -> map_name_static).
COW_MM_ASSET(sCowNameGreatBayCoast, "__OTR__map_name_static/gMapPointGreatBayCoastENGTex");
COW_MM_ASSET(sCowNameZoraCape, "__OTR__map_name_static/gMapPointZoraCapeENGTex");
COW_MM_ASSET(sCowNameSnowhead, "__OTR__map_name_static/gMapPointSnowheadENGTex");
COW_MM_ASSET(sCowNameMountainVillage, "__OTR__map_name_static/gMapPointMountainVillageENGTex");
COW_MM_ASSET(sCowNameClockTown, "__OTR__map_name_static/gMapPointClockTownENGTex");
COW_MM_ASSET(sCowNameMilkRoad, "__OTR__map_name_static/gMapPointMilkRoadENGTex");
COW_MM_ASSET(sCowNameWoodfall, "__OTR__map_name_static/gMapPointWoodfallENGTex");
COW_MM_ASSET(sCowNameSouthernSwamp, "__OTR__map_name_static/gMapPointSouthernSwampENGTex");
COW_MM_ASSET(sCowNameIkanaCanyon, "__OTR__map_name_static/gMapPointIkanaCanyonENGTex");
COW_MM_ASSET(sCowNameStoneTower, "__OTR__map_name_static/gMapPointStoneTowerENGTex");

// MM's map-page parchment tiles (sMapPageBgTextures): a 3x5 grid of 80x32 IA8 tiles framing the world map,
// in icon_item_static_yar except the col-1 row-0 title tile (language-specific, in icon_item_jpn_static).
COW_MM_ASSET(sCowMap00, "__OTR__icon_item_static_yar/gPauseMap00Tex");
COW_MM_ASSET(sCowMap01, "__OTR__icon_item_static_yar/gPauseMap01Tex");
COW_MM_ASSET(sCowMap02, "__OTR__icon_item_static_yar/gPauseMap02Tex");
COW_MM_ASSET(sCowMap03, "__OTR__icon_item_static_yar/gPauseMap03Tex");
COW_MM_ASSET(sCowMap04, "__OTR__icon_item_static_yar/gPauseMap04Tex");
COW_MM_ASSET(sCowMap10ENG, "__OTR__icon_item_jpn_static/gPauseMap10ENGTex");
COW_MM_ASSET(sCowMap11, "__OTR__icon_item_static_yar/gPauseMap11Tex");
COW_MM_ASSET(sCowMap12, "__OTR__icon_item_static_yar/gPauseMap12Tex");
COW_MM_ASSET(sCowMap13, "__OTR__icon_item_static_yar/gPauseMap13Tex");
COW_MM_ASSET(sCowMap14, "__OTR__icon_item_static_yar/gPauseMap14Tex");
COW_MM_ASSET(sCowMap20, "__OTR__icon_item_static_yar/gPauseMap20Tex");
COW_MM_ASSET(sCowMap21, "__OTR__icon_item_static_yar/gPauseMap21Tex");
COW_MM_ASSET(sCowMap22, "__OTR__icon_item_static_yar/gPauseMap22Tex");
COW_MM_ASSET(sCowMap23, "__OTR__icon_item_static_yar/gPauseMap23Tex");
COW_MM_ASSET(sCowMap24, "__OTR__icon_item_static_yar/gPauseMap24Tex");
const char* const sCowFrame[15] = {
    sCowMap00, sCowMap01, sCowMap02, sCowMap03, sCowMap04, sCowMap10ENG, sCowMap11, sCowMap12,
    sCowMap13, sCowMap14, sCowMap20, sCowMap21, sCowMap22, sCowMap23,    sCowMap24,
};
COW_MM_ASSET(sCowNamePanelL, "__OTR__icon_item_static_yar/gNamePanelLeftTex");  // IA8 72x24 (MM name box)
COW_MM_ASSET(sCowNamePanelR, "__OTR__icon_item_static_yar/gNamePanelRightTex"); // IA8 72x24

constexpr int COW_OWL_COUNT = 10; // OwlWarpId 0..9, bit i of MM's owlActivationFlags
constexpr int COW_OWL_CLOCK_TOWN = 4;
// A real pauseCtx->state that freezes the world and lets Interface_Draw show the Start "Return" prompt, but
// (>=18, outside 4..7/0xB..0x12, no KaleidoScope_Update case) draws no OOT pause pages -- kaleido no-ops.
constexpr u16 COW_PAUSE_STATE = 0x14;
constexpr int COW_MAP_W = 216;
constexpr int COW_MAP_H = 128;

const char* const sCowNameTexs[COW_OWL_COUNT] = {
    sCowNameGreatBayCoast, sCowNameZoraCape, sCowNameSnowhead,      sCowNameMountainVillage, sCowNameClockTown,
    sCowNameMilkRoad,      sCowNameWoodfall, sCowNameSouthernSwamp, sCowNameIkanaCanyon,     sCowNameStoneTower,
};
const char* const sCowOwlNames[COW_OWL_COUNT] = {
    "Great Bay Coast", "Zora Cape", "Snowhead",       "Mountain Village", "Clock Town",
    "Milk Road",       "Woodfall",  "Southern Swamp", "Ikana Canyon",     "Stone Tower",
};
// Owl icon quads in MM's map-page space (sVtxPageMapWorldQuadsX/Y), 24x12, top-left at (X, Y), Y up. On
// our flat map screen = (X + 161, 120 - Y).
const s16 sCowOwlX[COW_OWL_COUNT] = { -80, -64, -9, -3, -7, -16, -1, 23, 44, 54 };
const s16 sCowOwlY[COW_OWL_COUNT] = { -8, -38, 39, 26, 1, -7, -28, -27, -1, 24 };

enum CowState { COW_OFF, COW_CLOSING, COW_REFUSED, COW_FADE_IN, COW_SELECT, COW_CONFIRM, COW_FADE_OUT };
CowState sState = COW_OFF;
int sAlpha = 0;       // map / icons / plate
int sDim = 0;         // MM's R_PAUSE_OWL_WARP_ALPHA dimmer over the map
u16 sFlags = 0;       // activated statues
int sCursor = 0;      // OwlWarpId under the cursor
u16 sPendingText = 0; // refusal textbox to show once the ocarina session has closed (0 = open the chooser)
bool sWarpOnClose = false;
bool sStickLatch = false;
bool sHoldingLink = false;     // PLAYER_STATE1_IN_CUTSCENE set by us
bool sPromptCancelled = false; // B pressed while the Yes/No prompt was up: B closes it with the cursor still on Yes

// Recognition ring, fed from OnOcarinaNote (mirrors AudioOcarina_CheckSongsWithoutMusicStaff's rules:
// a note counts when the pitch changes and is not silence). The flag is consumed on the main thread.
constexpr int COW_RING = 8;
u8 sRing[COW_RING];
int sRingLen = 0;
u8 sPrevPitch = OCARINA_PITCH_NONE;
volatile bool sSongMatched = false;
const u8 kSoaringPitches[6] = { OCARINA_PITCH_F4, OCARINA_PITCH_B4, OCARINA_PITCH_D5,
                                OCARINA_PITCH_F4, OCARINA_PITCH_B4, OCARINA_PITCH_D5 };

CustomMessage sNoMarkMsg =
    CustomMessage("You have yet to leave your mark&on any of the statues in Termina.&There is nowhere to soar to.",
                  TEXTBOX_TYPE_BLACK);
CustomMessage sConfirmMsg;

bool CowEnabled() {
    return IS_RANDO && RAND_GET_OPTION(RSK_SONG_OF_SOARING_OOT) && Flags_GetRandomizerInf(RAND_INF_HAS_SONG_OF_SOARING);
}

void CowResetRing() {
    sRingLen = 0;
    sPrevPitch = OCARINA_PITCH_NONE;
}

void CowOnOcarinaNote(uint8_t pitch, float modulator, int8_t instrument) {
    (void)modulator;
    if (instrument == OCARINA_INSTRUMENT_OFF) {
        CowResetRing();
        return;
    }
    const bool counts = (pitch != sPrevPitch) && (pitch != OCARINA_PITCH_NONE);
    sPrevPitch = pitch;
    if (!counts) {
        return;
    }
    if (sRingLen == COW_RING) {
        for (int i = 1; i < COW_RING; i++) {
            sRing[i - 1] = sRing[i];
        }
        sRingLen--;
    }
    sRing[sRingLen++] = pitch;
    if (sRingLen < 6) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        if (sRing[sRingLen - 6 + i] != kSoaringPitches[i]) {
            return;
        }
    }
    sSongMatched = true;
    sRingLen = 0;
}

int CowFirstActivated(u16 flags) {
    if (flags & (1 << COW_OWL_CLOCK_TOWN)) {
        return COW_OWL_CLOCK_TOWN; // MM prefers Clock Town when it is lit (z_kaleido_setup.c)
    }
    for (int i = 0; i < COW_OWL_COUNT; i++) {
        if (flags & (1 << i)) {
            return i;
        }
    }
    return -1;
}

int CowStepCursor(int from, int dir) {
    for (int n = 1; n <= COW_OWL_COUNT; n++) {
        const int i = ((from + dir * n) % COW_OWL_COUNT + COW_OWL_COUNT) % COW_OWL_COUNT;
        if (sFlags & (1 << i)) {
            return i;
        }
    }
    return from;
}

// Enter/leave our owl-warp pause via a real pauseCtx->state (freezes the world like the pause menu). Leaving
// restores the player-actor overlay the pause swapped out (KaleidoScopeCall_LoadPlayer) and the Start alpha.
void CowFreeze(PlayState* play, bool freeze) {
    if (freeze) {
        play->pauseCtx.state = COW_PAUSE_STATE;
    } else if (play->pauseCtx.state == COW_PAUSE_STATE) {
        play->pauseCtx.state = 0;
        play->interfaceCtx.startAlpha = 0;
        KaleidoScopeCall_LoadPlayer();
        // Bring back the HUD the ocarina hid: we bypassed its normal restore, so run it here (z_message.c's
        // ocarina-close path) or the HUD stays dark until the next ocarina.
        if (gSaveContext.prevHudVisibilityMode == HUD_VISIBILITY_NO_CHANGE ||
            gSaveContext.prevHudVisibilityMode == HUD_VISIBILITY_NOTHING ||
            gSaveContext.prevHudVisibilityMode == HUD_VISIBILITY_NOTHING_ALT) {
            gSaveContext.prevHudVisibilityMode = HUD_VISIBILITY_ALL;
        }
        gSaveContext.hudVisibilityMode = HUD_VISIBILITY_NO_CHANGE;
        Interface_ChangeHudVisibilityMode(gSaveContext.prevHudVisibilityMode);
    }
}

void CowHoldLink(PlayState* play, bool hold) {
    Player* player = GET_PLAYER(play);
    if (hold) {
        player->stateFlags1 |= PLAYER_STATE1_IN_CUTSCENE;
    } else {
        player->stateFlags1 &= ~PLAYER_STATE1_IN_CUTSCENE;
    }
    sHoldingLink = hold;
}

// The song was played: end the ocarina session exactly like the vanilla B-cancel (z_message_PAL.c
// MSGMODE_OCARINA_PLAYING) and decide what follows once the message mode is clean. Main thread only.
void CowOnSongPlayed(PlayState* play) {
    MessageContext* msgCtx = &play->msgCtx;
    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
    Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME); // OOT's bank has no MM soaring jingle
    msgCtx->ocarinaMode = OCARINA_MODE_04;       // Link puts the ocarina away
    Message_CloseTextbox(play);
    // Same order as the vanilla warp-song branch (MSGMODE_SONG_PLAYED_ACT): a room that forbids warp
    // songs refuses first; the restriction-flag rule is rando-exempt there, so it is here.
    if (msgCtx->disableWarpSongs) {
        sPendingText = 0x88C; // "You can't warp here!"
    } else {
        const int flags = gComboOwlFlagsProvider ? gComboOwlFlagsProvider() : -1;
        sFlags = flags > 0 ? (u16)(flags & ((1 << COW_OWL_COUNT) - 1)) : 0;
        sCursor = CowFirstActivated(sFlags);
        sPendingText = (sCursor < 0) ? TEXT_COMBO_SOARING_NO_MARK : 0;
    }
    sState = COW_CLOSING;
}

// Close the chooser, then warp if asked.
void CowFinish(PlayState* play) {
    CowFreeze(play, false);
    CowHoldLink(play, false);
    sState = COW_OFF;
    if (sWarpOnClose) {
        sWarpOnClose = false;
        const int entrance = gComboOwlWarpEntranceProvider ? gComboOwlWarpEntranceProvider(sCursor) : -1;
        if (entrance >= 0) {
            Combo_RequestCrossSwitch(entrance); // persist + switch on the next clean frame
        }
    }
}

void CowUpdate() {
    PlayState* play = gPlayState;
    if (play == nullptr) {
        return;
    }
    MessageContext* msgCtx = &play->msgCtx;
    Input* input = &play->state.input[0];

    switch (sState) {
        case COW_OFF: {
            if (msgCtx->msgMode == MSGMODE_OCARINA_STARTING) {
                CowResetRing();
            }
            if (!sSongMatched) {
                return;
            }
            sSongMatched = false;
            if (!CowEnabled() || msgCtx->msgMode != MSGMODE_OCARINA_PLAYING ||
                msgCtx->ocarinaAction != OCARINA_ACTION_FREE_PLAY) {
                return; // not ours (a prompt, a scarecrow session, the song not owned): vanilla carries on
            }
            CowOnSongPlayed(play);
            return;
        }
        case COW_CLOSING: {
            if (msgCtx->msgMode != MSGMODE_NONE) {
                return; // the ocarina textbox is still closing
            }
            CowHoldLink(play, true); // Link stands still and START is refused (Play_InCsMode)
            if (sPendingText != 0) {
                Message_StartTextbox(play, sPendingText, NULL); // clean state, PauseWarp's shape
                sState = COW_REFUSED;
                return;
            }
            sAlpha = 0;
            sDim = 0;
            sWarpOnClose = false;
            sStickLatch = true; // require the stick to return to centre before it moves the cursor
            CowFreeze(play, true);
            func_800F64E0(1); // pause-menu open sound, like MM's owl map
            sState = COW_FADE_IN;
            return;
        }
        case COW_REFUSED: {
            if (msgCtx->msgMode == MSGMODE_NONE) {
                CowHoldLink(play, false);
                sState = COW_OFF;
            }
            return;
        }
        case COW_FADE_IN: {
            sAlpha = MIN(255, sAlpha + 31); // MM: alpha += 31, dimmer += 20 to 120
            sDim = MIN(120, sDim + 20);
            if (sAlpha >= 255 && sDim >= 120) {
                sState = COW_SELECT;
            }
            return;
        }
        case COW_SELECT: {
            int dir = 0;
            const s8 sx = input->cur.stick_x;
            const s8 sy = input->cur.stick_y;
            if (CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DUP)) {
                dir = -1;
            } else if (CHECK_BTN_ANY(input->press.button, BTN_DRIGHT | BTN_DDOWN)) {
                dir = 1;
            } else if (sx < -40 || sx > 40 || sy < -40 || sy > 40) {
                if (!sStickLatch) {
                    dir = (sx < -40 || sy > 40) ? -1 : 1;
                    sStickLatch = true;
                }
            } else {
                sStickLatch = false;
            }
            if (dir != 0) {
                const int next = CowStepCursor(sCursor, dir);
                if (next != sCursor) {
                    sCursor = next;
                    Sfx_PlaySfxCentered(NA_SE_SY_CURSOR);
                }
            }
            if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
                Sfx_PlaySfxCentered(NA_SE_SY_DECIDE);
                sConfirmMsg = CustomMessage(std::string("\x08Soar to %g") + sCowOwlNames[sCursor] + "%w?&&" +
                                                CustomMessage::TWO_WAY_CHOICE() + "%gYes&No%w\x09",
                                            TEXTBOX_TYPE_BLUE);
                sConfirmMsg.Format(); // '&' -> newline, colors, and the MESSAGE_END terminator
                Message_StartTextbox(play, TEXT_COMBO_SOARING_CONFIRM, NULL);
                sPromptCancelled = false;
                sState = COW_CONFIRM;
            } else if (CHECK_BTN_ANY(input->press.button, BTN_B | BTN_START)) {
                func_800F64E0(0); // pause-menu close sound, as when unpausing
                sState = COW_FADE_OUT;
            }
            return;
        }
        case COW_CONFIRM: {
            // The world stays frozen through the prompt, so step the message system ourselves (Play_Update
            // skips it while a pause state is set; Message_Draw still runs from Play_DrawOverlayElements).
            if (msgCtx->msgMode != MSGMODE_NONE) {
                if (!sPromptCancelled && CHECK_BTN_ALL(input->press.button, BTN_B)) {
                    // B cancels. Close it ourselves and skip Message_Update this frame so the message
                    // system doesn't also play its own confirm/pass sound on top of the cancel.
                    sPromptCancelled = true;
                    Sfx_PlaySfxCentered(NA_SE_SY_CANCEL);
                    Message_CloseTextbox(play);
                } else {
                    Message_Update(play); // navigate / confirm the choice, animate the close
                }
            }
            if (msgCtx->msgMode != MSGMODE_NONE) {
                return; // prompt still up (or still closing)
            }
            if (msgCtx->choiceIndex == 0 && !sPromptCancelled) {
                sWarpOnClose = true;
                sState = COW_FADE_OUT;
            } else {
                // "No" (A) already sounded via the message system; B sounded above. Just go back.
                sStickLatch = true;
                sState = COW_SELECT;
            }
            return;
        }
        case COW_FADE_OUT: {
            sAlpha = MAX(0, sAlpha - 63);
            sDim = MAX(0, sDim - 60);
            if (sAlpha > 0 || sDim > 0) {
                return;
            }
            CowFinish(play);
            return;
        }
    }
}

// Anything that reloads the world drops the chooser (a scene change can happen if a cutscene fires on
// the frame the song completes).
void CowReset() {
    if (gPlayState != nullptr) {
        CowFreeze(gPlayState, false);
        if (sHoldingLink) {
            CowHoldLink(gPlayState, false);
        }
    }
    sHoldingLink = false;
    sState = COW_OFF;
    sWarpOnClose = false;
    sSongMatched = false;
    CowResetRing();
}

} // namespace

// File scope, not the anonymous namespace: OPEN_DISPS / CLOSE_DISPS re-declare the frame-interpolation
// hooks at block scope, which inside a namespace would name a namespace-local symbol.
static void CowDraw() {
    PlayState* play = gPlayState;
    if (sState == COW_OFF || play == nullptr) {
        return;
    }

    // Drive the engine's Start "Return" prompt (Interface_Draw reads startAlpha right after this) and zero the
    // rest of the HUD so only it shows over the map. Off during the Yes/No box and the pre-freeze states.
    if (sState == COW_FADE_IN || sState == COW_SELECT || sState == COW_CONFIRM || sState == COW_FADE_OUT) {
        InterfaceContext* ic = &play->interfaceCtx;
        ic->startAlpha = (sState == COW_CONFIRM) ? 0 : (s16)sAlpha;
        ic->aAlpha = ic->bAlpha = ic->cLeftAlpha = ic->cDownAlpha = ic->cRightAlpha = 0;
        ic->healthAlpha = ic->magicAlpha = ic->minimapAlpha = 0;
    }

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    gDPSetTextureFilter(OVERLAY_DISP++,
                        G_TF_BILERP); // MM's owl map is bilinear (z_kaleido_map.c:722); point looked blocky upscaled

    // Everything MM-owned resolves against MM's ResourceManager between push and pop.
    gSPComboRMPush(OVERLAY_DISP++, "mm");

    // Map-page parchment frame behind the world map: the 3x5 grid of 80x32 IA8 tiles tinted MM's tan, with
    // row 0 as the "MAP" title band, rows 1-4 the framed map, and row 4 spilling below as a bottom border.
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 180, 180, 120, sAlpha);
    for (int i = 0; i < 15; i++) {
        const int fx = 37 + (i / 5) * 82; // column-major, like sMapPageBgTextures
        const int fy = 36 + (i % 5) * 33; // row 0 = title band above the map (y62), row 4 = bottom border
        gDPLoadTextureBlock(OVERLAY_DISP++, sCowFrame[i], G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        // dsdx/dtdy stretch the 80x32 tile over the 82x33 cell (80*1024/82, 32*1024/33).
        gSPTextureRectangle(OVERLAY_DISP++, fx << 2, fy << 2, (fx + 82) << 2, (fy + 33) << 2, G_TX_RENDERTILE, 0, 0,
                            999, 992);
    }
    gDPPipeSync(OVERLAY_DISP++);

    // Termina map: CI8 + 256-color palette, 16 strips of 8 rows (MM z_kaleido_map.c, flat path).
    gDPSetTextureFilter(OVERLAY_DISP++,
                        G_TF_POINT); // the world map itself is point-filtered in MM (z_kaleido_map.c:650)
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, sAlpha);
    gDPLoadTLUT_pal256(OVERLAY_DISP++, sCowMapTlut);
    gDPSetTextureLUT(OVERLAY_DISP++, G_TT_RGBA16);
    for (int j = 0, t = 61; j < COW_MAP_H / 8; j++, t += 8) {
        gDPLoadMultiTile(OVERLAY_DISP++, sCowMapTex, 0, G_TX_RENDERTILE, G_IM_FMT_CI, G_IM_SIZ_8b, COW_MAP_W, COW_MAP_H,
                         0, j * 8, COW_MAP_W - 1, (j + 1) * 8 - 1, 0, G_TX_NOMIRROR | G_TX_WRAP,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
        gDPSetTileSize(OVERLAY_DISP++, G_TX_RENDERTILE, 0, 0, (COW_MAP_W - 1) << G_TEXTURE_IMAGE_FRAC,
                       (8 - 1) << G_TEXTURE_IMAGE_FRAC);
        gSPTextureRectangle(OVERLAY_DISP++, 52 << 2, t << 2, (52 + COW_MAP_W) << 2, (t + 8) << 2, G_TX_RENDERTILE, 0, 0,
                            1 << 10, 1 << 10);
    }
    gDPSetTextureLUT(OVERLAY_DISP++, G_TT_NONE);      // the HUD / textbox after us must not index the palette
    gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP); // back to bilinear for the icons / panel / cursor

    // MM's dimmer over the map while choosing.
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, sDim);
    gDPFillRectangle(OVERLAY_DISP++, 51, 61, 271, 189);

    // One owl face per activated statue.
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, sAlpha);
    gDPLoadTextureBlock(OVERLAY_DISP++, sCowOwlTex, G_IM_FMT_RGBA, G_IM_SIZ_32b, 24, 12, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    for (int i = 0; i < COW_OWL_COUNT; i++) {
        if (!(sFlags & (1 << i))) {
            continue;
        }
        const int x = sCowOwlX[i] + 161;
        const int y = 120 - sCowOwlY[i];
        gSPTextureRectangle(OVERLAY_DISP++, x << 2, y << 2, (x + 24) << 2, (y + 12) << 2, G_TX_RENDERTILE, 0, 0,
                            1 << 10, 1 << 10);
    }

    // Name panel behind the location name -- MM's gItemNamePanelDL: gNamePanelLeft/RightTex, two IA8
    // 72x24 tiles side by side (144 wide), tinted MM's tan (prim 150,140,90), the same box MM draws.
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetRenderMode(OVERLAY_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 150, 140, 90, sAlpha);
    gDPLoadTextureBlock(OVERLAY_DISP++, sCowNamePanelL, G_IM_FMT_IA, G_IM_SIZ_8b, 72, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(OVERLAY_DISP++, 88 << 2, 195 << 2, 160 << 2, 219 << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    gDPLoadTextureBlock(OVERLAY_DISP++, sCowNamePanelR, G_IM_FMT_IA, G_IM_SIZ_8b, 72, 24, 0, G_TX_NOMIRROR | G_TX_WRAP,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(OVERLAY_DISP++, 160 << 2, 195 << 2, 232 << 2, 219 << 2, G_TX_RENDERTILE, 0, 0, 1 << 10,
                        1 << 10);

    // Location name on the panel, like MM's info panel ("Clock Town").
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, sAlpha);
    gDPLoadTextureBlock_4b(OVERLAY_DISP++, sCowNameTexs[sCursor], G_IM_FMT_IA, 128, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                           G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPTextureRectangle(OVERLAY_DISP++, 96 << 2, 199 << 2, 224 << 2, 215 << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);

    gSPComboRMPop(OVERLAY_DISP++);

    // Cursor: OOT's own pause-cursor corners (IA4 16x16, drawn at half size) framing the selected face.
    if (sState == COW_SELECT || sState == COW_CONFIRM) {
        const void* const corners[4] = { gPauseMenuCursorTopLeftTex, gPauseMenuCursorTopRightTex,
                                         gPauseMenuCursorBottomLeftTex, gPauseMenuCursorBottomRightTex };
        const int left = sCowOwlX[sCursor] + 161 - 4;
        const int top = 120 - sCowOwlY[sCursor] - 4;
        const int right = left + 32;
        const int bottom = top + 20;
        const int cx[4] = { left, right - 8, left, right - 8 };
        const int cy[4] = { top, top, bottom - 8, bottom - 8 };
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 120, 255, sAlpha);
        for (int k = 0; k < 4; k++) {
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, corners[k], G_IM_FMT_IA, 16, 16, 0, G_TX_NOMIRROR | G_TX_WRAP,
                                   G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangle(OVERLAY_DISP++, cx[k] << 2, cy[k] << 2, (cx[k] + 8) << 2, (cy[k] + 8) << 2,
                                G_TX_RENDERTILE, 0, 0, 2 << 10, 2 << 10);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

static void RegisterComboOwlWarp() {
    // Registered whenever a rando save is loaded; the seed option and item ownership are checked per use.
    const bool on = IS_RANDO;
    CowReset();
    // Format() appends MESSAGE_END; without it LoadIntoFont's raw text has no terminator and Message_Decode
    // overruns the 200-byte buffer into interfaceCtx (crash). Once only: it mutates.
    static bool sNoMarkFormatted = false;
    if (!sNoMarkFormatted) {
        sNoMarkMsg.Format();
        sNoMarkFormatted = true;
    }
    COND_HOOK(OnOcarinaNote, on, CowOnOcarinaNote);
    COND_HOOK(OnGameFrameUpdate, on, CowUpdate);
    COND_HOOK(OnPlayDrawEnd, on, CowDraw);
    COND_HOOK(OnSceneInit, on, [](int16_t sceneNum) { CowReset(); });
    COND_HOOK(OnLoadGame, on, [](int32_t fileNum) { CowReset(); });
    COND_ID_HOOK(OnOpenText, TEXT_COMBO_SOARING_NO_MARK, on, [](uint16_t* textId, bool* loadFromMessageTable) {
        sNoMarkMsg.LoadIntoFont();
        *loadFromMessageTable = false;
    });
    COND_ID_HOOK(OnOpenText, TEXT_COMBO_SOARING_CONFIRM, on, [](uint16_t* textId, bool* loadFromMessageTable) {
        sConfirmMsg.LoadIntoFont();
        *loadFromMessageTable = false;
    });
}

static RegisterShipInitFunc initFunc(RegisterComboOwlWarp, { "IS_RANDO" });

#endif // COMBO_BUILD
