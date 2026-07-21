#ifndef COMBO_TITLE_LOGOS_H
#define COMBO_TITLE_LOGOS_H

// ComboShip: dual-game title screen logos — OOT's logo cluster shrunk and shifted left, MM's
// title logo (mask + Zelda logo + subtitle, ROM-extracted assets from MM's archives) drawn
// beside it on the right.
// Both fade with EnMag's existing mainAlpha sequence.
//
// Included ONLY from soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c, after its helper functions
// and the LOGO_TEX / LOGO_X_SHIFT macros, so EnMag, the GBI macros, OOT's object_mag symbols and
// EnMag_DrawTextureI8 / Gfx_SetupDL_*Ptr are all in scope.
//
// MM texture paths resolve against MM's ResourceManager via the G_COMBO_RM_PUSH/POP interpreter
// bracket (libultraship/src/fast/interpreter.cpp; precedent combo/menu/ComboForeignAnim.h). If
// MM's RM is not registered the push logs once and the MM cluster simply doesn't render.

// --- layout knobs -------------------------------------------------------------------------------
#define CTL_OOT_SCALE 0.78f // OOT logo cluster
#define CTL_MM_SCALE 0.74f  // MM cluster — smaller so its ZELDA wordmark matches OOT's baked-in one
#define CTL_OOT_CX 82       // screen-space center X of the OOT cluster (320x240 space)
#define CTL_MM_CX 229       // screen-space center X of the MM cluster
#define CTL_CY 100          // shared center Y (both clusters' native anchor Y)
// ------------------------------------------------------------------------------------------------

// Active cluster scale — set to CTL_OOT_SCALE / CTL_MM_SCALE around each cluster's draw block.
static f32 sCtlScale = CTL_OOT_SCALE;

#define CTL_DIM(d) ((s16)((d)*sCtlScale + 0.5f))
#define CTL_DXDY ((u16)(1024.0f / sCtlScale + 0.5f))

// MM's En_Mag native layout anchor (cluster center the elements below are positioned around).
#define CTL_MM_ANCHOR_X 154.0f
#define CTL_MM_ANCHOR_Y 100.0f

// MM title logo assets (ROM-extracted, in mm.o2r). Redeclared here — soh cannot include mm headers.
#define dgTitleScreenMajorasMaskTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskTex[] = dgTitleScreenMajorasMaskTex;
#define dgTitleScreenZeldaLogoTex "__OTR__objects/object_mag/gTitleScreenZeldaLogoTex"
static const ALIGN_ASSET(2) char gTitleScreenZeldaLogoTex[] = dgTitleScreenZeldaLogoTex;
#define dgTitleScreenTheLegendOfTextTex "__OTR__objects/object_mag/gTitleScreenTheLegendOfTextTex"
static const ALIGN_ASSET(2) char gTitleScreenTheLegendOfTextTex[] = dgTitleScreenTheLegendOfTextTex;
#define dgTitleScreenMajorasMaskSubtitleTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskSubtitleTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskSubtitleTex[] = dgTitleScreenMajorasMaskSubtitleTex;
#define dgTitleScreenMajorasMaskSubtitleMaskTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskSubtitleMaskTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskSubtitleMaskTex[] = dgTitleScreenMajorasMaskSubtitleMaskTex;

// MM flame-effect grid behind its logo: 2x3 I4 masks + 4 I8 flame textures (display set).
#define CTL_MM_EFFECT_MASK(n)                                                  \
    static const ALIGN_ASSET(2) char gTitleScreenDisplayEffectMask##n##Tex[] = \
        "__OTR__objects/object_mag/gTitleScreenDisplayEffectMask" #n "Tex"
CTL_MM_EFFECT_MASK(00);
CTL_MM_EFFECT_MASK(01);
CTL_MM_EFFECT_MASK(02);
CTL_MM_EFFECT_MASK(10);
CTL_MM_EFFECT_MASK(11);
CTL_MM_EFFECT_MASK(12);
#define CTL_MM_FLAME(n)                                            \
    static const ALIGN_ASSET(2) char gTitleScreenFlame##n##Tex[] = \
        "__OTR__objects/object_mag/gTitleScreenFlame" #n "Tex"
