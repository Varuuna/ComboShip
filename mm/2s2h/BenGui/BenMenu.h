#ifndef BENMENU_H
#define BENMENU_H

#include "UIWidgets.hpp"
#include "Menu.h"
#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/DeveloperTools/DeveloperTools.h"
#include <fast/backends/gfx_rendering_api.h>
#include "ComboMenuABI.h"
#include <deque>

namespace BenGui {

class BenMenu : public Ship::Menu {
  public:
    BenMenu(const std::string& consoleVariable, const std::string& name);

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
    void Draw() override;

    void AddSidebarEntry(std::string sectionName, std::string sidbarName, uint32_t columnCount);
    WidgetInfo& AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType);
    void AddMenuElements();
    void AddSettings();
    void AddEnhancements();
    void AddDevTools();

    // === ComboShip C-ABI menu export (see combo/menu/ComboMenuABI.h) ===
    // Builds (once, cached) the flat CwMenu describing the whole BenMenu tree and
    // returns a pointer that is stable for the lifetime of this BenMenu instance.
    // comboui ingests the CwMenu and invokes back by index. EXACT MM analog of
    // SohMenu's exports.
    const CwMenu* ExportComboMenu();
    void InvokeCallbackByIndex(int32_t i);                          // runs widget i's .callback(*w)
    int32_t EvalDisabledByIndex(int32_t i, const char** outReason); // runs widget i's preFunc; 1 if disabled
    void DrawCustomByIndex(int32_t i);                              // runs widget i's customFunction(*w)

  private:
    bool mMenuElementsInitialized = false;

    // ComboShip menu-export backing storage. All of this lives as long as the BenMenu
    // instance, so the const char* / array pointers handed across the C-ABI stay valid
    // for the process lifetime (comboui caches the returned CwMenu*).
    bool mExported = false;
    CwMenu mMenu = {};
    std::vector<WidgetInfo*> mFlat;     // index -> source widget (the invoke key)
    std::vector<uint8_t> mFlatRando;    // parallel to mFlat: 1 if widget is in the "Rando" section
    std::vector<CwSection> mSections;   // reserved up-front; .data() stable after fill
    std::vector<CwSidebar> mSidebars;
    std::vector<CwWidget> mWidgets;
    std::vector<CwChoice> mChoices;
    std::deque<std::string> mOwnedStrings; // deque: never relocates existing elements
};
} // namespace BenGui

#endif // BENMENU_H
