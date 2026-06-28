/* combo/menu/ComboMenuABI.h
 * ComboShip: flat C-ABI menu contract. The games' C++ WidgetInfo / std::function /
 * options structs CANNOT cross the DLL boundary (ABI-diverged: std::function vs raw
 * ptr, differing enums/fields). So each game emits this POD array; comboui renders
 * declarative rows itself and invokes custom/preFunc/callback BY INDEX back into the
 * owning DLL. Strings point into the game's static storage (valid for process life).
 */
#ifndef COMBO_MENU_ABI_H
#define COMBO_MENU_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Normalized widget kind — the UNION of OOT and MM WidgetType. comboui renders these. */
typedef enum {
    CW_SEPARATOR = 0,
    CW_SEPARATOR_TEXT,
    CW_TEXT,
    CW_CHECKBOX,      /* CVar bool */
    CW_SLIDER_INT,    /* CVar int  */
    CW_SLIDER_FLOAT,  /* CVar float */
    CW_COMBOBOX,      /* CVar int, choices in options */
    CW_BTN_SELECTOR,  /* CVar int cycled by a button; no choice list */
    CW_INPUT_TEXT,    /* OOT WIDGET_INPUT/CVAR_INPUT */
    CW_COLOR,         /* color picker (useAlpha flag distinguishes 24/32) */
    CW_BUTTON,        /* fires callback by index */
    CW_WINDOW_BUTTON, /* toggles a window CVar */
    CW_AUDIO_BACKEND,
    CW_VIDEO_BACKEND,
    CW_CUSTOM /* drawn entirely by the owning game via index */
} CwKind;

typedef struct {
    const char* label;
    int32_t value; /* combobox: CVar value for this entry */
} CwChoice;

typedef struct {
    int32_t index; /* stable index into the game's flat widget list (the invoke key) */
    CwKind kind;
    const char* name;       /* display label */
    const char* cvar;       /* CVar backing this widget, or "" */
    const char* tooltip;    /* "" if none */
    const char* windowName; /* for CW_WINDOW_BUTTON, else "" */
    /* numeric ranges (sliders) */
    float fMin, fMax, fStep, fDefault;
    int32_t iMin, iMax, iStep, iDefault;
    int32_t bDefault; /* checkbox default */
    int32_t useAlpha; /* CW_COLOR: 1 if 32-bit */
    /* combobox choices */
    const CwChoice* choices;
    int32_t choiceCount;
    /* capability flags */
    int32_t hasCallback;       /* widget has a .Callback() to run after a CVar change */
    int32_t hasPreFunc;        /* widget has a per-frame disable evaluation */
    int32_t gameLoopDependent; /* action requires the game's frame loop; foreground-only */
    int32_t sameLine;
    int32_t hideInSearch;
    int32_t column; /* 0-based column index within the sidebar (for faithful multi-column layout) */
} CwWidget;

typedef struct {
    const char* sidebarName;
    uint32_t columnCount;
    const CwWidget* widgets; /* flattened across columns */
    int32_t widgetCount;
} CwSidebar;

typedef struct {
    const char* sectionLabel;
    const char* sidebarCvar;
    const CwSidebar* sidebars;
    int32_t sidebarCount;
} CwSection;

typedef struct {
    int32_t version; /* ABI version; bump on layout change */
    const CwSection* sections;
    int32_t sectionCount;
} CwMenu;

/* === Per-game exports (implemented in soh.dll / 2ship.dll) === */
typedef const CwMenu* (*Fn_ExportMenu)(void);
typedef void (*Fn_MenuInvokeCallback)(int32_t index);
typedef int32_t (*Fn_MenuEvalDisabled)(int32_t index, const char** outReason);
typedef void (*Fn_MenuDrawCustom)(int32_t index);
typedef void (*Fn_MenuApplyCVar)(const char* cvar); // re-run the game's ShipInit for a changed CVar

#ifdef __cplusplus
}
#endif
#endif /* COMBO_MENU_ABI_H */