CTL_MM_FLAME(0);
CTL_MM_FLAME(1);
CTL_MM_FLAME(2);
CTL_MM_FLAME(3);

// Map a native-layout coordinate (relative to its cluster anchor) into screen space.
static s16 ComboTitle_TX(f32 x, f32 anchorX, s16 destCX) {
    return (s16)(destCX + (x - anchorX) * sCtlScale + 0.5f);
}
static s16 ComboTitle_TY(f32 y, f32 anchorY) {
    return (s16)(CTL_CY + (y - anchorY) * sCtlScale + 0.5f);
}

// EnMag_DrawImageRGBA32 with a scaled destination rect (the original is fixed 1:1).
static void ComboTitle_DrawImageRGBA32Scaled(Gfx** gfxp, s16 centerX, s16 centerY, const char* source, u32 width,
                                             u32 height) {
    Gfx* gfx = *gfxp;
    u32 rectW = CTL_DIM(width);
    u32 rectH = CTL_DIM(height);
    u32 rectLeft = centerX - rectW / 2;
    u32 rectTop = centerY - rectH / 2;

    Gfx_SetupDL_56Ptr(&gfx);

    gDPSetTileCustom(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_32b, width, height, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                     G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

    gDPSetTextureImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_32b, width, source);

    gDPLoadSync(gfx++);
    gDPLoadTile(gfx++, G_TX_LOADTILE, 0, 0, (width - 1) << 2, (height - 1) << 2);

    gSPTextureRectangle(gfx++, rectLeft << 2, rectTop << 2, (rectLeft + rectW) << 2, (rectTop + rectH) << 2,
                        G_TX_RENDERTILE, 0, 0, CTL_DXDY, CTL_DXDY);

    *gfxp = gfx;
}

