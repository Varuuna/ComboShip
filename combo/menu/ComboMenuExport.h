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
