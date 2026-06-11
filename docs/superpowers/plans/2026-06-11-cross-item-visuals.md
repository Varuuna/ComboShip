# Cross-Game Item Visuals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Foreign (cross-game) items show real names in toasts and OOT shops, and — pending a spike — render the other game's actual model instead of the sentinel.

**Architecture:** Three increments per the approved spec (`docs/superpowers/specs/2026-06-11-cross-item-visuals-design.md`). Inc 1: dumps emit `displayName`, combo stamps it into the foreign map at generate time. Inc 2: `BuildMerchantMessage` + check tracker substitute the foreign map's displayName for `RG_COMBO_FOREIGN`. Inc 3: spike the `@mm:`/`@oot:` resource-path routing + RM resolution stack in the shared Fast3D interpreter, then (gate) generalize via per-game `*_GetItemDrawInfo` C-ABI exports.

**Tech Stack:** C/C++20, MSVC, nlohmann::json, shared libultraship Fast3D interpreter, GetProcAddress C-ABI between game DLLs.

**Build/verify commands** (from repo root; long timeouts, builds take minutes):
- `.\scripts\build-soh.ps1` / `.\scripts\build-2ship.ps1` / `.\scripts\build-comboship.ps1` / `.\scripts\build-libultraship.ps1`
- Runtime artifacts to inspect after a Generate: `x64\Debug\saves\combo\slot0.spoiler.json`, `slot0.foreign.json`
- Stale-PCH error C1853 → delete the named .pch, re-run. The game writes saves/logs under `x64\Debug\`.

**Project rules:** every vendored-file edit carries a `// ComboShip:` comment; libultraship/interpreter changes get a `docs/UPSTREAM_MERGES.md` entry (Task 11). MM has TWO item-name tables: `Items[].spoilerName` (`RI_*`, used for grants — NEVER change what's emitted as `name`) and `Items[].name` (human). Same trap class already bit once — see UPSTREAM_MERGES.md.

**Testing reality:** no unit harness covers the game DLLs or the combo exe's fill path. Verification is build + scripted inspection of generated JSON (exact commands given) + human in-game checks at increment ends. The lus_tests gtest suite must stay green (`cmake --build build/x64 --target lus_tests --config Debug; & build\x64\libultraship\tests\Debug\lus_tests.exe`).

---

## Increment 1 — displayName end to end

### Task 1: Dumps emit displayName

**Files:**
- Modify: `mm/2s2h/BenPort.cpp` (items loop in `MM_DumpRandoStaticData`, search `items.push_back`)
- Modify: `soh/soh/OTRGlobals.cpp:2759-2765` (items loop in `SOH_DumpRandoStaticData`)

- [ ] **Step 1: MM items emit displayName**

In `MM_DumpRandoStaticData` (BenPort.cpp), the items loop currently reads:

```cpp
    for (auto& [id, item] : Rando::StaticData::Items) {
        if (!item.spoilerName || item.spoilerName[0] == '\0') continue;
        items.push_back({ {"name", item.spoilerName} });
    }
```

Replace with:

```cpp
    for (auto& [id, item] : Rando::StaticData::Items) {
        if (!item.spoilerName || item.spoilerName[0] == '\0') continue;
        // ComboShip: "name" MUST stay spoilerName (RI_*) — grant lookup keys on it. "displayName"
        // is the human string (StaticData's unused .name field) for toasts/shops in the OTHER game.
        nlohmann::json entry = { {"name", item.spoilerName} };
        if (item.name && item.name[0] != '\0') {
            entry["displayName"] = item.name;
        }
        items.push_back(std::move(entry));
    }
```

(`RandoStaticItem.name` exists: `mm/2s2h/Rando/StaticData/StaticData.h:42`.)

- [ ] **Step 2: OOT items emit displayName**

In `SOH_DumpRandoStaticData` (OTRGlobals.cpp:2760-2765), replace:

```cpp
    for (int rg = 0; rg < RG_MAX; ++rg) {
        Rando::Item& item = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(rg));
        const std::string& name = item.GetName().GetEnglish();
        if (name.empty()) continue;
        items.push_back({ {"name", name} });
    }
```

with:

```cpp
    for (int rg = 0; rg < RG_MAX; ++rg) {
        Rando::Item& item = Rando::StaticData::RetrieveItem(static_cast<RandomizerGet>(rg));
        const std::string& name = item.GetName().GetEnglish();
        if (name.empty()) continue;
        // ComboShip: OOT item names are already human English; displayName == name keeps the
        // dump schema symmetric with MM's (which needs the distinction: RI_* vs human).
        items.push_back({ {"name", name}, {"displayName", name} });
    }
```

- [ ] **Step 3: Build both games**

Run: `.\scripts\build-soh.ps1` then `.\scripts\build-2ship.ps1` — Expected: zero errors.

- [ ] **Step 4: Commit**

```powershell
git add mm/2s2h/BenPort.cpp soh/soh/OTRGlobals.cpp
git commit -m "feat(combo): rando dumps emit human displayName per item"
```

### Task 2: Combo stamps displayName into foreign markers

**Files:**
- Modify: `combo/ComboShip.cpp` (`RunComboFill`, the block at ~lines 237-249 between `nlohmann::json::parse(spoiler)` and `WriteForeignFromAnnotations`)

- [ ] **Step 1: Build name→displayName maps and stamp the foreign array**

Current code (ComboShip.cpp:237-239):

```cpp
        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());
        ComboRando::WriteForeignFromAnnotations(kCanonicalSlot, foreignArr);
```

