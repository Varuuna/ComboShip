// combo/gui/ComboExtractScreen.h
// ComboShip: the combo-owned ROM-extraction screen, hosted in comboui.dll. Runs the libultraship
// frame loop and renders a single screen that gathers BOTH ROMs, then extracts them with progress
// bars. See ComboExtract.h for the ABI and docs/UPSTREAM_MERGES.md for the rationale.
#pragma once

#include "ComboExtract.h"
#include "ComboExport.h"

extern "C" COMBO_EXPORT int ComboUI_RunExtraction(const ComboExtractCallbacks* cb);
