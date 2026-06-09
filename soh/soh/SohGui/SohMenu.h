#ifndef SOHMENU_H
#define SOHMENU_H

#include <libultraship/libultraship.h>
#include "Menu.h"
#include <fast/backends/gfx_rendering_api.h>
#include "soh/cvar_prefixes.h"
#include "ComboMenuABI.h"
#include <deque>

extern "C" {
#include "z64.h"
}

#ifdef __cplusplus
extern "C" {
#endif
void enableBetaQuest();
void disableBetaQuest();
#ifdef __cplusplus
}
#endif

namespace SohGui {
static std::map<int32_t, const char*> languages = {
    { LANGUAGE_ENG, "English" },
    { LANGUAGE_GER, "German" },
    { LANGUAGE_FRA, "French" },
    { LANGUAGE_JPN, "Japanese" },
};
void UpdateMenuTricks();
void UpdateMenuLocations();

class SohMenu : public Ship::Menu {
  public:
    SohMenu(const std::string& consoleVariable, const std::string& name);

    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
    void Draw() override;

    void AddSidebarEntry(std::string sectionName, std::string sidbarName, uint32_t columnCount);
    WidgetInfo& AddWidget(WidgetPath& pathInfo, std::string widgetName, WidgetType widgetType);
    void AddMenuElements();
    void AddMenuSettings();
    void AddMenuEnhancements();
    void AddMenuDevTools();
    void AddMenuRandomizer();
    void AddMenuNetwork();
    static void UpdateLanguageMap(std::map<int32_t, const char*>& languageMap);

    // === ComboShip C-ABI menu export (see combo/menu/ComboMenuABI.h) ===
    // Builds (once, cached) the flat CwMenu describing the whole SohMenu tree and
    // returns a pointer that is stable for the lifetime of this SohMenu instance.
    // comboui ingests the CwMenu and invokes back by index.
    const CwMenu* ExportComboMenu();
    void InvokeCallbackByIndex(int32_t i);                       // runs widget i's .callback(*w)
    int32_t EvalDisabledByIndex(int32_t i, const char** outReason); // runs widget i's preFunc; 1 if disabled
    void DrawCustomByIndex(int32_t i);                           // runs widget i's customFunction(*w)

  private:
    char mGitCommitHashTruncated[8];
    bool mIsTaggedVersion;
    bool mMenuElementsInitialized = false;

    // ComboShip menu-export backing storage. All of this lives as long as the SohMenu
    // instance, so the const char* / array pointers handed across the C-ABI stay valid
    // for the process lifetime (comboui caches the returned CwMenu*).
    bool mExported = false;
    CwMenu mMenu = {};
    std::vector<WidgetInfo*> mFlat;     // index -> source widget (the invoke key)
    std::vector<uint8_t> mFlatRando;    // parallel to mFlat: 1 if widget is in the "Randomizer" section
    std::vector<CwSection> mSections;   // reserved up-front; .data() stable after fill
    std::vector<CwSidebar> mSidebars;
    std::vector<CwWidget> mWidgets;
    std::vector<CwChoice> mChoices;
    std::deque<std::string> mOwnedStrings; // deque: never relocates existing elements
};
} // namespace SohGui

#endif // SOHMENU_H
