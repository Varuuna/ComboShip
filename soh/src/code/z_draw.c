#include "global.h"
#include "objects/object_gi_key/object_gi_key.h"
#include "objects/object_gi_jewel/object_gi_jewel.h"
#include "objects/object_gi_melody/object_gi_melody.h"
#include "objects/object_gi_heart/object_gi_heart.h"
#include "objects/object_gi_compass/object_gi_compass.h"
#include "objects/object_gi_bosskey/object_gi_bosskey.h"
#include "objects/object_gi_medal/object_gi_medal.h"
#include "objects/object_gi_nuts/object_gi_nuts.h"
#include "objects/object_gi_hearts/object_gi_hearts.h"
#include "objects/object_gi_arrowcase/object_gi_arrowcase.h"
#include "objects/object_gi_bombpouch/object_gi_bombpouch.h"
#include "objects/object_gi_bottle/object_gi_bottle.h"
#include "objects/object_gi_stick/object_gi_stick.h"
#include "objects/object_gi_map/object_gi_map.h"
#include "objects/object_gi_shield_1/object_gi_shield_1.h"
#include "objects/object_gi_magicpot/object_gi_magicpot.h"
#include "objects/object_gi_bomb_1/object_gi_bomb_1.h"
#include "objects/object_gi_purse/object_gi_purse.h"
#include "objects/object_gi_gerudo/object_gi_gerudo.h"
#include "objects/object_gi_arrow/object_gi_arrow.h"
#include "objects/object_gi_bomb_2/object_gi_bomb_2.h"
#include "objects/object_gi_egg/object_gi_egg.h"
#include "objects/object_gi_scale/object_gi_scale.h"
#include "objects/object_gi_shield_2/object_gi_shield_2.h"
#include "objects/object_gi_hookshot/object_gi_hookshot.h"
#include "objects/object_gi_ocarina/object_gi_ocarina.h"
#include "objects/object_gi_milk/object_gi_milk.h"
#include "objects/object_gi_pachinko/object_gi_pachinko.h"
#include "objects/object_gi_boomerang/object_gi_boomerang.h"
#include "objects/object_gi_bow/object_gi_bow.h"
#include "objects/object_gi_glasses/object_gi_glasses.h"
#include "objects/object_gi_liquid/object_gi_liquid.h"
#include "objects/object_gi_shield_3/object_gi_shield_3.h"
#include "objects/object_gi_letter/object_gi_letter.h"
#include "objects/object_gi_clothes/object_gi_clothes.h"
#include "objects/object_gi_bean/object_gi_bean.h"
#include "objects/object_gi_fish/object_gi_fish.h"
#include "objects/object_gi_saw/object_gi_saw.h"
#include "objects/object_gi_hammer/object_gi_hammer.h"
#include "objects/object_gi_grass/object_gi_grass.h"
#include "objects/object_gi_longsword/object_gi_longsword.h"
#include "objects/object_gi_niwatori/object_gi_niwatori.h"
#include "objects/object_gi_bottle_letter/object_gi_bottle_letter.h"
#include "objects/object_gi_ocarina_0/object_gi_ocarina_0.h"
#include "objects/object_gi_boots_2/object_gi_boots_2.h"
#include "objects/object_gi_seed/object_gi_seed.h"
#include "objects/object_gi_gloves/object_gi_gloves.h"
#include "objects/object_gi_coin/object_gi_coin.h"
#include "objects/object_gi_ki_tan_mask/object_gi_ki_tan_mask.h"
#include "objects/object_gi_redead_mask/object_gi_redead_mask.h"
#include "objects/object_gi_skj_mask/object_gi_skj_mask.h"
#include "objects/object_gi_rabit_mask/object_gi_rabit_mask.h"
#include "objects/object_gi_truth_mask/object_gi_truth_mask.h"
#include "objects/object_gi_eye_lotion/object_gi_eye_lotion.h"
#include "objects/object_gi_powder/object_gi_powder.h"
#include "objects/object_gi_mushroom/object_gi_mushroom.h"
#include "objects/object_gi_ticketstone/object_gi_ticketstone.h"
#include "objects/object_gi_brokensword/object_gi_brokensword.h"
#include "objects/object_gi_prescription/object_gi_prescription.h"
#include "objects/object_gi_bracelet/object_gi_bracelet.h"
#include "objects/object_gi_soldout/object_gi_soldout.h"
#include "objects/object_gi_frog/object_gi_frog.h"
#include "objects/object_gi_golonmask/object_gi_golonmask.h"
#include "objects/object_gi_zoramask/object_gi_zoramask.h"
#include "objects/object_gi_gerudomask/object_gi_gerudomask.h"
#include "objects/object_gi_hoverboots/object_gi_hoverboots.h"
#include "objects/object_gi_m_arrow/object_gi_m_arrow.h"
#include "objects/object_gi_sutaru/object_gi_sutaru.h"
#include "objects/object_gi_goddess/object_gi_goddess.h"
#include "objects/object_gi_fire/object_gi_fire.h"
#include "objects/object_gi_insect/object_gi_insect.h"
#include "objects/object_gi_butterfly/object_gi_butterfly.h"
#include "objects/object_gi_ghost/object_gi_ghost.h"
#include "objects/object_gi_soul/object_gi_soul.h"
#include "objects/object_gi_dekupouch/object_gi_dekupouch.h"
#include "objects/object_gi_rupy/object_gi_rupy.h"
#include "objects/object_gi_sword_1/object_gi_sword_1.h"
#include "objects/object_fish/object_fish.h"
#include "objects/object_st/object_st.h"

#include "soh_assets.h"

// "Get Item" Model Draw Functions
void GetItem_DrawMaskOrBombchu(PlayState* play, s16 drawId);
void GetItem_DrawSoldOut(PlayState* play, s16 drawId);
void GetItem_DrawBlueFire(PlayState* play, s16 drawId);
void GetItem_DrawPoes(PlayState* play, s16 drawId);
void GetItem_DrawFairy(PlayState* play, s16 drawId);
void GetItem_DrawMirrorShield(PlayState* play, s16 drawId);
void GetItem_DrawSkullToken(PlayState* play, s16 drawId);
void GetItem_DrawEggOrMedallion(PlayState* play, s16 drawId);
void GetItem_DrawCompass(PlayState* play, s16 drawId);
void GetItem_DrawPotion(PlayState* play, s16 drawId);
void GetItem_DrawGoronSword(PlayState* play, s16 drawId);
void GetItem_DrawDekuNuts(PlayState* play, s16 drawId);
void GetItem_DrawRecoveryHeart(PlayState* play, s16 drawId);
void GetItem_DrawFish(PlayState* play, s16 drawId);
void GetItem_DrawOpa0(PlayState* play, s16 drawId);
void GetItem_DrawOpa0Xlu1(PlayState* play, s16 drawId);
void GetItem_DrawXlu01(PlayState* play, s16 drawId);
void GetItem_DrawOpa10Xlu2(PlayState* play, s16 drawId);
void GetItem_DrawMagicArrow(PlayState* play, s16 drawId);
void GetItem_DrawMagicSpell(PlayState* play, s16 drawId);
void GetItem_DrawOpa1023(PlayState* play, s16 drawId);
void GetItem_DrawOpa10Xlu32(PlayState* play, s16 drawId);
void GetItem_DrawSmallRupee(PlayState* play, s16 drawId);
void GetItem_DrawScale(PlayState* play, s16 drawId);
void GetItem_DrawBulletBag(PlayState* play, s16 drawId);
void GetItem_DrawWallet(PlayState* play, s16 drawId);
void GetItem_DrawJewel(PlayState* play, s16 drawId);
void GetItem_DrawJewelKokiri(PlayState* play, s16 drawId);
void GetItem_DrawJewelGoron(PlayState* play, s16 drawId);
void GetItem_DrawJewelZora(PlayState* play, s16 drawId);
void GetItem_DrawGenericMusicNote(PlayState* play, s16 drawId);
void GetItem_DrawTriforcePiece(PlayState* play, s16 drawId);
void GetItem_DrawFishingPole(PlayState* play, s16 drawId);

