#pragma once

#include "Menu.h"
#include <fast/backends/gfx_rendering_api.h>
#include "soh/cvar_prefixes.h"
#include "ComboMenuExport.h"

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
void MarkRandomizerMenusDirty();

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

    // === ComboShip C-ABI menu export (see combo/menu/ComboMenuExport.h) ===
    // Builds (once, cached) the flat CwMenu describing the whole SohMenu tree and
    // returns a pointer that is stable for the lifetime of this SohMenu instance.
    // comboui ingests the CwMenu and invokes back by index.
    const CwMenu* ExportComboMenu();
    void InvokeCallbackByIndex(int32_t i);                          // runs widget i's .callback(*w)
    int32_t EvalDisabledByIndex(int32_t i, const char** outReason); // runs widget i's preFunc; 1 if disabled
    void DrawCustomByIndex(int32_t i);                              // runs widget i's customFunction(*w)
    int32_t DrawWidgetByIndex(int32_t i, int32_t width); // draws widget i via real MenuDrawItem; 1 if changed

  private:
    char mGitCommitHashTruncated[8];
    bool mIsTaggedVersion;
    bool mMenuElementsInitialized = false;

    // ComboShip menu-export backing storage (combo-owned serializer; see ComboMenuExport.h).
    // Lives as long as this SohMenu instance so C-ABI pointers stay valid for process life.
    ComboMenuExport::State<WidgetInfo> mComboExport;
};
} // namespace SohGui
