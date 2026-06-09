#include "SohMenu.h"
#include <ship/window/gui/GuiMenuBar.h>
#include <ship/window/gui/GuiElement.h>
#include <ship/utils/StringHelper.h>
#include <spdlog/fmt/fmt.h>
#include "soh/OTRGlobals.h" // ComboShip: EvalDisabledByIndex foreground guard mirrors Menu::DrawElement

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
        while (sidebar.at(pathInfo.sidebarName).columnWidgets.size() < column + 1) {
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
               return !Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync();
           },
            "Disabling VSync not supported" } },
        { DISABLE_FOR_NO_WINDOWED_FULLSCREEN,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetInstance()->GetWindow()->SupportsWindowedFullscreen();
           },
            "Windowed Fullscreen not supported" } },
        { DISABLE_FOR_NO_MULTI_VIEWPORT,
          { [](disabledInfo& info) -> bool {
               return !Ship::Context::GetInstance()->GetWindow()->GetGui()->SupportsViewports();
           },
            "Multi-viewports not supported" } },
        { DISABLE_FOR_NOT_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() !=
                      Fast::WindowBackend::FAST3D_DXGI_DX11;
           },
            "Available Only on DirectX" } },
        { DISABLE_FOR_DIRECTX,
          { [](disabledInfo& info) -> bool {
               return Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
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
} // namespace

const CwMenu* SohMenu::ExportComboMenu() {
    if (mExported) {
        return &mMenu;
    }

    // Deterministic walk order, matching the live render walk in Menu.cpp:
    //   sections   -> menuOrder (fallback: sorted menuEntries keys)
    //   sidebars   -> MainMenuEntry.sidebarOrder (fallback: sorted sidebar keys)
    //   widgets    -> SidebarEntry.columnWidgets in column order, then vector order
    // The position a widget gets in mFlat is its stable CwWidget.index.

    // Build the section ordering.
    std::vector<std::string> sectionKeys = menuOrder;
    if (sectionKeys.empty()) {
        for (auto& kv : menuEntries) {
            sectionKeys.push_back(kv.first);
        }
        std::sort(sectionKeys.begin(), sectionKeys.end());
    }

    // ---- Pass 1: count everything so we can reserve and never reallocate. ----
    size_t sectionCount = 0;
    size_t sidebarCount = 0;
    size_t widgetCount = 0;
    size_t choiceCount = 0;

    for (auto& secKey : sectionKeys) {
        auto it = menuEntries.find(secKey);
        if (it == menuEntries.end()) {
            continue;
        }
        MainMenuEntry& entry = it->second;
        sectionCount++;

        std::vector<std::string> sidebarKeys = entry.sidebarOrder;
        if (sidebarKeys.empty()) {
            for (auto& kv : entry.sidebars) {
                sidebarKeys.push_back(kv.first);
            }
            std::sort(sidebarKeys.begin(), sidebarKeys.end());
        }
        for (auto& sbKey : sidebarKeys) {
            auto sbIt = entry.sidebars.find(sbKey);
            if (sbIt == entry.sidebars.end()) {
                continue;
            }
            sidebarCount++;
            for (auto& column : sbIt->second.columnWidgets) {
                for (auto& w : column) {
                    widgetCount++;
                    // Only CW_COMBOBOX contributes CwChoice entries (from ComboboxOptions::comboMap).
                    // Pass-2 fill uses the exact same kind==CW_COMBOBOX rule, so reserve == fill and
                    // mChoices never reallocates. Audio/Video backend emit zero choices here (their
                    // ComboboxOptions is empty at export; populated by the game at runtime).
                    if (WidgetTypeToCwKind(w.type) == CW_COMBOBOX && w.options) {
                        if (auto combo = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options)) {
                            choiceCount += combo->comboMap.size();
                        }
                    }
                }
            }
        }
    }

    // Reserve to final sizes. After this, .data() and element addresses are stable
    // because we never push beyond the reserved capacity.
    mSections.reserve(sectionCount);
    mSidebars.reserve(sidebarCount);
    mWidgets.reserve(widgetCount);
    mChoices.reserve(choiceCount);
    mFlat.reserve(widgetCount);

    // ---- Pass 2: fill. ----
    // We populate the flat mWidgets/mChoices fully (each CwWidget references mChoices.data()
    // by absolute offset, captured as a span start index + count, wired after fill). Then we
    // build mSidebars referencing mWidgets ranges, then mSections referencing mSidebars ranges.

    auto ownStr = [this](const std::string& s) -> const char* {
        mOwnedStrings.push_back(s);
        return mOwnedStrings.back().c_str();
    };

    struct SidebarRange {
        const char* name;
        uint32_t columnCount;
        size_t widgetStart;
        size_t widgetEnd;
    };
    struct SectionRange {
        const char* label;
        const char* sidebarCvar;
        size_t sidebarStart;
        size_t sidebarEnd;
    };
    std::vector<SidebarRange> sidebarRanges;
    sidebarRanges.reserve(sidebarCount);
    std::vector<SectionRange> sectionRanges;
    sectionRanges.reserve(sectionCount);

    // Track choice offsets per widget so we can wire choices->data() after mChoices is full.
    std::vector<std::pair<size_t, size_t>> widgetChoiceRange; // (start, count) into mChoices
    widgetChoiceRange.reserve(widgetCount);

    for (auto& secKey : sectionKeys) {
        auto it = menuEntries.find(secKey);
        if (it == menuEntries.end()) {
            continue;
        }
        MainMenuEntry& entry = it->second;

        std::vector<std::string> sidebarKeys = entry.sidebarOrder;
        if (sidebarKeys.empty()) {
            for (auto& kv : entry.sidebars) {
                sidebarKeys.push_back(kv.first);
            }
            std::sort(sidebarKeys.begin(), sidebarKeys.end());
        }

        size_t sectionSidebarStart = sidebarRanges.size();

        for (auto& sbKey : sidebarKeys) {
            auto sbIt = entry.sidebars.find(sbKey);
            if (sbIt == entry.sidebars.end()) {
                continue;
            }
            SidebarEntry& sb = sbIt->second;
            size_t sidebarWidgetStart = mWidgets.size();

            for (auto& column : sb.columnWidgets) {
                for (auto& w : column) {
                    int32_t index = (int32_t)mFlat.size();
                    mFlat.push_back(&w);

                    CwWidget cw = {};
                    cw.index = index;
                    cw.kind = WidgetTypeToCwKind(w.type);
                    cw.name = ownStr(w.name);
                    cw.cvar = w.cVar ? w.cVar : "";
                    cw.tooltip = (w.options ? ownStr(w.options->tooltip) : "");
                    cw.windowName = w.windowName ? w.windowName : "";
                    cw.hasCallback = (w.callback != nullptr) ? 1 : 0;
                    cw.hasPreFunc = (w.preFunc != nullptr) ? 1 : 0;
                    cw.gameLoopDependent = 0; // Phase 5 sets specific ones.
                    cw.sameLine = w.sameLine ? 1 : 0;
                    cw.hideInSearch = w.hideInSearch ? 1 : 0;

                    size_t choiceStart = 0;
                    size_t choiceCnt = 0;

                    if (w.options) {
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
                                    choiceStart = mChoices.size();
                                    for (auto& mp : o->comboMap) {
                                        CwChoice choice = {};
                                        choice.value = mp.first;
                                        choice.label = mp.second ? mp.second : "";
                                        mChoices.push_back(choice);
                                        choiceCnt++;
                                    }
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

                    cw.choices = nullptr; // wired after mChoices is fully populated
                    cw.choiceCount = (int32_t)choiceCnt;
                    widgetChoiceRange.emplace_back(choiceStart, choiceCnt);
                    mWidgets.push_back(cw);
                }
            }

            sidebarRanges.push_back(
                SidebarRange{ ownStr(sbKey), sb.columnCount, sidebarWidgetStart, mWidgets.size() });
        }

        sectionRanges.push_back(SectionRange{ ownStr(entry.label),
                                              entry.sidebarCvar ? entry.sidebarCvar : "", sectionSidebarStart,
                                              sidebarRanges.size() });
    }

    // Now that mChoices and mWidgets are fully populated (no further pushes), wire pointers.
    for (size_t i = 0; i < mWidgets.size(); i++) {
        auto& range = widgetChoiceRange[i];
        if (range.second > 0) {
            mWidgets[i].choices = mChoices.data() + range.first;
        }
    }

    // Build sidebars referencing mWidgets ranges.
    for (auto& sr : sidebarRanges) {
        CwSidebar sb = {};
        sb.sidebarName = sr.name;
        sb.columnCount = sr.columnCount;
        sb.widgetCount = (int32_t)(sr.widgetEnd - sr.widgetStart);
        sb.widgets = (sb.widgetCount > 0) ? (mWidgets.data() + sr.widgetStart) : nullptr;
        mSidebars.push_back(sb);
    }

    // Build sections referencing mSidebars ranges.
    for (auto& secR : sectionRanges) {
        CwSection sec = {};
        sec.sectionLabel = secR.label;
        sec.sidebarCvar = secR.sidebarCvar;
        sec.sidebarCount = (int32_t)(secR.sidebarEnd - secR.sidebarStart);
        sec.sidebars = (sec.sidebarCount > 0) ? (mSidebars.data() + secR.sidebarStart) : nullptr;
        mSections.push_back(sec);
    }

    mMenu.version = 1;
    mMenu.sectionCount = (int32_t)mSections.size();
    mMenu.sections = mSections.empty() ? nullptr : mSections.data();

    mExported = true;
    return &mMenu;
}

void SohMenu::InvokeCallbackByIndex(int32_t i) {
    if (i < 0 || i >= (int32_t)mFlat.size()) {
        return;
    }
    auto* w = mFlat[i];
    if (w && w->callback) {
        w->callback(*w);
    }
}

int32_t SohMenu::EvalDisabledByIndex(int32_t i, const char** outReason) {
    if (i < 0 || i >= (int32_t)mFlat.size()) {
        return 0;
    }
    auto* w = mFlat[i];
    if (!w || !w->preFunc) {
        return 0;
    }
    // ComboShip: preFuncs probe OOT live runtime + read disabledMap[*].active/.value (populated by the
    // per-frame disable pass Menu::DrawElement runs). comboui bypasses DrawElement and a backgrounded OOT
    // has no live state, so only evaluate when OOT is alive (same guard as Menu::DrawElement); else report
    // enabled (CVars still apply on resume; disable-state reflects live gameplay that doesn't exist while bg).
    if (OTRGlobals::Instance == nullptr || OTRGlobals::Instance->fontStandardLargest == nullptr) {
        return 0;
    }
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
    if (i < 0 || i >= (int32_t)mFlat.size()) {
        return;
    }
    auto* w = mFlat[i];
    if (w && w->customFunction) {
        w->customFunction(*w);
    }
}
} // namespace SohGui
