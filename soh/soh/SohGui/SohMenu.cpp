#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
<<<<<<< HEAD
#include <ship/utils/StringHelper.h>
#include "soh/OTRGlobals.h" // ComboShip: EvalDisabledByIndex foreground guard mirrors Menu::DrawElement
=======

#include "SohMenu.h"
>>>>>>> vendor-soh

extern "C" {
extern PlayState* gPlayState;
}

extern std::unordered_map<s16, const char*> warpPointSceneList;

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;

using namespace UIWidgets;

void SohMenu::AddSidebarEntry(std::string sectionName, std::string sidebarName, uint32_t columnCount) {
    assert(!sectionName.empty());
    assert(!sidebarName.empty());
    menuEntries.at(sectionName).sidebars.emplace(sidebarName, SidebarEntry{ .columnCount = columnCount });
    menuEntries.at(sectionName).sidebarOrder.push_back(sidebarName);
}

WidgetInfo& SohMenu::AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType) {
    assert(!widgetName.empty());                        // Must be unique
    assert(menuEntries.contains(pathInfo.sectionName)); // Section/header must already exist
    assert(menuEntries.at(pathInfo.sectionName).sidebars.contains(pathInfo.sidebarName)); // Sidebar must already exist
    std::unordered_map<std::string, SidebarEntry>& sidebar = menuEntries.at(pathInfo.sectionName).sidebars;
    uint8_t column = pathInfo.column;
    if (sidebar.contains(pathInfo.sidebarName)) {
        while (sidebar.at(pathInfo.sidebarName).columnWidgets.size() < static_cast<size_t>(column) + 1) {
            sidebar.at(pathInfo.sidebarName).columnWidgets.push_back({});
        }
    }
    SidebarEntry& entry = sidebar.at(pathInfo.sidebarName);
    entry.columnWidgets.at(column).push_back({ .name = widgetName, .type = widgetType });
    WidgetInfo& widget = entry.columnWidgets.at(column).back();
    switch (widgetType) {
        case WIDGET_CHECKBOX:
        case WIDGET_CVAR_CHECKBOX:
            widget.options = std::make_shared<CheckboxOptions>();
            break;
        case WIDGET_SLIDER_FLOAT:
        case WIDGET_CVAR_SLIDER_FLOAT:
            widget.options = std::make_shared<FloatSliderOptions>();
            break;
        case WIDGET_CVAR_BTN_SELECTOR:
            widget.options = std::make_shared<BtnSelectorOptions>();
            break;
        case WIDGET_SLIDER_INT:
        case WIDGET_CVAR_SLIDER_INT:
            widget.options = std::make_shared<IntSliderOptions>();
            break;
        case WIDGET_COMBOBOX:
        case WIDGET_CVAR_COMBOBOX:
        case WIDGET_AUDIO_BACKEND:
        case WIDGET_VIDEO_BACKEND:
            widget.options = std::make_shared<ComboboxOptions>();
            break;
        case WIDGET_BUTTON:
            widget.options = std::make_shared<ButtonOptions>();
            break;
        case WIDGET_WINDOW_BUTTON:
            widget.options = std::make_shared<WindowButtonOptions>();
            break;
        case WIDGET_CVAR_COLOR_PICKER:
        case WIDGET_COLOR_PICKER:
            widget.options = std::make_shared<ColorPickerOptions>();
            break;
        case WIDGET_SEPARATOR_TEXT:
        case WIDGET_TEXT:
            widget.options = std::make_shared<TextOptions>();
            break;
        case WIDGET_SEARCH:
        case WIDGET_SEPARATOR:
        default:
            widget.options = std::make_shared<WidgetOptions>();
    }
    return widget;
}

SohMenu::SohMenu(const std::string& consoleVariable, const std::string& name)
    : Menu(consoleVariable, name, 0, UIWidgets::Colors::LightBlue) {
}

void SohMenu::AddMenuElements() {
    AddMenuSettings();
    AddMenuEnhancements();
    AddMenuRandomizer();
    AddMenuNetwork();
    AddMenuDevTools();

    if (CVarGetInteger(CVAR_SETTING("Menu.SidebarSearch"), 0)) {
        InsertSidebarSearch();
    }

    for (auto& initFunc : MenuInit::GetInitFuncs()) {
        initFunc();
    }

    mMenuElementsInitialized = true;
}