typedef struct {
    /* 0x00 */ void (*drawFunc)(PlayState*, s16);
    /* 0x04 */ Gfx* dlists[8];
} DrawItemTableEntry; // size = 0x24

DrawItemTableEntry sDrawItemTable[] = {
    // bottle, OBJECT_GI_BOTTLE
    { GetItem_DrawOpa0Xlu1, { gGiBottleStopperDL, gGiBottleDL } },
    // small key, OBJECT_GI_KEY
    { GetItem_DrawOpa0, { gGiSmallKeyDL } },
    // minuet of forest, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiMinuetColorDL, gGiSongNoteDL } },
    // bolero of fire, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiBoleroColorDL, gGiSongNoteDL } },
    // serenade of water, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiSerenadeColorDL, gGiSongNoteDL } },
    // requiem of spirit, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiRequiemColorDL, gGiSongNoteDL } },
    // nocturne of shadow, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiNocturneColorDL, gGiSongNoteDL } },
    // prelude of light, OBJECT_GI_MELODY
    { GetItem_DrawXlu01, { gGiPreludeColorDL, gGiSongNoteDL } },
    // recovery heart, OBJECT_GI_HEART
    { GetItem_DrawRecoveryHeart, { gGiRecoveryHeartDL } },
    // boss key, OBJECT_GI_BOSSKEY
    { GetItem_DrawOpa0Xlu1, { gGiBossKeyDL, gGiBossKeyGemDL } },
    // compass, OBJECT_GI_COMPASS
    { GetItem_DrawCompass, { gGiCompassDL, gGiCompassGlassDL } },
    // forest medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiForestMedallionFaceDL, gGiMedallionDL } },
    // fire medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiFireMedallionFaceDL, gGiMedallionDL } },
    // water medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiWaterMedallionFaceDL, gGiMedallionDL } },
    // spirit medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiSpiritMedallionFaceDL, gGiMedallionDL } },
    // shadow medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiShadowMedallionFaceDL, gGiMedallionDL } },
    // light medallion, OBJECT_GI_MEDAL
    { GetItem_DrawEggOrMedallion, { gGiLightMedallionFaceDL, gGiMedallionDL } },
    // deku nuts, OBJECT_GI_NUTS
    { GetItem_DrawDekuNuts, { gGiNutDL } },
    // heart container, OBJECT_GI_HEARTS
    { GetItem_DrawXlu01, { gGiHeartBorderDL, gGiHeartContainerDL } },
    // heart piece, OBJECT_GI_HEARTS
    { GetItem_DrawXlu01, { gGiHeartBorderDL, gGiHeartPieceDL } },
    // quiver 30, OBJECT_GI_ARROWCASE
    { GetItem_DrawOpa1023, { gGiQuiverInnerDL, gGiQuiver30InnerColorDL, gGiQuiver30OuterColorDL, gGiQuiverOuterDL } },
    // quiver 40, OBJECT_GI_ARROWCASE
    { GetItem_DrawOpa1023, { gGiQuiverInnerDL, gGiQuiver40InnerColorDL, gGiQuiver40OuterColorDL, gGiQuiverOuterDL } },
    // quiver 50, OBJECT_GI_ARROWCASE
    { GetItem_DrawOpa1023, { gGiQuiverInnerDL, gGiQuiver50InnerColorDL, gGiQuiver50OuterColorDL, gGiQuiverOuterDL } },
    // bomb bag 20, OBJECT_GI_BOMBPOUCH
    { GetItem_DrawOpa1023, { gGiBombBagDL, gGiBombBag20BagColorDL, gGiBombBag20RingColorDL, gGiBombBagRingDL } },
    // bomb bag 30, OBJECT_GI_BOMBPOUCH
    { GetItem_DrawOpa1023, { gGiBombBagDL, gGiBombBag30BagColorDL, gGiBombBag30RingColorDL, gGiBombBagRingDL } },
    // bomb bag 40, OBJECT_GI_BOMBPOUCH
    { GetItem_DrawOpa1023, { gGiBombBagDL, gGiBombBag40BagColorDL, gGiBombBag40RingColorDL, gGiBombBagRingDL } },
    // stick, OBJECT_GI_STICK
    { GetItem_DrawOpa0, { gGiStickDL } },
    // dungeon map, OBJECT_GI_MAP
    { GetItem_DrawOpa0, { gGiDungeonMapDL } },
    // deku shield, OBJECT_GI_SHIELD_1
    { GetItem_DrawOpa0, { gGiDekuShieldDL } },
    // small magic jar, OBJECT_GI_MAGICPOT
    { GetItem_DrawOpa0, { gGiMagicJarSmallDL } },
    // large magic jar, OBJECT_GI_MAGICPOT
    { GetItem_DrawOpa0, { gGiMagicJarLargeDL } },
    // bombs, OBJECT_GI_BOMB_1
    { GetItem_DrawOpa0, { gGiBombDL } },
    // stone of agony, OBJECT_GI_MAP
    { GetItem_DrawOpa0, { gGiStoneOfAgonyDL } },
    // adult's wallet, OBJECT_GI_PURSE
    { GetItem_DrawWallet,
      { gGiWalletDL, gGiAdultWalletColorDL, gGiAdultWalletRupeeOuterColorDL, gGiWalletRupeeOuterDL,
        gGiAdultWalletStringColorDL, gGiWalletStringDL, gGiAdultWalletRupeeInnerColorDL, gGiWalletRupeeInnerDL } },
    // giant's wallet, OBJECT_GI_PURSE
    { GetItem_DrawWallet,
      { gGiWalletDL, gGiGiantsWalletColorDL, gGiGiantsWalletRupeeOuterColorDL, gGiWalletRupeeOuterDL,
        gGiGiantsWalletStringColorDL, gGiWalletStringDL, gGiGiantsWalletRupeeInnerColorDL, gGiWalletRupeeInnerDL } },
    // gerudo card, OBJECT_GI_GERUDO
    { GetItem_DrawOpa0, { gGiGerudoCardDL } },
    // arrows (small), OBJECT_GI_ARROW
    { GetItem_DrawOpa0, { gGiArrowSmallDL } },
    // arrows (medium), OBJECT_GI_ARROW
    { GetItem_DrawOpa0, { gGiArrowMediumDL } },
    // arrows (large), OBJECT_GI_ARROW
    { GetItem_DrawOpa0, { gGiArrowLargeDL } },
    // bombchus, OBJECT_GI_BOMB_2
    { GetItem_DrawMaskOrBombchu, { gGiBombchuDL } },
    // egg, OBJECT_GI_EGG
    { GetItem_DrawEggOrMedallion, { gGiEggMaterialDL, gGiEggDL } },
    // silver scale, OBJECT_GI_SCALE
    { GetItem_DrawScale, { gGiScaleWaterDL, gGiSilverScaleWaterColorDL, gGiSilverScaleColorDL, gGiScaleDL } },
    // gold scale, OBJECT_GI_SCALE
    { GetItem_DrawScale, { gGiScaleWaterDL, gGiGoldenScaleWaterColorDL, gGiGoldenScaleColorDL, gGiScaleDL } },
    // hylian shield, OBJECT_GI_SHIELD_2
    { GetItem_DrawOpa0, { gGiHylianShieldDL } },
    // hookshot, OBJECT_GI_HOOKSHOT
    { GetItem_DrawOpa0, { gGiHookshotDL } },
    // longshot, OBJECT_GI_HOOKSHOT
    { GetItem_DrawOpa0, { gGiLongshotDL } },
    // ocarina of time, OBJECT_GI_OCARINA
    { GetItem_DrawOpa0Xlu1, { gGiOcarinaTimeDL, gGiOcarinaTimeHolesDL } },
    // milk, OBJECT_GI_MILK
    { GetItem_DrawOpa0Xlu1, { gGiMilkBottleContentsDL, gGiMilkBottleDL } },
    // keaton mask, OBJECT_GI_KI_TAN_MASK
    { GetItem_DrawOpa0Xlu1, { gGiKeatonMaskDL, gGiKeatonMaskEyesDL } },
    // spooky mask, OBJECT_GI_REDEAD_MASK
    { GetItem_DrawOpa0, { gGiSpookyMaskDL } },
    // slingshot, OBJECT_GI_PACHINKO
    { GetItem_DrawOpa0, { gGiSlingshotDL } },
    // boomerang, OBJECT_GI_BOOMERANG
    { GetItem_DrawOpa0, { gGiBoomerangDL } },
    // bow, OBJECT_GI_BOW
    { GetItem_DrawOpa0, { gGiBowDL } },
    // lens, OBJECT_GI_GLASSES
    { GetItem_DrawOpa0Xlu1, { gGiLensDL, gGiLensGlassDL } },
    // green potion, OBJECT_GI_LIQUID
    { GetItem_DrawPotion,
      { gGiPotionPotDL, gGiGreenPotColorDL, gGiGreenLiquidColorDL, gGiPotionLiquidDL, gGiGreenPatternColorDL,
        gGiPotionPatternDL } },
    // red potion, OBJECT_GI_LIQUID
    { GetItem_DrawPotion,
      { gGiPotionPotDL, gGiRedPotColorDL, gGiRedLiquidColorDL, gGiPotionLiquidDL, gGiRedPatternColorDL,
        gGiPotionPatternDL } },
    // blue potion, OBJECT_GI_LIQUID
    { GetItem_DrawPotion,
      { gGiPotionPotDL, gGiBluePotColorDL, gGiBlueLiquidColorDL, gGiPotionLiquidDL, gGiBluePatternColorDL,
        gGiPotionPatternDL } },
    // mirror shield, OBJECT_GI_SHIELD_3
    { GetItem_DrawMirrorShield, { gGiMirrorShieldDL, gGiMirrorShieldSymbolDL } },
    // zelda's letter, OBJECT_GI_LETTER
    { GetItem_DrawOpa0Xlu1, { gGiLetterDL, gGiLetterWritingDL } },
    // goron tunic, OBJECT_GI_CLOTHES
    { GetItem_DrawOpa1023, { gGiTunicCollarDL, gGiGoronCollarColorDL, gGiGoronTunicColorDL, gGiTunicDL } },
    // zora tunic, OBJECT_GI_CLOTHES
    { GetItem_DrawOpa1023, { gGiTunicCollarDL, gGiZoraCollarColorDL, gGiZoraTunicColorDL, gGiTunicDL } },
    // beans, OBJECT_GI_BEAN
    { GetItem_DrawOpa0, { gGiBeanDL } },
    // fish, OBJECT_GI_FISH
    { GetItem_DrawFish, { gGiFishDL } },
    // saw, OBJECT_GI_SAW
    { GetItem_DrawOpa0, { gGiSawDL } },
    // hammer, OBJECT_GI_HAMMER
    { GetItem_DrawOpa0, { gGiHammerDL } },
    // grass, OBJECT_GI_GRASS
    { GetItem_DrawOpa0, { gGiGrassDL } },
    // biggorons sword, OBJECT_GI_LONGSWORD
    { GetItem_DrawGoronSword, { gGiBiggoronSwordDL } },
    // chicken, OBJECT_GI_NIWATORI
    { GetItem_DrawOpa10Xlu2, { gGiChickenDL, gGiChickenColorDL, gGiChickenEyesDL } },
    // ruto's letter, OBJECT_GI_BOTTLE_LETTER
    { GetItem_DrawOpa0Xlu1, { gGiLetterBottleContentsDL, gGiLetterBottleDL } },
    // fairy ocarina, OBJECT_GI_OCARINA_0
    { GetItem_DrawOpa0Xlu1, { gGiOcarinaFairyDL, gGiOcarinaFairyHolesDL } },
    // iron boots, OBJECT_GI_BOOTS_2
    { GetItem_DrawOpa0Xlu1, { gGiIronBootsDL, gGiIronBootsRivetsDL } },
    // seeds, OBJECT_GI_SEED
    { GetItem_DrawOpa0, { gGiSeedDL } },
    // silver gauntlets, OBJECT_GI_GLOVES
    { GetItem_DrawOpa10Xlu32,
      { gGiGauntletsDL, gGiSilverGauntletsColorDL, gGiGauntletsPlateDL, gGiSilverGauntletsPlateColorDL } },
    // golden gauntlets, OBJECT_GI_GLOVES
    { GetItem_DrawOpa10Xlu32,
      { gGiGauntletsDL, gGiGoldenGauntletsColorDL, gGiGauntletsPlateDL, gGiGoldenGauntletsPlateColorDL } },
    // yellow n coin, OBJECT_GI_COIN
    { GetItem_DrawOpa10Xlu2, { gGiCoinDL, gGiYellowCoinColorDL, gGiNDL } },
    // red n coin, OBJECT_GI_COIN
    { GetItem_DrawOpa10Xlu2, { gGiCoinDL, gGiRedCoinColorDL, gGiNDL } },
    // green n coin, OBJECT_GI_COIN
    { GetItem_DrawOpa10Xlu2, { gGiCoinDL, gGiGreenCoinColorDL, gGiNDL } },
    // blue n coin, OBJECT_GI_COIN
    { GetItem_DrawOpa10Xlu2, { gGiCoinDL, gGiBlueCoinColorDL, gGiNDL } },
    // skull mask, OBJECT_GI_SKJ_MASK
    { GetItem_DrawOpa0, { gGiSkullMaskDL } },
    // bunny hood OBJECT_GI_RABIT_MASK
    { GetItem_DrawOpa0Xlu1, { gGiBunnyHoodDL, gGiBunnyHoodEyesDL } },
    // mask of truth, OBJECT_GI_TRUTH_MASK
    { GetItem_DrawOpa0Xlu1, { gGiMaskOfTruthDL, gGiMaskOfTruthAccentsDL } },
    // eyedrops, OBJECT_GI_EYE_LOTION
    { GetItem_DrawOpa0Xlu1, { gGiEyeDropsCapDL, gGiEyeDropsBottleDL } },
    // odd potion, OBJECT_GI_POWDER
    { GetItem_DrawOpa0, { gGiOddPotionDL } },
    // odd mushroom, OBJECT_GI_MUSHROOM
    { GetItem_DrawOpa0, { gGiOddMushroomDL } },
    // claim check, OBJECT_GI_TICKETSTONE
    { GetItem_DrawOpa0Xlu1, { gGiClaimCheckDL, gGiClaimCheckWritingDL } },
    // broken goron's sword, OBJECT_GI_BROKENSWORD
    { GetItem_DrawGoronSword, { gGiBrokenGoronSwordDL } },
    // prescription, OBJECT_GI_PRESCRIPTION
    { GetItem_DrawOpa0Xlu1, { gGiPrescriptionDL, gGiPrescriptionWritingDL } },
    // goron bracelet, OBJECT_GI_BRACELET
    { GetItem_DrawOpa0, { gGiGoronBraceletDL } },
    // sold out, OBJECT_GI_SOLDOUT
    { GetItem_DrawSoldOut, { gGiSoldOutDL } },
    // frog, OBJECT_GI_FROG
    { GetItem_DrawOpa0Xlu1, { gGiFrogDL, gGiFrogEyesDL } },
    // goron mask, OBJECT_GI_GOLONMASK
    { GetItem_DrawMaskOrBombchu, { gGiGoronMaskDL } },
    // zora mask, OBJECT_GI_ZORAMASK
    { GetItem_DrawMaskOrBombchu, { gGiZoraMaskDL } },
    // gerudo mask, OBJECT_GI_GERUDOMASK
    { GetItem_DrawMaskOrBombchu, { gGiGerudoMaskDL } },
    // cojiro, OBJECT_GI_NIWATORI
    { GetItem_DrawOpa10Xlu2, { gGiChickenDL, gGiCojiroColorDL, gGiChickenEyesDL } },
    // hover boots, OBJECT_GI_HOVERBOOTS
    { GetItem_DrawOpa0, { gGiHoverBootsDL } },
    // fire arrows, OBJECT_GI_M_ARROW
    { GetItem_DrawMagicArrow, { gGiMagicArrowDL, gGiFireArrowColorDL, gGiArrowMagicDL } },
    // ice arrows, OBJECT_GI_M_ARROW
    { GetItem_DrawMagicArrow, { gGiMagicArrowDL, gGiIceArrowColorDL, gGiArrowMagicDL } },
    // light arrows, OBJECT_GI_M_ARROW
    { GetItem_DrawMagicArrow, { gGiMagicArrowDL, gGiLightArrowColorDL, gGiArrowMagicDL } },
    // skulltula token, OBJECT_GI_SUTARU
    { GetItem_DrawSkullToken, { gGiSkulltulaTokenDL, gGiSkulltulaTokenFlameDL } },
    // din's fire, OBJECT_GI_GODDESS
    { GetItem_DrawMagicSpell, { gGiMagicSpellDiamondDL, gGiDinsFireColorDL, gGiMagicSpellOrbDL } },
    // farore's wind, OBJECT_GI_GODDESS
    { GetItem_DrawMagicSpell, { gGiMagicSpellDiamondDL, gGiFaroresWindColorDL, gGiMagicSpellOrbDL } },
    // nayru's Love, OBJECT_GI_GODDESS
    { GetItem_DrawMagicSpell, { gGiMagicSpellDiamondDL, gGiNayrusLoveColorDL, gGiMagicSpellOrbDL } },
    // blue fire, OBJECT_GI_FIRE
    { GetItem_DrawBlueFire, { gGiBlueFireChamberstickDL, gGiBlueFireFlameDL } },
    // bugs, OBJECT_GI_INSECT
    { GetItem_DrawOpa0Xlu1, { gGiBugsContainerDL, gGiBugsGlassDL } },
    // butterfly, OBJECT_GI_BUTTERFLY
    { GetItem_DrawOpa0Xlu1, { gGiButterflyContainerDL, gGiButterflyGlassDL } },
    // poe, OBJECT_GI_GHOST
    { GetItem_DrawPoes,
      { gGiGhostContainerLidDL, gGiGhostContainerGlassDL, gGiGhostContainerContentsDL, gGiPoeColorDL } },
    // fairy, OBJECT_GI_SOUL
    { GetItem_DrawFairy, { gGiFairyContainerBaseCapDL, gGiFairyContainerGlassDL, gGiFairyContainerContentsDL } },
    // bullet bag 40, OBJECT_GI_DEKUPOUCH
    { GetItem_DrawBulletBag,
      { gGiBulletBagDL, gGiBulletBagColorDL, gGiBulletBagStringDL, gGiBulletBagStringColorDL, gGiBulletBagWritingDL } },
    // green rupee, OBJECT_GI_RUPY
    { GetItem_DrawSmallRupee,
      { gGiRupeeInnerDL, gGiGreenRupeeInnerColorDL, gGiRupeeOuterDL, gGiGreenRupeeOuterColorDL } },
    // blue rupee, OBJECT_GI_RUPY
    { GetItem_DrawSmallRupee,
      { gGiRupeeInnerDL, gGiBlueRupeeInnerColorDL, gGiRupeeOuterDL, gGiBlueRupeeOuterColorDL } },
    // red rupee, OBJECT_GI_RUPY
    { GetItem_DrawSmallRupee, { gGiRupeeInnerDL, gGiRedRupeeInnerColorDL, gGiRupeeOuterDL, gGiRedRupeeOuterColorDL } },
    // big poe, OBJECT_GI_GHOST
    { GetItem_DrawPoes,
      { gGiGhostContainerLidDL, gGiGhostContainerGlassDL, gGiGhostContainerContentsDL, gGiBigPoeColorDL } },
    // purple rupee, OBJECT_GI_RUPY
    { GetItem_DrawOpa10Xlu32,
      { gGiRupeeInnerDL, gGiPurpleRupeeInnerColorDL, gGiRupeeOuterDL, gGiPurpleRupeeOuterColorDL } },
    // gold rupee, OBJECT_GI_RUPY
    { GetItem_DrawOpa10Xlu32,
      { gGiRupeeInnerDL, gGiGoldRupeeInnerColorDL, gGiRupeeOuterDL, gGiGoldRupeeOuterColorDL } },
    // bullet bag 50, OBJECT_GI_DEKUPOUCH
    { GetItem_DrawBulletBag,
      { gGiBulletBagDL, gGiBulletBag50ColorDL, gGiBulletBagStringDL, gGiBulletBag50StringColorDL,
        gGiBulletBagWritingDL } },
    // kokiri sword, OBJECT_GI_SWORD_1
    { GetItem_DrawOpa0, { gGiKokiriSwordDL } },
    // gold skulltula token, OBJECT_ST
    { GetItem_DrawSkullToken, { gSkulltulaTokenDL, gSkulltulaTokenFlameDL } },

    { GetItem_DrawJewelKokiri, { gGiKokiriEmeraldGemDL, gGiKokiriEmeraldSettingDL } },
    { GetItem_DrawJewelGoron, { gGiGoronRubyGemDL, gGiGoronRubySettingDL } },
    { GetItem_DrawJewelZora, { gGiZoraSapphireGemDL, gGiZoraSapphireSettingDL } },

    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } }, // Generic

    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Zelda's  Lullaby
    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Epona's song
    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Saria's song
    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Sun's song
    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Song of time
    { GetItem_DrawGenericMusicNote, { gGiSongNoteDL } },  // Song of storms
    { GetItem_DrawTriforcePiece, { gTriforcePiece0DL } }, // Triforce Piece
    { GetItem_DrawFishingPole, { gGiFishingPoleDL } },    // Fishing Pole
};