Replace with:

```cpp
        auto j = nlohmann::json::parse(spoiler);
        auto foreignArr = j.value("foreign", nlohmann::json::array());

        // ComboShip: resolve human display names for foreign items from the dumps' items arrays
        // (each entry: {name, displayName}). The fill only carries itemName (the grant key:
        // English for OOT, RI_* for MM); displayName feeds toasts/shop text in the check's game.
        auto buildNameMap = [](const std::string& dump) {
            std::unordered_map<std::string, std::string> m;
            try {
                auto d = nlohmann::json::parse(dump);
                for (auto& it : d.value("items", nlohmann::json::array())) {
                    std::string n = it.value("name", "");
                    std::string dn = it.value("displayName", "");
                    if (!n.empty() && !dn.empty()) m.emplace(std::move(n), std::move(dn));
                }
            } catch (...) {}
            return m;
        };
        auto ootNames = buildNameMap(sohDump);
        auto mmNames  = buildNameMap(mmDump);
        for (auto& fm : foreignArr) {
            std::string itemGame = fm.value("itemGame", "");
            std::string itemName = fm.value("itemName", "");
            const auto& names = (itemGame == "mm") ? mmNames : ootNames;
            auto it = names.find(itemName);
            if (it != names.end()) {
                fm["displayName"] = it->second;
            }
        }

        ComboRando::WriteForeignFromAnnotations(kCanonicalSlot, foreignArr);
```

`WriteForeignFromAnnotations` already persists `displayName` with an itemName fallback (CrossForeign.h:59); toast sites already prefer `displayName` (hook_handlers.cpp:2791, CheckQueue.cpp:44 and both send sites) — no further changes.

Check `#include <unordered_map>` is present in ComboShip.cpp (add if missing).

- [ ] **Step 2: Build the launcher**

Run: `.\scripts\build-comboship.ps1` — Expected: zero errors.

- [ ] **Step 3: Verify on generated output**

Have the user (or controller, if a prior seed exists to regenerate) run a Generate, then:

```powershell
$f = Get-Content x64\Debug\saves\combo\slot0.foreign.json -Raw | ConvertFrom-Json
@($f.oot.PSObject.Properties) | Select-Object -First 5 | ForEach-Object { "$($_.Name) -> '$($_.Value.displayName)'" }
@($f.oot.PSObject.Properties | Where-Object { $_.Value.displayName -match '^RI_' }).Count
```

Expected: human display names ("Magic Jar (Small)" class, not `RI_*`); the second count is **0**.

- [ ] **Step 4: Commit**

```powershell
git add combo/ComboShip.cpp
git commit -m "feat(combo): stamp human displayName into foreign map at generate time"
```

---

## Increment 2 — Real names in OOT shops and trackers

### Task 3: Expose OOT_LookupForeign + shop message substitution

**Files:**
- Modify: `soh/soh/Enhancements/randomizer/hook_handlers.cpp` (~line 363, `OOT_LookupForeign`)
- Modify: `soh/soh/Enhancements/randomizer/hook_handlers.h` (declaration)
- Modify: `soh/soh/Enhancements/randomizer/Messages/MerchantMessages.cpp:27-49` (`BuildMerchantMessage`)

- [ ] **Step 1: De-static OOT_LookupForeign and declare it**

In hook_handlers.cpp, change

```cpp
static const ComboRando::ForeignItem* OOT_LookupForeign(int slot, const std::string& checkName) {
```

to

```cpp
// ComboShip: also used by MerchantMessages/check tracker to show the real foreign item name.
const ComboRando::ForeignItem* OOT_LookupForeign(int slot, const std::string& checkName) {
```

