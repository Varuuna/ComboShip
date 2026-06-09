# ComboShip Unified Settings Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A combo-owned `comboui.dll` that permanently owns the single ImGui menu, presenting `Shared | OOT | MM | Combo` scope tabs that surface all of each game's settings (engine settings drawn by combo once; each game's specific settings drawn by that game; cross-world Generate in Combo).

**Architecture:** A new combo-owned DLL links libultraship + ImGui and registers one `Ship::GuiWindow` as the menu via `gui->SetMenu`. It draws the scope-tab strip, draws the Shared (engine) and Combo (Generate) panels itself, and delegates the OOT/MM panels to thin per-game `*_DrawSettings(skipCsv)` exports so each game renders its own (ABI-local) widgets. The games stop installing their own menus under `COMBO_BUILD`.

**Tech Stack:** C++20, libultraship (`Ship::Gui`/`GuiWindow`), ImGui (linked transitively via libultraship), nlohmann::json, the shared CVar store + shared CRT across DLLs, Windows DLL C-ABI exports, multi-config VS CMake generator in `build/x64`.

**Spec:** `docs/superpowers/specs/2026-06-09-combo-unified-settings-menu-design.md`

---

## Testing Note (read before starting)

No unit-test harness exists for this GUI/cross-DLL/game-runtime code (project norm: manual verification). Strict TDD does not apply; each task's verification is **build + observe** with explicit expected output. Build succeeding + stated observable = green.

**Build commands** (repo root `E:\Git\ComboShip\Combo`, PowerShell). Build order matters; **never build `soh` and `2ship` in parallel** (they race on shared `OTRExporter.lib`/`ZAPD`):
- libultraship: `scripts/build-libultraship.ps1` (only if libultraship changed)
- soh: `scripts/build-soh.ps1` — or `cmake --build build/x64 --target soh --config Debug`
- 2ship: `scripts/build-2ship.ps1`
- comboui: **new** `scripts/build-comboui.ps1` (created in Task 1) — or `cmake --build build/x64 --target comboui --config Debug`
- exe: `scripts/build-comboship.ps1`

Runtime artifacts land in `build/x64/<sub>/Debug/` and are mirrored to `x64/Debug/` + next to the exe by combo POST_BUILD. Run `build/x64/combo/Debug/ComboShip.exe`.

**Commit after each task** (project preference). End commit messages with the Co-Authored-By trailer.

