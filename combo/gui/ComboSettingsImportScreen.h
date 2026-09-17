// combo/gui/ComboSettingsImportScreen.h
// ComboShip: the combo-owned first-launch settings-import screen, hosted in comboui.dll. Runs the
// libultraship frame loop and lets the player pick an existing SoH and/or 2Ship config to import.
// See ComboSettingsImport.h for the ABI.
#pragma once

#include "../ComboSettingsImport.h"
#include "ComboExport.h"

extern "C" COMBO_EXPORT int ComboUI_RunSettingsImport(const ComboSettingsImportCallbacks* cb,
                                                      ComboSettingsImportResult* out);