// Draws in place of the original logo block of EnMag_DrawInner (shield logo + subtitle texts +
// MQ subtitle), which stays untouched in z_en_mag.c and is skipped via the return value (always 1
// = handled). Same draw sequence and combiner state as the original, coordinates transformed
// around the original cluster anchor (160 + LOGO_X_SHIFT, 100), then MM's cluster on the right.
static s32 ComboTitle_DrawLogos(Gfx** gfxP, EnMag* mag, s32 isMQ) {
    // OOT's 3x3 flame-effect mask grid (same textures/order as the vendored block's local array).
    static void* sCtlOotEffectMasks[] = {
        gTitleEffectMask00Tex, gTitleEffectMask01Tex, gTitleEffectMask02Tex,
        gTitleEffectMask10Tex, gTitleEffectMask11Tex, gTitleEffectMask12Tex,
        gTitleEffectMask20Tex, gTitleEffectMask21Tex, gTitleEffectMask22Tex,
    };
    // MM's 2x3 display-effect grid + per-cell flame textures (MM's sEffectTextures order).
    static void* sCtlMmEffectMasks[] = {
        gTitleScreenDisplayEffectMask00Tex, gTitleScreenDisplayEffectMask01Tex, gTitleScreenDisplayEffectMask02Tex,
        gTitleScreenDisplayEffectMask10Tex, gTitleScreenDisplayEffectMask11Tex, gTitleScreenDisplayEffectMask12Tex,
    };
    static void* sCtlMmEffectFlames[] = {
        gTitleScreenFlame0Tex, gTitleScreenFlame1Tex, gTitleScreenFlame1Tex,
        gTitleScreenFlame2Tex, gTitleScreenFlame3Tex, gTitleScreenFlame3Tex,
    };
    // MM's display-effect colors oscillate between two targets over 30 frames
    // (EnMag_UpdateDisplayEffectColors in mm z_en_mag.c); replicated here since OOT's EnMag
    // doesn't carry MM's state. Env green stays at its init value (255).
    static s16 sCtlMmPrimTargets[2][3] = { { 155, 255, 55 }, { 255, 155, 255 } };
    static s16 sCtlMmEnvTargets[2][3] = { { 255, 255, 255 }, { 0, 255, 155 } };
    static s16 sCtlMmPrim[3] = { 255, 155, 255 };
    static s16 sCtlMmEnv[3] = { 0, 255, 155 };
    static s16 sCtlMmColorTimer = 30;
    static s16 sCtlMmColorIdx = 0;
    Gfx* gfx = *gfxP;
    f32 ox = 160 + LOGO_X_SHIFT; // OOT native cluster anchor X
    s16 mainAlpha = (s16)mag->mainAlpha;
    s16 i, j, k;
    sCtlScale = CTL_OOT_SCALE; // OOT blocks use this; MM blocks flip it below and restore

    // --- flame effects (behind both logos; same combiner state, OOT's fade timing) ---
    gDPSetCycleType(gfx++, G_CYC_2CYCLE);
    gDPSetAlphaCompare(gfx++, G_AC_THRESHOLD);
    gDPSetRenderMode(gfx++, G_RM_PASS, G_RM_CLD_SURF2);
    gDPSetCombineLERP(gfx++, TEXEL1, PRIMITIVE, PRIM_LOD_FRAC, TEXEL0, TEXEL1, 1, PRIM_LOD_FRAC, TEXEL0, PRIMITIVE,
                      ENVIRONMENT, COMBINED, ENVIRONMENT, COMBINED, 0, PRIMITIVE, 0);

    gDPSetPrimColor(gfx++, 0, (s16)mag->effectPrimLodFrac, (s16)mag->effectPrimColor[0], (s16)mag->effectPrimColor[1],
                    (s16)mag->effectPrimColor[2], (s16)mag->effectAlpha);
    gDPSetEnvColor(gfx++, (s16)mag->effectEnvColor[0], (s16)mag->effectEnvColor[1], (s16)mag->effectEnvColor[2], 255);

    if ((s16)mag->effectPrimLodFrac != 0) {
        // OOT grid: native 3x3, 64px pitch, origin (64 + LOGO_X_SHIFT, 0).
        for (k = 0, i = 0; i < 3; i++) {
            for (j = 0; j < 3; j++, k++) {
                EnMag_DrawEffectTextures(&gfx, sCtlOotEffectMasks[k], gTitleFlameEffectTex, 64, 64, 32, 32,
                                         ComboTitle_TX(64 + LOGO_X_SHIFT + j * 64, ox, CTL_OOT_CX),
                                         ComboTitle_TY(i * 64, 100.0f), CTL_DIM(64), CTL_DIM(64), CTL_DXDY, CTL_DXDY, 1,
                                         1, k, mag);
            }
        }

        // Step MM's oscillating effect colors (once per frame — this draw runs once per frame).
        sCtlMmColorTimer--;
        for (i = 0; i < 3; i++) {
            if (sCtlMmColorTimer <= 0) {
                sCtlMmPrim[i] = sCtlMmPrimTargets[sCtlMmColorIdx][i];
                sCtlMmEnv[i] = sCtlMmEnvTargets[sCtlMmColorIdx][i];
            } else {
                sCtlMmPrim[i] += (sCtlMmPrimTargets[sCtlMmColorIdx][i] - sCtlMmPrim[i]) / sCtlMmColorTimer;
                sCtlMmEnv[i] += (sCtlMmEnvTargets[sCtlMmColorIdx][i] - sCtlMmEnv[i]) / sCtlMmColorTimer;
            }
        }
        if (sCtlMmColorTimer <= 0) {
            sCtlMmColorTimer = 30;
            sCtlMmColorIdx ^= 1;
        }

        // MM grid: native 2x3, 64px pitch, origin (57, 38); per-cell flame texture (flag 0 so each
        // cell loads its own), MM colors with OOT's lodfrac/alpha so both sides fade in sync.
        gSPComboRMPush(gfx++, "mm");
        sCtlScale = CTL_MM_SCALE;

        gDPSetPrimColor(gfx++, 0, (s16)mag->effectPrimLodFrac, sCtlMmPrim[0], sCtlMmPrim[1], sCtlMmPrim[2],
                        (s16)mag->effectAlpha);
        gDPSetEnvColor(gfx++, sCtlMmEnv[0], sCtlMmEnv[1], sCtlMmEnv[2], 255);

        for (k = 0, i = 0; i < 2; i++) {
            for (j = 0; j < 3; j++, k++) {
                EnMag_DrawEffectTextures(&gfx, sCtlMmEffectMasks[k], sCtlMmEffectFlames[k], 64, 64, 32, 32,
                                         ComboTitle_TX(57 + j * 64, CTL_MM_ANCHOR_X, CTL_MM_CX),
                                         ComboTitle_TY(38 + i * 64, CTL_MM_ANCHOR_Y), CTL_DIM(64), CTL_DIM(64),
                                         CTL_DXDY, CTL_DXDY, 1, 1, 0, mag);
            }
        }

        gSPComboRMPop(gfx++);
        sCtlScale = CTL_OOT_SCALE;
    }

    // --- OOT cluster (left) ---
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, mainAlpha);

    if (mainAlpha != 0) {
        ComboTitle_DrawImageRGBA32Scaled(&gfx, CTL_OOT_CX, CTL_CY, LOGO_TEX, 160, 160);
    }

    Gfx_SetupDL_39Ptr(&gfx);

    gDPPipeSync(gfx++);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetCombineLERP(gfx++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);

    if (mainAlpha < 100) {
        gDPSetRenderMode(gfx++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    } else {
        gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    }

    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, mainAlpha);

    if (!isMQ) {
        gDPSetEnvColor(gfx++, 100, 0, 100, 255);
    } else {
        gDPSetEnvColor(gfx++, 0, 0, 100, 255);
    }

    if (mainAlpha != 0) {
        // Colored text positions transform; the 1px shadow offset is kept in screen space so the
        // shadow stays crisp at the reduced scale.
        s16 legendL = ComboTitle_TX(153 + LOGO_X_SHIFT, ox, CTL_OOT_CX);
        s16 legendT = ComboTitle_TY(72, 100.0f);
        s16 ocarinaL = ComboTitle_TX(151 + LOGO_X_SHIFT, ox, CTL_OOT_CX);
        s16 ocarinaT = ComboTitle_TY(126, 100.0f);

        EnMag_DrawTextureI8(&gfx, gTitleTheLegendOfTextTex, 72, 8, legendL + 1, legendT + 1, CTL_DIM(72), CTL_DIM(8),
                            CTL_DXDY, CTL_DXDY);
        EnMag_DrawTextureI8(&gfx, gTitleOcarinaOfTimeTMTextTex, 96, 8, ocarinaL + 1, ocarinaT + 1, CTL_DIM(96),
                            CTL_DIM(8), CTL_DXDY, CTL_DXDY);

        gDPPipeSync(gfx++);

        if (!isMQ) {
            gDPSetPrimColor(gfx++, 0, 0, 200, 200, 150, mainAlpha);
            gDPSetEnvColor(gfx++, 100, 100, 50, 255);
        } else {
            gDPSetPrimColor(gfx++, 0, 0, 100, 150, 255, mainAlpha);
            gDPSetEnvColor(gfx++, 20, 80, 160, 255);
        }

        EnMag_DrawTextureI8(&gfx, gTitleTheLegendOfTextTex, 72, 8, legendL, legendT, CTL_DIM(72), CTL_DIM(8), CTL_DXDY,
                            CTL_DXDY);
        EnMag_DrawTextureI8(&gfx, gTitleOcarinaOfTimeTMTextTex, 96, 8, ocarinaL, ocarinaT, CTL_DIM(96), CTL_DIM(8),
                            CTL_DXDY, CTL_DXDY);

        if (isMQ) {
            gDPPipeSync(gfx++);
            gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, (s16)mag->subAlpha);

            if (gSaveContext.language == LANGUAGE_JPN || ResourceMgr_GetGameVersion(0) == OOT_NTSC_JP_MQ) {
                ComboTitle_DrawImageRGBA32Scaled(&gfx, ComboTitle_TX(235, ox, CTL_OOT_CX), ComboTitle_TY(149, 100.0f),
                                                 gTitleUraLogoTex, 40, 40);
            } else {
                ComboTitle_DrawImageRGBA32Scaled(&gfx, ComboTitle_TX(174, ox, CTL_OOT_CX), ComboTitle_TY(145, 100.0f),
                                                 gTitleMasterQuestSubtitleTex, 128, 32);
            }
        }
    }

    // --- MM cluster (right) ---
    // Native positions from mm/src/overlays/actors/ovl_En_Mag/z_en_mag.c (mask 128x112 @ center
    // 124,103; Zelda logo 144x64 @ center 177,105; "The Legend of" 72x8 @ 158,71; subtitle 104x16
    // @ 151,124). MM's animated sparkle effects are intentionally skipped — static logo, fades
    // with mainAlpha.
    if (mainAlpha != 0) {
        gSPComboRMPush(gfx++, "mm");
        sCtlScale = CTL_MM_SCALE;

        gDPPipeSync(gfx++);
        gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, mainAlpha);

        ComboTitle_DrawImageRGBA32Scaled(&gfx, ComboTitle_TX(124, CTL_MM_ANCHOR_X, CTL_MM_CX),
                                         ComboTitle_TY(103, CTL_MM_ANCHOR_Y), gTitleScreenMajorasMaskTex, 128, 112);
        ComboTitle_DrawImageRGBA32Scaled(&gfx, ComboTitle_TX(177, CTL_MM_ANCHOR_X, CTL_MM_CX),
                                         ComboTitle_TY(105, CTL_MM_ANCHOR_Y), gTitleScreenZeldaLogoTex, 144, 64);

        Gfx_SetupDL_39Ptr(&gfx);

        gDPSetAlphaCompare(gfx++, G_AC_NONE);
        gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);

        if (mainAlpha < 100) {
            gDPSetRenderMode(gfx++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
        } else {
            gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
        }

        // "MAJORA'S MASK" subtitle: dark mask backing pass, then the colored pass (MM's order).
        gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, mainAlpha);
        EnMag_DrawTextureI8(&gfx, gTitleScreenMajorasMaskSubtitleMaskTex, 104, 16,
                            ComboTitle_TX(151, CTL_MM_ANCHOR_X, CTL_MM_CX), ComboTitle_TY(124, CTL_MM_ANCHOR_Y),
                            CTL_DIM(104), CTL_DIM(16), CTL_DXDY, CTL_DXDY);

        gDPPipeSync(gfx++);
        gDPSetPrimColor(gfx++, 0, 120, 208, 102, 222, mainAlpha);
        EnMag_DrawTextureI8(&gfx, gTitleScreenMajorasMaskSubtitleTex, 104, 16,
                            ComboTitle_TX(151, CTL_MM_ANCHOR_X, CTL_MM_CX), ComboTitle_TY(124, CTL_MM_ANCHOR_Y),
                            CTL_DIM(104), CTL_DIM(16), CTL_DXDY, CTL_DXDY);

        // "The Legend of" above the Zelda logo.
        gDPPipeSync(gfx++);
        gDPSetPrimColor(gfx++, 0, 0, 208, 102, 222, mainAlpha);
        EnMag_DrawTextureI8(&gfx, gTitleScreenTheLegendOfTextTex, 72, 8, ComboTitle_TX(158, CTL_MM_ANCHOR_X, CTL_MM_CX),
                            ComboTitle_TY(71, CTL_MM_ANCHOR_Y), CTL_DIM(72), CTL_DIM(8), CTL_DXDY, CTL_DXDY);

        gSPComboRMPop(gfx++);
        sCtlScale = CTL_OOT_SCALE;
    }

    *gfxP = gfx;
    return 1;
}

#endif /* COMBO_TITLE_LOGOS_H */
