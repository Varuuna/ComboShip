#pragma once

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif
const char* SohFileSelect_GetSettingText(u8 optionIndex, u8 language);
void SohFileSelect_ShowPresetModal();
#ifdef __cplusplus
};
#endif

typedef enum {
    RSM_START_RANDOMIZER,
    RSM_GENERATE_RANDOMIZER,
    RSM_OPEN_RANDOMIZER_SETTINGS,
    RSM_GENERATING,
    RSM_NO_RANDOMIZER_GENERATED,
#ifdef COMBO_BUILD
    RSM_FINALIZING, // ComboShip: post-fill generation phase (see SOH_GetComboGenPhase)
#endif
    RSM_MAX,
} RandomizerSettingsMenuEnums;
