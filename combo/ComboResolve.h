#pragma once

// Resolve a combo-ABI symbol from another already-loaded module. `module` is the
// logical name ("soh", "2ship", "comboui"). On Windows each module is a DLL with
// its own symbol namespace, looked up by file name. On ELF platforms the launcher
// dlopens every module RTLD_GLOBAL and the combo ABI is the only surface with
// default visibility (see ComboExport.h), so one process-wide lookup suffices and
// the module name is irrelevant.
#include <cstdio>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

inline void* Combo_ResolveSym(const char* module, const char* name) {
#ifdef _WIN32
    char dll[64];
    snprintf(dll, sizeof(dll), "%s.dll", module);
    HMODULE h = GetModuleHandleA(dll);
    return h != NULL ? reinterpret_cast<void*>(GetProcAddress(h, name)) : nullptr;
#else
    (void)module;
    return dlsym(RTLD_DEFAULT, name);
#endif
}
