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
    j = nlohmann::json{ {"srcGame", static_cast<int>(e.srcGame)}, {"dstGame", static_cast<int>(e.dstGame)},
                        {"itemName", e.itemName}, {"displayName", e.displayName},
                        {"srcCheckName", e.srcCheckName}, {"delivered", e.delivered} };
}
inline void from_json(const nlohmann::json& j, MailboxEntry& e) {
    e.srcGame      = static_cast<GameId>(j.value("srcGame", 0));
    e.dstGame      = static_cast<GameId>(j.value("dstGame", 0));
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
    std::ifstream in(MailboxPath(canonicalSlot));
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
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);   // atomic on same volume
    return !ec;
}

inline bool Enqueue(int canonicalSlot, const MailboxEntry& entry) {
    auto entries = LoadAll(canonicalSlot);
    entries.push_back(entry);
    return WriteAll(canonicalSlot, entries);
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
inline bool MarkAllDelivered(int canonicalSlot, GameId dstGame) {
    auto entries = LoadAll(canonicalSlot);
    for (auto& e : entries) {
        if (!e.delivered && e.dstGame == dstGame) e.delivered = true;
    }
    return WriteAll(canonicalSlot, entries);
}

} // namespace ComboRando