/**
 * Draw "Get Item" Model
 * Calls the corresponding draw function for the given draw ID
 */
void GetItem_Draw(PlayState* play, s16 drawId) {
    sDrawItemTable[drawId].drawFunc(play, drawId);
}

#ifdef COMBO_BUILD
// ComboShip: expose one sDrawItemTable row for cross-game rendering — the OOT analog of MM's
// GetItem_GetDrawTableEntry. The other game (MM) asks soh.dll which display lists draw a foreign OOT
// item and submits them through "__OTR__@oot:"-routed paths resolved against OOT's ResourceManager.
//
// Two tiers are exposed. SIMPLE funcs (drawKind 0) merely submit dlists under plain Gfx_SetupDL_25/26
// Opa/Xlu state (plus an optional uniform scale); outDlists is filled in SUBMISSION order and
// *outXluStart is the index of the first XLU-layer entry (-1 = all OPA). The remaining funcs need
// extra OOT runtime state (segment-8/9 texture scrolls, billboard rotation, per-instance prim/env
// color, grayscale) that can't be baked into a DL list: they set *outDrawKind to a CwDrawKind value
// (see combo/menu/ComboItemDrawABI.h) and fill outColors for JEWEL/MUSIC_NOTE, and the foreign game's
// per-kind handler re-binds the segment(s) + replays the matrices in its own frame. For those,
// outDlists carries the raw table row (identity order). Rows drawn under a setup other than 25 are
// reported by GetItem_GetDrawSetupDLs below — the consumer must submit that same setup, not 25.
// outColors is 16 bytes: primXlu[4], envXlu[4], primOpa[4], envOpa[4] (RGBA).
// Returns the dlist count, or 0 if the row is unsupported/undrawable.
s32 GetItem_GetDrawTableEntry(s32 drawId, void** outDlists, s32 maxDlists, s32* outXluStart, f32* outScale,
                              s32* outDrawKind, u8* outColors) {
    // Mirror of CwDrawKind (ABI header is C++/POD; z_draw.c is C so keep local names in sync).
    enum {
        KIND_SIMPLE = 0,
        KIND_GORON_SWORD = 1,
        KIND_DEKU_NUTS = 2,
        KIND_RECOVERY_HEART = 3,
        KIND_FISH = 4,
        KIND_POTION = 5,
        KIND_MIRROR_SHIELD = 6,
        KIND_BLUE_FIRE = 7,
        KIND_POES = 8,
        KIND_FAIRY = 9,
        KIND_JEWEL = 10,
        KIND_MAGIC_SPELL = 11,
        KIND_SCALE = 12,
        KIND_SKULL_TOKEN = 13,
        KIND_MUSIC_NOTE = 14
    };
    static const s8 sOrder0[] = { 0 };
    static const s8 sOrder01[] = { 0, 1 };
    static const s8 sOrder012[] = { 0, 1, 2 };
    static const s8 sOrder102[] = { 1, 0, 2 };
    static const s8 sOrder1023[] = { 1, 0, 2, 3 };
    static const s8 sOrder1032[] = { 1, 0, 3, 2 };
    static const s8 sOrder10234[] = { 1, 0, 2, 3, 4 };
    static const s8 sOrderWallet[] = { 1, 0, 2, 3, 4, 5, 6, 7 };
    static const s8 sIdent2[] = { 0, 1 };
    static const s8 sIdent3[] = { 0, 1, 2 };
    static const s8 sIdent4[] = { 0, 1, 2, 3 };
    static const s8 sIdent6[] = { 0, 1, 2, 3, 4, 5 };
    void (*drawFunc)(PlayState*, s16);
    Gfx** res;
    const s8* order;
    s32 count;
    s32 xluStart;
    s32 kind = KIND_SIMPLE;
    s32 i;

    if ((drawId < 0) || (drawId >= (s32)(sizeof(sDrawItemTable) / sizeof(sDrawItemTable[0]))) || (outDlists == NULL) ||
        (outXluStart == NULL) || (outScale == NULL) || (outDrawKind == NULL)) {
        return 0;
    }
    *outScale = 0.0f; // 0 = no extra scale
    *outDrawKind = KIND_SIMPLE;
    drawFunc = sDrawItemTable[drawId].drawFunc;
    res = sDrawItemTable[drawId].dlists;

    // -- Non-portable funcs: kind-tagged, carried in identity order for the consumer's 1:1 handler.
    if (drawFunc == GetItem_DrawGoronSword) {
        order = sOrder0;
        count = 1;
        xluStart = -1;
        kind = KIND_GORON_SWORD;
    } else if (drawFunc == GetItem_DrawDekuNuts) {
        order = sOrder0;
        count = 1;
        xluStart = -1;
        kind = KIND_DEKU_NUTS;
    } else if (drawFunc == GetItem_DrawRecoveryHeart) {
        order = sOrder0;
        count = 1;
        xluStart = 0;
        kind = KIND_RECOVERY_HEART;
    } else if (drawFunc == GetItem_DrawFish) {
        order = sOrder0;
        count = 1;
        xluStart = 0;
        kind = KIND_FISH;
    } else if (drawFunc == GetItem_DrawPotion) {
        order = sIdent6;
        count = 6;
        xluStart = 4;
        kind = KIND_POTION;
    } else if (drawFunc == GetItem_DrawMirrorShield) {
        order = sIdent2;
        count = 2;
        xluStart = 1;
        kind = KIND_MIRROR_SHIELD;
    } else if (drawFunc == GetItem_DrawBlueFire) {
        order = sIdent2;
        count = 2;
        xluStart = 1;
        kind = KIND_BLUE_FIRE;
    } else if (drawFunc == GetItem_DrawPoes) {
        order = sIdent4;
        count = 4;
        xluStart = 1;
        kind = KIND_POES;
    } else if (drawFunc == GetItem_DrawFairy) {
        order = sIdent3;
        count = 3;
        xluStart = 1;
        kind = KIND_FAIRY;
    } else if ((drawFunc == GetItem_DrawJewelKokiri) || (drawFunc == GetItem_DrawJewelGoron) ||
               (drawFunc == GetItem_DrawJewelZora)) {
        order = sIdent2;
        count = 2;
        xluStart = 0;
        kind = KIND_JEWEL;
        if (outColors != NULL) {
            // Per-layer colors the jewel wrapper funcs set before GetItem_DrawJewel (primXlu, envXlu,
            // primOpa, envOpa; alpha 255). The OPA setting layer is identical across all three stones.
            static const u8 kOpaPrim[3] = { 255, 255, 170 };
            static const u8 kOpaEnv[3] = { 150, 120, 0 };
            const u8* xp;
            const u8* xe;
            static const u8 kKokiriXluPrim[3] = { 255, 255, 160 };
            static const u8 kKokiriXluEnv[3] = { 0, 255, 0 };
            static const u8 kGoronXluPrim[3] = { 255, 170, 255 };
            static const u8 kGoronXluEnv[3] = { 255, 0, 100 };
            static const u8 kZoraXluPrim[3] = { 50, 255, 255 };
            static const u8 kZoraXluEnv[3] = { 50, 0, 150 };
            if (drawFunc == GetItem_DrawJewelKokiri) {
                xp = kKokiriXluPrim;
                xe = kKokiriXluEnv;
            } else if (drawFunc == GetItem_DrawJewelGoron) {
                xp = kGoronXluPrim;
                xe = kGoronXluEnv;
            } else {
                xp = kZoraXluPrim;
                xe = kZoraXluEnv;
            }
            for (i = 0; i < 3; i++) {
                outColors[i] = xp[i];     // primXlu
                outColors[4 + i] = xe[i]; // envXlu
                outColors[8 + i] = kOpaPrim[i];
                outColors[12 + i] = kOpaEnv[i];
            }
            outColors[3] = outColors[7] = outColors[11] = outColors[15] = 255; // alpha
        }
    } else if (drawFunc == GetItem_DrawMagicSpell) {
        order = sIdent3;
        count = 3;
        xluStart = 0;
        kind = KIND_MAGIC_SPELL;
    } else if (drawFunc == GetItem_DrawScale) {
        order = sIdent4;
        count = 4;
        xluStart = 0;
        kind = KIND_SCALE;
    } else if (drawFunc == GetItem_DrawGenericMusicNote) {
        order = sOrder0;
        count = 1;
        xluStart = 0;
        kind = KIND_MUSIC_NOTE;
        if (outColors != NULL) {
            // Grayscale tint by note slot (drawId - 120), mirroring DrawGenericMusicNote's table.
            static const u8 kNoteColors[7][3] = { { 255, 255, 255 }, { 109, 73, 143 }, { 217, 110, 48 },
                                                  { 62, 109, 23 },   { 237, 231, 62 }, { 98, 177, 211 },
                                                  { 146, 146, 146 } };
            s32 slot = drawId - 120;
            if (slot < 0 || slot > 6) {
                slot = 0;
            }
            outColors[0] = kNoteColors[slot][0];
            outColors[1] = kNoteColors[slot][1];
            outColors[2] = kNoteColors[slot][2];
            outColors[3] = 255;
        }
    } else if (drawFunc == GetItem_DrawTriforcePiece) {
        // Plain scaled OPA — no segment/matrix state, so the simple consumer path draws it (index 0
        // approximates the animating triforcePiece0/1/2 selection).
        order = sOrder0;
        count = 1;
        xluStart = -1;
        *outScale = 0.035f;
    } else if (drawFunc == GetItem_DrawFishingPole) {
        // Best-effort: rod only (dlists[0]) via the simple path; the lure/hook DLs aren't in the table
        // row to carry across.
        order = sOrder0;
        count = 1;
        xluStart = -1;
        *outScale = 0.2f;
    } else if (drawFunc == GetItem_DrawOpa0) {
        order = sOrder0;
        count = 1;
        xluStart = -1;
    } else if ((drawFunc == GetItem_DrawMaskOrBombchu)) {
        order = sOrder0;
        count = 1;
        xluStart = -1; // 26Opa -> 25Opa approximation
    } else if (drawFunc == GetItem_DrawOpa0Xlu1) {
        order = sOrder01;
        count = 2;
        xluStart = 1;
    } else if (drawFunc == GetItem_DrawXlu01) {
        order = sOrder01;
        count = 2;
        xluStart = 0;
    } else if (drawFunc == GetItem_DrawEggOrMedallion) {
        order = sOrder01;
        count = 2;
        xluStart = -1; // 26Opa -> 25Opa approximation, both OPA
    } else if (drawFunc == GetItem_DrawCompass) {
        // The XLU layer really uses SETUPDL_5; the consumer's 25Xlu is a close approximation.
        order = sOrder01;
        count = 2;
        xluStart = 1;
    } else if (drawFunc == GetItem_DrawMagicArrow) {
        order = sOrder012;
        count = 3;
        xluStart = 1;
    } else if (drawFunc == GetItem_DrawOpa10Xlu2) {
        order = sOrder102;
        count = 3;
        xluStart = 2;
    } else if (drawFunc == GetItem_DrawOpa1023) {
        order = sOrder1023;
        count = 4;
        xluStart = -1;
    } else if (drawFunc == GetItem_DrawOpa10Xlu32) {
        order = sOrder1032;
        count = 4;
        xluStart = 2;
    } else if (drawFunc == GetItem_DrawSmallRupee) {
        order = sOrder1032;
        count = 4;
        xluStart = 2;
        *outScale = 0.7f; // SmallRupee applies a 0.7 uniform model scale; carry it across
    } else if (drawFunc == GetItem_DrawBulletBag) {
        order = sOrder10234;
        count = 5;
        xluStart = 2;
    } else if (drawFunc == GetItem_DrawWallet) {
        order = sOrderWallet;
        count = 8;
        xluStart = -1;
    } else if (drawFunc == GetItem_DrawSkullToken) {
        // Body (OPA dlists[0]) + flame (XLU dlists[1]); the flame's animated segment-8 texture scroll
        // is replicated by the consumer's SKULL_TOKEN handler.
        order = sIdent2;
        count = 2;
        xluStart = 1;
        kind = KIND_SKULL_TOKEN;
    } else {
        return 0;
    }

    if (count > maxDlists) {
        count = maxDlists;
    }
    for (i = 0; i < count; i++) {
        if (res[order[i]] == NULL) {
            return 0; // padded/unused rows are not drawable
        }
        outDlists[i] = (void*)res[order[i]];
    }
    *outXluStart = (xluStart > count) ? count : xluStart;
    *outDrawKind = kind;
    return count;
}

