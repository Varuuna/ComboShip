#pragma once

// Export decoration for the combo ABI: the symbols the launcher, comboui, and the
// opposite game resolve at runtime (SOH_*/MM_*/Combo_*/ComboUI_*/OOT_*). On Windows
// the games build as DLLs and need dllexport; on ELF platforms the game libraries
// build with -fvisibility=hidden so their duplicated engine internals cannot
// interpose each other, and the combo ABI opts back into default visibility here.
#ifdef _WIN32
#define COMBO_EXPORT __declspec(dllexport)
#else
#define COMBO_EXPORT __attribute__((visibility("default")))
#endif
