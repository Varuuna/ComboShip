#include "global.h"
#include "interface/parameter_static/parameter_static.h"
#include "archives/icon_item_static/icon_item_static_yar.h"
#include "archives/icon_item_24_static/icon_item_24_static_yar.h"
#ifdef COMBO_BUILD
#include "2s2h_assets.h"                             // gEmptyTexture
#include "2s2h/Rando/MiscBehavior/CrossItemAssets.h" // ComboShip: OOT icons for cross-game items
#endif

// Bit Flag array in which gBitFlags[n] is (1 << n)
u32 gBitFlags[] = {
    (1 << 0),  (1 << 1),  (1 << 2),  (1 << 3),  (1 << 4),  (1 << 5),  (1 << 6),  (1 << 7),
    (1 << 8),  (1 << 9),  (1 << 10), (1 << 11), (1 << 12), (1 << 13), (1 << 14), (1 << 15),
    (1 << 16), (1 << 17), (1 << 18), (1 << 19), (1 << 20), (1 << 21), (1 << 22), (1 << 23),
    (1 << 24), (1 << 25), (1 << 26), (1 << 27), (1 << 28), (1 << 29), (1 << 30), (1 << 31),
};

// four-bit masks
u16 gEquipMasks[] = {
    0xF << 0,  // Sword
    0xF << 4,  // Shield
    0xF << 8,  // Unused (Tunic)
    0xF << 12, // Unused (Boots)
};

// four-bit masks
u16 gEquipNegMasks[] = {
    ~(0xF << 0),        // Sword
    ~(0xF << 4),        // Shield
    ~(0xF << 8),        // Unused (Tunic)
    (u16) ~(0xF << 12), // Unused (Boots)
};

// 3 = two bit masks
// 7 = three bit masks
u32 gUpgradeMasks[] = {
    7 << 0,  // Quivers
    7 << 3,  // Bomb Bags
    7 << 6,  // Unused (Strength)
    7 << 9,  // Unused (Scale)
    3 << 12, // Wallets
    7 << 14, // Unused (Deku Seed Bullet Bags)
    7 << 17, // Unused (Deku Stick)
    7 << 20, // Unused (Deku Nut)
};

// 3 = two-bit masks
// 7 = three-bit masks
u32 gUpgradeNegMasks[] = {
    ~(7 << 0),  // UPG_QUIVER
    ~(7 << 3),  // UPG_BOMB_BAG
    ~(7 << 6),  // UPG_STRENGTH
    ~(7 << 9),  // UPG_SCALE
    ~(3 << 12), // UPG_WALLET
    ~(7 << 14), // UPG_BULLET_BAG
    ~(7 << 17), // UPG_DEKU_STICKS
    ~(7 << 20), // UPG_DEKU_NUTS
};

u8 gEquipShifts[] = {
    0,  // Sword
    4,  // Shield
    8,  // Unused (Tunic)
    12, // Unused (Boots)
};

u8 gUpgradeShifts[] = {
    0,  // UPG_QUIVER
    3,  // UPG_BOMB_BAG
    6,  // UPG_STRENGTH
    9,  // UPG_SCALE
    12, // UPG_WALLET
    14, // UPG_BULLET_BAG
    17, // UPG_DEKU_STICKS
    20, // UPG_DEKU_NUTS
};

u16 gUpgradeCapacities[][4] = {
    { 0, 30, 40, 50 },     // UPG_QUIVER
    { 0, 20, 30, 40 },     // UPG_BOMB_BAG
    { 0, 0, 0, 0 },        // UPG_STRENGTH
    { 0, 0, 0, 0 },        // UPG_SCALE
    { 99, 200, 500, 500 }, // UPG_WALLET
    { 0, 30, 40, 50 },     // UPG_BULLET_BAG
    { 0, 10, 20, 30 },     // UPG_DEKU_STICKS
    { 0, 20, 30, 40 },     // UPG_DEKU_NUTS
};

// eight-bit masks
u32 gGsFlagsMask[] = {
    0xFF << 0,
    0xFF << 8,
    0xFF << 16,
    0xFF << 24,
};

u32 gGsFlagsShift[] = {
    0,
    8,
    16,
    24,
};

