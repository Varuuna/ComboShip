#include "core/ComboPlatform.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>

#ifdef _WIN32
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <dlfcn.h>
#endif

void ComboTerminateHandler() {
    std::cerr << "[ComboShip] std::terminate";
    if (auto ep = std::current_exception()) {
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception& e) { std::cerr << " — uncaught std::exception: " << e.what(); } catch (...) {
            std::cerr << " — uncaught non-std exception";
        }
    }
    std::cerr << std::endl;
    std::abort();
}

#ifdef _WIN32
LONG WINAPI ComboLateCrashFilter(PEXCEPTION_POINTERS ex) {
    FILE* f = nullptr;
    fopen_s(&f, "combo_late_crash.txt", "w");
    if (!f) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    fprintf(f, "[ComboShip] late crash: exception 0x%08lX at %p\n", ex->ExceptionRecord->ExceptionCode,
            ex->ExceptionRecord->ExceptionAddress);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT ctx = *ex->ContextRecord;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        char module[MAX_PATH] = "???";
        HMODULE hMod = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)frame.AddrPC.Offset, &hMod);
        if (hMod) {
            GetModuleFileNameA(hMod, module, sizeof(module));
        }
        char symBuf[sizeof(SYMBOL_INFO) + 256] = {};
        SYMBOL_INFO* sym = (SYMBOL_INFO*)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
            fprintf(f, "  %s + 0x%llx in %s\n", sym->Name, (unsigned long long)disp, module);
        } else {
            fprintf(f, "  0x%llx in %s\n", (unsigned long long)frame.AddrPC.Offset, module);
        }
    }
    fclose(f);
    return EXCEPTION_EXECUTE_HANDLER; // die quietly; the report is on disk
}
#endif

#ifdef _WIN32
DllHandle LoadDll(const char* name) {
    return LoadLibraryA(name);
}
void* GetSym(DllHandle h, const char* sym) {
    return (void*)GetProcAddress(h, sym);
}
void FreeDll(DllHandle h) {
    FreeLibrary(h);
}
std::string DllError() {
    return std::to_string(GetLastError());
}
#else
DllHandle LoadDll(const char* name) {
    return dlopen(name, RTLD_NOW | RTLD_GLOBAL);
}
void* GetSym(DllHandle h, const char* sym) {
    return dlsym(h, sym);
}
void FreeDll(DllHandle h) {
    dlclose(h);
}
std::string DllError() {
    return dlerror();
}
#endif