**Reconfigure after CMake changes:** adding the `comboui` target requires re-running CMake configure once: `cmake --preset <existing-preset>` or `cmake build/x64` (use the project's existing configure invocation). Tasks that touch CMake note this.

---

## File Structure

**Created (combo-owned):**
- `combo/gui/ComboMenu.h` / `.cpp` — `ComboRando::ComboMenu : Ship::GuiWindow`. Scope-tab strip; draws Shared + Combo panels; delegates OOT/MM. Hosts the `ComboUI_Register` export.
- `combo/gui/ComboSharedPanel.cpp` — the combo-drawn engine-settings panel (graphics/backends/interpolation/UI), split out so `ComboMenu.cpp` stays focused.
- `combo/gui/ComboGenProgress.h` — `struct ComboGenProgress` (atomics) shared by comboui (panel) and the exe (worker).
- `combo/CMakeLists.txt` — add the `comboui` SHARED target (alongside the existing `ComboShip` exe target).
- `scripts/build-comboui.ps1` — per-target build script mirroring the others.

**Modified (vendored, thin, `COMBO_BUILD`-guarded — each documented in `docs/UPSTREAM_MERGES.md` + `// ComboShip:` comments):**
- `CMakeLists.txt` (root) — `add_subdirectory` ordering already includes `combo`; no change if `comboui` is declared inside `combo/CMakeLists.txt`.
- `soh/soh/SohGui/Menu.h` / `Menu.cpp` — add `DrawContent(const std::set<std::string>& skipPaths)` (content-only, header/sidebar-path skip). Reuses existing per-header/sidebar/widget render.
- `soh/soh/SohGui/SohGui.cpp` / `.hpp` — guard out `gui->SetMenu(mSohMenu)` under `COMBO_BUILD` (keep building `mSohMenu`); add `SOH_DrawSettings` export wiring to `GetSohMenu()`.
- `soh/soh/OTRGlobals.cpp` — guard out the `SetMenu(GetSohMenu())` restore in `SOH_ReinitForResume`; add `SOH_DrawSettings` export.
- `mm/2s2h/BenGui/Menu.h` / `Menu.cpp` — same `DrawContent` addition.
- `mm/2s2h/BenGui/BenGui.cpp` / `.hpp` — guard out `gui->SetMenu`/`ActivateMenu` under `COMBO_BUILD` (keep building `mBenMenu`); add `GetBenMenu()` accessor.
- `mm/2s2h/BenPort.cpp` — guard out the `BenGui::ActivateMenu()` call in `MM_ResumeGame`; add `MM_DrawSettings` export.
- `combo/ComboShip.cpp` — `LoadDll("comboui.dll")` + `ComboUI_Register()` after `SOH_Init()`; resolve + wire the generate-request handler (carry Inc7); decouple save-time generate.
- `soh/src/code/z_sram.c` — keep forcing `QUEST_RANDOMIZER`, drop the save-time fill call (carry Inc7).

---

## Phase 0 — comboui.dll skeleton + menu ownership

### Task 1: Create the `comboui` DLL target with an empty `ComboMenu`

**Files:**
- Create: `combo/gui/ComboMenu.h`
- Create: `combo/gui/ComboMenu.cpp`
- Modify: `combo/CMakeLists.txt`
- Create: `scripts/build-comboui.ps1`

- [ ] **Step 1: Write `ComboMenu.h`**

```cpp
// combo/gui/ComboMenu.h
// ComboShip: combo-owned unified settings menu, hosted in comboui.dll.
#pragma once

#include <libultraship/libultraship.h>
#include <string>

namespace ComboRando {

class ComboMenu final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    ~ComboMenu() override {}

  protected:
    void InitElement() override {}
    void DrawElement() override;       // draws the scope-tab strip + active panel
    void UpdateElement() override {}

  private:
    void DrawSharedPanel();            // ComboSharedPanel.cpp
    void DrawGamePanel(const char* gameKey); // delegates to SOH_/MM_DrawSettings
    void DrawComboPanel();             // Generate (added in Phase 3)
};

} // namespace ComboRando

extern "C" __declspec(dllexport) void ComboUI_Register(void);
```

- [ ] **Step 2: Write `ComboMenu.cpp` (skeleton: tabs + placeholder content)**

```cpp
// combo/gui/ComboMenu.cpp
#include "ComboMenu.h"
#include <imgui.h>
#include <memory>

namespace ComboRando {

static std::shared_ptr<ComboMenu> sComboMenu;

void ComboMenu::DrawElement() {
    if (ImGui::BeginTabBar("ComboScopeTabs")) {
        if (ImGui::BeginTabItem("Shared")) { DrawSharedPanel(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("OOT"))    { DrawGamePanel("oot"); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("MM"))     { DrawGamePanel("mm");  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Combo"))  { DrawComboPanel(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

// Placeholder bodies until later tasks.
void ComboMenu::DrawSharedPanel() { ImGui::TextUnformatted("Shared engine settings (todo Phase 2)"); }
void ComboMenu::DrawGamePanel(const char* gameKey) { ImGui::Text("%s settings (todo Phase 1)", gameKey); }
void ComboMenu::DrawComboPanel() { ImGui::TextUnformatted("Cross-world Generate (todo Phase 3)"); }

} // namespace ComboRando

extern "C" __declspec(dllexport) void ComboUI_Register(void) {
    auto ctx = Ship::Context::GetInstance();
    if (!ctx || !ctx->GetWindow() || !ctx->GetWindow()->GetGui()) {
        return; // GUI not ready; exe will not have called us yet in that case
    }
    auto gui = ctx->GetWindow()->GetGui();
    // Match the existing menu-visibility CVar so the in-game menu hotkey toggles us.
    ComboRando::sComboMenu = std::make_shared<ComboRando::ComboMenu>("gOpenWindows.Menu", "Combo Menu");
    gui->SetMenu(ComboRando::sComboMenu);
}
```

- [ ] **Step 3: Add the `comboui` target to `combo/CMakeLists.txt`**

Insert before the `ComboShip` executable block (after `project(...)`):

```cmake
# ─── comboui.dll: combo-owned unified settings menu (links libultraship + ImGui) ───
set(COMBOUI_SOURCES
    gui/ComboMenu.cpp
)
add_library(comboui SHARED ${COMBOUI_SOURCES})
set_target_properties(comboui PROPERTIES PREFIX "")          # output "comboui.dll", not "libcomboui.dll"
target_compile_definitions(comboui PRIVATE COMBO_BUILD)
target_include_directories(comboui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}                               # so "gui/..." and "rando/..." resolve
    ${CMAKE_CURRENT_SOURCE_DIR}/../libultraship/include
)
find_package(nlohmann_json REQUIRED)
target_link_libraries(comboui PRIVATE libultraship nlohmann_json::nlohmann_json)
add_dependencies(comboui libultraship)
```

Add `comboui` to the exe's dependencies and copy it. In the existing `add_dependencies(ComboShip ...)`:
```cmake
add_dependencies(ComboShip
    soh
    2ship
    libultraship
    comboui
)
```
In the POST_BUILD block, after the `libultraship.dll` copies, add:
```cmake
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:comboui>
            ${CMAKE_SOURCE_DIR}/x64/Debug/comboui.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:comboui>
            $<TARGET_FILE_DIR:ComboShip>
```

- [ ] **Step 4: Write `scripts/build-comboui.ps1`**

Mirror an existing per-target script. Minimal version:
```powershell
# scripts/build-comboui.ps1 — build only the comboui target
param([switch]$Release)
$config = if ($Release) { "Release" } else { "Debug" }
cmake --build build/x64 --target comboui --config $config
```

- [ ] **Step 5: Reconfigure CMake + build comboui**

Run (use the project's existing configure command if different):
```
cmake build/x64
scripts/build-comboui.ps1
```
Expected: configure picks up the new `comboui` target; build produces `build/x64/combo/Debug/comboui.dll` (+ `comboui.lib`). Confirm the export:
```
dumpbin /exports build/x64/combo/Debug/comboui.dll | Select-String "ComboUI_Register"
```
Expected: `ComboUI_Register` listed.

- [ ] **Step 6: Commit**

```
git add combo/gui/ComboMenu.h combo/gui/ComboMenu.cpp combo/CMakeLists.txt scripts/build-comboui.ps1
git commit -m "Inc7/menu: add comboui.dll skeleton with empty scope-tab ComboMenu"
```

---

### Task 2: Load comboui from the exe and take over the menu

**Files:**
- Modify: `combo/ComboShip.cpp` (DLL load block ~lines 301-382; boot sequence ~lines 438-467; cleanup ~lines 567-575)

- [ ] **Step 1: Add the comboui module handle + register-fn pointer**

With the other module/fn-pointer statics (~lines 62-90), add:
```cpp
typedef void (*FnComboUIRegister)(void);
static DllHandle           comboUIModule    = nullptr;
static FnComboUIRegister   ComboUI_Register = nullptr;
```

- [ ] **Step 2: Load comboui + register, right after `SOH_Init()` completes**

After the `std::cout << "[ComboShip] OOT initialized." << std::endl;` line (~447) and BEFORE the eager-MM-boot block:
```cpp
    // ComboShip: load the combo-owned menu DLL and install the unified menu now that
    // OOT has created the shared Gui. comboui owns the menu for the whole process.
    comboUIModule = LoadDll("comboui.dll");
    if (comboUIModule) {
        ComboUI_Register = (FnComboUIRegister)GetSym(comboUIModule, "ComboUI_Register");
        if (ComboUI_Register) {
            ComboUI_Register();
            std::cout << "[ComboShip] comboui registered (unified menu installed)." << std::endl;
        } else {
            std::cerr << "[ComboShip] WARNING: comboui.dll missing ComboUI_Register" << std::endl;
        }
    } else {
        std::cerr << "[ComboShip] WARNING: failed to load comboui.dll (" << DllError() << ")" << std::endl;
    }
```

- [ ] **Step 3: Free comboui on cleanup**

In the cleanup block (~lines 567-575), after `FreeDll(sohModule);`:
```cpp
    if (comboUIModule) FreeDll(comboUIModule);
```

- [ ] **Step 4: Suppress SoH's own SetMenu under COMBO_BUILD**

In `soh/soh/SohGui/SohGui.cpp` `SetupMenu()` (~lines 103-111), keep the `make_shared<SohMenu>` but guard the install:
```cpp
void SetupMenu() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    mSohMenu = std::make_shared<SohMenu>(CVAR_WINDOW("Menu"), "Port Menu");
#ifndef COMBO_BUILD
    gui->SetMenu(mSohMenu); // ComboShip: comboui owns the menu; SoH only builds its tree.
#endif
    // ... rest unchanged ...
}
```
In `soh/soh/OTRGlobals.cpp` `SOH_ReinitForResume` (the `ctx->GetWindow()->GetGui()->SetMenu(SohGui::GetSohMenu());` line, ~2536):
```cpp
#ifndef COMBO_BUILD
    ctx->GetWindow()->GetGui()->SetMenu(SohGui::GetSohMenu());
#endif
    // ComboShip: comboui's menu stays installed across transitions; do not restore SohMenu.
```
(Note: this is inside an existing `#ifdef COMBO_BUILD` function. Use `#ifndef COMBO_BUILD` around just the SetMenu line so the combo build skips it.)

- [ ] **Step 5: Suppress MM's own SetMenu/ActivateMenu under COMBO_BUILD**

In `mm/2s2h/BenGui/BenGui.cpp` `SetupMenu()` (~lines 77-90), keep the `make_shared<BenMenu>` but guard the install:
```cpp
void SetupMenu() {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    mBenMenu = std::make_shared<BenMenu>("gWindows.Menu", "Settings Menu");
#ifndef COMBO_BUILD
    gui->SetMenu(mBenMenu); // ComboShip: comboui owns the menu; MM only builds its tree.
#endif
    // ... rest unchanged ...
}
```
In `BenGui::ActivateMenu()` (~lines 95-102), guard the whole install so it no-ops under combo:
```cpp
void ActivateMenu() {
#ifndef COMBO_BUILD
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (mBenMenu == nullptr) { SetupMenu(); } else { gui->SetMenu(mBenMenu); }
#endif
}
```
In `mm/2s2h/BenPort.cpp` `MM_ResumeGame` (the `BenGui::ActivateMenu();` call ~2574): leave the call (it now no-ops under COMBO_BUILD via the guard above), but add a comment:
```cpp
    BenGui::ActivateMenu(); // ComboShip: no-op under COMBO_BUILD (comboui owns the menu)
```

- [ ] **Step 6: Build soh, 2ship, comboui, ComboShip**

Run (serialized soh then 2ship):
```
scripts/build-soh.ps1
scripts/build-2ship.ps1
scripts/build-comboui.ps1
scripts/build-comboship.ps1
```
Expected: all clean.

- [ ] **Step 7: Manual verification (menu takeover)**

Run the exe. Open the menu with the usual hotkey. Expected:
- The "Combo Menu" appears with four tabs (Shared | OOT | MM | Combo), each showing placeholder text.
- Neither game's own port menu appears.
- The menu still opens after triggering an OOT↔MM transition (mask shop → MM, clock tower → OOT). **Checkpoint:** if the hotkey stops toggling the menu after a transition, note it — MM's input toggles `gWindows.Menu` while SoH toggles `gOpenWindows.Menu`; comboui currently uses `gOpenWindows.Menu`. If MM can't toggle it, Task 9 (risks) covers binding both.

- [ ] **Step 8: Commit**

```
git add combo/ComboShip.cpp soh/soh/SohGui/SohGui.cpp soh/soh/OTRGlobals.cpp mm/2s2h/BenGui/BenGui.cpp mm/2s2h/BenPort.cpp
git commit -m "Inc7/menu: comboui takes over the menu; suppress games' SetMenu under COMBO_BUILD"
```

---

## Phase 1 — Delegate OOT/MM tabs to each game

### Task 3: Add `DrawContent(skipPaths)` to each game's `Ship::Menu`

**Files:**
- Modify: `soh/soh/SohGui/Menu.h` (class decl ~lines 10-67), `soh/soh/SohGui/Menu.cpp` (`DrawElement` ~lines 570-950)
- Modify: `mm/2s2h/BenGui/Menu.h`, `mm/2s2h/BenGui/Menu.cpp` (`DrawElement` ~lines 538-897)

`DrawContent` renders the full menu content (header row + sidebar + widgets) **content-only** (no `ImGui::Begin`/`End`), skipping any header or `"Header/Sidebar"` path in `skipPaths`. It reuses the same per-header/sidebar/widget drawing the existing `DrawElement` already does — factor the header-loop body so both call it.

- [ ] **Step 1 (SoH): declare `DrawContent` in `Menu.h`**

In `class Menu` (public section, `soh/soh/SohGui/Menu.h`):
```cpp
    // ComboShip: draw the menu content (header row + active sidebar + widgets) WITHOUT opening
    // a window, skipping any header name or "Header/Sidebar" path present in skipPaths.
    // Used by SOH_DrawSettings so comboui can embed OOT's settings under its OOT tab.
    void DrawContent(const std::set<std::string>& skipPaths);
```
Add `#include <set>` and `#include <string>` to `Menu.h` if not present.

- [ ] **Step 2 (SoH): implement `DrawContent` by refactoring `DrawElement`**

In `soh/soh/SohGui/Menu.cpp`: the existing `DrawElement` draws the header row from `menuOrder`, tracks the active header via `CVAR_SETTING("Menu.ActiveHeader")`, then draws the active header's sidebar + widget columns. Extract the body that "draws the header row and the active header's content" into `DrawContent`, honoring `skipPaths`:

```cpp
void Menu::DrawContent(const std::set<std::string>& skipPaths) {
    const char* activeHeaderCvar = CVAR_SETTING("Menu.ActiveHeader");
    std::string activeHeader = CVarGetString(activeHeaderCvar, "");

    // 1. Header row — skip any header in skipPaths; ensure activeHeader is a non-skipped, existing header.
    auto isSkipped = [&](const std::string& path) { return skipPaths.count(path) > 0; };
    std::vector<std::string> headers;
    for (auto& label : menuOrder) {
        if (!menuEntries.count(label)) continue;
        if (isSkipped(label)) continue;
        headers.push_back(label);
    }
    if (headers.empty()) { ImGui::TextUnformatted("No settings."); return; }
    if (std::find(headers.begin(), headers.end(), activeHeader) == headers.end()) {
        activeHeader = headers.front();
        CVarSetString(activeHeaderCvar, activeHeader.c_str());
    }
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        bool sel = (headers[i] == activeHeader);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(headers[i].c_str())) {
            activeHeader = headers[i];
            CVarSetString(activeHeaderCvar, activeHeader.c_str());
        }
        if (sel) ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // 2. Active header: sidebar (skip "Header/Sidebar" paths) + widget columns.
    MainMenuEntry& entry = menuEntries.at(activeHeader);
    std::string sidebarCvar = entry.sidebarCvar ? entry.sidebarCvar : "";
    std::string activeSidebar = sidebarCvar.empty() ? "" : CVarGetString(sidebarCvar.c_str(), "");
    std::vector<std::string> sidebars;
    for (auto& s : entry.sidebarOrder) {
        if (isSkipped(activeHeader + "/" + s)) continue;
        sidebars.push_back(s);
    }
    if (sidebars.empty()) { ImGui::TextUnformatted("No settings in this section."); return; }
    if (std::find(sidebars.begin(), sidebars.end(), activeSidebar) == sidebars.end()) {
        activeSidebar = sidebars.front();
        if (!sidebarCvar.empty()) CVarSetString(sidebarCvar.c_str(), activeSidebar.c_str());
    }
    ImGui::BeginChild("sohSidebar", ImVec2(180, 0), true);
    for (auto& s : sidebars) {
        if (ImGui::Selectable(s.c_str(), s == activeSidebar)) {
            activeSidebar = s;
            if (!sidebarCvar.empty()) CVarSetString(sidebarCvar.c_str(), s.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("sohWidgets", ImVec2(0, 0), true);
    SidebarEntry& sb = entry.sidebars.at(activeSidebar);
    for (size_t col = 0; col < sb.columnWidgets.size(); ++col) {
        for (auto& w : sb.columnWidgets.at(col)) {
            MenuDrawItem(w, 90 / std::max<uint32_t>(sb.columnCount, 1), CVarGetInteger(CVAR_SETTING("Menu.Theme"), 0));
        }
    }
    ImGui::EndChild();

    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}
```

Notes for the implementer:
- `MenuDrawItem` is the existing per-widget draw method used by `DrawElement` (soh `Menu.cpp` ~line 920). Match its real signature (theme index arg). If it is `private`, this method is a member so it has access.
- This intentionally uses a simpler sidebar/column layout than the full themed `DrawElement` (which adds search, themed buttons, `ModernMenuSidebarEntry`). Functional parity first; visual polish can reuse the existing helpers later. Keep `DrawElement` itself unchanged so the non-combo build is unaffected.

- [ ] **Step 3 (MM): mirror `DrawContent` in `mm/2s2h/BenGui/Menu.h` + `Menu.cpp`**

Identical method, adapted to MM's names: active-header CVar is the literal `"gSettings.Menu.ActiveHeader"`, theme CVar `"gSettings.Menu.Theme"`; MM's `MenuDrawItem` (BenGui `Menu.cpp` ~line 860) signature. The struct names (`MainMenuEntry`/`SidebarEntry`/`menuEntries`/`menuOrder`/`MenuDrawItem`) are the same in MM's copy. Use child IDs `"mmSidebar"`/`"mmWidgets"`.

- [ ] **Step 4: Build soh + 2ship**

Run:
```
scripts/build-soh.ps1
scripts/build-2ship.ps1
```
Expected: clean. (No behavior change yet — `DrawContent` is unused until Task 4.)

- [ ] **Step 5: Commit**

```
git add soh/soh/SohGui/Menu.h soh/soh/SohGui/Menu.cpp mm/2s2h/BenGui/Menu.h mm/2s2h/BenGui/Menu.cpp
git commit -m "Inc7/menu: add Menu::DrawContent(skipPaths) content-only render to both games"
```

---

### Task 4: `SOH_DrawSettings` / `MM_DrawSettings` exports + comboui delegation

**Files:**
- Modify: `soh/soh/OTRGlobals.cpp` (new export), `soh/soh/SohGui/SohGui.hpp`/`.cpp` (`GetSohMenu` already exists)
- Modify: `mm/2s2h/BenGui/BenGui.cpp`/`.hpp` (add `GetBenMenu`), `mm/2s2h/BenPort.cpp` (new export)
- Modify: `combo/gui/ComboMenu.cpp` (`DrawGamePanel`)

- [ ] **Step 1: SoH export**

In `soh/soh/OTRGlobals.cpp`, near the other combo exports:
```cpp
#ifdef COMBO_BUILD
#include <set>
#include <sstream>
// ComboShip: draw OOT's menu content (content-only) into the current ImGui window,
// skipping the comma-separated header/"Header/Sidebar" paths in skipCsv (so comboui can
// omit the engine sidebars it draws itself in the Shared tab).
extern "C" __declspec(dllexport) void SOH_DrawSettings(const char* skipCsv) {
    auto menu = SohGui::GetSohMenu();
    if (!menu) return;
    std::set<std::string> skip;
    if (skipCsv && skipCsv[0]) {
        std::stringstream ss(skipCsv); std::string item;
        while (std::getline(ss, item, ',')) if (!item.empty()) skip.insert(item);
    }
    menu->DrawContent(skip);
}
#endif
```
`GetSohMenu()` already exists (`SohGui.hpp:47`). No change needed there.

- [ ] **Step 2: MM accessor + export**

In `mm/2s2h/BenGui/BenGui.hpp`, declare:
```cpp
std::shared_ptr<BenMenu> GetBenMenu(); // ComboShip
```
In `mm/2s2h/BenGui/BenGui.cpp`, define (next to `mBenMenu`):
```cpp
std::shared_ptr<BenMenu> GetBenMenu() { return mBenMenu; } // ComboShip
```
In `mm/2s2h/BenPort.cpp`, near the other `MM_` exports:
```cpp
#ifdef COMBO_BUILD
#include <set>
#include <sstream>
// ComboShip: draw MM's menu content (content-only), skipping the given paths.
extern "C" __declspec(dllexport) void MM_DrawSettings(const char* skipCsv) {
    auto menu = BenGui::GetBenMenu();
    if (!menu) return;
    std::set<std::string> skip;
    if (skipCsv && skipCsv[0]) {
        std::stringstream ss(skipCsv); std::string item;
        while (std::getline(ss, item, ',')) if (!item.empty()) skip.insert(item);
    }
    menu->DrawContent(skip);
}
#endif
```
(Include `BenGui.hpp` in BenPort.cpp if not already.)

- [ ] **Step 3: comboui resolves the exports from the loaded game DLLs**

comboui.dll cannot link the game DLLs; it resolves the exports at runtime with `GetProcAddress`. In `combo/gui/ComboMenu.cpp`, add near the top:
```cpp
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
typedef void (*FnDrawSettings)(const char*);
FnDrawSettings ResolveDraw(const char* dll, const char* sym) {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA(dll);   // already loaded by the exe
    return h ? (FnDrawSettings)GetProcAddress(h, sym) : nullptr;
#else
    return nullptr;
#endif
}
FnDrawSettings sSohDraw = nullptr;
FnDrawSettings sMmDraw  = nullptr;
// Engine sidebars comboui draws itself in the Shared tab → skip them in the game tabs.
const char* kSohSkip = "Settings/Graphics,Settings/General";
const char* kMmSkip  = "Settings/Graphics,Settings/General";
} // namespace
```
Implement `DrawGamePanel`:
```cpp
void ComboMenu::DrawGamePanel(const char* gameKey) {
    if (std::string(gameKey) == "oot") {
        if (!sSohDraw) sSohDraw = ResolveDraw("soh.dll", "SOH_DrawSettings");
        if (sSohDraw) sSohDraw(kSohSkip);
        else ImGui::TextUnformatted("OOT settings unavailable (SOH_DrawSettings not found).");
    } else {
        if (!sMmDraw) sMmDraw = ResolveDraw("2ship.dll", "MM_DrawSettings");
        if (sMmDraw) sMmDraw(kMmSkip);
        else ImGui::TextUnformatted("MM settings unavailable (MM_DrawSettings not found).");
    }
}
```
Add `#include <string>` if not already present.

- [ ] **Step 4: Build soh, 2ship, comboui**

```
scripts/build-soh.ps1
scripts/build-2ship.ps1
scripts/build-comboui.ps1
```
Expected: clean. Confirm exports:
```
dumpbin /exports build/x64/soh/Debug/soh.dll | Select-String "SOH_DrawSettings"
dumpbin /exports build/x64/mm/Debug/2ship.dll | Select-String "MM_DrawSettings"
```
Expected: both present.

- [ ] **Step 5: Manual verification (game tabs render)**

Run the exe. Open the menu:
- **OOT** tab shows SoH's headers (Enhancements / Randomizer / Dev Tools / Network) with their sidebars + working widgets; **Graphics** and **General** sidebars under Settings are absent (skipped).
- **MM** tab shows MM's headers (Enhancements / Rando / Dev Tools) similarly; Graphics/General skipped.
- Toggling a widget (e.g. an Enhancement checkbox) persists and affects that game.
- After an OOT↔MM transition, both tabs still render.

- [ ] **Step 6: Commit**

```
git add soh/soh/OTRGlobals.cpp mm/2s2h/BenGui/BenGui.cpp mm/2s2h/BenGui/BenGui.hpp mm/2s2h/BenPort.cpp combo/gui/ComboMenu.cpp
git commit -m "Inc7/menu: delegate OOT/MM tabs to per-game DrawSettings exports"
```

---

## Phase 2 — Combo-drawn Shared (engine) panel

### Task 5: Implement the Shared panel against libultraship

**Files:**
- Create: `combo/gui/ComboSharedPanel.cpp`
- Modify: `combo/gui/ComboMenu.cpp` (`DrawSharedPanel` calls into it), `combo/CMakeLists.txt` (add source)

Scope (refinement of spec §3.1, per the audio/controls finding): the Shared tab covers the genuinely-shared, libultraship-applied settings — **Graphics** (internal resolution, MSAA, texture filter, vsync, windowed-fullscreen, video backend, interpolation FPS, match refresh rate) and **UI chrome** (menu theme, ImGui scale, cursor visibility, background opacity). **Audio volume and controller mapping stay under each game tab** (different CVars/apply per game), so they are NOT skipped from the game tabs and NOT drawn here.

- [ ] **Step 1: Write `ComboSharedPanel.cpp`**

```cpp
// combo/gui/ComboSharedPanel.cpp
// ComboShip: combo-owned engine-settings panel drawn directly against libultraship.
#include "ComboMenu.h"
#include <libultraship/libultraship.h>
#include <imgui.h>

// libultraship CVar names (engine-level; identical in both ports).
#define CVAR_INTERNAL_RESOLUTION "gInternalResolution"
#define CVAR_MSAA_VALUE          "gMSAAValue"
#define CVAR_TEXTURE_FILTER      "gTextureFilter"
#define CVAR_VSYNC_ENABLED       "gVsyncEnabled"
#define CVAR_SDL_WINDOWED_FS     "gSdlWindowedFullscreen"
#define CVAR_INTERPOLATION_FPS   "gInterpolationFPS"
#define CVAR_MATCH_REFRESH       "gMatchRefreshRate"
#define CVAR_MENU_THEME          "gSettings.Menu.Theme"
#define CVAR_IMGUI_SCALE         "gSettings.ImGuiScale"
#define CVAR_CURSOR_VISIBILITY   "gSettings.CursorVisibility"
#define CVAR_MENU_BG_OPACITY     "gSettings.Menu.BackgroundOpacity"

namespace ComboRando {

static void Save() {
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void ComboMenu::DrawSharedPanel() {
    auto window = Ship::Context::GetInstance()->GetWindow();

    ImGui::SeparatorText("Graphics");

    // Internal resolution (0.5x – 2.0x)
    float res = CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1.0f);
    if (ImGui::SliderFloat("Internal Resolution", &res, 0.5f, 2.0f, "%.2fx")) {
        CVarSetFloat(CVAR_INTERNAL_RESOLUTION, res);
        window->SetResolutionMultiplier(res);
        Save();
    }

    // MSAA (1–8)
    int msaa = CVarGetInteger(CVAR_MSAA_VALUE, 1);
    if (ImGui::SliderInt("Anti-aliasing (MSAA)", &msaa, 1, 8)) {
        CVarSetInteger(CVAR_MSAA_VALUE, msaa);
        window->SetMsaaLevel(msaa);
        Save();
    }

    // Texture filter (0 = three-point, 1 = bilinear, 2 = none) — engine reads the CVar.
    {
        const char* labels[] = { "Three-Point", "Bilinear", "None" };
        int tf = CVarGetInteger(CVAR_TEXTURE_FILTER, 0);
        if (tf < 0 || tf > 2) tf = 0;
        if (ImGui::BeginCombo("Texture Filter", labels[tf])) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::Selectable(labels[i], i == tf)) { CVarSetInteger(CVAR_TEXTURE_FILTER, i); Save(); }
            ImGui::EndCombo();
        }
    }

    // VSync (engine reads the CVar each present)
    bool vsync = CVarGetInteger(CVAR_VSYNC_ENABLED, 1) != 0;
    if (ImGui::Checkbox("VSync", &vsync)) { CVarSetInteger(CVAR_VSYNC_ENABLED, vsync ? 1 : 0); Save(); }

    // Windowed fullscreen
    bool fs = CVarGetInteger(CVAR_SDL_WINDOWED_FS, 0) != 0;
    if (ImGui::Checkbox("Windowed Fullscreen", &fs)) {
        CVarSetInteger(CVAR_SDL_WINDOWED_FS, fs ? 1 : 0);
        window->SetFullscreen(fs);
        Save();
    }

    // Video backend selector
    {
        auto available = window->GetAvailableWindowBackends();   // std::shared_ptr<std::vector<...>> per Window.h
        std::string current = window->GetWindowBackendName();
        if (ImGui::BeginCombo("Renderer", current.c_str())) {
            if (available) {
                for (auto backendId : *available) {
                    std::string name = window->GetWindowBackendName(backendId); // adjust to real signature
                    if (ImGui::Selectable(name.c_str(), name == current)) {
                        Ship::Context::GetInstance()->GetConfig()->SetInt("Window.Backend.Id", (int)backendId);
                        Ship::Context::GetInstance()->GetConfig()->Save();
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("(Renderer change applies on restart)");
    }

    // Interpolation FPS + match refresh rate
    int fps = CVarGetInteger(CVAR_INTERPOLATION_FPS, 20);
    if (ImGui::SliderInt("Interpolation FPS", &fps, 20, 360)) { CVarSetInteger(CVAR_INTERPOLATION_FPS, fps); Save(); }
    bool match = CVarGetInteger(CVAR_MATCH_REFRESH, 0) != 0;
    if (ImGui::Checkbox("Match Refresh Rate", &match)) { CVarSetInteger(CVAR_MATCH_REFRESH, match ? 1 : 0); Save(); }

    ImGui::SeparatorText("Interface");

    int theme = CVarGetInteger(CVAR_MENU_THEME, 0);
    if (ImGui::SliderInt("Menu Theme", &theme, 0, 7)) { CVarSetInteger(CVAR_MENU_THEME, theme); Save(); }
    int scale = CVarGetInteger(CVAR_IMGUI_SCALE, 1);
    if (ImGui::SliderInt("UI Scale", &scale, 0, 3)) { CVarSetInteger(CVAR_IMGUI_SCALE, scale); Save(); }
    bool cursor = CVarGetInteger(CVAR_CURSOR_VISIBILITY, 0) != 0;
    if (ImGui::Checkbox("Always Show Cursor", &cursor)) {
        CVarSetInteger(CVAR_CURSOR_VISIBILITY, cursor ? 1 : 0);
        window->SetForceCursorVisibility(cursor);
        Save();
    }
    float opacity = CVarGetFloat(CVAR_MENU_BG_OPACITY, 0.9f);
    if (ImGui::SliderFloat("Menu Background Opacity", &opacity, 0.0f, 1.0f)) {
        CVarSetFloat(CVAR_MENU_BG_OPACITY, opacity); Save();
    }

    ImGui::SeparatorText("Audio / Controls");
    ImGui::TextWrapped("Audio volume and controller mapping are per-game (different settings + apply paths) "
                       "and live under the OOT and MM tabs.");
}

} // namespace ComboRando
```

Implementer notes (verify exact libultraship signatures while wiring — they are in `libultraship/include/ship/window/Window.h`):
- `SetResolutionMultiplier(float)`, `SetMsaaLevel(uint32_t)`, `SetFullscreen(bool)`, `SetForceCursorVisibility(bool)`.
- `GetAvailableWindowBackends()` / `GetWindowBackendName()` exact return types — adapt the backend combo to the real API (it may return a vector of `{WindowBackend id, const char* name}` pairs). If the API shape differs, render the backend as read-only text + a note rather than guessing.
- Theme/scale ranges (0–7 / 0–3) match the games' menus; confirm against `gSettings.Menu.Theme` usage if a different max exists.

- [ ] **Step 2: Remove the placeholder `DrawSharedPanel` from `ComboMenu.cpp`**

Delete the placeholder `void ComboMenu::DrawSharedPanel() { ... }` line in `ComboMenu.cpp` (the real definition now lives in `ComboSharedPanel.cpp`).

- [ ] **Step 3: Add the new source to `combo/CMakeLists.txt`**

```cmake
set(COMBOUI_SOURCES
    gui/ComboMenu.cpp
    gui/ComboSharedPanel.cpp
)
```

- [ ] **Step 4: Reconfigure + build comboui**

```
cmake build/x64
scripts/build-comboui.ps1
```
Expected: clean.

- [ ] **Step 5: Manual verification (Shared tab)**

Run the exe. Shared tab:
- Changing Internal Resolution / MSAA / VSync / Fullscreen takes visible effect immediately.
- Theme/scale/cursor/opacity changes apply to the menu.
- Confirm Graphics/General are NOT shown under OOT/MM tabs (skipped), while Audio volume IS still reachable under each game tab (not skipped).

- [ ] **Step 6: Commit**

```
git add combo/gui/ComboSharedPanel.cpp combo/gui/ComboMenu.cpp combo/CMakeLists.txt
git commit -m "Inc7/menu: combo-drawn Shared engine-settings panel (graphics + UI chrome)"
```

---

## Phase 3 — Combo tab (cross-world Generate)

### Task 6: Shared progress struct + worker, decoupled from save creation

**Files:**
- Create: `combo/gui/ComboGenProgress.h`
- Modify: `combo/rando/CrossWorldRando.h` (progress reporting), `combo/ComboShip.cpp` (worker + new exports wiring), `soh/soh/OTRGlobals.cpp` (generate-request + seed-gate exports), `soh/src/code/z_sram.c` (decouple)

This carries the Inc7 Generate mechanics into the Combo tab. (These were specified in the superseded Inc7 plan; reproduced here so this plan is self-contained.)

- [ ] **Step 1: Write `ComboGenProgress.h`**

```cpp
// combo/gui/ComboGenProgress.h
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
namespace ComboRando {
struct ComboGenProgress {
    std::atomic<int>      phase{ 0 };   // 0 Idle,1 Preparing,2 Placing,3 Finalizing
    std::atomic<int>      placed{ 0 };
    std::atomic<int>      total{ 0 };
    std::atomic<bool>     running{ false };
    std::atomic<bool>     done{ false };
    std::atomic<bool>     success{ false };
    std::atomic<uint32_t> seed{ 0 };
    std::atomic<int>      foreignCount{ 0 };
    char                  error[256] = { 0 };
    void Reset() { phase=0; placed=0; total=0; success=false; seed=0; foreignCount=0; error[0]='\0'; }
    void SetError(const char* m){ if(!m){error[0]='\0';return;} std::strncpy(error,m,sizeof(error)-1); error[sizeof(error)-1]='\0'; }
    static const char* PhaseLabel(int p){ switch(p){case 1:return "Preparing pools";case 2:return "Placing items";case 3:return "Finalizing";default:return "Idle";} }
};
} // namespace ComboRando
```

- [ ] **Step 2: Add progress reporting to `CrossWorldCombinedFill`**

In `combo/rando/CrossWorldRando.h`: add `#include "gui/ComboGenProgress.h"` after the json include; extend the signature with a trailing `ComboRando::ComboGenProgress* progress = nullptr`; set `progress->phase=1` after `result.success=false;`; before the placement loop set `phase=2` and `total=allItems.size()`; as the loop's first statement set `progress->placed=(int)idx`; before building the spoiler set `placed=total` and `phase=3`. (All guarded by `if (progress)`.)

- [ ] **Step 3: soh exports for generate-request + seed-gate**

In `soh/soh/OTRGlobals.cpp` (COMBO_BUILD block):
```cpp
#include "gui/ComboGenProgress.h"
extern "C" void (*gComboGenerateRequestCallback)(const char*, ComboRando::ComboGenProgress*) = nullptr;
extern "C" __declspec(dllexport) void SOH_SetOnComboGenerateRequestCallback(void (*cb)(const char*, ComboRando::ComboGenProgress*)) { gComboGenerateRequestCallback = cb; }
extern "C" __declspec(dllexport) void SOH_TriggerComboGenerate(const char* seed, ComboRando::ComboGenProgress* p) { if (gComboGenerateRequestCallback) gComboGenerateRequestCallback(seed, p); }
extern "C" __declspec(dllexport) void SOH_SetSeedGenerated(uint8_t g) { if (OTRGlobals::Instance && OTRGlobals::Instance->gRandoContext) OTRGlobals::Instance->gRandoContext->SetSeedGenerated(g != 0); }
```
comboui will call `SOH_TriggerComboGenerate` (resolved via `GetProcAddress` from soh.dll, same pattern as `ResolveDraw`).

- [ ] **Step 4: Refactor the exe's generate into a seed+progress worker**

In `combo/ComboShip.cpp`, add includes (`#include "gui/ComboGenProgress.h"`, `<thread>`, `<atomic>`), the new fn-pointer typedefs/statics, resolve the new symbols, replace `Combo_OnGenerate(int fileNum)` with the worker below, and register the request handler at startup.

Function-pointer statics (with the others ~lines 100-140):
```cpp
typedef void (*FnSetGenReqCb)(void (*)(const char*, ComboRando::ComboGenProgress*));
typedef void (*FnSetSeedGenerated)(uint8_t);
static FnSetGenReqCb      SOH_SetOnComboGenerateRequestCallback = nullptr;
static FnSetSeedGenerated SOH_SetSeedGenerated                  = nullptr;
static std::thread        g_GenerateThread;
static std::atomic<bool>  g_GenerateBusy{ false };
extern "C" uint32_t Ship_Hash(const char* str);     // libultraship export (drop if already declared)
extern "C" int      Ship_Random(int min, int max);  // libultraship export (drop if already declared)
```

Symbol resolution (with the other `GetSym` calls):
```cpp
    SOH_SetOnComboGenerateRequestCallback = (FnSetGenReqCb)      GetSym(sohModule, "SOH_SetOnComboGenerateRequestCallback");
    SOH_SetSeedGenerated                  = (FnSetSeedGenerated) GetSym(sohModule, "SOH_SetSeedGenerated");
```

Worker + request handler (replaces `Combo_OnGenerate`; `g_PendingMMPlacements` is the existing static at ComboShip.cpp:140, consumed unchanged by `Combo_OnOOTSaveInit`):
```cpp
static void RunComboFill(std::string inputSeed, ComboRando::ComboGenProgress* progress) {
    auto fail = [&](const char* msg) {
        if (progress) { progress->SetError(msg); progress->success.store(false); progress->done.store(true); }
        std::cerr << "[ComboShip] RunComboFill: " << msg << "\n";
        g_GenerateBusy.store(false);
    };
    if (!SOH_DumpRandoStaticData || !MM_DumpRandoStaticData) { fail("dump functions not resolved"); return; }
    std::string sohDump = SOH_DumpRandoStaticData();
    std::string mmDump  = MM_DumpRandoStaticData();
    if (sohDump.empty() || mmDump.empty()) { fail("empty static-data dump"); return; }

    if (inputSeed.empty()) inputSeed = std::to_string(Ship_Random(0, 1000000));
    uint32_t masterSeed = Ship_Hash(inputSeed.c_str());

    std::string spoiler;
    bool usedCombinedFill = false;
    if (Combo_SOH_Rando_Reset && Combo_SOH_Rando_SetOwnedItems && Combo_SOH_Rando_GetReachableChecks &&
        Combo_SOH_Rando_PlaceItem && Combo_MM_Rando_Reset && Combo_MM_Rando_SetOwnedItems &&
        Combo_MM_Rando_GetReachableChecks && Combo_MM_Rando_PlaceItem && Combo_MM_Rando_Restore) {
        ComboRando::OracleFns ootOracle = { Combo_SOH_Rando_Reset, Combo_SOH_Rando_SetOwnedItems,
                                            Combo_SOH_Rando_GetReachableChecks, Combo_SOH_Rando_PlaceItem };
        ComboRando::OracleFns mmOracle  = { Combo_MM_Rando_Reset, Combo_MM_Rando_SetOwnedItems,
                                            Combo_MM_Rando_GetReachableChecks, Combo_MM_Rando_PlaceItem };
        auto result = ComboRando::CrossWorldCombinedFill(sohDump, mmDump, masterSeed, ootOracle, mmOracle, "", progress);
        if (result.success) { spoiler = result.spoilerJson; usedCombinedFill = true; }
        else { Combo_MM_Rando_Restore(); fail((std::string("combined fill failed: ") + result.error).c_str()); return; }
        Combo_MM_Rando_Restore();
    }
    if (!usedCombinedFill) spoiler = ComboRando::CrossWorldGenerateSpoiler(sohDump, mmDump, masterSeed);

    const int kCanonicalSlot = 0;
    try {
        std::error_code ec;
        std::filesystem::create_directories("saves/combo", ec);
        std::ofstream(std::string("saves/combo/slot") + std::to_string(kCanonicalSlot) + ".spoiler.json",
                      std::ios::trunc) << spoiler;
        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());
        ComboRando::WriteForeignFromAnnotations(kCanonicalSlot, foreignArr);
        nlohmann::json ootApply = j.value("oot", nlohmann::json::object());
        nlohmann::json mmApply  = j.value("mm",  nlohmann::json::object());
        for (const auto& fm : foreignArr) {
            std::string cg = fm.value("checkGame", ""), cn = fm.value("checkName", "");
            if (cn.empty()) continue;
            if (cg == "oot")     ootApply[cn] = ComboRando::kForeignSentinelNameOOT;
            else if (cg == "mm") mmApply[cn]  = ComboRando::kForeignSentinelNameMM;
        }
        if (SOH_ApplyRandoPlacements) SOH_ApplyRandoPlacements(ootApply.dump().c_str());
        else if (SOH_SetSeedGenerated) SOH_SetSeedGenerated(1);
        g_PendingMMPlacements = mmApply.dump();
        if (progress) {
            progress->seed.store(masterSeed);
            progress->foreignCount.store((int)foreignArr.size());
            progress->success.store(true);
            progress->done.store(true);
        }
    } catch (const std::exception& e) { fail((std::string("post-fill exception: ") + e.what()).c_str()); return; }
    g_GenerateBusy.store(false);
}

static void Combo_OnGenerateRequest(const char* inputSeed, ComboRando::ComboGenProgress* progress) {
    if (g_GenerateBusy.exchange(true)) return;            // a run is already in flight
    if (g_GenerateThread.joinable()) g_GenerateThread.join();
    g_GenerateThread = std::thread(RunComboFill, std::string(inputSeed ? inputSeed : ""), progress);
}
```

Startup registration (replace the old `SOH_SetOnComboGenerateCallback(Combo_OnGenerate)` block ~lines 499-501):
```cpp
    if (SOH_SetOnComboGenerateRequestCallback) {
        SOH_SetOnComboGenerateRequestCallback(Combo_OnGenerateRequest);
        std::cout << "[ComboShip] Combo generate-request handler registered." << std::endl;
    }
    // (Old save-time SOH_SetOnComboGenerateCallback registration removed — generation is window-driven.)
```
Keep `SOH_SetOnNewSaveCallback(Combo_OnOOTSaveInit)` as-is. (If `Ship_Hash`/`Ship_Random` are already declared via an included libultraship header, drop the local `extern "C"` decls to avoid redefinition.)

- [ ] **Step 5: Decouple save creation in `z_sram.c`**

Replace the combo block in `Sram_InitSave` so it only forces the quest type:
```c
    // ComboShip: generation is window-driven now; only force the rando quest type here.
    fileChooseCtx->questType[fileChooseCtx->buttonIndex] = QUEST_RANDOMIZER;
    (void)gComboGenerateCallback; // retained symbol; no longer invoked
```

- [ ] **Step 6: Build soh, comboui, ComboShip**

```
scripts/build-soh.ps1
scripts/build-comboui.ps1
scripts/build-comboship.ps1
```
Expected: clean. Confirm `SOH_TriggerComboGenerate` exported.

- [ ] **Step 7: Commit**

```
git add combo/gui/ComboGenProgress.h combo/rando/CrossWorldRando.h combo/ComboShip.cpp soh/soh/OTRGlobals.cpp soh/src/code/z_sram.c
git commit -m "Inc7/menu: threaded cross-world generate worker, decoupled from save creation"
```

---

### Task 7: Combo tab UI (seed field, Generate, progress)

**Files:**
- Modify: `combo/gui/ComboMenu.h` (member `ComboGenProgress` + seed buffer), `combo/gui/ComboMenu.cpp` (`DrawComboPanel`)

- [ ] **Step 1: Add members to `ComboMenu.h`**

```cpp
#include "ComboGenProgress.h"
// ... in class private:
    char mSeedBuf[128] = { 0 };
    ComboGenProgress mProgress;
    std::string mStatusLine;
```
Add `#include <string>`.

- [ ] **Step 2: Implement `DrawComboPanel` in `ComboMenu.cpp`**

```cpp
namespace {
typedef void (*FnTriggerGenerate)(const char*, ComboRando::ComboGenProgress*);
FnTriggerGenerate sTrigger = nullptr;
FnTriggerGenerate ResolveTrigger() {
#ifdef _WIN32
    HMODULE h = GetModuleHandleA("soh.dll");
    return h ? (FnTriggerGenerate)GetProcAddress(h, "SOH_TriggerComboGenerate") : nullptr;
#else
    return nullptr;
#endif
}
} // namespace

void ComboMenu::DrawComboPanel() {
    ImGui::TextWrapped("Generate a cross-world randomizer seed spanning OOT and MM.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("Seed", "(blank = random)", mSeedBuf, sizeof(mSeedBuf));
    ImGui::SameLine();
    const bool busy = mProgress.running.load();
    if (busy) ImGui::BeginDisabled();
    if (ImGui::Button("Generate")) {
        if (!sTrigger) sTrigger = ResolveTrigger();
        if (sTrigger) {
            mProgress.Reset();
            mProgress.done.store(false);
            mProgress.running.store(true);
            mStatusLine.clear();
            sTrigger(mSeedBuf, &mProgress);
        } else {
            mStatusLine = "Generate unavailable (SOH_TriggerComboGenerate not found).";
        }
    }
    if (busy) ImGui::EndDisabled();

    // Latch result when the worker finishes.
    if (mProgress.done.load() && mProgress.running.load()) {
        if (mProgress.success.load()) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Seed 0x%X - %d cross-world placements",
                     mProgress.seed.load(), mProgress.foreignCount.load());
            mStatusLine = std::string("Done: ") + buf;
        } else {
            mStatusLine = std::string("Error: ") + mProgress.error;
        }
        mProgress.running.store(false);
    }

    if (mProgress.running.load()) {
        int placed = mProgress.placed.load(), total = mProgress.total.load();
        float frac = (total > 0) ? (float)placed / (float)total : 0.0f;
        ImGui::TextUnformatted(ComboGenProgress::PhaseLabel(mProgress.phase.load()));
        ImGui::ProgressBar(frac, ImVec2(360, 0));
        ImGui::Text("%d / %d", placed, total);
    } else if (!mStatusLine.empty()) {
        ImGui::TextUnformatted(mStatusLine.c_str());
    }
}
```

- [ ] **Step 3: Build comboui**

```
scripts/build-comboui.ps1
```
Expected: clean.

- [ ] **Step 4: Manual verification (Generate)**

Run the exe. Combo tab:
- Enter `test1`, press Generate → progress bar moves through "Placing items", then status `Done: Seed 0x… · N cross-world placements`.
- Same seed twice → identical `saves/combo/slot0.spoiler.json`; blank → differs.
- File create is gated until Generate has run; allowed after; the created save uses the generated placements (OOT) + stashed MM slice.

- [ ] **Step 5: Commit**

```
git add combo/gui/ComboMenu.h combo/gui/ComboMenu.cpp
git commit -m "Inc7/menu: Combo tab — seed field, Generate, threaded progress"
```

---

## Phase 4 — Docs + final verification

### Task 8: Document seams + full verification pass

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md`

- [ ] **Step 1: Add an "Inc7 — Unified settings menu" section to `docs/UPSTREAM_MERGES.md`**

Document each vendored seam with WHY: `Menu.h`/`Menu.cpp` `DrawContent` (both games); `SohGui.cpp`/`OTRGlobals.cpp` SetMenu suppression + `SOH_DrawSettings`/generate-request/seed-gate exports; `BenGui.cpp`/`.hpp`/`BenPort.cpp` SetMenu suppression + `GetBenMenu` + `MM_DrawSettings`; `z_sram.c` decouple. Confirm each carries a `// ComboShip:` comment.

- [ ] **Step 2: Full clean verification (spec §9)**

Build in order: `scripts/build-soh.ps1; scripts/build-2ship.ps1; scripts/build-comboui.ps1; scripts/build-comboship.ps1`. Then verify in the running exe:
- [ ] Menu opens with four tabs.
- [ ] Shared: resolution/MSAA/vsync/theme changes take effect.
- [ ] OOT/MM: each shows that game's specific settings; Graphics/General not duplicated there; audio volume still reachable per-game.
- [ ] Toggling a game CVar persists + affects that game.
- [ ] Combo: Generate runs with a moving bar; fixed seed → identical spoiler; blank → differs.
- [ ] "Start Game" gated until Generate; allowed after.
- [ ] Menu persists + both game tabs work across an OOT↔MM transition.
- [ ] No crash; capture `x64/Debug/combo_abort_stack.txt` if any.

- [ ] **Step 3: Commit**

```
git add docs/UPSTREAM_MERGES.md
git commit -m "Inc7/menu: document unified-menu seams in UPSTREAM_MERGES"
```

- [ ] **Step 4: Finish the branch**

Use superpowers:finishing-a-development-branch to decide merge/PR/cleanup for the `randomizer` branch work.

---

## Task 9: Risks / checkpoints (address inline as they arise)

| Risk | Where | Mitigation |
|------|-------|------------|
| Menu hotkey CVar differs per game (`gOpenWindows.Menu` vs `gWindows.Menu`) → MM can't toggle the menu | Task 2 Step 7 | Have comboui's `ComboMenu` sync visibility from BOTH CVars each frame (in `UpdateElement`: if either flips, toggle), or pick the one the active input handler uses. Confirm which CVar each game's pause/menu input toggles. |
| `DrawContent` simplified layout diverges from the games' themed menu look | Task 3 | Functional first. To match visuals, call the existing `ModernMenuSidebarEntry`/themed helpers (they're members) instead of `Selectable`/`Button`. |
| libultraship backend API signatures (`GetAvailableWindowBackends`/`GetWindowBackendName`) differ from assumed | Task 5 Step 1 | Verify against `Window.h`; if the shape differs, render renderer as read-only text + restart note rather than a live selector. |
| A game-specific widget callback touches live game state while that game is backgrounded | Task 4/8 | Note any misbehaving widget; pass a "foreground" flag into `*_DrawSettings` and disable-with-tooltip those widgets when not foreground. |
| OOT rando `Context` reset between Generate and save create | Task 6/7 | Verify; if reset, stash `ootApply` and re-apply at save-init (mirror the MM stash). |
| MM `OnFileCreate` self-generation double-runs | Task 7 | Verify; gate under `COMBO_BUILD` in `mm/2s2h/Rando/MiscBehavior/OnFileCreate.cpp` if it does. |
```