#ifdef COMBO_BUILD
TexturePtr gItemIcons[ITEM_COMBO_TABLE_SIZE] = {
#else
TexturePtr gItemIcons[] = {
#endif
    gItemIconOcarinaOfTimeTex,       // ITEM_OCARINA_OF_TIME
    gItemIconBowTex,                 // ITEM_BOW
    gItemIconFireArrowTex,           // ITEM_ARROW_FIRE
    gItemIconIceArrowTex,            // ITEM_ARROW_ICE
    gItemIconLightArrowTex,          // ITEM_ARROW_LIGHT
    gItemIconFairyOcarinaTex,        // ITEM_OCARINA_FAIRY
    gItemIconBombTex,                // ITEM_BOMB
    gItemIconBombchuTex,             // ITEM_BOMBCHU
    gItemIconDekuStickTex,           // ITEM_DEKU_STICK
    gItemIconDekuNutTex,             // ITEM_DEKU_NUT
    gItemIconMagicBeansTex,          // ITEM_MAGIC_BEANS
    gItemIconSlingshotTex,           // ITEM_SLINGSHOT
    gItemIconPowderKegTex,           // ITEM_POWDER_KEG
    gItemIconPictographBoxTex,       // ITEM_PICTOGRAPH_BOX
    gItemIconLensofTruthTex,         // ITEM_LENS_OF_TRUTH
    gItemIconHookshotTex,            // ITEM_HOOKSHOT
    gItemIconGreatFairysSwordTex,    // ITEM_SWORD_GREAT_FAIRY
    gItemIconLongshotTex,            // ITEM_LONGSHOT
    gItemIconEmptyBottleTex,         // ITEM_BOTTLE
    gItemIconRedPotionTex,           // ITEM_POTION_RED
    gItemIconGreenPotionTex,         // ITEM_POTION_GREEN
    gItemIconBluePotionTex,          // ITEM_POTION_BLUE
    gItemIconBottledFairyTex,        // ITEM_FAIRY
    gItemIconBottledDekuPrincessTex, // ITEM_DEKU_PRINCESS
    gItemIconBottledFullMilkTex,     // ITEM_MILK_BOTTLE
    gItemIconBottledHalfMilkTex,     // ITEM_MILK_HALF
    gItemIconBottledFishTex,         // ITEM_FISH
    gItemIconBottledBugTex,          // ITEM_BUG
    gItemIconBottledBlueFireTex,     // ITEM_BLUE_FIRE
    gItemIconBottledPoeTex,          // ITEM_POE
    gItemIconBottledBigPoeTex,       // ITEM_BIG_POE
    gItemIconSpringWaterTex,         // ITEM_SPRING_WATER
    gItemIconHotSpringWaterTex,      // ITEM_HOT_SPRING_WATER
    gItemIconBottledZoraEggTex,      // ITEM_ZORA_EGG
    gItemIconBottledGoldDustTex,     // ITEM_GOLD_DUST
    gItemIconBottledMushroomTex,     // ITEM_MUSHROOM
    gItemIconBottledSeahorseTex,     // ITEM_SEAHORSE
    gItemIconChateauRomaniTex,       // ITEM_CHATEAU
    gItemIconBottledHylianLoachTex,  // ITEM_HYLIAN_LOACH
    gItemIconEmptyBottle2Tex,        // ITEM_OBABA_DRINK
    gItemIconMoonsTearTex,           // ITEM_MOONS_TEAR
    gItemIconLandDeedTex,            // ITEM_DEED_LAND
    gItemIconSwampDeedTex,           // ITEM_DEED_SWAMP
    gItemIconMountainDeedTex,        // ITEM_DEED_MOUNTAIN
    gItemIconOceanDeedTex,           // ITEM_DEED_OCEAN
    gItemIconRoomKeyTex,             // ITEM_ROOM_KEY
    gItemIconLetterToMamaTex,        // ITEM_LETTER_MAMA
    gItemIconLetterToKafeiTex,       // ITEM_LETTER_TO_KAFEI
    gItemIconPendantOfMemoriesTex,   // ITEM_PENDANT_OF_MEMORIES
    gItemIconTingleMapTex,           // ITEM_TINGLE_MAP
    gItemIconDekuMaskTex,            // ITEM_MASK_DEKU
    gItemIconGoronMaskTex,           // ITEM_MASK_GORON
    gItemIconZoraMaskTex,            // ITEM_MASK_ZORA
    gItemIconFierceDeityMaskTex,     // ITEM_MASK_FIERCE_DEITY
    gItemIconMaskOfTruthTex,         // ITEM_MASK_TRUTH
    gItemIconKafeisMaskTex,          // ITEM_MASK_KAFEIS_MASK
    gItemIconAllNightMaskTex,        // ITEM_MASK_ALL_NIGHT
    gItemIconBunnyHoodTex,           // ITEM_MASK_BUNNY
    gItemIconKeatonMaskTex,          // ITEM_MASK_KEATON
    gItemIconGaroMaskTex,            // ITEM_MASK_GARO
    gItemIconRomaniMaskTex,          // ITEM_MASK_ROMANI
    gItemIconCircusLeaderMaskTex,    // ITEM_MASK_CIRCUS_LEADER
    gItemIconPostmansHatTex,         // ITEM_MASK_POSTMAN
    gItemIconCouplesMaskTex,         // ITEM_MASK_COUPLE
    gItemIconGreatFairyMaskTex,      // ITEM_MASK_GREAT_FAIRY
    gItemIconGibdoMaskTex,           // ITEM_MASK_GIBDO
    gItemIconDonGeroMaskTex,         // ITEM_MASK_DON_GERO
    gItemIconKamaroMaskTex,          // ITEM_MASK_KAMARO
    gItemIconCaptainsHatTex,         // ITEM_MASK_CAPTAIN
    gItemIconStoneMaskTex,           // ITEM_MASK_STONE
    gItemIconBremenMaskTex,          // ITEM_MASK_BREMEN
    gItemIconBlastMaskTex,           // ITEM_MASK_BLAST
    gItemIconMaskOfScentsTex,        // ITEM_MASK_SCENTS
    gItemIconGiantsMaskTex,          // ITEM_MASK_GIANT
    gItemIconBowFireTex,             // ITEM_BOW_FIRE
    gItemIconBowIceTex,              // ITEM_BOW_ICE
    gItemIconBowLightTex,            // ITEM_BOW_LIGHT
    gItemIconKokiriSwordTex,         // ITEM_SWORD_KOKIRI
    gItemIconRazorSwordTex,          // ITEM_SWORD_RAZOR
    gItemIconGildedSwordTex,         // ITEM_SWORD_GILDED
    gItemIconFierceDeitySwordTex,    // ITEM_SWORD_DEITY
    gItemIconHerosShieldTex,         // ITEM_SHIELD_HERO
    gItemIconMirrorShieldTex,        // ITEM_SHIELD_MIRROR
    gItemIconQuiver30Tex,            // ITEM_QUIVER_30
    gItemIconQuiver40Tex,            // ITEM_QUIVER_40
    gItemIconQuiver50Tex,            // ITEM_QUIVER_50
    gItemIconBombBag20Tex,           // ITEM_BOMB_BAG_20
    gItemIconBombBag30Tex,           // ITEM_BOMB_BAG_30
    gItemIconBombBag40Tex,           // ITEM_BOMB_BAG_40
    gItemIconDefaultWalletTex,       // ITEM_WALLET_DEFAULT
    gItemIconAdultsWalletTex,        // ITEM_WALLET_ADULT
    gItemIconGiantsWalletTex,        // ITEM_WALLET_GIANT
    gItemIconFishingRodTex,          // ITEM_FISHING_ROD
    gItemIconOdolwasRemainsTex,      // ITEM_REMAINS_ODOLWA
    gItemIconGohtsRemainsTex,        // ITEM_REMAINS_GOHT
    gItemIconGyorgsRemainsTex,       // ITEM_REMAINS_GYORG
    gItemIconTwinmoldsRemainsTex,    // ITEM_REMAINS_TWINMOLD
    gItemIconSongNoteTex,            // ITEM_SONG_SONATA
    gItemIconSongNoteTex,            // ITEM_SONG_LULLABY
    gItemIconSongNoteTex,            // ITEM_SONG_NOVA
    gItemIconSongNoteTex,            // ITEM_SONG_ELEGY
    gItemIconSongNoteTex,            // ITEM_SONG_OATH
    gItemIconSongNoteTex,            // ITEM_SONG_SARIA
    gItemIconSongNoteTex,            // ITEM_SONG_TIME
    gItemIconSongNoteTex,            // ITEM_SONG_HEALING
    gItemIconSongNoteTex,            // ITEM_SONG_EPONA
    gItemIconSongNoteTex,            // ITEM_SONG_SOARING
    gItemIconSongNoteTex,            // ITEM_SONG_STORMS
    gItemIconSongNoteTex,            // ITEM_SONG_SUN
    gItemIconBombersNotebookTex,     // ITEM_BOMBERS_NOTEBOOK
    gQuestIconGoldSkulltulaTex,      // ITEM_SKULL_TOKEN
    gQuestIconHeartContainerTex,     // ITEM_HEART_CONTAINER
    gQuestIconPieceOfHeartTex,       // ITEM_HEART_PIECE
    gItemIconSongNoteTex,            // ITEM_71
    gItemIconSongNoteTex,            // ITEM_72
    gItemIconSongNoteTex,            // ITEM_SONG_LULLABY_INTRO
    gQuestIconBossKeyTex,            // ITEM_KEY_BOSS
    gQuestIconDungeonMapTex,         // ITEM_COMPASS
    gQuestIconCompassTex,            // ITEM_DUNGEON_MAP
    gQuestIconGoldSkulltula2Tex,     // ITEM_STRAY_FAIRIES
    gQuestIconSmallKeyTex,           // ITEM_KEY_SMALL
    gQuestIconSmallMagicJarTex,      // ITEM_MAGIC_JAR_SMALL
    gQuestIconBigMagicJarTex,        // ITEM_MAGIC_JAR_BIG
    gItemIconHeartPiece1Tex,         // ITEM_HEART_PIECE_2
    gItemIconHeartPiece2Tex,         // ITEM_INVALID_1
    gItemIconHeartPiece3Tex,         // ITEM_INVALID_2
    gOcarinaCUpTex,                  // ITEM_INVALID_3
    gOcarinaCDownTex,                // ITEM_INVALID_4
    gOcarinaCLeftTex,                // ITEM_INVALID_5
    gOcarinaCRightTex,               // ITEM_INVALID_6
    gOcarinaATex,                    // ITEM_INVALID_7
#ifdef COMBO_BUILD
    // ComboShip (cross-game items): the ids between ITEM_INVALID_7 and ITEM_COMBO_MIN used to fall past the
    // end of this table (callers then drew gEmptyTexture); keep them empty explicitly.
    gEmptyTexture, // 0x83
    gEmptyTexture, // 0x84
    gEmptyTexture, // 0x85
    gEmptyTexture, // 0x86
    gEmptyTexture, // 0x87
    gEmptyTexture, // 0x88
    gEmptyTexture, // 0x89
    gEmptyTexture, // 0x8A
    gEmptyTexture, // 0x8B
    gEmptyTexture, // 0x8C
    gEmptyTexture, // 0x8D
    gEmptyTexture, // 0x8E
    gEmptyTexture, // 0x8F
    gEmptyTexture, // 0x90
    gEmptyTexture, // 0x91
    gEmptyTexture, // 0x92
    gEmptyTexture, // 0x93
    gEmptyTexture, // 0x94
    gEmptyTexture, // 0x95
    gEmptyTexture, // 0x96
    gEmptyTexture, // 0x97
    gEmptyTexture, // 0x98
    gEmptyTexture, // 0x99
    gEmptyTexture, // 0x9A
    gEmptyTexture, // 0x9B
    gEmptyTexture, // 0x9C
    gEmptyTexture, // 0x9D
    gEmptyTexture, // 0x9E
    gEmptyTexture, // 0x9F
    gEmptyTexture, // 0xA0
    gEmptyTexture, // 0xA1
    gEmptyTexture, // 0xA2
    gEmptyTexture, // 0xA3
    gEmptyTexture, // 0xA4
    gEmptyTexture, // 0xA5
    gEmptyTexture, // 0xA6
    gEmptyTexture, // 0xA7
    gEmptyTexture, // 0xA8
    gEmptyTexture, // 0xA9
    gEmptyTexture, // 0xAA
    gEmptyTexture, // 0xAB
    gEmptyTexture, // 0xAC
    gEmptyTexture, // 0xAD
    gEmptyTexture, // 0xAE
    gEmptyTexture, // 0xAF
    gEmptyTexture, // 0xB0
    gEmptyTexture, // 0xB1
    gEmptyTexture, // 0xB2
    gEmptyTexture, // 0xB3
    gEmptyTexture, // 0xB4
    gEmptyTexture, // 0xB5
    gEmptyTexture, // 0xB6
    gEmptyTexture, // 0xB7
    // OOT icons, routed to OOT's ResourceManager by the "@oot:" texture marker (libultraship
    // interpreter, LoadTextureResourceRouted; Fast3dGui::LoadGuiTexture for ImGui).
    [ITEM_COMBO_BOOTS_IRON] = gComboIconBootsIronTex,
    [ITEM_COMBO_BOOTS_HOVER] = gComboIconBootsHoverTex,
    [ITEM_COMBO_TUNIC_GORON] = gComboIconTunicGoronTex,
    [ITEM_COMBO_TUNIC_ZORA] = gComboIconTunicZoraTex,
    [ITEM_COMBO_MASK_GERUDO] = gComboIconMaskGerudoTex,
    [ITEM_COMBO_MASK_SKULL] = gComboIconMaskSkullTex,
    [ITEM_COMBO_MASK_SPOOKY] = gComboIconMaskSpookyTex,
    [ITEM_COMBO_SPELL_FIRE] = gComboIconDinsFireTex,
    [ITEM_COMBO_SPELL_WIND] = gComboIconFaroresWindTex,
    [ITEM_COMBO_SPELL_LOVE] = gComboIconNayrusLoveTex,
    [ITEM_COMBO_HAMMER] = gComboIconHammerTex,
    [ITEM_COMBO_BOOMERANG] = gComboIconBoomerangTex,
    [ITEM_COMBO_SLINGSHOT] = gComboIconSlingshotTex,
#endif
};

// Used to map item IDs to inventory slots
#ifdef COMBO_BUILD
u8 gItemSlots[ITEM_COMBO_TABLE_SIZE] = {
#else
u8 gItemSlots[77] = {
#endif
    SLOT_OCARINA,            // ITEM_OCARINA_OF_TIME
    SLOT_BOW,                // ITEM_BOW
    SLOT_ARROW_FIRE,         // ITEM_ARROW_FIRE
    SLOT_ARROW_ICE,          // ITEM_ARROW_ICE
    SLOT_ARROW_LIGHT,        // ITEM_ARROW_LIGHT
    SLOT_TRADE_DEED,         // ITEM_OCARINA_FAIRY
    SLOT_BOMB,               // ITEM_BOMB
    SLOT_BOMBCHU,            // ITEM_BOMBCHU
    SLOT_DEKU_STICK,         // ITEM_DEKU_STICK
    SLOT_DEKU_NUT,           // ITEM_DEKU_NUT
    SLOT_MAGIC_BEANS,        // ITEM_MAGIC_BEANS
    SLOT_TRADE_KEY_MAMA,     // ITEM_SLINGSHOT
    SLOT_POWDER_KEG,         // ITEM_POWDER_KEG
    SLOT_PICTOGRAPH_BOX,     // ITEM_PICTOGRAPH_BOX
    SLOT_LENS_OF_TRUTH,      // ITEM_LENS_OF_TRUTH
    SLOT_HOOKSHOT,           // ITEM_HOOKSHOT
    SLOT_SWORD_GREAT_FAIRY,  // ITEM_SWORD_GREAT_FAIRY
    SLOT_BOTTLE_1,           // ITEM_LONGSHOT
    SLOT_BOTTLE_1,           // ITEM_BOTTLE
    SLOT_BOTTLE_1,           // ITEM_POTION_RED
    SLOT_BOTTLE_1,           // ITEM_POTION_GREEN
    SLOT_BOTTLE_1,           // ITEM_POTION_BLUE
    SLOT_BOTTLE_1,           // ITEM_FAIRY
    SLOT_BOTTLE_1,           // ITEM_DEKU_PRINCESS
    SLOT_BOTTLE_1,           // ITEM_MILK_BOTTLE
    SLOT_BOTTLE_1,           // ITEM_MILK_HALF
    SLOT_BOTTLE_1,           // ITEM_FISH
    SLOT_BOTTLE_1,           // ITEM_BUG
    SLOT_BOTTLE_1,           // ITEM_BLUE_FIRE
    SLOT_BOTTLE_1,           // ITEM_POE
    SLOT_BOTTLE_1,           // ITEM_BIG_POE
    SLOT_BOTTLE_1,           // ITEM_SPRING_WATER
    SLOT_BOTTLE_1,           // ITEM_HOT_SPRING_WATER
    SLOT_BOTTLE_1,           // ITEM_ZORA_EGG
    SLOT_BOTTLE_1,           // ITEM_GOLD_DUST
    SLOT_BOTTLE_1,           // ITEM_MUSHROOM
    SLOT_BOTTLE_1,           // ITEM_SEAHORSE
    SLOT_BOTTLE_1,           // ITEM_CHATEAU
    SLOT_BOTTLE_1,           // ITEM_HYLIAN_LOACH
    SLOT_BOTTLE_1,           // ITEM_OBABA_DRINK
    SLOT_TRADE_DEED,         // ITEM_MOONS_TEAR
    SLOT_TRADE_DEED,         // ITEM_DEED_LAND
    SLOT_TRADE_DEED,         // ITEM_DEED_SWAMP
    SLOT_TRADE_DEED,         // ITEM_DEED_MOUNTAIN
    SLOT_TRADE_DEED,         // ITEM_DEED_OCEAN
    SLOT_TRADE_KEY_MAMA,     // ITEM_ROOM_KEY
    SLOT_TRADE_KEY_MAMA,     // ITEM_LETTER_MAMA
    SLOT_TRADE_COUPLE,       // ITEM_LETTER_TO_KAFEI
    SLOT_TRADE_COUPLE,       // ITEM_PENDANT_OF_MEMORIES
    SLOT_TRADE_COUPLE,       // ITEM_TINGLE_MAP
    SLOT_MASK_DEKU,          // ITEM_MASK_DEKU
    SLOT_MASK_GORON,         // ITEM_MASK_GORON
    SLOT_MASK_ZORA,          // ITEM_MASK_ZORA
    SLOT_MASK_FIERCE_DEITY,  // ITEM_MASK_FIERCE_DEITY
    SLOT_MASK_TRUTH,         // ITEM_MASK_TRUTH
    SLOT_MASK_KAFEIS_MASK,   // ITEM_MASK_KAFEIS_MASK
    SLOT_MASK_ALL_NIGHT,     // ITEM_MASK_ALL_NIGHT
    SLOT_MASK_BUNNY,         // ITEM_MASK_BUNNY
    SLOT_MASK_KEATON,        // ITEM_MASK_KEATON
    SLOT_MASK_GARO,          // ITEM_MASK_GARO
    SLOT_MASK_ROMANI,        // ITEM_MASK_ROMANI
    SLOT_MASK_CIRCUS_LEADER, // ITEM_MASK_CIRCUS_LEADER
    SLOT_MASK_POSTMAN,       // ITEM_MASK_POSTMAN
    SLOT_MASK_COUPLE,        // ITEM_MASK_COUPLE
    SLOT_MASK_GREAT_FAIRY,   // ITEM_MASK_GREAT_FAIRY
    SLOT_MASK_GIBDO,         // ITEM_MASK_GIBDO
    SLOT_MASK_DON_GERO,      // ITEM_MASK_DON_GERO
    SLOT_MASK_KAMARO,        // ITEM_MASK_KAMARO
    SLOT_MASK_CAPTAIN,       // ITEM_MASK_CAPTAIN
    SLOT_MASK_STONE,         // ITEM_MASK_STONE
    SLOT_MASK_BREMEN,        // ITEM_MASK_BREMEN
    SLOT_MASK_BLAST,         // ITEM_MASK_BLAST
    SLOT_MASK_SCENTS,        // ITEM_MASK_SCENTS
    SLOT_MASK_GIANT,         // ITEM_MASK_GIANT
    SLOT_BOW,                // ITEM_BOW_FIRE
    SLOT_BOW,                // ITEM_BOW_ICE
    SLOT_BOW,                // ITEM_BOW_LIGHT
#ifdef COMBO_BUILD
    // ComboShip (cross-game items): explicit SLOT_NONE up to ITEM_COMBO_MIN. Callers guard with
    // `itemId < ARRAY_COUNT(gItemSlots) && gItemSlots[itemId] != SLOT_NONE`; a zero-filled gap would
    // read as SLOT_OCARINA and write those items over the ocarina. (MSVC C has no range designators.)
    SLOT_NONE, // 0x4D
    SLOT_NONE, // 0x4E
    SLOT_NONE, // 0x4F
    SLOT_NONE, // 0x50
    SLOT_NONE, // 0x51
    SLOT_NONE, // 0x52
    SLOT_NONE, // 0x53
    SLOT_NONE, // 0x54
    SLOT_NONE, // 0x55
    SLOT_NONE, // 0x56
    SLOT_NONE, // 0x57
    SLOT_NONE, // 0x58
    SLOT_NONE, // 0x59
    SLOT_NONE, // 0x5A
    SLOT_NONE, // 0x5B
    SLOT_NONE, // 0x5C
    SLOT_NONE, // 0x5D
    SLOT_NONE, // 0x5E
    SLOT_NONE, // 0x5F
    SLOT_NONE, // 0x60
    SLOT_NONE, // 0x61
    SLOT_NONE, // 0x62
    SLOT_NONE, // 0x63
    SLOT_NONE, // 0x64
    SLOT_NONE, // 0x65
    SLOT_NONE, // 0x66
    SLOT_NONE, // 0x67
    SLOT_NONE, // 0x68
    SLOT_NONE, // 0x69
    SLOT_NONE, // 0x6A
    SLOT_NONE, // 0x6B
    SLOT_NONE, // 0x6C
    SLOT_NONE, // 0x6D
    SLOT_NONE, // 0x6E
    SLOT_NONE, // 0x6F
    SLOT_NONE, // 0x70
    SLOT_NONE, // 0x71
    SLOT_NONE, // 0x72
    SLOT_NONE, // 0x73
    SLOT_NONE, // 0x74
    SLOT_NONE, // 0x75
    SLOT_NONE, // 0x76
    SLOT_NONE, // 0x77
    SLOT_NONE, // 0x78
    SLOT_NONE, // 0x79
    SLOT_NONE, // 0x7A
    SLOT_NONE, // 0x7B
    SLOT_NONE, // 0x7C
    SLOT_NONE, // 0x7D
    SLOT_NONE, // 0x7E
    SLOT_NONE, // 0x7F
    SLOT_NONE, // 0x80
    SLOT_NONE, // 0x81
    SLOT_NONE, // 0x82
    SLOT_NONE, // 0x83
    SLOT_NONE, // 0x84
    SLOT_NONE, // 0x85
    SLOT_NONE, // 0x86
    SLOT_NONE, // 0x87
    SLOT_NONE, // 0x88
    SLOT_NONE, // 0x89
    SLOT_NONE, // 0x8A
    SLOT_NONE, // 0x8B
    SLOT_NONE, // 0x8C
    SLOT_NONE, // 0x8D
    SLOT_NONE, // 0x8E
    SLOT_NONE, // 0x8F
    SLOT_NONE, // 0x90
    SLOT_NONE, // 0x91
    SLOT_NONE, // 0x92
    SLOT_NONE, // 0x93
    SLOT_NONE, // 0x94
    SLOT_NONE, // 0x95
    SLOT_NONE, // 0x96
    SLOT_NONE, // 0x97
    SLOT_NONE, // 0x98
    SLOT_NONE, // 0x99
    SLOT_NONE, // 0x9A
    SLOT_NONE, // 0x9B
    SLOT_NONE, // 0x9C
    SLOT_NONE, // 0x9D
    SLOT_NONE, // 0x9E
    SLOT_NONE, // 0x9F
    SLOT_NONE, // 0xA0
    SLOT_NONE, // 0xA1
    SLOT_NONE, // 0xA2
    SLOT_NONE, // 0xA3
    SLOT_NONE, // 0xA4
    SLOT_NONE, // 0xA5
    SLOT_NONE, // 0xA6
    SLOT_NONE, // 0xA7
    SLOT_NONE, // 0xA8
    SLOT_NONE, // 0xA9
    SLOT_NONE, // 0xAA
    SLOT_NONE, // 0xAB
    SLOT_NONE, // 0xAC
    SLOT_NONE, // 0xAD
    SLOT_NONE, // 0xAE
    SLOT_NONE, // 0xAF
    SLOT_NONE, // 0xB0
    SLOT_NONE, // 0xB1
    SLOT_NONE, // 0xB2
    SLOT_NONE, // 0xB3
    SLOT_NONE, // 0xB4
    SLOT_NONE, // 0xB5
    SLOT_NONE, // 0xB6
    SLOT_NONE, // 0xB7
    // OOT items share the trade-slot cycles like OoTMM's extra items.
    [ITEM_COMBO_BOOTS_IRON] = SLOT_TRADE_KEY_MAMA,
    [ITEM_COMBO_TUNIC_GORON] = SLOT_TRADE_KEY_MAMA,
    [ITEM_COMBO_BOOTS_HOVER] = SLOT_TRADE_COUPLE,
    [ITEM_COMBO_TUNIC_ZORA] = SLOT_TRADE_COUPLE,
    [ITEM_COMBO_MASK_GERUDO] = SLOT_TRADE_DEED,
    [ITEM_COMBO_MASK_SKULL] = SLOT_TRADE_KEY_MAMA,
    [ITEM_COMBO_MASK_SPOOKY] = SLOT_TRADE_COUPLE,
    [ITEM_COMBO_SPELL_FIRE] = SLOT_TRADE_DEED,
    [ITEM_COMBO_SPELL_WIND] = SLOT_TRADE_KEY_MAMA,
    [ITEM_COMBO_SPELL_LOVE] = SLOT_TRADE_COUPLE,
    [ITEM_COMBO_HAMMER] = SLOT_TRADE_DEED,
    [ITEM_COMBO_BOOMERANG] = SLOT_TRADE_KEY_MAMA,
    [ITEM_COMBO_SLINGSHOT] = SLOT_TRADE_COUPLE,
#endif
};

s16 gItemPrices[] = {
    0,   // ITEM_OCARINA_OF_TIME
    0,   // ITEM_BOW
    0,   // ITEM_ARROW_FIRE
    0,   // ITEM_ARROW_ICE
    0,   // ITEM_ARROW_LIGHT
    0,   // ITEM_OCARINA_FAIRY
    0,   // ITEM_BOMB
    0,   // ITEM_BOMBCHU
    0,   // ITEM_DEKU_STICK
    0,   // ITEM_DEKU_NUT
    0,   // ITEM_MAGIC_BEANS
    0,   // ITEM_SLINGSHOT
    0,   // ITEM_POWDER_KEG
    0,   // ITEM_PICTOGRAPH_BOX
    0,   // ITEM_LENS_OF_TRUTH
    0,   // ITEM_HOOKSHOT
    0,   // ITEM_SWORD_GREAT_FAIRY
    0,   // ITEM_LONGSHOT
    0,   // ITEM_BOTTLE
    20,  // ITEM_POTION_RED
    20,  // ITEM_POTION_GREEN
    20,  // ITEM_POTION_BLUE
    20,  // ITEM_FAIRY
    0,   // ITEM_DEKU_PRINCESS
    20,  // ITEM_MILK_BOTTLE
    5,   // ITEM_MILK_HALF
    20,  // ITEM_FISH
    20,  // ITEM_BUG
    50,  // ITEM_BLUE_FIRE
    50,  // ITEM_POE
    200, // ITEM_BIG_POE
    20,  // ITEM_SPRING_WATER
    20,  // ITEM_HOT_SPRING_WATER
    20,  // ITEM_ZORA_EGG
    200, // ITEM_GOLD_DUST
    5,   // ITEM_MUSHROOM
    0,   // ITEM_SEAHORSE
    200, // ITEM_CHATEAU
    20,  // ITEM_HYLIAN_LOACH
    0,   // ITEM_OBABA_DRINK
    0,   // ITEM_MOONS_TEAR
    0,   // ITEM_DEED_LAND
    0,   // ITEM_DEED_SWAMP
    0,   // ITEM_DEED_MOUNTAIN
    0,   // ITEM_DEED_OCEAN
    0,   // ITEM_ROOM_KEY
    0,   // ITEM_LETTER_MAMA
    0,   // ITEM_LETTER_KAFEI
    0,   // ITEM_PENDANT_OF_MEMORIES
    0,   // ITEM_TINGLE_MAP
};

// Used to map scene indexes to their region in Termina
u16 gSceneIdsPerRegion[REGION_MAX][27] = {
    // REGION_GREAT_BAY
    {
        SCENE_30GYOSON,
        SCENE_LABO,
        SCENE_FISHERMAN,
        SCENE_SINKAI,
        SCENE_31MISAKI,
        SCENE_TORIDE,
        SCENE_KAIZOKU,
        SCENE_PIRATE,
        SCENE_35TAKI,
        SCENE_KINDAN2,
        0xFFFF,
    },
    // REGION_ZORA_HALL
    {
        SCENE_33ZORACITY,
        SCENE_BANDROOM,
        0xFFFF,
    },
    // REGION_ROMANI_RANCH
    {
        SCENE_F01,
        SCENE_ROMANYMAE,
        SCENE_OMOYA,
        SCENE_F01C,
        SCENE_F01_B,
        SCENE_KOEPONARACE,
        0xFFFF,
    },
    // REGION_DEKU_PALACE
    {
        SCENE_22DEKUCITY,
        SCENE_DEKU_KING,
        SCENE_26SARUNOMORI,
        SCENE_DANPEI,
        0xFFFF,
    },
    // REGION_WOODFALL
    {
        SCENE_20SICHITAI,
        SCENE_20SICHITAI2,
        SCENE_MAP_SHOP,
        SCENE_WITCH_SHOP,
        SCENE_21MITURINMAE,
        SCENE_KINSTA1,
        0xFFFF,
    },
    // REGION_CLOCK_TOWN
    {
        SCENE_00KEIKOKU,   SCENE_TENMON_DAI, SCENE_13HUBUKINOMITI, SCENE_24KEMONOMITI, SCENE_SYATEKI_MORI,
        SCENE_IKANAMAE,    SCENE_TOWN,       SCENE_SYATEKI_MIZU,   SCENE_BOWLING,      SCENE_TAKARAYA,
        SCENE_YADOYA,      SCENE_SONCHONOIE, SCENE_MILK_BAR,       SCENE_ICHIBA,       SCENE_BOMYA,
        SCENE_AYASHIISHOP, SCENE_8ITEMSHOP,  SCENE_DOUJOU,         SCENE_POSTHOUSE,    SCENE_TAKARAKUJI,
        SCENE_BACKTOWN,    SCENE_CLOCKTOWER, SCENE_INSIDETOWER,    SCENE_OKUJOU,       SCENE_ALLEY,
        SCENE_DEKUTES,     0xFFFF,
    },
    // REGION_SNOWHEAD
    {
        SCENE_10YUKIYAMANOMURA,
        SCENE_10YUKIYAMANOMURA2,
        SCENE_KAJIYA,
        SCENE_12HAKUGINMAE,
        SCENE_14YUKIDAMANOMITI,
        SCENE_GORONRACE,
        SCENE_GORON_HAKA,
        SCENE_17SETUGEN,
        SCENE_17SETUGEN2,
        0xFFFF,
    },
    // REGION_IKANA_GRAVEYARD
    {
        SCENE_BOTI,
        SCENE_DANPEI2TEST,
        0xFFFF,
    },
    // REGION_IKANA_CANYON
    {
        SCENE_CASTLE,
        SCENE_IKNINSIDE,
        SCENE_IKANA,
        SCENE_SECOM,
        SCENE_MUSICHOUSE,
        SCENE_RANDOM,
        SCENE_REDEAD,
        SCENE_TOUGITES,
        SCENE_HAKASHITA,
        0xFFFF,
    },
    // REGION_GORON_VILLAGE
    {
        SCENE_11GORONNOSATO,
        SCENE_11GORONNOSATO2,
        SCENE_16GORON_HOUSE,
        SCENE_GORONSHOP,
        0xFFFF,
    },
    // REGION_STONE_TOWER
    {
        SCENE_F40,
        SCENE_F41,
        0xFFFF,
    },
};

s32 Inventory_GetBtnBItem(PlayState* play) {
    if (gSaveContext.buttonStatus[EQUIP_SLOT_B] == BTN_DISABLED) {
        return ITEM_NONE;
    } else if (gSaveContext.bButtonStatus == BTN_DISABLED) {
        return ITEM_NONE;
    } else if (CUR_FORM_EQUIP(EQUIP_SLOT_B) == ITEM_NONE) {
        //! @bug "Weird B": If the B button is empty and there's a B do action text displaying, pressing B will use the
        //! item with the same id as the current do action. It's unclear what this code was originally intended for.
        if (play->interfaceCtx.bButtonPlayerDoActionActive &&
            play->interfaceCtx.bButtonPlayerDoAction != DO_ACTION_ATTACK) {
            return play->interfaceCtx.bButtonPlayerDoAction;
        }
        return ITEM_NONE;
    } else {
        return CUR_FORM_EQUIP(EQUIP_SLOT_B);
    }
}

/**
 * Only changes shield
 */
void Inventory_ChangeEquipment(s16 value) {
    SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, value);
}

/**
 * Only deletes shield, equipment argument unused and is a remnant of OoT
 */
u8 Inventory_DeleteEquipment(PlayState* play, s16 equipment) {
    Player* player = GET_PLAYER(play);

    if (GET_CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != EQUIP_VALUE_SHIELD_NONE) {
        SET_EQUIP_VALUE(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_NONE);
        Player_SetEquipmentData(play, player);
        return true;
    }

    return false;
}

void Inventory_ChangeUpgrade(s16 upgrade, u32 value) {
    u32 upgrades = GET_SAVE_INVENTORY_UPGRADES;

    upgrades &= gUpgradeNegMasks[upgrade];
    upgrades |= value << gUpgradeShifts[upgrade];

    gSaveContext.save.saveInfo.inventory.upgrades = upgrades;
}

s32 Inventory_IsMapVisible(s16 sceneId) {
    s16 index = 0;

    /**
     * a single index of scenesVisible can only hold 32 bits. So for every 32 scenes in the scene enum,
     * increment to the next index of scenesVisible so that every scene gets a unique flag in scenesVisible,
     * 224 bits were allocated to this although there are only 112 scenes
     */
    if (sceneId >= 0x20) {
        if (sceneId < 0x40) {
            index = 1;
        } else if (sceneId < 0x60) {
            index = 2;
        } else if (sceneId < 0x80) {
            index = 3;
        } else if (sceneId < 0xA0) {
            index = 4;
        } else if (sceneId < 0xC0) {
            index = 5;
        } else if (sceneId < 0xE0) {
            index = 6;
        }
    }

    if (gSaveContext.save.saveInfo.scenesVisible[index] & gBitFlags[sceneId - (index << 5)]) {
        return true;
    }

    return false;
}

static u16 sSceneIdsPerTingleMap[TINGLE_MAP_MAX][12] = {
    {
        // TINGLE_MAP_CLOCK_TOWN
        SCENE_00KEIKOKU,
        SCENE_BOTI,
        SCENE_13HUBUKINOMITI,
        SCENE_24KEMONOMITI,
        SCENE_IKANAMAE,
        SCENE_TOWN,
        SCENE_ICHIBA,
        SCENE_BACKTOWN,
        SCENE_CLOCKTOWER,
        SCENE_ALLEY,
        0xFFFF,
    },
    {
        // TINGLE_MAP_WOODFALL
        SCENE_20SICHITAI,
        SCENE_20SICHITAI2,
        SCENE_21MITURINMAE,
        SCENE_22DEKUCITY,
        SCENE_DEKU_KING,
        SCENE_KINSTA1,
        0xFFFF,
    },
    {
        // TINGLE_MAP_SNOWHEAD
        SCENE_10YUKIYAMANOMURA,
        SCENE_10YUKIYAMANOMURA2,
        SCENE_11GORONNOSATO,
        SCENE_11GORONNOSATO2,
        SCENE_16GORON_HOUSE,
        SCENE_12HAKUGINMAE,
        SCENE_14YUKIDAMANOMITI,
        SCENE_GORONRACE,
        SCENE_17SETUGEN,
        SCENE_17SETUGEN2,
        0xFFFF,
    },
    {
        // TINGLE_MAP_ROMANI_RANCH
        SCENE_F01,
        SCENE_ROMANYMAE,
        SCENE_OMOYA,
        SCENE_F01C,
        SCENE_F01_B,
        SCENE_KOEPONARACE,
        0xFFFF,
    },
    {
        // TINGLE_MAP_GREAT_BAY
        SCENE_30GYOSON,
        SCENE_SINKAI,
        SCENE_31MISAKI,
        SCENE_TORIDE,
        SCENE_KAIZOKU,
        SCENE_33ZORACITY,
        SCENE_35TAKI,
        0xFFFF,
    },
    {
        // TINGLE_MAP_STONE_TOWER
        SCENE_F40,
        SCENE_F41,
        SCENE_CASTLE,
        SCENE_IKANA,
        SCENE_REDEAD,
        0xFFFF,
    },
};

/**
 * Removing clouds from the World Map is achieved by purchasing a tingle map
 */
void Inventory_SetWorldMapCloudVisibility(s16 tingleIndex) {
    s16 i = 0;
    s16 index = 0;
    u16(*tingleMapSceneIds)[] = &sSceneIdsPerTingleMap[tingleIndex];

    if ((tingleIndex >= 0) && (tingleIndex < TINGLE_MAP_MAX)) {
        while (true) {
            if ((*tingleMapSceneIds)[i] == 0xFFFF) {
                break;
            }

            /**
             * a single index of scenesVisible can only hold 32 bits. So for every 32 scenes in the scene enum,
             * increment to the next index of scenesVisible so that every scene gets a unique flag in scenesVisible,
             * 224 bits were allocated to this although there are only 112 scenes
             */
            if (((s16)(*tingleMapSceneIds)[i]) < 0x20) {
                index = 0;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0x40) {
                index = 1;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0x60) {
                index = 2;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0x80) {
                index = 3;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0xA0) {
                index = 4;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0xC0) {
                index = 5;
            } else if (((s16)(*tingleMapSceneIds)[i]) < 0xE0) {
                index = 6;
            }

            gSaveContext.save.saveInfo.scenesVisible[index] = gSaveContext.save.saveInfo.scenesVisible[index] |
                                                              gBitFlags[(s16)(*tingleMapSceneIds)[i] - (index << 5)];
            i++;
        }

        if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_CLOCK_TOWN]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 3;
        } else if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_WOODFALL]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 0x1C;
        } else if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_SNOWHEAD]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 0xE0;
        } else if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_ROMANI_RANCH]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 0x100;
        } else if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_GREAT_BAY]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 0x1E00;
        } else if (*tingleMapSceneIds == sSceneIdsPerTingleMap[TINGLE_MAP_STONE_TOWER]) {
            gSaveContext.save.saveInfo.worldMapCloudVisibility |= 0x6000;
        }
    }

    R_MINIMAP_DISABLED = false;
}

