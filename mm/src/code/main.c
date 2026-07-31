#ifdef _WIN32
#include <Windows.h>
#endif
#include <stdio.h>
#include <locale.h>
#include "ComboExport.h"

#include "audiomgr.h"
#include "fault.h"
#include "idle.h"
#include "irqmgr.h"
#include "padmgr.h"
#include "scheduler.h"
#include "CIC6105.h"
#include "stack.h"
#include "stackcheck.h"
#include "BenPort.h"
#include <libultraship/bridge/crashhandlerbridge.h>

// Variables are put before most headers as a hacky way to bypass bss reordering
OSMesgQueue sSerialEventQueue;
OSMesg sSerialMsgBuf[1];
uintptr_t gSegments[NUM_SEGMENTS];
SchedContext gSchedContext;
IrqMgrClient sIrqClient;
OSMesgQueue sIrqMgrMsgQueue;
OSMesg sIrqMgrMsgBuf[60];
OSThread gGraphThread;
STACK(sGraphStack, 0x1800);
STACK(sSchedStack, 0x600);
STACK(sAudioStack, 0x800);
STACK(sPadMgrStack, 0x500);
StackEntry sGraphStackInfo;
StackEntry sSchedStackInfo;
StackEntry sAudioStackInfo;
StackEntry sPadMgrStackInfo;
AudioMgr sAudioMgr;
static s32 sBssPad;
PadMgr gPadMgr;

#include "main.h"
#include "buffers.h"
#include "global.h"
#include "system_heap.h"
#include "z64thread.h"

s32 gScreenWidth = SCREEN_WIDTH;
s32 gScreenHeight = SCREEN_HEIGHT;
size_t gSystemHeapSize = 0;

void InitOTR(int argc, char* argv[]);
void Heaps_Free(void);
#ifdef COMBO_BUILD
// ComboShip: when nonzero, MM_RunMain initializes MM but skips its blocking game loop
// (Graph_ThreadEntry). Set by MM_BootForCombo for the eager OOT-startup boot.
extern int gComboBootOnly;
#endif
#ifdef __GNUC__
#define SDL_main main
#endif

int SDL_main(int argc, char* argv[] /* void* arg*/) {
    intptr_t fb;
    intptr_t sysHeap;
    s32 exit;
    s16* msg;

// Attach console for windows so we can conditionally display it when running the extractor
#ifdef _WIN32
    AllocConsole();
    (void)freopen("CONIN$", "r", stdin);
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#ifndef _DEBUG
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
    // Allow non-ascii characters for Windows
    setlocale(LC_ALL, ".UTF8");
#endif // _WIN32

    InitOTR(argc, argv);
    CrashHandlerRegisterCallback(CrashHandler_PrintExt);
    Heaps_Alloc();

    gScreenWidth = SCREEN_WIDTH;
    gScreenHeight = SCREEN_HEIGHT;

    Nmi_Init();
    Fault_Init();
    Check_RegionIsSupported();
    Check_ExpansionPak();
    sysHeap = gSystemHeap;
    // fb = FRAMEBUFFERS_START_ADDR;
    // gSystemHeapSize = fb - sysHeap;
    SystemHeap_Init(sysHeap, SYSTEM_HEAP_SIZE);

    Regs_Init();

    R_ENABLE_ARENA_DBG = 0;

    osCreateMesgQueue(&sSerialEventQueue, sSerialMsgBuf, ARRAY_COUNT(sSerialMsgBuf));
    osSetEventMesg(OS_EVENT_SI, &sSerialEventQueue, OS_MESG_PTR(NULL));

    osCreateMesgQueue(&sIrqMgrMsgQueue, sIrqMgrMsgBuf, ARRAY_COUNT(sIrqMgrMsgBuf));
    PadMgr_Init(&sSerialEventQueue, &gIrqMgr, Z_THREAD_ID_PADMGR, Z_PRIORITY_PADMGR, STACK_TOP(sPadMgrStack));

    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gSchedContext,
                  &gIrqMgr);
#if 0
    StackCheck_Init(&sSchedStackInfo, sSchedStack, STACK_TOP(sSchedStack), 0, 0x100, "sched");
    Sched_Init(&gSchedContext, STACK_TOP(sSchedStack), Z_PRIORITY_SCHED, gViConfigModeType, 1, &gIrqMgr);

    CIC6105_AddRomInfoFaultPage();

    IrqMgr_AddClient(&gIrqMgr, &sIrqClient, &sIrqMgrMsgQueue);

    StackCheck_Init(&sAudioStackInfo, sAudioStack, STACK_TOP(sAudioStack), 0, 0x100, "audio");
    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gSchedContext,
                  &gIrqMgr);

    StackCheck_Init(&sPadMgrStackInfo, sPadMgrStack, STACK_TOP(sPadMgrStack), 0, 0x100, "padmgr");

    AudioMgr_Unlock(&sAudioMgr);
    StackCheck_Init(&sGraphStackInfo, sGraphStack, STACK_TOP(sGraphStack), 0, 0x100, "graph");
    osCreateThread(&gGraphThread, Z_THREAD_ID_GRAPH, Graph_ThreadEntry, NULL, STACK_TOP(sGraphStack), Z_PRIORITY_GRAPH);
    osStartThread(&gGraphThread);