// ComboShip: one setup DL by index, for the bespoke Randomizer_Draw* funcs whose setup the gid-keyed
// table can't express (Jabber Nut / Bombchu Bag use 26 Opa, the rando compass 5 Xlu).
void* GetItem_GetSetupDL(s32 index) {
    extern Gfx sSetupDL[SETUPDL_MAX][6]; // z_rcp.c; not declared in a header

    if ((index < 0) || (index >= SETUPDL_MAX)) {
        return NULL;
    }
    return sSetupDL[index];
}

// ComboShip: the setup DL a row's draw func emits before its display lists, for the funcs that don't
// use plain 25. NULL = 25 Opa/Xlu (the consumer's own). The foreign consumer must submit the SAME
// setup: 26 is 1-CYCLE without fog, and a list authored for it renders through the wrong combiner
// under 2-cycle 25 — the second cycle wins and samples TEXEL1, i.e. the host's leftover tile.
void GetItem_GetDrawSetupDLs(s32 drawId, void** outOpa, void** outXlu) {
    extern Gfx sSetupDL[SETUPDL_MAX][6]; // z_rcp.c; not declared in a header
    void (*drawFunc)(PlayState*, s16);

    if (outOpa != NULL) {
        *outOpa = NULL;
    }
    if (outXlu != NULL) {
        *outXlu = NULL;
    }
    if ((drawId < 0) || (drawId >= (s32)(sizeof(sDrawItemTable) / sizeof(sDrawItemTable[0])))) {
        return;
    }

    drawFunc = sDrawItemTable[drawId].drawFunc;
    if ((drawFunc == GetItem_DrawMaskOrBombchu) || (drawFunc == GetItem_DrawEggOrMedallion)) {
        if (outOpa != NULL) {
            *outOpa = sSetupDL[SETUPDL_26];
        }
    } else if ((drawFunc == GetItem_DrawSoldOut) || (drawFunc == GetItem_DrawCompass)) {
        // Both draw their XLU layer with Gfx_SetupDL(POLY_XLU_DISP, 5); Compass keeps 25 on OPA.
        if (outXlu != NULL) {
            *outXlu = sSetupDL[5];
        }
    }
}
#endif