void SohMenu::InitElement() {
    Ship::Menu::InitElement();

    disabledMap = {
        { DISABLE_FOR_NO_VSYNC,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->CanDisableVerticalSync();
           },
            "Disabling VSync not supported" } },
        { DISABLE_FOR_NO_WINDOWED_FULLSCREEN,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->SupportsWindowedFullscreen();
           },
            "Windowed Fullscreen not supported" } },
        { DISABLE_FOR_NO_MULTI_VIEWPORT,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SupportsViewports();
           },
            "Multi-viewports not supported" } },
        { DISABLE_FOR_NOT_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() !=
                      Fast::WindowBackend::FAST3D_DXGI_DX11;
           },
            "Available Only on DirectX" } },
        { DISABLE_FOR_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetRawInstance()->GetWindow()->GetWindowBackend() ==
                      Fast::WindowBackend::FAST3D_DXGI_DX11;
           },
            "Not Available on DirectX" } },
        { DISABLE_FOR_MATCH_REFRESH_RATE_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger(CVAR_SETTING("MatchRefreshRate"), 0); },
            "Match Refresh Rate is Enabled" } },
        { DISABLE_FOR_ADVANCED_RESOLUTION_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 0); },
            "Advanced Resolution Enabled" } },
        { DISABLE_FOR_VERTICAL_RES_TOGGLE_ON,
          { [](disabledInfo& info) -> bool {
               return CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);
           },
            "Vertical Resolution Toggle Enabled" } },
        { DISABLE_FOR_LOW_RES_MODE_ON,
          { [](disabledInfo& info) -> bool { return CVarGetInteger(CVAR_LOW_RES_MODE, 0); }, "N64 Mode Enabled" } },
        { DISABLE_FOR_NULL_PLAY_STATE,
          { [](disabledInfo& info) -> bool { return gPlayState == NULL; }, "Save Not Loaded" } },
        { DISABLE_FOR_DEBUG_MODE_OFF,
          { [](disabledInfo& info) -> bool { return !CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0); },
            "Debug Mode is Disabled" } },
        { DISABLE_FOR_FRAME_ADVANCE_OFF,
          { [](disabledInfo& info) -> bool { return !(gPlayState != nullptr && gPlayState->frameAdvCtx.enabled); },
            "Frame Advance is Disabled" } },
        { DISABLE_FOR_ADVANCED_RESOLUTION_OFF,
          { [](disabledInfo& info) -> bool { return !CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", 0); },
            "Advanced Resolution is Disabled" } },
        { DISABLE_FOR_VERTICAL_RESOLUTION_OFF,
          { [](disabledInfo& info) -> bool {
               return !CVarGetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 0);
           },
            "Vertical Resolution Toggle is Off" } },
    };
}

void SohMenu::UpdateElement() {
    Ship::Menu::UpdateElement();
}

void SohMenu::Draw() {
    Ship::Menu::Draw();
}

void SohMenu::DrawElement() {
    if (mMenuElementsInitialized) {
        Ship::Menu::DrawElement();
    }
}

// === ComboShip C-ABI menu export ============================================

