# Cross-World Randomizer — Increment 1: Mailbox + Cross-Game Grant Plumbing

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the cross-game item delivery channel — a shared, crash-safe mailbox file plus a debug "send" trigger in each game and an automatic "receive & grant" drain — proving an item collected in one game can be granted in the other, before any generation work exists.

**Architecture:** A header-only mailbox (`combo/rando/CrossMailbox.h`) compiled into `soh.dll`, `2ship.dll`, and `ComboShip.exe`. All three read/write one combo-owned JSON file `saves/combo/slot{N}.mailbox.json` (keyed by the canonical OOT slot N; MM derives N = mmFileNum − 1) via `nlohmann::json` + `std::filesystem` with atomic temp-rename writes. SEND (this increment): a debug console command / menu button enqueues an entry. RECEIVE: each game, on its player-update hook, loads undelivered entries addressed to it, grants them, marks them delivered. No new DLL exports are required for the channel — receive is in-game file reads. (This supersedes the spec's "`*_DrainMailbox` exports" note; reconcile the spec in Task 0.)

**Tech Stack:** C++17, `nlohmann::json` (already vendored, used by both save systems), `std::filesystem`, libultraship `GameInteractor` hooks, CMake (VS 2022 generator, build dir `build/x64`).

**Reference:** Spec `docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-scope-a.md`. Memory `[[comboship-crossgame-randomizer]]`, `[[comboship-build-targets]]`, `[[comboship-hm64-principle]]`, `[[document-post-merge-changes]]`.

**Verification reality:** No unit-test harness exists for combo/game C++. Each task is verified by (a) the relevant target building clean, and (b) a documented in-game manual check + log/file inspection. Build targets individually — never rebuild everything (`[[comboship-build-targets]]`).

---

### Task 0: Reconcile spec + branch hygiene

**Files:**
- Modify: `docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-scope-a.md`

- [ ] **Step 1: Confirm branch**

Run: `git branch --show-current`
Expected: `randomizer`

- [ ] **Step 2: Update the spec's mailbox description to the header-only design**

In the spec, under "Architecture" item 2 and Increment 1, replace the "`*_DrainMailbox` exports" wording with: mailbox is a header-only module (`combo/rando/CrossMailbox.h`) compiled into all three modules; receive is an in-game file read on the player-update hook; no new exports needed for the channel in Increment 1. Leave Increments 2–3 (which DO add pool/placement exports) unchanged.

- [ ] **Step 3: Commit the spec reconciliation**

```bash
git add docs/superpowers/specs/2026-06-04-combo-crossworld-randomizer-scope-a.md
git commit -m "docs: reconcile cross-world rando spec to header-only mailbox for Increment 1"
```

---

### Task 1: The shared mailbox module (header-only)

**Files:**
- Create: `combo/rando/CrossMailbox.h`

This is the one source of truth for the mailbox format + file IO, compiled into all three modules. Keep it dependency-light: only `<nlohmann/json.hpp>`, `<filesystem>`, `<fstream>`, `<vector>`, `<string>`, `<cstdint>`.

- [ ] **Step 1: Write the header**

```cpp
// combo/rando/CrossMailbox.h
// ComboShip: cross-world randomizer mailbox — shared by soh.dll, 2ship.dll, ComboShip.exe.
// One JSON file per canonical OOT slot holds items collected for the OTHER game, not yet granted.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace ComboRando {

enum GameId : uint8_t { GAME_OOT = 0, GAME_MM = 1 };

struct MailboxEntry {
    GameId      srcGame;       // where it was collected
    GameId      dstGame;       // where it must be granted
    std::string itemName;      // item key in srcGame's namespace (RG_*/RI_* spoiler name)
    std::string displayName;   // human string for the "received" text
    std::string srcCheckName;  // provenance (debug/spoiler)
    bool        delivered;     // true once dstGame has granted it
};

inline void to_json(nlohmann::json& j, const MailboxEntry& e) {
    j = nlohmann::json{ {"srcGame", (int)e.srcGame}, {"dstGame", (int)e.dstGame},
                        {"itemName", e.itemName}, {"displayName", e.displayName},
                        {"srcCheckName", e.srcCheckName}, {"delivered", e.delivered} };
}
inline void from_json(const nlohmann::json& j, MailboxEntry& e) {
    e.srcGame      = (GameId)j.value("srcGame", 0);
    e.dstGame      = (GameId)j.value("dstGame", 0);
    e.itemName     = j.value("itemName", std::string{});
    e.displayName  = j.value("displayName", std::string{});
    e.srcCheckName = j.value("srcCheckName", std::string{});
    e.delivered    = j.value("delivered", false);
}

// Combo-owned, cwd-relative — all three modules share the process working directory.
inline std::filesystem::path MailboxPath(int canonicalSlot) {
    return std::filesystem::path("saves") / "combo" /
           ("slot" + std::to_string(canonicalSlot) + ".mailbox.json");
}

inline std::vector<MailboxEntry> LoadAll(int canonicalSlot) {
    std::vector<MailboxEntry> out;
    std::error_code ec;
    auto path = MailboxPath(canonicalSlot);
    if (!std::filesystem::exists(path, ec)) return out;
    std::ifstream in(path);
    if (!in.is_open()) return out;
    try {
        nlohmann::json j; in >> j;
        for (const auto& item : j.value("entries", nlohmann::json::array())) {
            out.push_back(item.get<MailboxEntry>());
        }
    } catch (...) { /* corrupt file -> treat as empty; never throw across the channel */ }
    return out;
}

inline bool WriteAll(int canonicalSlot, const std::vector<MailboxEntry>& entries) {
    std::error_code ec;
    auto path = MailboxPath(canonicalSlot);
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path; tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return false;
        nlohmann::json j;
        j["entries"] = entries;
        out << j.dump(2);
        if (!out.good()) return false;
    }
    std::filesystem::rename(tmp, path, ec);   // atomic on same volume
    return !ec;
}

inline void Enqueue(int canonicalSlot, const MailboxEntry& entry) {
    auto entries = LoadAll(canonicalSlot);
    entries.push_back(entry);
    WriteAll(canonicalSlot, entries);
}

// Returns undelivered entries addressed to dstGame (does not mutate the file).
inline std::vector<MailboxEntry> LoadPending(int canonicalSlot, GameId dstGame) {
    std::vector<MailboxEntry> pending;
    for (const auto& e : LoadAll(canonicalSlot)) {
        if (!e.delivered && e.dstGame == dstGame) pending.push_back(e);
    }
    return pending;
}

// Marks every undelivered entry addressed to dstGame as delivered, persists.
inline void MarkAllDelivered(int canonicalSlot, GameId dstGame) {
    auto entries = LoadAll(canonicalSlot);
    for (auto& e : entries) {
        if (!e.delivered && e.dstGame == dstGame) e.delivered = true;
    }
    WriteAll(canonicalSlot, entries);
}

} // namespace ComboRando
```

- [ ] **Step 2: Verify it compiles standalone**

Run (from repo root, adjust include dir to the vcpkg/installed json path used by the build — see `CMakeCache.txt` `nlohmann_json_DIR`):
```bash
g++ -std=c++17 -fsyntax-only -I <nlohmann_include_dir> combo/rando/CrossMailbox.h
```
Expected: no output (clean). If `nlohmann/json.hpp` isn't on a convenient path, skip this step — Task 3's combo build is the real compile check.

- [ ] **Step 3: Commit**

```bash
git add combo/rando/CrossMailbox.h
git commit -m "feat(combo-rando): add header-only cross-world mailbox module"
```

---

### Task 2: Compile the mailbox header into the combo layer

**Files:**
- Modify: `combo/CMakeLists.txt` (add `combo/rando` to the ComboShip target's include dirs if not already covered)
- Modify: `combo/ComboShip.cpp:12-16` (add the include + a startup log of any leftover mailbox)

Confirm the combo target sees the header and that all three modules will share the same relative path. The combo layer doesn't enqueue/receive in Increment 1, but including it here is the cheapest compile gate for the header.

- [ ] **Step 1: Inspect the combo CMake target**

Run: `git show HEAD:combo/CMakeLists.txt`
Identify the ComboShip executable target name and its `target_include_directories`. Confirm `nlohmann_json` is linkable from the combo target (it links libultraship which brings json). If json headers are not visible to the combo target, add `find_package(nlohmann_json CONFIG REQUIRED)` + `target_link_libraries(<ComboShipTarget> PRIVATE nlohmann_json::nlohmann_json)`.

- [ ] **Step 2: Include the header + log leftover mailbox on startup**

In `combo/ComboShip.cpp`, add after the existing includes (around line 16):
```cpp
#include "rando/CrossMailbox.h"
```
And in `main()`, right after the working-directory line (`std::string workDir = ...;`, ~line 144):
```cpp
    // ComboShip: surface any mailbox left from a previous session (debug aid; harmless if absent).
    {
        auto leftover = ComboRando::LoadAll(0);
        if (!leftover.empty()) {
            std::cout << "[ComboShip] mailbox slot0 has " << leftover.size()
                      << " entr" << (leftover.size() == 1 ? "y" : "ies") << " on startup\n";
        }
    }
```

- [ ] **Step 3: Build the combo target**

Run: `cmake --build build/x64 --target ComboShip --config Debug`
Expected: builds clean. (Resolves the real header compile + json visibility.)

- [ ] **Step 4: Commit**

```bash
git add combo/CMakeLists.txt combo/ComboShip.cpp
git commit -m "build(combo): compile CrossMailbox into ComboShip + startup mailbox log"
```

---

### Task 3: OOT — receive drain on player update

**Files:**
- Modify: `soh/soh/Enhancements/randomizer/hook_handlers.cpp` (add an `OnPlayerUpdate` handler that drains the mailbox; register it alongside the existing rando hooks)
- Reference (read, do not edit): `soh/soh/Enhancements/randomizer/hook_handlers.cpp:353` (`RandomizerOnPlayerUpdateForRCQueueHandler`) for the registration + player-update pattern; `soh/src/code/z_actor.c:2027` (`GiveItemEntryWithoutActor`) and `Item_Give` for the give path.

For Increment 1 the grant uses a single hardcoded, unmistakable effect (give the item named in the entry IF it maps to a known shared `ITEM_*`, else give a fixed sentinel item) so we can *see* delivery without the full mapping table (that arrives in Increment 3). Use `Item_Give` for simplicity.

- [ ] **Step 1: Add the include + canonical-slot helper**

At the top of `hook_handlers.cpp` (with the other includes):
```cpp
#include "rando/CrossMailbox.h"  // ComboShip: cross-world mailbox
```
Add the ComboShip include path for soh.dll: in `soh/CMakeLists.txt` add `${CMAKE_SOURCE_DIR}/combo` to soh's `target_include_directories` (so `rando/CrossMailbox.h` resolves). Confirm whether `${CMAKE_SOURCE_DIR}` points at the repo root in this build; if not, use the correct repo-root variable already used elsewhere in `soh/CMakeLists.txt`.

- [ ] **Step 2: Write the drain handler**

Add this function in `hook_handlers.cpp` near the other `Randomizer*Handler` functions, guarded for combo builds:
```cpp
#ifdef COMBO_BUILD
// ComboShip: grant any cross-world items addressed to OOT for the current slot.
static void RandomizerOnPlayerUpdateForCrossMailboxHandler() {
    if (gPlayState == nullptr) return;
    // OOT's file number is the canonical slot.
    int slot = gSaveContext.fileNum;
    auto pending = ComboRando::LoadPending(slot, ComboRando::GAME_OOT);
    if (pending.empty()) return;

    for (const auto& e : pending) {
        // Increment 1: prove delivery with a visible, safe grant. Full RG_*/RI_* mapping = Increment 3.
        Item_Give(gPlayState, ITEM_RUPEE_BLUE);
        SPDLOG_INFO("[ComboShip] OOT received cross item '{}' (from MM): granted placeholder rupee",
                    e.itemName);
    }
    ComboRando::MarkAllDelivered(slot, ComboRando::GAME_OOT);
}
#endif
```
(`ITEM_RUPEE_BLUE` is an OOT `ITEM_*` constant; confirm the exact spelling in `soh/include/z64item.h` and substitute if different.)

- [ ] **Step 3: Register the handler**

Find the block in `hook_handlers.cpp` (inside the `OnLoadGame`/`IS_RANDO` setup around line 2740) where `RandomizerOnPlayerUpdateForRCQueueHandler` is registered via `GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(...)`. Add, right after it:
```cpp
#ifdef COMBO_BUILD
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerUpdate>(
        RandomizerOnPlayerUpdateForCrossMailboxHandler);
#endif
```

- [ ] **Step 4: Build soh**

Run: `cmake --build build/x64 --target soh --config Debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add soh/soh/Enhancements/randomizer/hook_handlers.cpp soh/CMakeLists.txt
git commit -m "feat(soh-rando): drain cross-world mailbox on player update (Increment 1 placeholder grant)"
```

---

### Task 4: MM — receive drain on player update

**Files:**
- Modify: `mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp` (or a new sibling `CrossMailbox.cpp` in the same dir) — add a drain that runs each frame on the player actor update; register it the same way `CheckQueue` is registered.
- Reference (read, do not edit): `mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp:29` (the `OnActorUpdate(ACTOR_PLAYER)`/per-frame pattern) and `mm/2s2h/Rando/GiveItem.cpp` for the give path; `mm/include/z64save.h` for the slot field.

MM's canonical slot = its file number − 1 (OOT slot N ↔ MM file N+1).

- [ ] **Step 1: Add the include + combo include path for 2ship**

In `mm/CMakeLists.txt` add the repo-root `combo` dir to 2ship's `target_include_directories` (mirror whatever repo-root variable mm's CMake already uses). In the chosen `.cpp`:
```cpp
#include "rando/CrossMailbox.h"  // ComboShip: cross-world mailbox
```

- [ ] **Step 2: Write the drain function**

```cpp
#ifdef COMBO_BUILD
// ComboShip: grant any cross-world items addressed to MM for the current slot.
static void Rando_CrossMailboxDrain() {
    if (gPlayState == nullptr) return;
    int slot = gSaveContext.fileNum - 1;            // canonical OOT slot
    if (slot < 0) return;
    auto pending = ComboRando::LoadPending(slot, ComboRando::GAME_MM);
    if (pending.empty()) return;

    for (const auto& e : pending) {
        // Increment 1: prove delivery with a visible, safe grant. Full mapping = Increment 3.
        Item_Give(gPlayState, ITEM_RUPEE_BLUE);
        SPDLOG_INFO("[ComboShip] MM received cross item '{}' (from OOT): granted placeholder rupee",
                    e.itemName.c_str());
    }
    ComboRando::MarkAllDelivered(slot, ComboRando::GAME_MM);
}
#endif
```
(Confirm MM's blue-rupee constant name in `mm/include/z64item.h`; MM's `gSaveContext.fileNum` field name — verify against `mm/include/z64save.h`, adjust if it differs.)

- [ ] **Step 3: Register the drain**

Wherever `Rando::MiscBehavior::CheckQueue` is registered to `OnActorUpdate(ACTOR_PLAYER)` (in `MiscBehavior.cpp` or `CheckQueue.cpp`), register `Rando_CrossMailboxDrain` the same way, guarded `#ifdef COMBO_BUILD`. It must only be active in a rando save (mirror the existing `IS_RANDO`/`SAVETYPE_RANDO` guard the CheckQueue uses).

- [ ] **Step 4: Build 2ship**

Run: `cmake --build build/x64 --target 2ship --config Debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add mm/2s2h/Rando/MiscBehavior/CheckQueue.cpp mm/CMakeLists.txt
git commit -m "feat(mm-rando): drain cross-world mailbox on player update (Increment 1 placeholder grant)"
```

---

### Task 5: OOT — debug "send to MM" console command

**Files:**
- Modify: `soh/soh/Enhancements/debugconsole.cpp` (register a new command `cross_send`)
- Reference: `soh/soh/Enhancements/randomizer/randomizer.cpp:1315`-area shows `GenerateRandomizer` invoked from the console; follow the existing `CMD_REGISTER`/command-handler pattern already in `debugconsole.cpp`.

- [ ] **Step 1: Add the include**

```cpp
#include "rando/CrossMailbox.h"  // ComboShip
```

- [ ] **Step 2: Write the command handler**

Following the file's existing command-handler signature (match the surrounding handlers exactly — they take the console + args and return a result enum):
```cpp
#ifdef COMBO_BUILD
// ComboShip: `cross_send <itemName>` — queue a fake MM-bound item for the current slot.
static bool CrossSendHandler(std::shared_ptr<Ship::Console> console,
                             const std::vector<std::string>& args, std::string* output) {
    std::string itemName = (args.size() > 1) ? args[1] : "DEBUG_ITEM";
    ComboRando::MailboxEntry e{ ComboRando::GAME_OOT, ComboRando::GAME_MM,
                                itemName, itemName, "DEBUG_OOT_CONSOLE", false };
    ComboRando::Enqueue(gSaveContext.fileNum, e);
    if (output) *output = "Queued '" + itemName + "' for MM (slot " +
                          std::to_string(gSaveContext.fileNum) + ").";
    return true;
}
#endif
```
(Adapt the signature/return type to the exact pattern used by the other handlers in this file — read two neighbouring `*Handler` functions first and mirror them.)

- [ ] **Step 3: Register the command**

In the console-registration block (where other commands are registered), add `#ifdef COMBO_BUILD` registration of `cross_send` → `CrossSendHandler`, mirroring the neighbouring registrations exactly.

- [ ] **Step 4: Build soh**

Run: `cmake --build build/x64 --target soh --config Debug`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add soh/soh/Enhancements/debugconsole.cpp
git commit -m "feat(soh): debug `cross_send` console command to enqueue a cross-world item"
```

---

### Task 6: MM — debug "send to OOT" trigger

**Files:**
- Modify: MM's dev/enhancement menu or console registration (find the existing 2Ship dev-tools menu — e.g. `mm/2s2h/DeveloperTools/` or the Rando settings window `mm/2s2h/Rando/CheckTracker/CheckTracker.cpp`) and add a button "Cross-send debug item to OOT".
- Reference: how MM registers menu buttons / ImGui dev actions (mirror an existing button in the same window).

- [ ] **Step 1: Add the include where the button lives**

```cpp
#include "rando/CrossMailbox.h"  // ComboShip
```

- [ ] **Step 2: Add the button**

In the chosen ImGui window's draw function, guarded `#ifdef COMBO_BUILD`:
```cpp
#ifdef COMBO_BUILD
    if (ImGui::Button("Cross-send debug item to OOT")) {
        int slot = gSaveContext.fileNum - 1;
        ComboRando::MailboxEntry e{ ComboRando::GAME_MM, ComboRando::GAME_OOT,
                                    "DEBUG_ITEM", "DEBUG_ITEM", "DEBUG_MM_MENU", false };
        ComboRando::Enqueue(slot, e);
    }
#endif
```

- [ ] **Step 3: Build 2ship**

Run: `cmake --build build/x64 --target 2ship --config Debug`
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add <the edited MM menu file> mm/CMakeLists.txt
git commit -m "feat(mm): debug button to enqueue a cross-world item for OOT"
```

---

### Task 7: End-to-end manual verification + document

**Files:**
- Modify: `docs/UPSTREAM_MERGES.md` (record the COMBO_BUILD additions per `[[document-post-merge-changes]]`)

- [ ] **Step 1: Build everything needed (each target individually)**

```bash
cmake --build build/x64 --target libultraship --config Debug
cmake --build build/x64 --target soh --config Debug
cmake --build build/x64 --target 2ship --config Debug
cmake --build build/x64 --target ComboShip --config Debug
```
Expected: all clean. (libultraship only if its CMake changed; usually skip.)

- [ ] **Step 2: Manual OOT→MM delivery test**

Launch `x64/Debug/ComboShip.exe`. Start a randomizer save (so the rando hooks register). Open the console (`` ` ``) and run `cross_send TEST_BOW`. Confirm the console prints "Queued 'TEST_BOW' for MM (slot 0)." and that `saves/combo/slot0.mailbox.json` now exists with one undelivered `dstGame:1` entry. Trigger the portal to MM (enter Mido's House). In MM, walk a frame; expect a blue rupee granted and a log line `[ComboShip] MM received cross item 'TEST_BOW'` in `x64/Debug/logs/Ship of Harkinian.log`. Confirm the entry in `slot0.mailbox.json` is now `delivered:true`.

- [ ] **Step 3: Manual MM→OOT delivery test**

In MM, click the "Cross-send debug item to OOT" button. Confirm a new `dstGame:0` entry in `slot0.mailbox.json`. Portal back to OOT (Clock Tower). Expect a blue rupee + `[ComboShip] OOT received cross item` log line, and the entry marked delivered.

- [ ] **Step 4: Crash-safety spot check**

Enqueue an entry, then hard-kill the process (close the window / Task Manager) before switching games. Relaunch; confirm `slot0.mailbox.json` still holds the undelivered entry and it is delivered on the next switch. (Validates the atomic-write + file-backed design.)

- [ ] **Step 5: Document the COMBO_BUILD deltas**

In `docs/UPSTREAM_MERGES.md`, add a "Cross-World Randomizer Increment 1" entry listing each `#ifdef COMBO_BUILD` addition (hook_handlers.cpp drain + registration, MM drain + registration, debug send command/button, combo include) with the WHY (cross-game item delivery channel). Ensure every code site also carries a `// ComboShip:` comment.

- [ ] **Step 6: Commit**

```bash
git add docs/UPSTREAM_MERGES.md
git commit -m "docs: record Increment 1 cross-world mailbox COMBO_BUILD changes"
```

---

## Self-Review notes

- **Spec coverage:** This increment implements spec "Increment 1" (mailbox + grant plumbing). Generation (Increment 2) and real send-interception/markers/presentation (Increment 3) are intentionally out of scope here — the placeholder grant (blue rupee) and debug triggers stand in for them.
- **Deferred to Increment 3:** the real item-name→give mapping table (here every received item grants a blue rupee), the real send branch points (`hook_handlers.cpp:380` / `CheckQueue.cpp:37`), foreign-item save markers, and the gift model + text. The mailbox `itemName`/`displayName` fields are already carried so Increment 3 only swaps the grant + presentation.
- **Open confirmations for the implementer (verify against real headers, adjust inline):** exact blue-rupee `ITEM_*` constant in each game; MM's `gSaveContext.fileNum` field name; the exact console-command handler signature in `debugconsole.cpp`; the repo-root CMake variable for adding the `combo` include dir to soh/2ship; whether `nlohmann_json` is already linkable from the ComboShip target.
- **Type consistency:** `ComboRando::GameId` / `MailboxEntry` / `Enqueue` / `LoadPending` / `MarkAllDelivered` / `LoadAll` / `WriteAll` / `MailboxPath` are used identically across Tasks 1–6.