/**
 * Draw "Get Item" Model from a `GetItemEntry`
 * Uses the Custom Draw Function if it exists, or just calls `GetItem_Draw`
 */
void GetItemEntry_Draw(PlayState* play, GetItemEntry getItemEntry) {
    if (getItemEntry.drawFunc != NULL) {
        getItemEntry.drawFunc(play, &getItemEntry);
    } else {
        GetItem_Draw(play, getItemEntry.gid);
    }
}

// All remaining functions in this file are draw functions referenced in the table and called by the function above

/* 0x0178 */ u8 primXluColor[3];
/* 0x017B */ u8 envXluColor[3];
/* 0x017E */ u8 primOpaColor[3];
/* 0x0181 */ u8 envOpaColor[3];

void GetItem_DrawJewelKokiri(PlayState* play, s16 drawId) {
    primXluColor[2] = 160;
    primXluColor[0] = 255;
    primXluColor[1] = 255;
    envXluColor[0] = 0;
    envXluColor[1] = 255;
    envXluColor[2] = 0;
    primOpaColor[2] = 170;
    primOpaColor[0] = 255;
    primOpaColor[1] = 255;
    envOpaColor[1] = 120;
    envOpaColor[0] = 150;
    envOpaColor[2] = 0;

    GetItem_DrawJewel(play, drawId);
}