namespace {
CwKind WidgetTypeToCwKind(WidgetType t) {
    switch (t) {
        case WIDGET_SEPARATOR:
            return CW_SEPARATOR;
        case WIDGET_SEPARATOR_TEXT:
            return CW_SEPARATOR_TEXT;
        case WIDGET_TEXT:
            return CW_TEXT;
        case WIDGET_CHECKBOX:
        case WIDGET_CVAR_CHECKBOX:
            return CW_CHECKBOX;
        case WIDGET_SLIDER_INT:
        case WIDGET_CVAR_SLIDER_INT:
            return CW_SLIDER_INT;
        case WIDGET_SLIDER_FLOAT:
        case WIDGET_CVAR_SLIDER_FLOAT:
            return CW_SLIDER_FLOAT;
        case WIDGET_COMBOBOX:
        case WIDGET_CVAR_COMBOBOX:
            return CW_COMBOBOX;
        case WIDGET_CVAR_BTN_SELECTOR:
            // BtnSelector is a discrete int cycled by a button; its options are BtnSelectorOptions
            // (no comboMap). Distinct kind so the emitter reads the correct options type.
            return CW_BTN_SELECTOR;
        case WIDGET_INPUT:
        case WIDGET_CVAR_INPUT:
            return CW_INPUT_TEXT;
        case WIDGET_COLOR_PICKER:
        case WIDGET_CVAR_COLOR_PICKER:
            return CW_COLOR;
        case WIDGET_BUTTON:
            return CW_BUTTON;
        case WIDGET_WINDOW_BUTTON:
            return CW_WINDOW_BUTTON;
        case WIDGET_AUDIO_BACKEND:
            return CW_AUDIO_BACKEND;
        case WIDGET_VIDEO_BACKEND:
            return CW_VIDEO_BACKEND;
        case WIDGET_SEARCH:
            // comboui renders its own search box; expose as plain text so it isn't a live control.
            return CW_TEXT;
        case WIDGET_CUSTOM:
        default:
            return CW_CUSTOM;
    }
}

// Policy for the combo-owned serializer (combo/menu/ComboMenuExport.h): OOT's enum mapping,
// options structs, and section naming. The walk itself lives in combo/.
struct SohExportPolicy {
    using Widget = WidgetInfo;
    static CwKind Kind(const Widget& w) {
        return WidgetTypeToCwKind(w.type);
    }
    static bool IsRandoSection(const std::string& label) {
        return label == "Randomizer";
    }
    static std::string Tooltip(const Widget& w) {
        return w.options ? w.options->tooltip : std::string();
    }
    static size_t CountChoices(const Widget& w) {
        // Only CW_COMBOBOX contributes CwChoice entries (from ComboboxOptions::comboMap).
        // Audio/Video backend emit zero choices here (their ComboboxOptions is empty at
        // export; populated by the game at runtime).
        if (WidgetTypeToCwKind(w.type) != CW_COMBOBOX || !w.options) {
            return 0;
        }
        auto o = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
        return o ? o->comboMap.size() : 0;
    }
    static void EmitChoices(const Widget& w, std::vector<CwChoice>& out) {
        if (WidgetTypeToCwKind(w.type) != CW_COMBOBOX || !w.options) {
            return;
        }
        auto o = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
        if (!o) {
            return;
        }
        for (auto& mp : o->comboMap) {
            CwChoice choice = {};
            choice.value = mp.first;
            choice.label = mp.second ? mp.second : "";
            out.push_back(choice);
        }
    }
    static void FillOptions(const Widget& w, CwWidget& cw) {
        if (!w.options) {
            return;
        }
        switch (cw.kind) {
            case CW_CHECKBOX: {
                if (auto o = std::static_pointer_cast<UIWidgets::CheckboxOptions>(w.options)) {
                    cw.bDefault = o->defaultValue ? 1 : 0;
                }
                break;
            }
            case CW_SLIDER_INT: {
                if (auto o = std::static_pointer_cast<UIWidgets::IntSliderOptions>(w.options)) {
                    cw.iMin = o->min;
                    cw.iMax = o->max;
                    cw.iStep = o->step;
                    cw.iDefault = o->defaultValue;
                }
                break;
            }
            case CW_SLIDER_FLOAT: {
                if (auto o = std::static_pointer_cast<UIWidgets::FloatSliderOptions>(w.options)) {
                    cw.fMin = o->min;
                    cw.fMax = o->max;
                    cw.fStep = o->step;
                    cw.fDefault = o->defaultValue;
                }
                break;
            }
            case CW_COLOR: {
                if (auto o = std::static_pointer_cast<UIWidgets::ColorPickerOptions>(w.options)) {
                    cw.useAlpha = o->useAlpha ? 1 : 0;
                }
                break;
            }
            case CW_COMBOBOX: {
                if (auto o = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options)) {
                    cw.iDefault = (int32_t)o->defaultIndex;
                }
                break;
            }
            case CW_BTN_SELECTOR: {
                if (auto o = std::static_pointer_cast<UIWidgets::BtnSelectorOptions>(w.options)) {
                    cw.iDefault = o->defaultValue;
                }
                break;
            }
            default:
                break;
        }
    }
};
} // namespace

const CwMenu* SohMenu::ExportComboMenu() {
    // Walk + flatten live in the combo-owned serializer; this member just supplies the
    // protected menu tree and OOT's policy.
    return ComboMenuExport::Build<SohExportPolicy>(mComboExport, menuOrder, menuEntries);
}

void SohMenu::InvokeCallbackByIndex(int32_t i) {
    if (i < 0 || i >= (int32_t)mComboExport.flat.size()) {
        return;
    }
    auto* w = mComboExport.flat[i];
    if (w && w->callback) {
        // Ensure InitElement ran (comboui never installs this menu) so a callback that touches
        // disabledMap/menu state doesn't fault. Idempotent.
        Init();
        w->callback(*w);
    }
}

