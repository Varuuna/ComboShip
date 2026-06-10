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

TEST(ComboMenuExport, ChoicesStorageIsExactFitSoWiredPointersStayStable) {
    Fixture f;
    f.Build();
    // Pass-1 reserve was exact-fit: a Policy whose EmitChoices over-pushed would have
    // grown past the reservation and reallocated, dangling every wired choices pointer
    // (and tripping the asserts in Build in debug builds).
    EXPECT_EQ(f.state.choices.size(), f.state.choices.capacity());
}

} // namespace