void GetItem_DrawJewelGoron(PlayState* play, s16 drawId) {
    primXluColor[1] = 170;
    primXluColor[0] = 255;
    primXluColor[2] = 255;
    envXluColor[2] = 100;
    envXluColor[0] = 255;
    envXluColor[1] = 0;
    primOpaColor[2] = 170;
    primOpaColor[0] = 255;
    primOpaColor[1] = 255;
    envOpaColor[1] = 120;
    envOpaColor[0] = 150;
    envOpaColor[2] = 0;

    GetItem_DrawJewel(play, drawId);
}

void GetItem_DrawJewelZora(PlayState* play, s16 drawId) {
    primXluColor[0] = 50;
    primXluColor[1] = 255;
    primXluColor[2] = 255;
    envXluColor[2] = 150;
    envXluColor[0] = 50;
    envXluColor[1] = 0;
    primOpaColor[2] = 170;
    primOpaColor[0] = 255;
    primOpaColor[1] = 255;
    envOpaColor[1] = 120;
    envOpaColor[0] = 150;
    envOpaColor[2] = 0;

    GetItem_DrawJewel(play, drawId);
}

void GetItem_DrawJewel(PlayState* play, s16 drawId) {
    OPEN_DISPS(play->state.gfxCtx);

    gSPSegment(POLY_XLU_DISP++, 9,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 % 256, (256 - (0 % 256)) - 1, 64, 64, 1, 0 % 256,
                                  (256 - (0 % 256)) - 1, 16, 16, 0, 0, 0, 0));

    gSPSegment(POLY_OPA_DISP++, 8, Gfx_TexScrollEx(play->state.gfxCtx, (u8)0, (u8)0, 16, 16, 0, 0));

    Matrix_Push();
    Matrix_RotateZYX(0, -0x4000, 0x4000, MTXMODE_APPLY);

    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_demo_effect.c", 2597),
              G_MTX_NOPUSH | G_MTX_LOAD);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, "../z_demo_effect.c", 2599),
              G_MTX_NOPUSH | G_MTX_LOAD);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // func_8002ED80(&this->actor, play, 0);
    gDPSetPrimColor(POLY_XLU_DISP++, 0, 128, primXluColor[0], primXluColor[1], primXluColor[2], 255);
    gDPSetEnvColor(POLY_XLU_DISP++, envXluColor[0], envXluColor[1], envXluColor[2], 255);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // func_8002EBCC(&this->actor, play, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 128, primOpaColor[0], primOpaColor[1], primOpaColor[2], 255);
    gDPSetEnvColor(POLY_OPA_DISP++, envOpaColor[0], envOpaColor[1], envOpaColor[2], 255);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawMaskOrBombchu(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_26Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawSoldOut(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    POLY_XLU_DISP = Gfx_SetupDL(POLY_XLU_DISP, 5);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawBlueFire(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0), 0 * (play->state.frames * 0), 16,
                                  32, 1, 1 * (play->state.frames * 1), 1 * -(play->state.frames * 8), 16, 32, 0, 0, 1,
                                  -8));
    Matrix_Push();
    Matrix_Translate(-8.0f, -2.0f, 0.0f, MTXMODE_APPLY);
    Matrix_ReplaceRotation(&play->billboardMtxF);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawPoes(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0), 0 * (play->state.frames * 0), 16,
                                  32, 1, 1 * (play->state.frames * 1), 1 * -(play->state.frames * 6), 16, 32, 0, 0, 1,
                                  -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawFairy(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0), 0 * (play->state.frames * 0), 32,
                                  32, 1, 1 * (play->state.frames * 1), 1 * -(play->state.frames * 6), 32, 32, 0, 0, 1,
                                  -6));
    Matrix_Push();
    Matrix_ReplaceRotation(&play->billboardMtxF);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);
    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawMirrorShield(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0) % 256,
                                  1 * (play->state.frames * 2) % 256, 64, 64, 1, 0 * (play->state.frames * 0) % 128,
                                  1 * (play->state.frames * 1) % 128, 32, 32, 0, 2, 0, 1));
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawSkullToken(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0), 1 * -(play->state.frames * 5),
                                  32, 32, 1, 0 * (play->state.frames * 0), 0 * (play->state.frames * 0), 32, 64, 0, -5,
                                  0, 0));
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawEggOrMedallion(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_26Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawCompass(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    POLY_XLU_DISP = Gfx_SetupDL(POLY_XLU_DISP, 5);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawPotion(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, -1 * (play->state.frames * 1), 1 * (play->state.frames * 1),
                                  32, 32, 1, -1 * (play->state.frames * 1), 1 * (play->state.frames * 1), 32, 32, -1, 1,
                                  -1, 1));
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[2]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[3]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[4]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[5]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawGoronSword(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 1 * (play->state.frames * 1), 0 * (play->state.frames * 1), 32,
                                  32, 1, 0 * (play->state.frames * 1), 0 * (play->state.frames * 1), 32, 32, 1, 0, 0,
                                  0));
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawDekuNuts(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 1 * (play->state.frames * 6), 1 * (play->state.frames * 6), 32,
                                  32, 1, 1 * (play->state.frames * 6), 1 * (play->state.frames * 6), 32, 32, 6, 6, 6,
                                  6));
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawRecoveryHeart(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 1), 1 * -(play->state.frames * 3),
                                  32, 32, 1, 0 * (play->state.frames * 1), 1 * -(play->state.frames * 2), 32, 32, 0, -3,
                                  0, -2));
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.Hearts.Changed"), 0)) {
        Color_RGB8 color = CVarGetColor24(CVAR_COSMETIC("Consumable.Hearts.Value"), (Color_RGB8){ 255, 70, 50 });
        gDPSetGrayscaleColor(POLY_XLU_DISP++, color.r, color.g, color.b, 255);
        gSPGrayscale(POLY_XLU_DISP++, true);
    }
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.Hearts.Changed"), 0)) {
        gSPGrayscale(POLY_XLU_DISP++, false);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawFish(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0 * (play->state.frames * 0), 1 * (play->state.frames * 1), 32,
                                  32, 1, 0 * (play->state.frames * 0), 1 * (play->state.frames * 1), 32, 32, 0, 1, 0,
                                  1));
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawOpa0(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawOpa0Xlu1(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawGenericMusicNote(PlayState* play, s16 drawId) {
    s32 pad;
    s16 color_slot = drawId - 120; // 0 = generic
    s16* colors[7][3] = {
        { 255, 255, 255 }, // Generic Song (full white)
        { 109, 73, 143 },  // Lullaby
        { 217, 110, 48 },  // Epona
        { 62, 109, 23 },   // Saria
        { 237, 231, 62 },  // Sun
        { 98, 177, 211 },  // Time
        { 146, 146, 146 }  // Storms
    };

    OPEN_DISPS(play->state.gfxCtx);

    gSPMatrix(POLY_XLU_DISP++, Matrix_NewMtx(play->state.gfxCtx, __FILE__, __LINE__), G_MTX_MODELVIEW | G_MTX_LOAD);
    gDPSetGrayscaleColor(POLY_XLU_DISP++, colors[color_slot][0], colors[color_slot][1], colors[color_slot][2], 255);
    gSPGrayscale(POLY_XLU_DISP++, true);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPGrayscale(POLY_XLU_DISP++, false);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawXlu01(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawOpa10Xlu2(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawMagicArrow(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawMagicSpell(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 1 * (play->state.frames * 2), 1 * -(play->state.frames * 6),
                                  32, 32, 1, 1 * (play->state.frames * 1), -1 * (play->state.frames * 2), 32, 32, 2, -6,
                                  1, -2));
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawOpa1023(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[2]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[3]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawOpa10Xlu32(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawSmallRupee(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_Scale(0.7f, 0.7f, 0.7f, MTXMODE_APPLY);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawScale(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 1 * (play->state.frames * 2), -1 * (play->state.frames * 2),
                                  64, 64, 1, 1 * (play->state.frames * 4), 1 * -(play->state.frames * 4), 32, 32, 2, -2,
                                  4, -4));
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[0]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawBulletBag(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);

    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[2]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_XLU_DISP++, sDrawItemTable[drawId].dlists[4]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawWallet(PlayState* play, s16 drawId) {
    s32 pad;

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[1]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[0]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[2]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[3]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[4]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[5]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[6]);
    gSPDisplayList(POLY_OPA_DISP++, sDrawItemTable[drawId].dlists[7]);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawTriforcePiece(PlayState* play, s16 drawId) {
    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    Matrix_Scale(0.035f, 0.035f, 0.035f, MTXMODE_APPLY);

    uint8_t index = gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected % 3;
    Gfx* triforcePieceDL;

    switch (index) {
        case 1:
            triforcePieceDL = (Gfx*)gTriforcePiece1DL;
            break;
        case 2:
            triforcePieceDL = (Gfx*)gTriforcePiece2DL;
            break;
        default:
            triforcePieceDL = (Gfx*)gTriforcePiece0DL;
            break;
    }

    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);

    gSPDisplayList(POLY_OPA_DISP++, triforcePieceDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

void GetItem_DrawFishingPole(PlayState* play, s16 drawId) {
    Vec3f pos;
    OPEN_DISPS(play->state.gfxCtx);

    // Draw rod
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Scale(0.2, 0.2, 0.2, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gGiFishingPoleDL);

    // Draw lure
    Matrix_Push();
    Matrix_Scale(5.0f, 5.0f, 5.0f, MTXMODE_APPLY);
    pos.x = 0.0f;
    pos.y = -25.5f;
    pos.z = -4.0f;
    Matrix_Translate(pos.x, pos.y, pos.z, MTXMODE_APPLY);
    Matrix_RotateZ(M_PI / -2, MTXMODE_APPLY);
    Matrix_RotateY((M_PI / -2) - 0.2f, MTXMODE_APPLY);
    Matrix_Scale(0.006f, 0.006f, 0.006f, MTXMODE_APPLY);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gFishingLureFloatDL);

    // Draw hooks
    Matrix_RotateY(0.2f, MTXMODE_APPLY);
    Matrix_Translate(0.0f, 0.0f, -300.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gFishingLureHookDL);
    Matrix_RotateZ(M_PI / 2, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gFishingLureHookDL);

    Matrix_Translate(0.0f, -2200.0f, 700.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gFishingLureHookDL);
    Matrix_RotateZ(M_PI / 2, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, Matrix_NewMtx(play->state.gfxCtx, (char*)__FILE__, __LINE__),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gFishingLureHookDL);

    Matrix_Pop();

    CLOSE_DISPS(play->state.gfxCtx);
}