int32_t SohMenu::EvalDisabledByIndex(int32_t i, const char** outReason) {
    if (i < 0 || i >= (int32_t)mComboExport.flat.size()) {
        return 0;
    }
    auto* w = mComboExport.flat[i];
    if (!w || !w->preFunc) {
        return 0;
    }
    // ComboShip: preFuncs probe OOT live runtime + read disabledMap (populated by the per-frame disable
    // pass in Menu::DrawElement). comboui bypasses DrawElement and a backgrounded OOT has no live state,
    // so only evaluate when OOT is alive (same guard as Menu::DrawElement); else report enabled (disable
    // state reflects live gameplay that doesn't exist while backgrounded).
    if (OTRGlobals::Instance == nullptr || OTRGlobals::Instance->fontStandardLargest == nullptr) {
        return 0;
    }
    // comboui owns the menu slot, so libultraship never installs/Init()s this menu — and OOT populates
    // disabledMap in InitElement(). Without Init(), a preFunc that reads disabledMap.at(KEY) throws
    // out_of_range (intermittent: only worked when a custom-draw happened to Init() first). Idempotent.
    Init();
    for (auto& [reason, info] : disabledMap) {
        info.active = info.evaluation(info);
    }
    if (w->options) {
        w->ResetDisables();
    }
    w->preFunc(*w);
    bool d = (w->options && w->options->disabled);
    if (d && outReason) {
        *outReason = w->options->disabledTooltip.c_str();
    }
    return d ? 1 : 0;
}

void SohMenu::DrawCustomByIndex(int32_t i) {
    if (i < 0 || i >= (int32_t)mComboExport.flat.size()) {
        return;
    }
    auto* w = mComboExport.flat[i];
    if (!w || !w->customFunction) {
        return;
    }
    // Custom widgets can depend on OOT live subsystems. comboui owns the menu and may render this tab
    // while OOT is backgrounded; those subsystems aren't initialized then. Only draw the real widget
    // when OOT is live (same guard as DrawElement/EvalDisabledByIndex); otherwise show a placeholder.
    // Declarative widgets + CVars still work while backgrounded.
    // Rando widgets must be editable even while OOT is backgrounded, so they are exempt.
    bool live = !(OTRGlobals::Instance == nullptr || OTRGlobals::Instance->fontStandardLargest == nullptr);
    bool isRando = (i >= 0 && i < (int32_t)mComboExport.flatRando.size() && mComboExport.flatRando[i]);
    if (!live && !isRando) {
        ImGui::TextDisabled("Available while Ocarina of Time is the active game.");
        return;
    }
    w->customFunction(*w);
}

int32_t SohMenu::DrawWidgetByIndex(int32_t i, int32_t width) {
    if (i < 0 || i >= (int32_t)mComboExport.flat.size()) {
        return 0;
    }
    auto* w = mComboExport.flat[i];
    if (!w) {
        return 0;
    }
    // Same dormant guard as DrawCustomByIndex: widgets that probe OOT's live runtime (preFunc /
    // customFunction) need the game alive; pure declarative/CVar widgets (all rando settings) are
    // always safe to draw + edit while backgrounded, so they are exempt.
    bool live = !(OTRGlobals::Instance == nullptr || OTRGlobals::Instance->fontStandardLargest == nullptr);
    bool isRando = (i < (int32_t)mComboExport.flatRando.size() && mComboExport.flatRando[i]);
    bool needsLive = (w->preFunc != nullptr) || (w->customFunction != nullptr);
    if (needsLive && !live && !isRando) {
        ImGui::TextDisabled("%s", w->name.c_str());
        return 0;
    }
    // Fresh disable pass only when this widget's preFunc consults disable state — MenuDrawItem reads
    // disabledMap solely inside its preFunc branch (mirrors Menu::DrawElement's top-of-frame pass).
    if (w->preFunc) {
        for (auto& [reason, info] : disabledMap) {
            info.active = info.evaluation(info);
        }
    }
    // SoH's Combobox does comboMap.at(value), which THROWS (and would unwind across the C-ABI
    // boundary and crash) if the CVar value isn't a current option key. comboui's search/flat render
    // skips the section update funcs that normally keep it in range — skip drawing when out of range.
    if (w->type == WIDGET_CVAR_COMBOBOX && w->cVar && w->cVar[0]) {
        auto opts = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w->options);
        if (!opts || opts->comboMap.find(CVarGetInteger(w->cVar, 0)) == opts->comboMap.end()) {
            return 0;
        }
    }
    // Change snapshot for the caller's cross-game audio mirror. Delegated kinds are int- or
    // float-backed CVars, so an int+float compare covers them without allocating.
    bool hasCvar = (w->cVar && w->cVar[0]);
    int32_t beforeI = hasCvar ? CVarGetInteger(w->cVar, 0) : 0;
    float beforeF = hasCvar ? CVarGetFloat(w->cVar, 0.0f) : 0.0f;
    MenuDrawItem(*w, (uint32_t)(width > 0 ? width : 90), GetMenuThemeColor());
    if (!hasCvar) {
        return 0;
    }
    return (CVarGetInteger(w->cVar, 0) != beforeI || CVarGetFloat(w->cVar, 0.0f) != beforeF) ? 1 : 0;
}
} // namespace SohGui