/**
 * Also stores the players filename into dekuPlaygroundPlayerName
 */
void Inventory_SaveDekuPlaygroundHighScore(s16 timerId) {
    s16 i;

    gSaveContext.save.saveInfo.dekuPlaygroundHighScores[CURRENT_DAY - 1] = gSaveContext.timerCurTimes[timerId];

    for (i = 0; i < 8; i++) {
        gSaveContext.save.saveInfo.inventory.dekuPlaygroundPlayerName[CURRENT_DAY - 1][i] =
            gSaveContext.save.saveInfo.playerData.playerName[i];
    }
}

void Inventory_IncrementSkullTokenCount(s16 sceneIndex) {
    if (sceneIndex == SCENE_KINSTA1) {
        // Swamp Spider House (increment high bits of skullTokenCount)
        gSaveContext.save.saveInfo.skullTokenCount =
            ((u16)(((gSaveContext.save.saveInfo.skullTokenCount & 0xFFFF0000) >> 0x10) + 1) << 0x10) |
            (gSaveContext.save.saveInfo.skullTokenCount & 0xFFFF);
    } else {
        // Ocean Spider House (increment low bits of skullTokenCount)
        gSaveContext.save.saveInfo.skullTokenCount = (((u16)gSaveContext.save.saveInfo.skullTokenCount + 1) & 0xFFFF) |
                                                     (gSaveContext.save.saveInfo.skullTokenCount & 0xFFFF0000);
    }
}

s16 Inventory_GetSkullTokenCount(s16 sceneIndex) {
    if (sceneIndex == SCENE_KINSTA1) {
        // Swamp Spider House
        return (gSaveContext.save.saveInfo.skullTokenCount & 0xFFFF0000) >> 0x10;
    } else {
        // Ocean Spider House
        return gSaveContext.save.saveInfo.skullTokenCount & 0xFFFF;
    }
}

void Inventory_SaveLotteryCodeGuess(PlayState* play) {
    u16 lotteryCodeGuess;

    lotteryCodeGuess = ((play->msgCtx.unk12054[0] & 0xF) << 8);  // First Digit
    lotteryCodeGuess |= ((play->msgCtx.unk12054[1] & 0xF) << 4); // Second Digit
    lotteryCodeGuess |= (play->msgCtx.unk12054[2] & 0xF);        // Third Digit
    HS_SET_LOTTERY_CODE_GUESS(lotteryCodeGuess);
}