#endif

    Graph_ThreadEntry(0);

    exit = false;

    while (!exit) {
        msg = NULL;
        osRecvMesg(&sIrqMgrMsgQueue, (OSMesg*)&msg, OS_MESG_BLOCK);
        if (msg == NULL) {
            break;
        }

        switch (*msg) {
            case OS_SC_PRE_NMI_MSG:
                Nmi_SetPrenmiStart();
                break;

            case OS_SC_NMI_MSG:
                exit = true;
                break;
        }
    }

    IrqMgr_RemoveClient(&gIrqMgr, &sIrqClient);
    osDestroyThread(&gGraphThread);

    DeinitOTR();

#ifdef _WIN32
    FreeConsole();
#endif
    Heaps_Free();
}

#ifdef COMBO_BUILD
// ComboShip: entry point — same as SDL_main but skips AllocConsole/FreeConsole, since the combo
// executable already owns the console.
COMBO_EXPORT
void MM_RunMain(void) {
    intptr_t sysHeap;

    setlocale(LC_ALL, ".UTF8");

    // ComboShip: InitOTR now takes (argc, argv) for CLI extraction; the combo MM boot has no CLI
    // args (extraction runs separately via MM_Extract).
    InitOTR(0, NULL);
    Heaps_Alloc();

    gScreenWidth = SCREEN_WIDTH;
    gScreenHeight = SCREEN_HEIGHT;

    Nmi_Init();
    Fault_Init();
    Check_RegionIsSupported();
    Check_ExpansionPak();
    sysHeap = gSystemHeap;
    SystemHeap_Init(sysHeap, SYSTEM_HEAP_SIZE);

    Regs_Init();

    R_ENABLE_ARENA_DBG = 0;

    osCreateMesgQueue(&sSerialEventQueue, sSerialMsgBuf, ARRAY_COUNT(sSerialMsgBuf));
    osSetEventMesg(OS_EVENT_SI, &sSerialEventQueue, OS_MESG_PTR(NULL));

    osCreateMesgQueue(&sIrqMgrMsgQueue, sIrqMgrMsgBuf, ARRAY_COUNT(sIrqMgrMsgBuf));
    PadMgr_Init(&sSerialEventQueue, &gIrqMgr, Z_THREAD_ID_PADMGR, Z_PRIORITY_PADMGR, STACK_TOP(sPadMgrStack));

    AudioMgr_Init(&sAudioMgr, STACK_TOP(sAudioStack), Z_PRIORITY_AUDIOMGR, Z_THREAD_ID_AUDIOMGR, &gSchedContext,
                  &gIrqMgr);

#ifdef COMBO_BUILD
    // ComboShip: MM_BootForCombo sets gComboBootOnly to initialize MM at OOT startup WITHOUT running
    // its blocking game loop — the cross-world oracle only needs the region graph + runtime. The loop
    // runs later via MM_ResumeGame on the first portal transition.
    if (!gComboBootOnly) {
        Graph_ThreadEntry(0);
    }
#else
    Graph_ThreadEntry(0);
    DeinitOTR();
#endif
}

// ComboShip: on resume, re-enter only the MM game loop on the already-booted process. The one-time
// heap/thread/IRQ setup from MM_RunMain() persists for the process lifetime and must NOT re-run.
// Graph_ThreadEntry runs `while (WindowIsRunning()) RunFrame();` and returns once the shared
// window's running flag is cleared again. Mirrors SOH_RunGameLoop.
COMBO_EXPORT
void MM_RunGameLoop(void) {
    Graph_ThreadEntry(0);
}

// MM's RunFrame state-0 path re-runs SysCfb_Init() + SystemArena_Malloc, which need a FRESH system
// arena. MM_RunGameLoop skips MM_RunMain's SystemHeap_Init, so on resume the arena is stale/advanced
// and SystemArena_Malloc returns a bad pointer -> RunFrame crashes in memset. Reset the arena over
// the EXISTING gSystemHeap buffer (no re-malloc, no leak) before resuming. (OOT doesn't need this:
// its SysCfb_Init is in Main(), not RunFrame, so its resume never re-carves the arena.)
void MM_ResetSystemHeapForResume(void) {
    SystemHeap_Init(gSystemHeap, SYSTEM_HEAP_SIZE);
    // Resetting the system arena orphans arena-allocated globals like gRegEditor (which holds SREG,
    // incl. R_UPDATE_RATE used as a divisor at frame time). Re-run Regs_Init to re-establish them,
    // mirroring MM_RunMain's order (SystemHeap_Init then Regs_Init).
    Regs_Init();
}
#endif