In hook_handlers.h add (inside the existing include guard; match the file's existing COMBO_BUILD usage if present, otherwise wrap):

```cpp
#ifdef COMBO_BUILD
#include "combo/rando/CrossForeign.h"
// ComboShip: per-slot foreign-item lookup (defined in hook_handlers.cpp).
const ComboRando::ForeignItem* OOT_LookupForeign(int slot, const std::string& checkName);
#endif
```

If `combo/rando` is not on soh's include path, use the same relative include form hook_handlers.cpp itself uses for CrossForeign.h (check its include line and copy it).

- [ ] **Step 2: Substitute in BuildMerchantMessage**

In MerchantMessages.cpp, current final branch (lines 39-46):

```cpp
    } else {
        const Rando::Item& item = Rando::StaticData::RetrieveItem(rgid);
        if (Rando::StaticData::GetLocation(rc)->IsShop()) {
            itemName = CustomMessage(Rando::StaticData::RetrieveItem(rgid).GetName());
        } else {
            itemName = item.GetHint().GetHintMessage();
        }
    }
```

Replace with:

```cpp
    } else {
#ifdef COMBO_BUILD
        // ComboShip: a shop/merchant slot holding RG_COMBO_FOREIGN actually sells an MM item.
        // Show the real item's name from the foreign map instead of "Combo Foreign Item".
        if (rgid == RG_COMBO_FOREIGN) {
            const ComboRando::ForeignItem* fi =
                OOT_LookupForeign(gSaveContext.fileNum, Rando::StaticData::GetLocation(rc)->GetName());
            if (fi != nullptr && !fi->displayName.empty()) {
                itemName = CustomMessage(Text{ fi->displayName.c_str(), fi->displayName.c_str(),
                                               fi->displayName.c_str() });
                color = "%g";
                msg.Replace("[[color]]", color);
                msg.InsertNames({ itemName, CustomMessage(std::to_string(location->GetPrice())) });
                return;
            }
        }
#endif
        const Rando::Item& item = Rando::StaticData::RetrieveItem(rgid);
        if (Rando::StaticData::GetLocation(rc)->IsShop()) {
            itemName = CustomMessage(Rando::StaticData::RetrieveItem(rgid).GetName());
        } else {
            itemName = item.GetHint().GetHintMessage();
        }
    }
```

Add the needed includes at the top of MerchantMessages.cpp (mirror hook_handlers.cpp's forms): the hook_handlers.h (or a direct extern declaration) and `soh/soh/Enhancements/randomizer/hook_handlers.h`. If `Text{...}` doesn't accept `const char*` triples directly, use `Text{ std::string(fi->displayName), std::string(fi->displayName), std::string(fi->displayName) }` — check Text's constructors in `soh/soh/Enhancements/randomizer/text.h` and use whichever compiles.

NOTE the early-return duplicates the tail (`Replace` + `InsertNames`) — keep the duplication; restructuring the vanilla tail would grow the upstream diff.

- [ ] **Step 3: Build soh** — `.\scripts\build-soh.ps1`, zero errors.

- [ ] **Step 4: Commit**

```powershell
git add soh/soh/Enhancements/randomizer/hook_handlers.cpp soh/soh/Enhancements/randomizer/hook_handlers.h soh/soh/Enhancements/randomizer/Messages/MerchantMessages.cpp
git commit -m "feat(soh): shops show the real MM item name for foreign slots"
```

### Task 4: Check tracker substitution (OOT)

**Files:**
- Modify: `soh/soh/Enhancements/randomizer/randomizer_check_tracker.cpp:2070-2080`

- [ ] **Step 1: Substitute in the tracker's revealItemName path**

Current code (~2073):

```cpp
    } else if (revealItemName) {
        txt = itemLoc->GetPlacedItem().GetName().GetForLanguage(gSaveContext.language);
    }
```

Replace with:

```cpp
    } else if (revealItemName) {
        txt = itemLoc->GetPlacedItem().GetName().GetForLanguage(gSaveContext.language);
#ifdef COMBO_BUILD
        // ComboShip: reveal the real MM item behind the foreign sentinel.
        if (itemLoc->GetPlacedRandomizerGet() == RG_COMBO_FOREIGN) {
            const ComboRando::ForeignItem* fi = OOT_LookupForeign(
                gSaveContext.fileNum, Rando::StaticData::GetLocation(itemLoc->GetRandomizerCheck())->GetName());
            if (fi != nullptr && !fi->displayName.empty()) {
                txt = fi->displayName;
            }
        }
#endif
    }
```

Add the hook_handlers.h include. If `itemLoc->GetRandomizerCheck()` doesn't exist, the enclosing code has the `RandomizerCheck` in scope (this function iterates checks) — use the local variable the surrounding lines use for `itemLoc` lookup (read the enclosing ~30 lines and reuse it); report the substitution in the task report.

- [ ] **Step 2: Audit the remaining "Combo Foreign Item" surfaces**

The spec lists item-tracker tooltips (`randomizer_item_tracker.cpp:815/1289/1311/1365`) and trap
display (`Traps.cpp:1771`) as secondary surfaces. Reasoned expectation: these display OWNED
inventory items, and foreign items are never granted to OOT (they divert to the mailbox), so
they should be unreachable for RG_COMBO_FOREIGN. VERIFY that reasoning: read each site briefly;
if any can display a placed-but-unowned item (like the check tracker does), apply the same
OOT_LookupForeign substitution there (same pattern as Step 1). Report the verdict per site.

- [ ] **Step 3: Build soh** — zero errors. **Step 4: Commit**

```powershell
git add soh/soh/Enhancements/randomizer/randomizer_check_tracker.cpp
git commit -m "feat(soh): check tracker reveals real foreign item names"
```
(include any Step-2 files actually edited)

### Task 5: MM-side name surfaces (verify, fix if visible)

**Files:**
- Inspect: `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp`, `mm/2s2h/Rando/StaticData/Items.cpp` (RI_COMBO_FOREIGN entry), any Tingle/shop name path
- Modify only what the inspection shows is player-visible.

- [ ] **Step 1: Find what MM shows for RI_COMBO_FOREIGN**

```powershell
# Where is the sentinel item defined and what is its human name field?
# (it was added in Inc6 near the enum terminator)
```
Run: `Grep pattern="RI_COMBO_FOREIGN" path="mm/2s2h" -n` and read the `Items.cpp` entry (what's in its `.name`?) plus every display site (check tracker item reveal, shop item naming for RCTYPE_SHOP/TINGLE_SHOP — search `Items[` + `.name` usages in mm/2s2h/Rando that drive UI text).

- [ ] **Step 2: For each player-visible site, substitute via the foreign map**

MM already loads its foreign map in CheckQueue.cpp (`g_mmForeignMap`, keyed by `StaticData::Checks[].name` — the RC_* id, NOT CheckNames). Expose a lookup mirroring OOT's:

In `mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp`, after the `g_mmForeignMap` cache block (~line 60), add:

```cpp
#ifdef COMBO_BUILD
// ComboShip: lookup for UI surfaces (tracker/shops) that want the real OOT item name behind
// RI_COMBO_FOREIGN. Returns nullptr when the check isn't foreign. Keyed by Checks[].name (RC_*).
const ComboRando::ForeignItem* MM_LookupForeign(RandoCheckId rc) {
    int slot = gSaveContext.fileNum;
    if (slot != g_mmForeignSlot) {
        g_mmForeignMap = ComboRando::LoadForeignForGame(slot, ComboRando::GAME_MM);
        g_mmForeignSlot = slot;
    }
    auto it = g_mmForeignMap.find(Rando::StaticData::Checks[rc].name);
    return it != g_mmForeignMap.end() ? &it->second : nullptr;
}
#endif
```

Declare it in `mm/2s2h/Rando/MiscBehavior/MiscBehavior.h` inside the existing `#ifdef COMBO_BUILD` block:

```cpp
const ComboRando::ForeignItem* MM_LookupForeign(RandoCheckId rc);
```

(add `#include "combo/rando/CrossForeign.h"` in the form CheckQueue.cpp uses). Then at each visible site found in Step 1, where the item name for a check whose `RANDO_SAVE_CHECKS[rc].randoItemId == RI_COMBO_FOREIGN` is displayed, substitute `MM_LookupForeign(rc)->displayName` when non-null. Show each edit in the task report.

If Step 1 finds NO player-visible surface (plausible — MM tracker may not reveal item names), report that with evidence and skip the edits; the MM_LookupForeign helper is then NOT added (YAGNI).

- [ ] **Step 3: Build 2ship** — zero errors. **Step 4: Commit** (message reflects what was found/changed).

**Increment 1+2 human checkpoint:** regenerate + new save; confirm: toasts show human names both directions; an OOT shop slot with an MM item shows its real name (browse + buy dialog); check tracker reveal shows real names.

---

## Increment 3 — Cross-game model rendering

### Task 6: Named-RM registry in libultraship

**Files:**
- Create: `libultraship/include/ship/resource/CrossRMRegistry.h`
- Modify: `soh/soh/OTRGlobals.cpp` (after OOT's RM exists / is (re)activated — the spot where `sOOTResourceManager` is stored), `mm/2s2h/BenPort.cpp` (where `sMMResourceManager` is stored, ~line 154 area)

- [ ] **Step 1: Write the registry (header-only, tiny)**

```cpp
// libultraship/include/ship/resource/CrossRMRegistry.h
// ComboShip: process-wide registry of per-game ResourceManagers so the Fast3D interpreter can
// route "@<game>:"-prefixed resource paths to the owning game's RM (cross-game item rendering).
// Games register once at boot; pointers are shared_ptr so lifetime is safe even mid-frame.
#pragma once
#include <memory>
#include <string>
#include <unordered_map>

namespace Ship {
class ResourceManager;

class CrossRMRegistry {
  public:
    static void Register(const std::string& name, std::shared_ptr<ResourceManager> rm) {
        Map()[name] = std::move(rm);
    }
    static std::shared_ptr<ResourceManager> Get(const std::string& name) {
        auto it = Map().find(name);
        return it != Map().end() ? it->second : nullptr;
    }

  private:
    static std::unordered_map<std::string, std::shared_ptr<ResourceManager>>& Map() {
        static std::unordered_map<std::string, std::shared_ptr<ResourceManager>> sMap;
        return sMap;
    }
};
} // namespace Ship
```

- [ ] **Step 2: Register both RMs**

soh: at the point OOT's resident RM is stored (search OTRGlobals.cpp for `sOOTResourceManager =`), add directly after, guarded like its surroundings:

```cpp
    Ship::CrossRMRegistry::Register("oot", sOOTResourceManager); // ComboShip: cross-game rendering
```

mm: after `sMMResourceManager` is stored (BenPort.cpp ~154 area):

```cpp
    Ship::CrossRMRegistry::Register("mm", sMMResourceManager); // ComboShip: cross-game rendering
```

Add `#include "ship/resource/CrossRMRegistry.h"` (or `<ship/resource/CrossRMRegistry.h>` matching neighbors) in both files. If either file stores the RM in more than one place, register at each store site (idempotent map assignment).

- [ ] **Step 3: Build libultraship + both games** — zero errors. **Step 4: Commit**

```powershell
git add libultraship/include/ship/resource/CrossRMRegistry.h libultraship/src/ship/resource/CrossRMRegistry.cpp soh/soh/OTRGlobals.cpp mm/2s2h/BenPort.cpp
git commit -m "feat(lus): named ResourceManager registry for cross-game resource routing"
```

### Task 7: Interpreter routing + resolution stack (SPIKE part 1)

This task is exploratory by design — the anchors below are verified; exact integration follows what the code allows. Document every deviation in the task report.

**Files:**
- Modify: `libultraship/src/fast/interpreter.cpp` (handlers at :3553 `gfx_dl_otr_filepath_handler_custom`, :3962 `gfx_set_timg_otr_filepath_handler_custom`, :3546 vtx filepath, the `G_SETTIMG_OTR_HASH` handler at ~:3905-3915, and every other `Ship::Context::GetInstance()->GetResourceManager()` use in this file — enumerate with grep)

- [ ] **Step 1: Add the override-RM state + helper**

Near the interpreter's other file-scope state (it already has `g_exec_stack`), add:

```cpp
// ComboShip: cross-game resource routing. A "@<game>:" marker after "__OTR__" in a DL path
// routes that DL — and everything it references (textures/vtx/sub-DLs, by path OR hash) — to the
// named game's ResourceManager. The override is scoped to the routed DL via the exec stack depth.
struct CrossRMOverride {
    std::shared_ptr<Ship::ResourceManager> rm;
    size_t execDepthAtPush;
};
static std::vector<CrossRMOverride> g_crossRMStack;

static std::shared_ptr<Ship::ResourceManager> ActiveResMgr() {
    if (!g_crossRMStack.empty()) {
        return g_crossRMStack.back().rm;
    }
    return Ship::Context::GetInstance()->GetResourceManager();
}
```

Include `ship/resource/CrossRMRegistry.h`.

- [ ] **Step 2: Route every resolution site through ActiveResMgr()**

Grep interpreter.cpp for `Context::GetInstance()->GetResourceManager()` and replace each use with `ActiveResMgr()` (verified sites: :3546 vtx, :3557 DL, :3913-3915 timg-hash incl. its `GetArchiveManager()->HashToCString(hash)`, :3970 timg-filepath; expect a handful more — list them all in the report). The HASH variants matter: binary-extracted DLs reference textures/vertices by hash, resolved through the RM's ArchiveManager — routing must cover them or MM DL internals resolve against OOT archives.

- [ ] **Step 3: Recognize the marker and push/pop the override**

In `gfx_dl_otr_filepath_handler_custom` (:3553), before resolution:

```cpp
    char* fileName = (char*)cmd->words.w1;
    // ComboShip: "__OTR__@mm:objects/..." routes this DL to the named game's RM.
    std::shared_ptr<Ship::ResourceManager> routedRM = nullptr;
    static constexpr char kOtrPrefix[] = "__OTR__";
    if (strncmp(fileName, kOtrPrefix, sizeof(kOtrPrefix) - 1) == 0 &&
        fileName[sizeof(kOtrPrefix) - 1] == '@') {
        const char* gameStart = fileName + sizeof(kOtrPrefix) - 1 + 1; // past '@'
        const char* colon = strchr(gameStart, ':');
        if (colon != nullptr) {
            std::string gameName(gameStart, colon - gameStart);
            routedRM = Ship::CrossRMRegistry::Get(gameName);
            // Rebuild the real path: "__OTR__" + path-after-colon. The routed string was built
            // by the foreign draw helper and is stable storage; use a local std::string for the
            // stripped path and resolve with c_str() within this call.
        }
    }
```

Then: if `routedRM != nullptr`, resolve the stripped path via `routedRM->GetResourceRawPointer(strippedPath.c_str())` and, when taking the `g_exec_stack.call(...)` branch, push `{ routedRM, <current exec stack depth> }` onto `g_crossRMStack`. Pop in the DL-return path: find where `g_exec_stack` unwinds (the `G_ENDDL` handler / `ret`-equivalent in this file) and pop `g_crossRMStack` entries whose `execDepthAtPush >= ` the post-return depth. Inspect the `g_exec_stack` struct definition (same file) to get exact depth accessors — document what you find.

CAUTION: if the stripped-path resolution must outlive this call (the interpreter may re-walk commands), heap-interning the stripped string (static `std::unordered_set<std::string>` of seen paths) is acceptable — note the choice.

- [ ] **Step 4: Build libultraship + run lus_tests + boot the game**

`.\scripts\build-libultraship.ps1`, then both games + comboship (interpreter is shared — everything links it). Run lus_tests (all green incl. pre-existing failure unchanged). Launch ComboShip briefly (use the pattern: `Start-Process x64\Debug\ComboShip.exe -WorkingDirectory x64\Debug`, wait ~30s, kill) — confirm the game boots and renders normally (no prefix in use yet ⇒ zero behavior change expected).

- [ ] **Step 5: Commit**

```powershell
git add libultraship/src/fast/interpreter.cpp
git commit -m "feat(lus): @game: resource-path routing with scoped RM resolution stack (spike)"
```

### Task 8: SPIKE part 2 — one MM item rendered in OOT (DECISION GATE)

**Files:**
- Modify: `soh/soh/Enhancements/randomizer/item_list.cpp:465` area (give RG_COMBO_FOREIGN a custom draw func — pattern: line 458 `itemTable[RG_ROCS_FEATHER].SetCustomDrawFunc(Randomizer_DrawRocsFeather);`)
- Modify: the file defining `Randomizer_DrawRocsFeather` (grep soh for it — likely `soh/soh/Enhancements/randomizer/draw.cpp`) — add `Randomizer_DrawComboForeign` next to it.

- [ ] **Step 1: Pick the spike asset**

Find an MM get-item DL path: grep `mm/assets/objects` for a mask or heart-piece gGi DL header (e.g. `Get-ChildItem mm\assets\objects -Recurse -Filter "*.h" | Select-String "gGi.*MaskDL|gGiHeart"` ). Record the exact `__OTR__objects/...` string and the MM drawFunc layout for that item (mm/src/code/z_draw.c sDrawItemTable entry: which dlists, OPA/XLU).

- [ ] **Step 2: Write the spike draw func**

Model it on `Randomizer_DrawRocsFeather`'s structure (matrix setup + gSPDisplayList). Spike version, hardcoded:

```cpp
// ComboShip SPIKE: prove cross-RM rendering — draws a hardcoded MM item model via the
// "@mm:" routed path. Generalized in the next task; remove the hardcoding then.
extern "C" void Randomizer_DrawComboForeign(PlayState* play, GetItemEntry* getItemEntry) {
    static const char sMMItemDL[] = "__OTR__@mm:objects/<RECORDED_PATH_FROM_STEP_1>";
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    MATRIX_FINALIZE_AND_LOAD(POLY_OPA_DISP++, play->state.gfxCtx);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sMMItemDL);
    CLOSE_DISPS(play->state.gfxCtx);
}
```

(`<RECORDED_PATH_FROM_STEP_1>` is filled with the actual recorded path during execution — it is a spike parameter, not a placeholder for the engineer to invent. Copy the exact OPEN/SETUP/MATRIX macro usage from Randomizer_DrawRocsFeather — names above are indicative; the neighboring function is authoritative.) Register: `itemTable[RG_COMBO_FOREIGN].SetCustomDrawFunc(Randomizer_DrawComboForeign);` after line 465.

- [ ] **Step 3: Build + human verification (GATE)**

Build soh + libultraship. User launches with a foreign-bearing seed, opens a shop with a foreign slot (shop shelves render GetItemEntry models) or finds a foreign freestanding item. **Gate criteria:** (a) the MM model renders (geometry + textures, not garbage); (b) OOT's OWN items in the same scene still render correctly (texture-cache collision check — same-named assets from both games rendered in one frame); (c) no crash on scene load/exit.

- If PASS → continue to Task 9. Commit: `git commit -m "spike(soh): MM item model rendered in OOT via @mm: routing"`.
- If FAIL on texture-cache aliasing: investigate the interpreter's texture cache keying (grep the texture cache map in interpreter.cpp/gfx code; if keyed by path string, the inner MM paths collide with OOT's — extend routing to decorate the cache key with the RM identity). One fix attempt; if structural → STOP, report, and the controller switches Increment 3 to the lookalike-mapping fallback (spec's documented bail-out — separate plan addendum).
- If FAIL otherwise (resolution misses, exec-stack pop bugs): debug within the spike scope; after 3 failed root-cause attempts STOP per debugging discipline and report.

### Task 9 (GATED on Task 8 PASS): Generalize OOT←MM

**Files:**
- Create: `combo/menu/ComboItemDrawABI.h` (C-ABI POD, alongside the existing ComboMenuABI.h pattern)
- Modify: `mm/src/code/z_draw.c` (accessor for sDrawItemTable entry), `mm/2s2h/BenPort.cpp` (`MM_GetItemDrawInfo` export)
- Modify: soh `Randomizer_DrawComboForeign` (de-hardcode; per-check lookup + cached draw info)

- [ ] **Step 1: Define the ABI**

```c
/* combo/menu/ComboItemDrawABI.h — ComboShip: cross-game item draw info (C ABI, POD only).
 * The owning game returns its sDrawItemTable data for one item so the OTHER game can render
 * the model via "@<game>:"-routed resource paths. Strings point into the owning game's static
 * storage (path literals from asset headers — process-lifetime). */
#ifndef COMBO_ITEM_DRAW_ABI_H
#define COMBO_ITEM_DRAW_ABI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CW_DRAW_MAX_DLISTS 8
typedef struct {
    const char* dlists[CW_DRAW_MAX_DLISTS]; /* OTR path strings, NULL-terminated list */
    int32_t     dlistCount;
    int32_t     xluStartIndex; /* dlists[0..xluStart-1] are OPA layers, rest XLU; -1 = all OPA */
} CwItemDrawInfo;
/* Returns 1 and fills out on success; 0 if the item is unknown/undrawable. */
typedef int32_t (*Fn_GetItemDrawInfo)(const char* itemName, CwItemDrawInfo* out);
#ifdef __cplusplus
}
#endif
#endif
```

- [ ] **Step 2: MM export**

`mm/src/code/z_draw.c`: add a `// ComboShip:` accessor returning the table row (the table is file-static):

```c
#ifdef COMBO_BUILD
// ComboShip: expose one draw-table row for cross-game rendering (see ComboItemDrawABI.h).
s32 GetItem_GetDrawTableEntry(s32 drawId, Gfx** outDlists, s32 maxDlists) {
    if (drawId < 0 || drawId >= ARRAY_COUNT(sDrawItemTable)) return 0;
    s32 n = 0;
    for (; n < maxDlists && sDrawItemTable[drawId].dlists[n] != NULL; n++) {
        outDlists[n] = sDrawItemTable[drawId].dlists[n];
    }
    return n;
}
#endif
```

(Adjust to the actual `DrawItemTableEntry` field layout in mm z_draw.c — read it first; if dlists array length is fixed at fewer than 8 entries, clamp.) BenPort.cpp export:

```cpp
#include "combo/menu/ComboItemDrawABI.h" // with the other COMBO_BUILD includes
extern "C" s32 GetItem_GetDrawTableEntry(s32 drawId, Gfx** outDlists, s32 maxDlists);

extern "C" __declspec(dllexport) int32_t MM_GetItemDrawInfo(const char* itemName, CwItemDrawInfo* out) {
    if (!itemName || !out) return 0;
    RandoItemId id = Rando::GetItemIdFromName(itemName); // spoilerName lookup (RI_*)
    if (id == RI_UNKNOWN) return 0;
    GetItemDrawId drawId = Rando::StaticData::Items[id].drawId;
    Gfx* dls[CW_DRAW_MAX_DLISTS] = {};
    int32_t n = GetItem_GetDrawTableEntry((s32)drawId, dls, CW_DRAW_MAX_DLISTS);
    if (n <= 0) return 0;
    out->dlistCount = n;
    out->xluStartIndex = -1; // spike scope: draw all layers OPA; refine if visuals demand it
    for (int32_t i = 0; i < n; i++) out->dlists[i] = (const char*)dls[i]; // path strings
    return 1;
}
```

(`GetItemIdFromName` exists: StaticData.h:53. Verify it keys spoilerName; if it keys `.name`, iterate `Items` comparing `spoilerName` like Combo_MM_Rando_SetOwnedItems does at BenPort.cpp:3034-3038.)

- [ ] **Step 3: De-hardcode the OOT draw func**

`Randomizer_DrawComboForeign`: resolve once per check, cache by checkName:
1. `fi = OOT_LookupForeign(gSaveContext.fileNum, checkName)` — obtaining the checkName from the draw context is the hard part: `GetItemEntry` doesn't carry the check. Investigate what context the custom draw funcs receive (read 2-3 existing Randomizer_Draw* funcs + their call site in z_player/get-item code); if the check is unavailable at draw time, fall back to caching keyed by the currently-queued check (`randomizerQueuedCheck` extern in hook_handlers.cpp) for get-item draws, and for shop shelves use the EnGirlA actor's check (it knows its RandomizerCheck). Document what works; shop-shelf coverage may land as a follow-up edit in EnGirlA's draw path.
2. `MM_GetItemDrawInfo` via `GetProcAddress(GetModuleHandleA("2ship.dll"), "MM_GetItemDrawInfo")` (cache the fn ptr; comboui's ComboMenuModel.cpp:14-36 is the pattern).
3. Build routed strings `"__OTR__@mm:" + (path + strlen("__OTR__"))` interned in a static `std::unordered_map<std::string, std::string>` keyed by original path; submit each via `gSPDisplayList`.
4. Any step fails → draw the sentinel (call the default GID_RUPEE_BLUE draw) — never blank.

- [ ] **Step 4: Build all, human verify multiple MM item types in OOT (chest get-item, freestanding, shop shelf), commit**

```powershell
git add combo/menu/ComboItemDrawABI.h mm/src/code/z_draw.c mm/2s2h/BenPort.cpp soh/soh/Enhancements/randomizer/... # actual files
git commit -m "feat: OOT renders real MM item models for foreign checks"
```

### Task 10 (GATED on Task 8 PASS): Generalize MM←OOT

**Files:**
- Modify: `soh/src/code/z_draw.c` (same accessor pattern as Task 9 Step 2, `// ComboShip:` guarded), `soh/soh/OTRGlobals.cpp` (`SOH_GetItemDrawInfo` export: itemName is the OOT ENGLISH name → find RG via the same `itemNameToEnum` map `SOH_ApplyRandoPlacements` uses (OTRGlobals.cpp:2777+ — read it), then `RetrieveItem(rg)` → GID → `sDrawItemTable` row)
- Modify: `mm/2s2h/Rando/DrawItem.cpp` — `Rando::DrawItem` (line ~494): add an `RI_COMBO_FOREIGN` case before the default: `MM_LookupForeign(randoCheckId)` (Task 5 helper — add it now if Task 5 skipped it) → `SOH_GetItemDrawInfo` via `GetProcAddress(GetModuleHandleA("soh.dll"), ...)` → routed `"@oot:"` paths → submit; fallback: current default branch.

Steps mirror Task 9 exactly (ABI is shared; only the name-lookup differs). Build all; human verify an OOT item model rendering in MM (grass/pot drop or check item). Commit: `git commit -m "feat: MM renders real OOT item models for foreign checks"`.

### Task 11: Documentation + cleanup

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md`

- [ ] **Step 1: Document** the interpreter routing (mechanism, why `ActiveResMgr()` replaced direct Context lookups, the `@game:` marker grammar, the exec-stack-scoped override), the z_draw.c accessors, and the SetCustomDrawFunc registration — each with the WHY, per the project documentation rule. If the spike FAILED and the lookalike fallback was taken instead, document that outcome and the reason.
- [ ] **Step 2: Sweep** for leftover spike hardcoding (grep `SPIKE` in soh/ mm/ libultraship/) — remove or convert to comments referencing this plan.
- [ ] **Step 3: Run lus_tests** (green), build all five targets (green).
- [ ] **Step 4: Commit**

```powershell
git add docs/UPSTREAM_MERGES.md
git commit -m "docs: cross-game item rendering mechanism + upstream-merge notes"
```

---

## Execution notes for the controller

- Tasks 1-2 then 3-5 are independent of Inc 3; land them first — they're pure wins even if the spike fails.
- Tasks 7-8 are ONE spike: same engineer/subagent context if possible; the Task 8 gate decides Tasks 9-10.
- Human verification points: after Task 5 (names everywhere), after Task 8 (gate), after Tasks 9/10 (model rendering each direction).
- 2ship's spdlog goes nowhere in combo — any MM-side diagnostics during the spike must be file-writes (`saves/combo/debug-*.json` pattern) or routed through soh-side logging.

---

## Increment 3b — Animated foreign items (added 2026-06-11; see spec "Increment 3b")

### Task 12: Interpreter bracket commands G_COMBO_RM_PUSH / G_COMBO_RM_POP

**Files:**
- Modify: `libultraship/include/libultraship/libultra/gbi.h` (two opcodes from the free 0x2A-0x30
  range + g-form emit macros), `libultraship/include/fast/lus_gbi.h` (OTR_ opcode constants),
  `libultraship/src/fast/interpreter.cpp` (two handlers + dispatch registration; read how existing
  custom handlers register).

Semantics: PUSH carries a game-name string in w1 (interned literal, e.g. "mm"); handler does
`CrossRMRegistry::Get(name)` and pushes `{rm, BRACKET_SENTINEL}` onto `g_crossRMStack`, where
`BRACKET_SENTINEL = SIZE_MAX` so the ENDDL pop loop (`cmd_stack.size() <= execDepthAtPush` — always
false for SIZE_MAX) NEVER pops it. POP handler pops the top entry IFF it is a bracket entry
(sentinel depth); log + ignore otherwise (unbalanced). Unknown game in PUSH: log-once + push
NOTHING, and POP tolerates the imbalance via a per-frame bracket counter (track pushes that were
skipped so POP skips symmetrically — simplest: a small `static int g_skippedBracketPushes` that POP
decrements first). Frame-start `g_crossRMStack.clear()` (already present) covers leaks; reset the
skip counter there too.

Steps: implement; build lus + all targets; lus_tests green (436/437 incl. pre-existing failure);
boot-smoke unchanged (no brackets emitted yet); commit `feat(lus): cross-RM bracket commands for
raw-pointer DL spans`.

### Task 13: ABI animated variant + MM stray-fairy description + combo-owned host draw + soh seam

**Files:**
- Modify: `combo/menu/ComboItemDrawABI.h` — add:
```c
typedef struct {
    const char* skelPath;     /* FlexSkeleton resource (OTR path) */
    const char* animPath;     /* Animation resource */
    const char* texAnimPath;  /* AnimatedMaterial resource, or NULL */
    float       scale;        /* model scale (stray fairy: 0.03f) */
    int32_t     billboard;    /* 1 = face camera (Matrix_ReplaceRotation on billboard mtx) */
    int32_t     xlu;          /* 1 = draw on XLU layer with 25Xlu setup */
    int32_t     limbCount;    /* skeleton limb count (jointTable sizing) */
    int32_t     hiddenLimb;   /* limb index to null out (stray fairy: right-facing head), -1 none */
} CwItemAnimDrawInfo;
typedef int32_t (*Fn_GetItemAnimDrawInfo)(const char* itemName, CwItemAnimDrawInfo* out);
```
- Modify: `mm/2s2h/BenPort.cpp` — `MM_GetItemAnimDrawInfo` export: stray-fairy RI_* ids map to
  { gStrayFairySkel path, gStrayFairyFlyingAnim path, per-area gStrayFairy*TexAnim path, 0.03f,
  billboard=1, xlu=1, STRAY_FAIRY_LIMB_MAX, STRAY_FAIRY_LIMB_RIGHT_FACING_HEAD }. Path strings come
  from mm/assets headers (they ARE the path literals); limb constants from the EnElforg overlay
  header (grep STRAY_FAIRY_LIMB_). Items not in the animated class → return 0.
- Create: `combo/render/ComboForeignAnim.h` — TU-glue header (menu-extraction pattern: included by
  the HOST game's draw TU; no own engine includes; documents its include-order contract). Contents:
  - AnimatedMat handler subset ported from mm/src/code/z_scene_proc.c:59-414 (ONLY the types the
    stray-fairy texanims use — inspect the resources/types first; likely TexCycle and/or ColorLerp;
    port exactly those handlers + the dispatcher, parameterized on (gfxCtx, frames, segmentBase)).
    The AnimatedMaterial struct is 8-byte pure data — define a local mirror struct.
  - `ComboForeignAnim_Draw(const CwItemAnimDrawInfo* info, const char* game /*"mm"*/,
     PlayState-ish host params...)`: loads skel/anim/texanim ONCE per item via
    `Ship::CrossRMRegistry::Get(game)->LoadResource(<path minus __OTR__>)` (check the exact
    LoadResource/GetResourceDataByName API + path form — the registry RM handle is a plain
    ResourceManager; hold the shared_ptrs in a static cache so resources stay alive), then drives
    the HOST's SkelAnime: host functions are visible because this header is included in the host
    TU (SkelAnime_InitFlex/Update/DrawFlex, Matrix_*, OPEN_DISPS — same pattern as the host's own
    draw funcs). Per-item static SkelAnime state keyed by itemName (mirror MM's
    DrawStrayFairy:64-78 single-instance approach incl. its caveats comment).
  - Brackets: emit G_COMBO_RM_PUSH("mm") before the texanim+skeletal submission, POP after
    (host-side emit macros from Task 12).
  - Segment hygiene: after the draw, restore segments the texanim touched — inspect what the
    ported handlers write (likely one segment) and re-set it to the host's expected value or NULL
    (verify what OOT expects: read OOT Play_Draw segment setup; document the choice).
  - Failure ladder: any load/lookup failure → return 0 so the caller falls back (static-DL path →
    sentinel).
- Modify: `soh/soh/Enhancements/randomizer/draw.cpp` — in the foreign draw: when
  `MM_GetItemDrawInfo` returns 0, try `MM_GetItemAnimDrawInfo` (GetProcAddress, cached); on
  success call `ComboForeignAnim_Draw(...)`; on failure sentinel as today. Include the TU-glue
  header. (~10-15 lines.)

Steps: read-first (texanim resource type + handler set used by the fairy; EnElforg limb constants;
CrossRMRegistry load API), implement, build mm+soh+lus chain, boot-smoke, commit
`feat: animated cross-game item rendering (MM stray fairies in OOT)`.

### Task 14: Gate + docs
Human gate: a foreign OOT check/shop slot holding a stray fairy renders MM's animated fairy
(billboarded, animated wings, per-area coloring), OOT scene visuals unaffected after viewing
(segment check), no crash. Then fold Increment 3b into Task 11's UPSTREAM_MERGES.md entry
(brackets + AnimatedMat port note). Stray-fairy timing constant divergence acceptable if visible.
