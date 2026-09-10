// ComboShip: platform shims for the launcher — DLL loading plus the two crash/terminate handlers.
#pragma once

#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX // std::min/max in the rando headers
#include <Windows.h>
typedef HMODULE DllHandle;
#else
typedef void* DllHandle;
#endif

DllHandle LoadDll(const char* name);
void* GetSym(DllHandle h, const char* sym);
void FreeDll(DllHandle h);
std::string DllError();

// Surfaces the real exception behind a silent terminate()/exit(3). Install with std::set_terminate.
void ComboTerminateHandler();

#ifdef _WIN32
// Last-chance crash capture for the post-teardown window (FreeLibrary / CRT exit / static dtors),
// which libultraship's Context-dependent CrashHandler can't cover. Writes combo_late_crash.txt.
LONG WINAPI ComboLateCrashFilter(PEXCEPTION_POINTERS ex);
#endif
