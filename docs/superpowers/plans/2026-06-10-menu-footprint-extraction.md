# Menu Footprint Extraction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the ComboShip-written menu code currently duplicated inside vendored `soh/` and `mm/` into combo-owned headers, shrinking the vendored diff from ~1,630 lines to ~850 lines without changing behavior.

**Architecture:** Three combo-owned, header-only units under `combo/menu/` (already on both games' include paths — zero CMake changes to the games): (1) `ComboMenuExport.h`, a duck-typed template that owns the two-pass CwMenu tree-flattening algorithm currently copy-pasted in `SohMenu.cpp` and `BenMenu.cpp`; each game keeps only a small Policy struct for its enum mapping and options structs. (2) `ComboMenuDrawContent.h`, the shared `Menu::DrawContent` ImGui layout body; each game keeps a Hooks struct and a ~20-line wrapper. (3) `ComboMenuSharedContext.h`, the duplicated per-DLL ImGui-context helper. The headers compile *into* each game DLL (no new ABI surface, no new exports) but are *owned* by combo/, per the combo-ownership principle. Upstream `DrawElement` and all other upstream code is untouched.

**Tech Stack:** C++20, header-only templates, ImGui, GoogleTest (existing `lus_tests` target in `libultraship/tests`), CMake/MSVC (`build/x64` superbuild + `build-lus-tests` standalone test build).

**Constraints (from project memory):**
- Only relocate code *we* wrote on this branch. Never refactor upstream functions (e.g. `DrawElement`).
- Build targets individually (`scripts/build-soh.ps1`, `scripts/build-2ship.ps1`), not the whole tree.
- Behavior must be identical; the CwMenu emitted after refactor must match the current one (verified by tests + runtime smoke).
- Vendored-file changes keep their `// ComboShip:` comments.

**Execution errata (plan executed 2026-06-10; deviations applied during implementation):**
- Tasks 5/6: `MenuDrawItem` is a non-static `Menu` member, so the `DrawItem` shim cannot call it bare as written below; both games use a file-scope `Menu* gSohDrawHooksMenu` / `gBenDrawHooksMenu` (anonymous namespace) set to `this` at the top of `DrawContent` — see the safety comment in the source.
- Tests run from the main `build/x64` tree (`build\x64\libultraship\tests\Debug\lus_tests.exe`); the standalone `build-lus-tests` tree has a pre-existing broken pre-link step (`filter_def.cmake` path resolves wrong in standalone configure).
- Review additions: reserve==fill asserts + 7th test in the serializer; `#error` TU-glue guards in both glue headers; helper namespace is `ComboMenuContext` (not `ComboMenuGlue` as written in Task 7).

**Known intentional micro-differences (accepted, do not "fix" back):**
1. Empty tooltips are no longer copied into `mOwnedStrings` (output is still `""`).
2. MM's redundant `GetVectorIndexOf(entry.sidebarOrder, sectionIndex)` clause in DrawContent is dropped — `std::find` over `visibleSidebars` (a subset of `sidebarOrder`) subsumes it.
3. Widget label width in DrawContent uses the *clamped* column count (`columns`, forced to 1 when `availW < 800`) in both games. MM already did this; OOT used the raw `sb.columnCount`. Visual difference only in windows narrower than 800px.

---

### Task 1: Failing unit test for the generic serializer

The serializer is pure data-structure code, so it gets a real mock-based gtest in the existing `lus_tests` target (precedent: `resource_manager_scope_tests.cpp` was added there on this branch).

**Files:**
- Create: `libultraship/tests/combo_menu_export_tests.cpp`
- Modify: `libultraship/tests/CMakeLists.txt` (add source + include dir)

- [ ] **Step 1: Add the test file to the test CMake**

In `libultraship/tests/CMakeLists.txt`, add `combo_menu_export_tests.cpp` to the `add_executable(lus_tests ...)` list (after `resource_manager_scope_tests.cpp`):

```cmake
    resource_manager_scope_tests.cpp
    combo_menu_export_tests.cpp
)
```

And after the `target_link_libraries(lus_tests ...)` block, add:

```cmake
# ComboShip: combo-owned menu serializer is header-only; tests mock the game-side menu types.
target_include_directories(lus_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../combo/menu)
```

(`CMAKE_CURRENT_SOURCE_DIR` not `CMAKE_SOURCE_DIR` — `build-lus-tests` configures `libultraship/` standalone, so `CMAKE_SOURCE_DIR` is NOT the repo root there.)

- [ ] **Step 2: Write the failing test**

Create `libultraship/tests/combo_menu_export_tests.cpp`:

```cpp
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

#include "ComboMenuExport.h"

// Mock of the duck-typed surface ComboMenuExport::Build walks. Mirrors the shape of the
// games' Menu trees (menuOrder -> menuEntries -> sidebarOrder/sidebars -> columnWidgets)
// and of WidgetInfo (name/cVar/windowName/callback/preFunc/sameLine/hideInSearch).
namespace {

enum MockType { MT_CHECKBOX, MT_COMBOBOX, MT_CUSTOM };

struct MockWidget {
    MockType type = MT_CHECKBOX;
    std::string name;
    const char* cVar = nullptr;
    const char* windowName = nullptr;
    void (*callback)() = nullptr;
    void (*preFunc)() = nullptr;
    bool sameLine = false;
    bool hideInSearch = false;
    std::vector<std::pair<int32_t, const char*>> comboChoices;
    std::string tooltip;
};

struct MockSidebar {
    uint32_t columnCount = 1;
    std::vector<std::vector<MockWidget>> columnWidgets;
};

struct MockEntry {
    std::string label;
    const char* sidebarCvar = nullptr;
    std::map<std::string, MockSidebar> sidebars;
    std::vector<std::string> sidebarOrder;
};

struct MockPolicy {
    using Widget = MockWidget;
    static CwKind Kind(const Widget& w) {
        switch (w.type) {
            case MT_COMBOBOX:
                return CW_COMBOBOX;
            case MT_CUSTOM:
                return CW_CUSTOM;
            default:
                return CW_CHECKBOX;
        }
    }
    static bool IsRandoSection(const std::string& label) {
        return label == "Rando";
    }
    static std::string Tooltip(const Widget& w) {
        return w.tooltip;
    }
    static size_t CountChoices(const Widget& w) {
        return w.type == MT_COMBOBOX ? w.comboChoices.size() : 0;
    }
    static void EmitChoices(const Widget& w, std::vector<CwChoice>& out) {
        if (w.type != MT_COMBOBOX) {
            return;
        }
        for (auto& [value, label] : w.comboChoices) {
            CwChoice c = {};
            c.value = value;
            c.label = label ? label : "";
            out.push_back(c);
        }
    }
    static void FillOptions(const Widget& w, CwWidget& cw) {
        if (cw.kind == CW_COMBOBOX) {
            cw.iDefault = 7; // sentinel: proves FillOptions ran for comboboxes
        }
    }
};

void DummyCallback() {
}

MockWidget MakeWidget(MockType t, std::string name) {
    MockWidget w;
    w.type = t;
    w.name = std::move(name);
    return w;
}

// Tree: menuOrder {Settings, Rando}
//   Settings (sidebarOrder {General, Audio}):
//     General, 2 columns: col0 = [A:checkbox(cvar,callback,tooltip), B:combobox(2 choices)]
//                          col1 = [C:custom(sameLine)]
//     Audio, 1 column:    col0 = [D:checkbox]
//   Rando (sidebarOrder {Seed}):
//     Seed, 1 column:     col0 = [E:checkbox]
struct Fixture {
    std::vector<std::string> menuOrder = { "Settings", "Rando" };
    std::map<std::string, MockEntry> menuEntries;
    ComboMenuExport::State<MockWidget> state;

    Fixture() {
        MockEntry settings;
        settings.label = "Settings";
        settings.sidebarCvar = "gCombo.SettingsSidebar";
        settings.sidebarOrder = { "General", "Audio" };

        MockSidebar general;
        general.columnCount = 2;
        MockWidget a = MakeWidget(MT_CHECKBOX, "A");
        a.cVar = "gA";
        a.callback = DummyCallback;
        a.tooltip = "tip-a";
        MockWidget b = MakeWidget(MT_COMBOBOX, "B");
        b.comboChoices = { { 10, "Ten" }, { 20, "Twenty" } };
        MockWidget c = MakeWidget(MT_CUSTOM, "C");
        c.sameLine = true;
        general.columnWidgets = { { a, b }, { c } };
        settings.sidebars["General"] = general;

        MockSidebar audio;
        audio.columnWidgets = { { MakeWidget(MT_CHECKBOX, "D") } };
        settings.sidebars["Audio"] = audio;

        MockEntry rando;
        rando.label = "Rando";
        rando.sidebarOrder = { "Seed" };
        MockSidebar seed;
        seed.columnWidgets = { { MakeWidget(MT_CHECKBOX, "E") } };
        rando.sidebars["Seed"] = seed;

        menuEntries["Settings"] = settings;
        menuEntries["Rando"] = rando;
    }

    const CwMenu* Build() {
        return ComboMenuExport::Build<MockPolicy>(state, menuOrder, menuEntries);
    }
};

TEST(ComboMenuExport, FlattensInDeterministicOrderWithStableIndices) {
    Fixture f;
    const CwMenu* menu = f.Build();
    ASSERT_NE(menu, nullptr);
    EXPECT_EQ(menu->version, 1);
    ASSERT_EQ(menu->sectionCount, 2);
    ASSERT_EQ(f.state.widgets.size(), 5u);
    ASSERT_EQ(f.state.flat.size(), 5u);

    // Walk order: Settings/General col0 (A, B), col1 (C), Settings/Audio (D), Rando/Seed (E).
    const char* expectedNames[] = { "A", "B", "C", "D", "E" };
    for (int i = 0; i < 5; i++) {
        EXPECT_EQ(f.state.widgets[i].index, i);
        EXPECT_STREQ(f.state.widgets[i].name, expectedNames[i]);
        EXPECT_STREQ(f.state.flat[i]->name.c_str(), expectedNames[i]);
    }

    EXPECT_EQ(f.state.widgets[0].kind, CW_CHECKBOX);
    EXPECT_STREQ(f.state.widgets[0].cvar, "gA");
    EXPECT_EQ(f.state.widgets[0].hasCallback, 1);
    EXPECT_EQ(f.state.widgets[0].hasPreFunc, 0);
    EXPECT_STREQ(f.state.widgets[0].tooltip, "tip-a");
    EXPECT_EQ(f.state.widgets[2].kind, CW_CUSTOM);
    EXPECT_EQ(f.state.widgets[2].sameLine, 1);
    EXPECT_STREQ(f.state.widgets[2].cvar, "");
    EXPECT_STREQ(f.state.widgets[2].tooltip, "");
}

TEST(ComboMenuExport, WiresChoicesAndRunsFillOptions) {
    Fixture f;
    f.Build();
    const CwWidget& b = f.state.widgets[1];
    EXPECT_EQ(b.kind, CW_COMBOBOX);
    ASSERT_EQ(b.choiceCount, 2);
    ASSERT_NE(b.choices, nullptr);
    EXPECT_EQ(b.choices, f.state.choices.data()); // first (only) choice span starts at offset 0
    EXPECT_EQ(b.choices[0].value, 10);
    EXPECT_STREQ(b.choices[0].label, "Ten");
    EXPECT_EQ(b.choices[1].value, 20);
    EXPECT_STREQ(b.choices[1].label, "Twenty");
    EXPECT_EQ(b.iDefault, 7); // FillOptions sentinel
    // Non-combobox widgets carry no choices.
    EXPECT_EQ(f.state.widgets[0].choiceCount, 0);
    EXPECT_EQ(f.state.widgets[0].choices, nullptr);
}

TEST(ComboMenuExport, SectionAndSidebarRangesPointIntoStableStorage) {
    Fixture f;
    const CwMenu* menu = f.Build();
    ASSERT_EQ(menu->sectionCount, 2);
    const CwSection& settings = menu->sections[0];
    EXPECT_STREQ(settings.sectionLabel, "Settings");
    EXPECT_STREQ(settings.sidebarCvar, "gCombo.SettingsSidebar");
    ASSERT_EQ(settings.sidebarCount, 2);
    EXPECT_STREQ(settings.sidebars[0].sidebarName, "General");
    EXPECT_EQ(settings.sidebars[0].columnCount, 2u);
    EXPECT_EQ(settings.sidebars[0].widgetCount, 3);
    EXPECT_EQ(settings.sidebars[0].widgets, f.state.widgets.data());
    EXPECT_STREQ(settings.sidebars[1].sidebarName, "Audio");
    EXPECT_EQ(settings.sidebars[1].widgets, f.state.widgets.data() + 3);

    const CwSection& rando = menu->sections[1];
    EXPECT_STREQ(rando.sectionLabel, "Rando");
    EXPECT_STREQ(rando.sidebarCvar, "");
    ASSERT_EQ(rando.sidebarCount, 1);
    EXPECT_EQ(rando.sidebars, f.state.sidebars.data() + 2);
}

TEST(ComboMenuExport, FlatRandoMarksOnlyRandoSectionWidgets) {
    Fixture f;
    f.Build();
    ASSERT_EQ(f.state.flatRando.size(), 5u);
    EXPECT_EQ(f.state.flatRando[0], 0);
    EXPECT_EQ(f.state.flatRando[3], 0);
    EXPECT_EQ(f.state.flatRando[4], 1); // E lives in the "Rando" section
}

TEST(ComboMenuExport, SecondBuildReturnsCachedPointerWithoutRebuilding) {
    Fixture f;
    const CwMenu* first = f.Build();
    size_t widgets = f.state.widgets.size();
    size_t ownedStrings = f.state.ownedStrings.size();
    const CwMenu* second = f.Build();
    EXPECT_EQ(first, second);
    EXPECT_EQ(f.state.widgets.size(), widgets);
    EXPECT_EQ(f.state.ownedStrings.size(), ownedStrings);
}

TEST(ComboMenuExport, EmptyMenuOrderFallsBackToSortedKeys) {
    Fixture f;
    f.menuOrder.clear();
    const CwMenu* menu = f.Build();
    ASSERT_EQ(menu->sectionCount, 2);
    // std::map keys sorted: "Rando" < "Settings".
    EXPECT_STREQ(menu->sections[0].sectionLabel, "Rando");
    EXPECT_STREQ(menu->sections[1].sectionLabel, "Settings");
}

} // namespace
```

- [ ] **Step 3: Run the build to verify it fails**

```powershell
cmake --build build-lus-tests --target lus_tests --config Debug
```

Expected: **FAIL** — `Cannot open include file: 'ComboMenuExport.h'` (the header doesn't exist yet).

---

### Task 2: Implement `combo/menu/ComboMenuExport.h`

The generic serializer. This is the SoH walker (`SohMenu.cpp:234-494`) verbatim, with the five game-specific points routed through a Policy: `Kind`, `IsRandoSection`, `Tooltip`, `CountChoices`/`EmitChoices`, `FillOptions`.

**Files:**
- Create: `combo/menu/ComboMenuExport.h`

- [ ] **Step 1: Write the header**

```cpp
/* combo/menu/ComboMenuExport.h
 * ComboShip-owned generic CwMenu serializer. The two-pass tree-flattening algorithm was
 * previously copy-pasted in soh/soh/SohGui/SohMenu.cpp and mm/2s2h/BenGui/BenMenu.cpp;
 * it is identical between the games, so it lives here once. This header is compiled INTO
 * each game DLL (it walks the game's private C++ menu tree, which cannot cross the DLL
 * boundary — see ComboMenuABI.h); only the SOURCE is combo-owned, minimizing the vendored
 * footprint.
 *
 * Duck-typed expectations on the menu tree (satisfied by both games and by test mocks):
 *   menuEntries:  map<std::string, Entry>  with .find/.begin/.end
 *   Entry:        .label (std::string), .sidebarCvar (const char*),
 *                 .sidebarOrder (vector<std::string>), .sidebars (map<std::string, Sidebar>)
 *   Sidebar:      .columnCount (uint32_t), .columnWidgets (vector<vector<Widget>>)
 *   Widget:       .name (std::string), .cVar/.windowName (const char*),
 *                 .callback/.preFunc (nullptr-comparable), .sameLine/.hideInSearch (bool)
 *
 * Per-game Policy contract:
 *   struct Policy {
 *       using Widget = ...;                                            // the game's WidgetInfo
 *       static CwKind Kind(const Widget&);                             // WidgetType -> CwKind
 *       static bool IsRandoSection(const std::string& label);         // "Randomizer" / "Rando"
 *       static std::string Tooltip(const Widget&);                    // "" if none
 *       static size_t CountChoices(const Widget&);                    // Pass-1 reserve count;
 *       static void EmitChoices(const Widget&, std::vector<CwChoice>&); // MUST emit exactly
 *                                                                     // CountChoices() entries
 *       static void FillOptions(const Widget&, CwWidget&);            // defaults/ranges/useAlpha;
 *                                                                     // cw.kind is already set
 *   };
 */
#ifndef COMBO_MENU_EXPORT_H
#define COMBO_MENU_EXPORT_H

#include "ComboMenuABI.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace ComboMenuExport {

// Backing storage for one game's exported CwMenu. Lives as long as the owning Menu instance,
// so the const char* / array pointers handed across the C-ABI stay valid for process life
// (comboui caches the returned CwMenu*).
template <typename TWidget>
struct State {
    bool exported = false;
    CwMenu menu = {};
    std::vector<TWidget*> flat;      // index -> source widget (the invoke key)
    std::vector<uint8_t> flatRando;  // parallel to flat: 1 if widget is in the rando section
    std::vector<CwSection> sections; // reserved up-front; .data() stable after fill
    std::vector<CwSidebar> sidebars;
    std::vector<CwWidget> widgets;
    std::vector<CwChoice> choices;
    std::deque<std::string> ownedStrings; // deque: never relocates existing elements
};

template <typename TPolicy, typename TMenuEntries>
inline const CwMenu* Build(State<typename TPolicy::Widget>& st, const std::vector<std::string>& menuOrder,
                           TMenuEntries& menuEntries) {
    if (st.exported) {
        return &st.menu;
    }

    // Deterministic walk order, matching the live render walk in the games' Menu.cpp:
    //   sections -> menuOrder (fallback: sorted menuEntries keys)
    //   sidebars -> Entry.sidebarOrder (fallback: sorted sidebar keys)
    //   widgets  -> Sidebar.columnWidgets in column order, then vector order
    // The position a widget gets in st.flat is its stable CwWidget.index.
    std::vector<std::string> sectionKeys = menuOrder;
    if (sectionKeys.empty()) {
        for (auto& kv : menuEntries) {
            sectionKeys.push_back(kv.first);
        }
        std::sort(sectionKeys.begin(), sectionKeys.end());
    }

    auto sidebarKeysOf = [](auto& entry) {
        std::vector<std::string> keys = entry.sidebarOrder;
        if (keys.empty()) {
            for (auto& kv : entry.sidebars) {
                keys.push_back(kv.first);
            }
            std::sort(keys.begin(), keys.end());
        }
        return keys;
    };

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
        sectionCount++;
        for (auto& sbKey : sidebarKeysOf(it->second)) {
            auto sbIt = it->second.sidebars.find(sbKey);
            if (sbIt == it->second.sidebars.end()) {
                continue;
            }
            sidebarCount++;
            for (auto& column : sbIt->second.columnWidgets) {
                for (auto& w : column) {
                    widgetCount++;
                    // MUST match Pass-2's EmitChoices count so reserve == fill and
                    // st.choices never reallocates (the Policy contract).
                    choiceCount += TPolicy::CountChoices(w);
                }
            }
        }
    }

    // Reserve to final sizes. After this, .data() and element addresses are stable
    // because we never push beyond the reserved capacity.
    st.sections.reserve(sectionCount);
    st.sidebars.reserve(sidebarCount);
    st.widgets.reserve(widgetCount);
    st.choices.reserve(choiceCount);
    st.flat.reserve(widgetCount);
    st.flatRando.reserve(widgetCount);

    // ---- Pass 2: fill. ----
    // Populate the flat st.widgets/st.choices fully (each CwWidget references st.choices.data()
    // by absolute offset, captured as a span start index + count, wired after fill). Then build
    // st.sidebars referencing st.widgets ranges, then st.sections referencing st.sidebars ranges.
    auto ownStr = [&st](const std::string& s) -> const char* {
        st.ownedStrings.push_back(s);
        return st.ownedStrings.back().c_str();
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

    // Track choice offsets per widget so we can wire choices->data() after st.choices is full.
    std::vector<std::pair<size_t, size_t>> widgetChoiceRange; // (start, count) into st.choices
    widgetChoiceRange.reserve(widgetCount);

    for (auto& secKey : sectionKeys) {
        auto it = menuEntries.find(secKey);
        if (it == menuEntries.end()) {
            continue;
        }
        auto& entry = it->second;
        size_t sectionSidebarStart = sidebarRanges.size();

        for (auto& sbKey : sidebarKeysOf(entry)) {
            auto sbIt = entry.sidebars.find(sbKey);
            if (sbIt == entry.sidebars.end()) {
                continue;
            }
            auto& sb = sbIt->second;
            size_t sidebarWidgetStart = st.widgets.size();

            for (auto& column : sb.columnWidgets) {
                for (auto& w : column) {
                    int32_t index = (int32_t)st.flat.size();
                    st.flat.push_back(&w);
                    // Keep flatRando perfectly parallel to flat (same site, same count).
                    // Rando widgets are exempt from the foreground-only placeholder in
                    // the game's DrawCustomByIndex.
                    st.flatRando.push_back(TPolicy::IsRandoSection(entry.label) ? 1 : 0);

                    CwWidget cw = {};
                    cw.index = index;
                    cw.kind = TPolicy::Kind(w);
                    cw.name = ownStr(w.name);
                    cw.cvar = w.cVar ? w.cVar : "";
                    std::string tip = TPolicy::Tooltip(w);
                    cw.tooltip = tip.empty() ? "" : ownStr(tip);
                    cw.windowName = w.windowName ? w.windowName : "";
                    cw.hasCallback = (w.callback != nullptr) ? 1 : 0;
                    cw.hasPreFunc = (w.preFunc != nullptr) ? 1 : 0;
                    cw.gameLoopDependent = 0;
                    cw.sameLine = w.sameLine ? 1 : 0;
                    cw.hideInSearch = w.hideInSearch ? 1 : 0;

                    size_t choiceStart = st.choices.size();
                    TPolicy::EmitChoices(w, st.choices);
                    size_t choiceCnt = st.choices.size() - choiceStart;
                    TPolicy::FillOptions(w, cw);

                    cw.choices = nullptr; // wired after st.choices is fully populated
                    cw.choiceCount = (int32_t)choiceCnt;
                    widgetChoiceRange.emplace_back(choiceStart, choiceCnt);
                    st.widgets.push_back(cw);
                }
            }

            sidebarRanges.push_back(SidebarRange{ ownStr(sbKey), sb.columnCount, sidebarWidgetStart,
                                                  st.widgets.size() });
        }

        sectionRanges.push_back(SectionRange{ ownStr(entry.label), entry.sidebarCvar ? entry.sidebarCvar : "",
                                              sectionSidebarStart, sidebarRanges.size() });
    }

    // Now that st.choices and st.widgets are fully populated (no further pushes), wire pointers.
    for (size_t i = 0; i < st.widgets.size(); i++) {
        auto& range = widgetChoiceRange[i];
        if (range.second > 0) {
            st.widgets[i].choices = st.choices.data() + range.first;
        }
    }

    // Build sidebars referencing st.widgets ranges.
    for (auto& sr : sidebarRanges) {
        CwSidebar sb = {};
        sb.sidebarName = sr.name;
        sb.columnCount = sr.columnCount;
        sb.widgetCount = (int32_t)(sr.widgetEnd - sr.widgetStart);
        sb.widgets = (sb.widgetCount > 0) ? (st.widgets.data() + sr.widgetStart) : nullptr;
        st.sidebars.push_back(sb);
    }

    // Build sections referencing st.sidebars ranges.
    for (auto& secR : sectionRanges) {
        CwSection sec = {};
        sec.sectionLabel = secR.label;
        sec.sidebarCvar = secR.sidebarCvar;
        sec.sidebarCount = (int32_t)(secR.sidebarEnd - secR.sidebarStart);
        sec.sidebars = (sec.sidebarCount > 0) ? (st.sidebars.data() + secR.sidebarStart) : nullptr;
        st.sections.push_back(sec);
    }

    st.menu.version = 1;
    st.menu.sectionCount = (int32_t)st.sections.size();
    st.menu.sections = st.sections.empty() ? nullptr : st.sections.data();

    st.exported = true;
    return &st.menu;
}

} // namespace ComboMenuExport

#endif // COMBO_MENU_EXPORT_H
```

- [ ] **Step 2: Build and run the tests**

```powershell
cmake --build build-lus-tests --target lus_tests --config Debug
& build-lus-tests\tests\Debug\lus_tests.exe --gtest_filter=ComboMenuExport.*
```

Expected: build succeeds, all 6 `ComboMenuExport.*` tests **PASS**. (If the exe path differs, locate it with `Get-ChildItem build-lus-tests -Recurse -Filter lus_tests.exe`.)

- [ ] **Step 3: Commit**

```powershell
git add combo/menu/ComboMenuExport.h libultraship/tests/combo_menu_export_tests.cpp libultraship/tests/CMakeLists.txt
git commit -m "feat(combo): combo-owned generic CwMenu serializer template + unit tests"
```

---

### Task 3: Adopt the serializer in SoH

Replace `SohMenu.cpp`'s hand-rolled walker (lines ~234-494) with a Policy + one-line delegate. Keep `WidgetTypeToCwKind` (OOT enum), `InvokeCallbackByIndex`, `EvalDisabledByIndex`, `DrawCustomByIndex` (game-specific guards/Init).

**Files:**
- Modify: `soh/soh/SohGui/SohMenu.h:8-9, 70-78` (storage swap)
- Modify: `soh/soh/SohGui/SohMenu.cpp:232-494` (policy + delegate replace walker), `:496-562` (rename members)

- [ ] **Step 1: Swap the backing storage in `SohMenu.h`**

Replace the includes `#include "ComboMenuABI.h"` and `#include <deque>` (lines 8-9) with:

```cpp
#include "ComboMenuExport.h"
```

Replace the private storage block (lines 70-78, from `bool mExported = false;` through `std::deque<std::string> mOwnedStrings; ...`) with:

```cpp
    // ComboShip menu-export backing storage (combo-owned serializer; see ComboMenuExport.h).
    // Lives as long as this SohMenu instance so C-ABI pointers stay valid for process life.
    ComboMenuExport::State<WidgetInfo> mComboExport;
```

Keep the comment block above it (lines 67-69) — fold it into the new comment as shown. The four public method declarations (lines 57-60) are unchanged.

- [ ] **Step 2: Replace the walker in `SohMenu.cpp`**

Keep the existing anonymous-namespace `WidgetTypeToCwKind` (lines 185-231) exactly as is. Immediately after it (still inside the anonymous namespace, before the closing `} // namespace` at line 232), add the policy:

```cpp
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
```

Then **delete** the entire `SohMenu::ExportComboMenu()` body (lines 234-494) and replace with:

```cpp
const CwMenu* SohMenu::ExportComboMenu() {
    // Walk + flatten live in the combo-owned serializer; this member just supplies the
    // protected menu tree and OOT's policy.
    return ComboMenuExport::Build<SohExportPolicy>(mComboExport, menuOrder, menuEntries);
}
```

- [ ] **Step 3: Rename storage members in the three by-index methods**

In `InvokeCallbackByIndex`, `EvalDisabledByIndex`, `DrawCustomByIndex` (former lines 496-562): replace every `mFlat` with `mComboExport.flat` and every `mFlatRando` with `mComboExport.flatRando`. No other changes — the Init() calls, foreground guards, and comments stay verbatim. Resulting bodies:

```cpp
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
```

(`EvalDisabledByIndex` / `DrawCustomByIndex`: same two-identifier rename; in `DrawCustomByIndex` the rando check becomes `bool isRando = (i >= 0 && i < (int32_t)mComboExport.flatRando.size() && mComboExport.flatRando[i]);`.)

- [ ] **Step 4: Build SoH**

```powershell
.\scripts\build-soh.ps1
```

Expected: builds with no errors.

- [ ] **Step 5: Commit**

```powershell
git add soh/soh/SohGui/SohMenu.h soh/soh/SohGui/SohMenu.cpp
git commit -m "refactor(soh): menu export delegates to combo-owned serializer (policy only stays in soh)"
```

---

### Task 4: Adopt the serializer in MM

Same operation on `BenMenu.cpp` (lines ~2326-2613). Keep MM's `WidgetTypeToCwKind`; the `ComboboxChoiceCount` helper becomes the policy's `CountChoices`.

**Files:**
- Modify: `mm/2s2h/BenGui/BenMenu.h:9-10, 46-54` (storage swap)
- Modify: `mm/2s2h/BenGui/BenMenu.cpp:2326-2613` (policy + delegate), `:2615-2674` (rename members)

- [ ] **Step 1: Swap the backing storage in `BenMenu.h`**

Replace `#include "ComboMenuABI.h"` and `#include <deque>` (lines 9-10) with:

```cpp
#include "ComboMenuExport.h"
```

Replace the private storage block (lines 46-54, `bool mExported = false;` through `mOwnedStrings`) with:

```cpp
    // ComboShip menu-export backing storage (combo-owned serializer; see ComboMenuExport.h).
    // Lives as long as this BenMenu instance so C-ABI pointers stay valid for process life.
    ComboMenuExport::State<WidgetInfo> mComboExport;
```

- [ ] **Step 2: Replace the walker in `BenMenu.cpp`**

Keep `WidgetTypeToCwKind` (lines 2280-2324). Replace the `ComboboxChoiceCount` helper (lines 2326-2344) and everything up to the end of `ExportComboMenu` (line 2613) with the policy + delegate. The policy goes inside the existing anonymous namespace (where `ComboboxChoiceCount` was):

```cpp
// Policy for the combo-owned serializer (combo/menu/ComboMenuExport.h): MM's enum mapping,
// comboVariant choices, and section naming. The walk itself lives in combo/. EXACT MM analog
// of SohMenu.cpp's SohExportPolicy.
struct BenExportPolicy {
    using Widget = WidgetInfo;
    static CwKind Kind(const Widget& w) {
        return WidgetTypeToCwKind(w.type);
    }
    static bool IsRandoSection(const std::string& label) {
        return label == "Rando";
    }
    static std::string Tooltip(const Widget& w) {
        // MM's tooltip is const char* (string-literal backed); copy for uniform lifetime
        // handling, matching the OOT emitter.
        return (w.options && w.options->tooltip) ? std::string(w.options->tooltip) : std::string();
    }
    // Number of CwChoice entries a widget contributes. MUST equal what EmitChoices pushes so
    // the serializer's reserve == fill and choices never reallocate. Only CW_COMBOBOX
    // contributes; comboVariant holds either a (map*) or a (vec*) — null contributes zero.
    static size_t CountChoices(const Widget& w) {
        if (WidgetTypeToCwKind(w.type) != CW_COMBOBOX || !w.options) {
            return 0;
        }
        auto o = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
        if (!o) {
            return 0;
        }
        if (o->comboVariant.index() == 0) {
            UIWidgets::ComboMap_t map = std::get<0>(o->comboVariant);
            return map ? map->size() : 0;
        } else {
            UIWidgets::ComboVec_t vec = std::get<1>(o->comboVariant);
            return vec ? vec->size() : 0;
        }
    }
    static void EmitChoices(const Widget& w, std::vector<CwChoice>& out) {
        if (WidgetTypeToCwKind(w.type) != CW_COMBOBOX || !w.options) {
            return;
        }
        auto o = std::static_pointer_cast<UIWidgets::ComboboxOptions>(w.options);
        if (!o) {
            return;
        }
        if (o->comboVariant.index() == 0) {
            UIWidgets::ComboMap_t map = std::get<0>(o->comboVariant);
            if (map) {
                for (auto& mp : *map) {
                    CwChoice choice = {};
                    choice.value = mp.first;
                    choice.label = mp.second ? mp.second : "";
                    out.push_back(choice);
                }
            }
        } else {
            UIWidgets::ComboVec_t vec = std::get<1>(o->comboVariant);
            if (vec) {
                for (size_t i = 0; i < vec->size(); i++) {
                    CwChoice choice = {};
                    choice.value = (int32_t)i;
                    choice.label = (*vec)[i] ? (*vec)[i] : "";
                    out.push_back(choice);
                }
            }
        }
    }
    static void FillOptions(const Widget& w, CwWidget& cw) {
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
        // MM has no ColorPickerOptions; derive useAlpha straight from the WidgetType.
        if (cw.kind == CW_COLOR) {
            cw.useAlpha = (w.type == WIDGET_COLOR_32) ? 1 : 0;
        }
    }
};
```

And the delegate (replacing `BenMenu::ExportComboMenu()` lines 2347-2613):

```cpp
const CwMenu* BenMenu::ExportComboMenu() {
    // Walk + flatten live in the combo-owned serializer; this member just supplies the
    // protected menu tree and MM's policy.
    return ComboMenuExport::Build<BenExportPolicy>(mComboExport, menuOrder, menuEntries);
}
```

- [ ] **Step 3: Rename storage members in the three by-index methods**

In `InvokeCallbackByIndex` / `EvalDisabledByIndex` / `DrawCustomByIndex` (lines 2615-2674): `mFlat` → `mComboExport.flat`, `mFlatRando` → `mComboExport.flatRando`. Everything else (no Init() in MM's invoke, the foreground guards, `disabledMap` loop, `disabledTooltip` without `.c_str()`) stays verbatim.

- [ ] **Step 4: Build 2Ship**

```powershell
.\scripts\build-2ship.ps1
```

Expected: builds with no errors.

- [ ] **Step 5: Commit**

```powershell
git add mm/2s2h/BenGui/BenMenu.h mm/2s2h/BenGui/BenMenu.cpp
git commit -m "refactor(mm): menu export delegates to combo-owned serializer (policy only stays in mm)"
```

---

### Task 5: Create `combo/menu/ComboMenuDrawContent.h` and adopt in SoH

The shared `DrawContent` body. ImGui layout code has no practical unit-test seam in this repo, so verification is compile + the runtime smoke in Task 8. This is TU-glue (like an `.inl`): it must be included from the game's `Menu.cpp` *after* ImGui internals and the game's UIWidgets are in scope — documented in the header.

**Files:**
- Create: `combo/menu/ComboMenuDrawContent.h`
- Modify: `soh/soh/SohGui/Menu.cpp:952-1186` (replace body with hooks + wrapper)

- [ ] **Step 1: Write the shared header**

```cpp
/* combo/menu/ComboMenuDrawContent.h
 * ComboShip-owned shared body for the games' Menu::DrawContent — renders one game's menu
 * (header row / sidebar / content columns, mirroring the games' upstream DrawElement layout)
 * inside comboui's already-open window, with onlyPaths/skipPaths filtering. Was previously
 * copy-pasted in soh/soh/SohGui/Menu.cpp and mm/2s2h/BenGui/Menu.cpp.
 *
 * TU-GLUE HEADER: include from the game's Menu.cpp AFTER imgui_internal.h (GImGui, ImVec2
 * math operators), libultraship (CVar bridge, Ship::Context) and the game's UIWidgets are
 * in scope. Not standalone.
 *
 * Per-game Hooks contract (static members; resolves each game's own UIWidgets/draw functions):
 *   struct Hooks {
 *       static bool HeaderEntry(const std::string& label);   // ModernMenuHeaderEntry
 *       static bool SidebarEntry(const std::string& label);  // ModernMenuSidebarEntry
 *       static void PushStyleButton(TTheme theme);           // UIWidgets::PushStyleButton
 *       static void PopStyleButton();                        // UIWidgets::PopStyleButton
 *       static void DrawItem(Widget& w, int labelWidth, TTheme theme); // MenuDrawItem
 *       static void RunUpdateFuncs(const std::string& header, const std::string& section);
 *   };
 */
#ifndef COMBO_MENU_DRAW_CONTENT_H
#define COMBO_MENU_DRAW_CONTENT_H

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace ComboMenuDraw {

struct Config {
    const char* idPrefix;      // "SOH" / "MM" — prefix for ImGui child-window IDs
    const char* headerCvar;    // CVar storing the active header label
    const char* defaultHeader; // fallback header label (both games: "Settings")
    ImFont* headerFont;        // pushed for the header row + sidebar (fontStandardLargest)
    ImFont* contentFont;       // pushed for the content columns (MM: fontMonoLarger); nullptr = none
};

// The caller (the game's Menu::DrawContent) has already done its per-frame setup: the
// foreground guard, the disabledMap evaluation pass, and theme/member updates.
template <typename THooks, typename TMenuEntries, typename TTheme>
inline void DrawContent(const Config& cfg, TTheme themeIndex, const std::vector<std::string>& menuOrder,
                        TMenuEntries& menuEntries, const std::set<std::string>& onlyPaths,
                        const std::set<std::string>& skipPaths) {
    // onlyPaths non-empty → allow-list mode; empty → skip-list mode.
    auto headerVisible = [&](const std::string& h) -> bool {
        if (!menuEntries.count(h)) {
            return false;
        }
        if (!onlyPaths.empty()) {
            // Header shown iff onlyPaths contains "H" or any "H/..." entry.
            if (onlyPaths.count(h)) {
                return true;
            }
            const std::string prefix = h + "/";
            for (auto& p : onlyPaths) {
                if (p.rfind(prefix, 0) == 0) {
                    return true;
                }
            }
            return false;
        }
        return skipPaths.count(h) == 0;
    };

    auto sidebarVisible = [&](const std::string& h, const std::string& s) -> bool {
        if (!onlyPaths.empty()) {
            // If onlyPaths has bare "H" with no "H/..." entries, show all sidebars.
            bool hasSidebarEntries = false;
            const std::string prefix = h + "/";
            for (auto& p : onlyPaths) {
                if (p.rfind(prefix, 0) == 0) {
                    hasSidebarEntries = true;
                    break;
                }
            }
            if (!hasSidebarEntries) {
                return true; // bare "H" in onlyPaths → all sidebars
            }
            return onlyPaths.count(h + "/" + s) > 0;
        }
        return skipPaths.count(h + "/" + s) == 0;
    };

    std::string headerIndex = CVarGetString(cfg.headerCvar, cfg.defaultHeader);

    // Build visible header list; fall back to first if stored header is filtered out.
    std::vector<std::string> visibleHeaders;
    for (auto& label : menuOrder) {
        if (headerVisible(label)) {
            visibleHeaders.push_back(label);
        }
    }
    if (visibleHeaders.empty()) {
        ImGui::TextUnformatted("No settings.");
        return;
    }
    if (std::find(visibleHeaders.begin(), visibleHeaders.end(), headerIndex) == visibleHeaders.end()) {
        headerIndex = visibleHeaders.front();
        CVarSetString(cfg.headerCvar, headerIndex.c_str());
    }

    // -----------------------------------------------------------------------
    // Themed rendering — mirrors the games' DrawElement layout without the
    // Begin/End wrapper (comboui's outer window is already open around us),
    // search bar, or quit/reset/close buttons.
    // -----------------------------------------------------------------------
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImGuiStyle& style = ImGui::GetStyle();

    // Use available content area for sizing (DrawElement sizes off viewport WorkSize).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float availW = avail.x > 0 ? avail.x : 800.0f;
    float availH = avail.y > 0 ? avail.y : 600.0f;

    ImGui::PushFont(cfg.headerFont);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    // Compute header row height from text + padding (mirrors DrawElement).
    ImVec2 firstSize = ImGui::CalcTextSize(visibleHeaders.front().c_str());
    float headerHeight = firstSize.y + style.FramePadding.y * 2;

    std::string blockId = std::string(cfg.idPrefix) + " Menu Block";
    std::string headerChildId = std::string(cfg.idPrefix) + " Header Selection";

    ImVec2 pos = window->DC.CursorPos;
    ImGui::SetNextWindowPos(pos);
    ImGui::BeginChild(blockId.c_str(), ImVec2(availW, availH),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

    // Header selection child (no close/quit buttons — comboui handles those).
    ImGui::SetNextWindowSizeConstraints({ 0, headerHeight }, { availW, headerHeight });
    ImGui::BeginChild(headerChildId.c_str(), ImVec2(availW, headerHeight),
                      ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_HorizontalScrollbar);
    for (size_t i = 0; i < visibleHeaders.size(); ++i) {
        if (i != 0) {
            ImGui::SameLine();
        }
        const std::string& label = visibleHeaders[i];
        // Defer the selection change to AFTER the Pop (mirrors DrawElement) — otherwise a click
        // flips headerIndex mid-iteration and the guarded PopStyleColor is skipped (ImGui assert).
        std::string nextHeader = "";
        THooks::PushStyleButton(themeIndex);
        if (headerIndex != label) {
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        }
        if (THooks::HeaderEntry(menuEntries.at(label).label)) {
            nextHeader = label;
            CVarSetString(cfg.headerCvar, label.c_str());
            CVarSave();
        }
        if (headerIndex != label) {
            ImGui::PopStyleColor();
        }
        THooks::PopStyleButton();
        if (!nextHeader.empty()) {
            headerIndex = nextHeader;
        }
    }
    ImGui::EndChild();

    // Divider line below header row (mirrors DrawElement).
    {
        ImGuiWindow* blk = g.CurrentWindow;
        ImVec2 lp = blk->DC.CursorPos;
        blk->DrawList->AddRectFilled(lp, lp + ImVec2{ availW, 4 }, ImGui::GetColorU32({ 255, 255, 255, 255 }), true,
                                     style.WindowRounding);
        ImGui::Dummy(ImVec2(availW, 4));
    }

    // Sidebar + content area layout (mirrors DrawElement).
    float sectionHeight = availH - headerHeight - 4 - style.ItemSpacing.y * 2;
    float columnHeight = sectionHeight - style.ItemSpacing.y * 4;
    float sidebarWidth = 200 - style.ItemSpacing.x;
    if (availW > 1600) {
        sidebarWidth = availW * 0.15f;
    }

    auto& entry = menuEntries.at(headerIndex);
    const char* rawSidebarCvar = entry.sidebarCvar;
    std::string sidebarCvarStr = rawSidebarCvar ? rawSidebarCvar : "";
    std::string sectionIndex = sidebarCvarStr.empty() ? "" : CVarGetString(sidebarCvarStr.c_str(), "");

    // Build visible sidebars list for active header.
    std::vector<std::string> visibleSidebars;
    for (auto& s : entry.sidebarOrder) {
        if (sidebarVisible(headerIndex, s)) {
            visibleSidebars.push_back(s);
        }
    }
    if (visibleSidebars.empty()) {
        ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        return;
    }
    if (std::find(visibleSidebars.begin(), visibleSidebars.end(), sectionIndex) == visibleSidebars.end()) {
        sectionIndex = visibleSidebars.front();
        if (!sidebarCvarStr.empty()) {
            CVarSetString(sidebarCvarStr.c_str(), sectionIndex.c_str());
        }
    }

    ImGui::SetNextWindowSizeConstraints({ sidebarWidth, 0 }, { sidebarWidth, columnHeight });
    ImGui::BeginChild((entry.label + " Section").c_str(), { sidebarWidth, columnHeight * 3 },
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize, ImGuiWindowFlags_NoTitleBar);
    for (auto& sidebarLabel : visibleSidebars) {
        // Defer the selection change to AFTER the Pop (mirrors DrawElement) — otherwise a click
        // flips sectionIndex mid-iteration and the guarded PopStyleColor is skipped (ImGui assert).
        std::string nextSection = "";
        THooks::PushStyleButton(themeIndex);
        if (sectionIndex != sidebarLabel) {
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        }
        if (THooks::SidebarEntry(sidebarLabel)) {
            nextSection = sidebarLabel;
            if (!sidebarCvarStr.empty()) {
                CVarSetString(sidebarCvarStr.c_str(), sidebarLabel.c_str());
            }
            CVarSave();
        }
        if (sectionIndex != sidebarLabel) {
            ImGui::PopStyleColor();
        }
        THooks::PopStyleButton();
        if (!nextSection.empty()) {
            sectionIndex = nextSection;
        }
    }
    ImGui::EndChild();
    ImGui::PopFont(); // pop headerFont — content area uses content/default font (matches DrawElement)

    if (cfg.contentFont) {
        ImGui::PushFont(cfg.contentFont); // MM: fontMonoLarger for content (mirrors MM DrawElement)
    }

    // Vertical divider between sidebar and content columns.
    {
        ImGuiWindow* blk = g.CurrentWindow;
        ImVec2 dvPos = blk->DC.CursorPos;
        blk->DrawList->AddRectFilled(dvPos, dvPos + ImVec2{ 4, sectionHeight - style.FramePadding.y * 2 },
                                     ImGui::GetColorU32({ 255, 255, 255, 255 }), true, style.WindowRounding);
    }

    // Content columns (mirrors DrawElement).
    float sectionWidth = availW - sidebarWidth - 4 - style.ItemSpacing.x * 4;
    std::string sectionMenuId = sectionIndex + " Settings";
    auto& sb = entry.sidebars.at(sectionIndex);
    int columns = (int)sb.columnCount;
    size_t columnFuncs = sb.columnWidgets.size();
    if (availW < 800) {
        columns = 1;
    }
    float columnWidth = (sectionWidth - style.ItemSpacing.x * columns) / columns;
    bool useColumns = columns > 1;

    // Run per-section update funcs before drawing widgets, as DrawElement does.
    THooks::RunUpdateFuncs(entry.label, sectionIndex);

    ImGui::SameLine();
    if (!useColumns) {
        ImGui::SetNextWindowSizeConstraints({ sectionWidth, 0 }, { sectionWidth, columnHeight });
        ImGui::BeginChild(sectionMenuId.c_str(), { sectionWidth, availH * 4 }, ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoTitleBar);
    }
    for (size_t i = 0; i < columnFuncs; i++) {
        std::string sectionId = sectionMenuId + " Column " + std::to_string(i);
        if (useColumns) {
            ImGui::SetNextWindowSizeConstraints({ columnWidth, 0 }, { columnWidth, columnHeight });
            ImGui::BeginChild(sectionId.c_str(), { columnWidth, availH * 4 }, ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoTitleBar);
        }
        for (auto& w : sb.columnWidgets.at(i)) {
            THooks::DrawItem(w, 90 / std::max<int>(columns, 1), themeIndex);
        }
        if (useColumns) {
            ImGui::EndChild();
        }
        if (i < (size_t)columns - 1) {
            ImGui::SameLine();
        }
    }
    if (!useColumns) {
        ImGui::EndChild();
    }

    if (cfg.contentFont) {
        ImGui::PopFont(); // pop contentFont
    }
    ImGui::PopStyleVar(); // FramePadding
    ImGui::EndChild();    // Menu Block
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

} // namespace ComboMenuDraw

#endif // COMBO_MENU_DRAW_CONTENT_H
```

- [ ] **Step 2: Adopt in `soh/soh/SohGui/Menu.cpp`**

Add the include after the file's existing includes (top of file):

```cpp
#include "ComboMenuDrawContent.h" // ComboShip: shared DrawContent body (combo-owned)
```

Replace the entire `Menu::DrawContent` body (lines 952-1186) with:

```cpp
// ComboShip: glue for the combo-owned shared DrawContent body (combo/menu/ComboMenuDrawContent.h).
// Resolves OOT's own UIWidgets / draw functions for the template.
namespace {
struct SohDrawHooks {
    static bool HeaderEntry(const std::string& label) {
        return ModernMenuHeaderEntry(label);
    }
    static bool SidebarEntry(const std::string& label) {
        return ModernMenuSidebarEntry(label);
    }
    static void PushStyleButton(UIWidgets::Colors theme) {
        UIWidgets::PushStyleButton(theme);
    }
    static void PopStyleButton() {
        UIWidgets::PopStyleButton();
    }
    static void DrawItem(WidgetInfo& w, int labelWidth, UIWidgets::Colors theme) {
        MenuDrawItem(w, labelWidth, theme);
    }
    static void RunUpdateFuncs(const std::string& header, const std::string& section) {
        if (MenuInit::GetUpdateFuncs().contains(header)) {
            if (MenuInit::GetUpdateFuncs()[header].contains(section)) {
                for (auto& updateFunc : MenuInit::GetUpdateFuncs()[header][section]) {
                    updateFunc();
                }
            }
        }
    }
};
} // namespace

void Menu::DrawContent(const std::set<std::string>& onlyPaths, const std::set<std::string>& skipPaths) {
    // ComboShip: guard — same as DrawElement.
    if (OTRGlobals::Instance->fontStandardLargest == nullptr) return;

    // ComboShip: per-frame setup — replicate what DrawElement does at its top since
    // DrawElement itself never runs in combo (comboui owns the menu).
    for (auto& [reason, info] : disabledMap) {
        info.active = info.evaluation(info);
    }
    raceDisableActive = CVarGetInteger(CVAR_SETTING("DisableChanges"), 0);
    menuThemeIndex = static_cast<UIWidgets::Colors>(CVarGetInteger(CVAR_SETTING("Menu.Theme"), defaultThemeIndex));

    ComboMenuDraw::Config cfg = {};
    cfg.idPrefix = "SOH";
    cfg.headerCvar = CVAR_SETTING("Menu.ActiveHeader");
    cfg.defaultHeader = "Settings";
    cfg.headerFont = OTRGlobals::Instance->fontStandardLargest;
    cfg.contentFont = nullptr; // OOT content area uses the default font (matches DrawElement)
    ComboMenuDraw::DrawContent<SohDrawHooks>(cfg, menuThemeIndex, menuOrder, menuEntries, onlyPaths, skipPaths);
}
```

(If the compiler reports `ModernMenuHeaderEntry`/`MenuDrawItem` not declared at the hooks' location, move the hooks struct + `DrawContent` to *after* those functions' definitions in the file — the old body at line 952 was already after them, so replacing in place preserves ordering.)

- [ ] **Step 3: Build SoH**

```powershell
.\scripts\build-soh.ps1
```

Expected: builds with no errors.

- [ ] **Step 4: Commit**

```powershell
git add combo/menu/ComboMenuDrawContent.h soh/soh/SohGui/Menu.cpp
git commit -m "refactor(soh): DrawContent delegates to combo-owned shared renderer"
```

---

### Task 6: Adopt the shared DrawContent in MM

**Files:**
- Modify: `mm/2s2h/BenGui/Menu.cpp:899-1129` (replace body with hooks + wrapper)

- [ ] **Step 1: Adopt in `mm/2s2h/BenGui/Menu.cpp`**

Add the include after the file's existing includes:

```cpp
#include "ComboMenuDrawContent.h" // ComboShip: shared DrawContent body (combo-owned)
```

Replace the entire `Menu::DrawContent` body (lines 899-1129) with:

```cpp
// ComboShip: glue for the combo-owned shared DrawContent body (combo/menu/ComboMenuDrawContent.h).
// Resolves MM's own UIWidgets / draw functions for the template.
namespace {
struct BenDrawHooks {
    static bool HeaderEntry(const std::string& label) {
        return ModernMenuHeaderEntry(label);
    }
    static bool SidebarEntry(const std::string& label) {
        return ModernMenuSidebarEntry(label);
    }
    static void PushStyleButton(UIWidgets::Colors theme) {
        UIWidgets::PushStyleButton(theme);
    }
    static void PopStyleButton() {
        UIWidgets::PopStyleButton();
    }
    static void DrawItem(WidgetInfo& w, int labelWidth, UIWidgets::Colors theme) {
        MenuDrawItem(w, labelWidth, theme);
    }
    static void RunUpdateFuncs(const std::string& header, const std::string& section) {
        if (MenuInit::GetUpdateFuncs().contains(header)) {
            if (MenuInit::GetUpdateFuncs()[header].contains(section)) {
                for (auto& updateFunc : MenuInit::GetUpdateFuncs()[header][section]) {
                    updateFunc();
                }
            }
        }
    }
};
} // namespace

void Menu::DrawContent(const std::set<std::string>& onlyPaths, const std::set<std::string>& skipPaths) {
    // ComboShip: guard — same as DrawElement.
    if (OTRGlobals::Instance->fontStandardLargest == nullptr) return;

    // ComboShip: per-frame setup — replicate what DrawElement does at its top since
    // DrawElement itself never runs in combo (comboui owns the menu).
    for (auto& [reason, info] : disabledMap) {
        info.active = info.evaluation(info);
    }
    menuThemeIndex = static_cast<UIWidgets::Colors>(CVarGetInteger("gSettings.Menu.Theme", defaultThemeIndex));

    ComboMenuDraw::Config cfg = {};
    cfg.idPrefix = "MM";
    cfg.headerCvar = "gSettings.Menu.ActiveHeader";
    cfg.defaultHeader = "Settings";
    cfg.headerFont = OTRGlobals::Instance->fontStandardLargest;
    cfg.contentFont = OTRGlobals::Instance->fontMonoLarger; // mirrors MM DrawElement's content font
    ComboMenuDraw::DrawContent<BenDrawHooks>(cfg, menuThemeIndex, menuOrder, menuEntries, onlyPaths, skipPaths);
}
```

(Note: the old MM body's extra `GetVectorIndexOf(entry.sidebarOrder, sectionIndex)` check is intentionally dropped — see "Known intentional micro-differences" #2 in the header of this plan.)

- [ ] **Step 2: Build 2Ship**

```powershell
.\scripts\build-2ship.ps1
```

Expected: builds with no errors.

- [ ] **Step 3: Commit**

```powershell
git add mm/2s2h/BenGui/Menu.cpp
git commit -m "refactor(mm): DrawContent delegates to combo-owned shared renderer"
```

---

### Task 7: Shared ImGui-context helper

`ComboMenu_UseSharedImGuiContext()` is duplicated verbatim in `soh/soh/OTRGlobals.cpp:2586-2591` and `mm/2s2h/BenPort.cpp:3110-3115`, plus two more inline copies inside `SOH_MenuDrawCustom` (OTRGlobals.cpp:2613-2616) and `MM_MenuDrawCustom` (BenPort.cpp:3140-3143).

**Files:**
- Create: `combo/menu/ComboMenuSharedContext.h`
- Modify: `soh/soh/OTRGlobals.cpp:2586-2616` (delete static helper, use shared one)
- Modify: `mm/2s2h/BenPort.cpp:3110-3143` (same)

- [ ] **Step 1: Write the header**

```cpp
/* combo/menu/ComboMenuSharedContext.h
 * ComboShip-owned helper, compiled into each game DLL. Each DLL has its own per-module ImGui
 * GImGui; when a game is backgrounded it isn't current, so any cross-DLL export that can reach
 * an ImGui call (menu build, callbacks, disable eval, custom draw) must point it at the shared
 * libultraship context first.
 *
 * TU-GLUE HEADER: include from the game's port file AFTER libultraship.h / ImGui are in scope.
 */
#ifndef COMBO_MENU_SHARED_CONTEXT_H
#define COMBO_MENU_SHARED_CONTEXT_H

namespace ComboMenuGlue {

inline void UseSharedImGuiContext() {
    auto ctx = Ship::Context::GetInstance();
    if (ctx && ctx->GetWindow() && ctx->GetWindow()->GetGui()) {
        ImGui::SetCurrentContext(ctx->GetWindow()->GetGui()->GetImGuiContext());
    }
}

} // namespace ComboMenuGlue

#endif // COMBO_MENU_SHARED_CONTEXT_H
```

- [ ] **Step 2: Adopt in `soh/soh/OTRGlobals.cpp`**

Add near the other ComboShip includes in the file:

```cpp
#include "ComboMenuSharedContext.h" // ComboShip: shared per-DLL ImGui context helper (combo-owned)
```

Delete the `static void ComboMenu_UseSharedImGuiContext() { ... }` definition (lines 2586-2591, keep the explanatory comment above it pointing at the header instead), and replace all call sites `ComboMenu_UseSharedImGuiContext();` with `ComboMenuGlue::UseSharedImGuiContext();`. In `SOH_MenuDrawCustom` (lines 2613-2616), replace the four inline lines (`auto ctx = ...` through the closing `}`) with `ComboMenuGlue::UseSharedImGuiContext();` (keep the comment).

- [ ] **Step 3: Adopt in `mm/2s2h/BenPort.cpp`**

Same operation: add the include, delete the static helper (lines 3110-3115), replace call sites with `ComboMenuGlue::UseSharedImGuiContext();`, and replace the inline copy in `MM_MenuDrawCustom` (lines 3140-3143).

- [ ] **Step 4: Build both games**

```powershell
.\scripts\build-soh.ps1
.\scripts\build-2ship.ps1
```

Expected: both build with no errors.

- [ ] **Step 5: Commit**

```powershell
git add combo/menu/ComboMenuSharedContext.h soh/soh/OTRGlobals.cpp mm/2s2h/BenPort.cpp
git commit -m "refactor: shared ImGui-context helper moves to combo-owned header"
```

---

### Task 8: Full verification + docs

- [ ] **Step 1: Rebuild everything that changed**

```powershell
.\scripts\build-libultraship.ps1
.\scripts\build-soh.ps1
.\scripts\build-2ship.ps1
.\scripts\build-comboui.ps1
.\scripts\build-comboship.ps1
```

Expected: all green. Also re-run the serializer tests:

```powershell
cmake --build build-lus-tests --target lus_tests --config Debug
& build-lus-tests\tests\Debug\lus_tests.exe --gtest_filter=ComboMenuExport.*
```

Expected: all PASS.

- [ ] **Step 2: Runtime smoke (launch ComboShip — needs a human or `verify` run)**

Checklist (this exercises every moved code path):
1. Launch ComboShip; open the menu. Shared tab hub renders.
2. OOT tab: headers + sidebars render (shared DrawContent, SoH side); switch headers and sidebars; toggle a checkbox; open a combobox and confirm its choices are populated (serializer choice wiring); a disabled widget shows its disable reason (EvalDisabled path).
3. MM tab while MM is backgrounded: declarative widgets render; custom widgets show the "Available while Majora's Mask is the active game." placeholder; Rando widgets are editable (flatRando exemption).
4. Transition to MM; MM tab now renders live custom widgets; content area uses the mono font (contentFont config).
5. Rando: open the Randomizer section on both games; Generate flow still works.

If anything regresses, `git log --oneline` pinpoints which extraction commit to bisect.

- [ ] **Step 3: Update docs**

Per the project's documentation rule, update `docs/UPSTREAM_MERGES.md`: in the section describing menu-related deviations in vendored code, note that the serializer walk, DrawContent body, and ImGui-context helper now live in combo-owned headers (`combo/menu/ComboMenuExport.h`, `ComboMenuDrawContent.h`, `ComboMenuSharedContext.h`) and that the vendored files retain only policy/hooks glue — so future upstream pulls only ever conflict on the small glue blocks, not the algorithm.

- [ ] **Step 4: Commit**

```powershell
git add docs/UPSTREAM_MERGES.md
git commit -m "docs: record combo-owned menu extraction in upstream-merge notes"
```

---

## Expected outcome

| Area | Before (vendored lines) | After |
|---|---|---|
| `soh/` serializer (`SohMenu.cpp/.h`) | ~415 | ~205 (kind map + policy + by-index methods) |
| `soh/` DrawContent (`Menu.cpp`) | ~237 | ~60 (hooks + wrapper) |
| `mm/` serializer (`BenMenu.cpp/.h`) | ~420 | ~230 |
| `mm/` DrawContent (`Menu.cpp`) | ~237 | ~60 |
| ImGui-context helpers (both) | ~30 | ~6 |
| **Total vendored** | **~1,340 (of 1,630)** | **~560 (of ~850)** |

The walk/layout algorithms — the parts most likely to need future fixes — become single-source and combo-owned. What stays vendored is irreducible glue: enum mappings over game-private types, C-ABI export stubs, foreground guards, and rando domain logic.
