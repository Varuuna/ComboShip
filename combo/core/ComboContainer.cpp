#include "core/ComboContainer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

// ---------- ComboShip merged per-slot save container (Save/file{N+1}.combosav) ----------
// One JSON file per slot holds both games' saves verbatim + combo metadata (completion + baked rando).
// The launcher mediates every per-slot read/write through Combo_ReadGameSave/Combo_WriteGameSave
// (pushed into each DLL at boot), so the in-process cache stays authoritative. Single mutex serializes
// OOT's thread-pool writes, MM's synchronous writes, and Anchor cross-writes. Write = temp+rename,
// never torn on crash. Each container carries "comboRelease" (COMBO_RELEASE_VERSION); a container from a
// different major.minor is backed up to .bak and recreated — the launcher is the sole save-compat gate.
// See docs/deviations/boot-shutdown.md.
std::mutex g_containerMutex;
static std::map<int, nlohmann::json> g_containerCache;
// Slots whose container was backed up for a COMBO_RELEASE_VERSION mismatch; guarded by g_containerMutex.
// OOT drains it on the main thread (Combo_TakeEvictionNotice) to surface a popup.
static std::vector<int> g_evictedSlots;


static std::filesystem::path ComboContainerPath(int fileNum) {
    return std::filesystem::path("Save") / ("file" + std::to_string(fileNum + 1) + ".combosav");
}

// Only the three real save slots have a container. Callbacks reached from a game's gSaveContext.fileNum
// can carry 0xFF (no save loaded) or 0xFE (Boss Rush) — those must never create a phantom container.
bool ComboIsValidSlot(int fileNum) {
    return fileNum >= 0 && fileNum <= 2;
}

// Save compat is gated on major.minor only: patch releases must never change save-affecting behavior.
static std::string ComboReleaseMajorMinor(const std::string& v) {
    size_t dot = v.find('.');
    return v.substr(0, dot == std::string::npos ? std::string::npos : v.find('.', dot + 1));
}

// Hold g_containerMutex. Returns a ref into the cache; loads from disk or creates a fresh container.
static nlohmann::json& LoadOrCreateContainer(int fileNum) {
    auto it = g_containerCache.find(fileNum);
    if (it != g_containerCache.end())
        return it->second;
    nlohmann::json j;
    auto path = ComboContainerPath(fileNum);
    std::error_code ec;
    bool existed = std::filesystem::exists(path, ec);
    std::ifstream in(path);
    bool parsed = false;
    if (in.is_open()) {
        try {
            in >> j;
            parsed = j.is_object();
        } catch (...) { parsed = false; }
    }
    in.close();
    // Never silently overwrite an existing-but-unreadable container: a fresh cache entry would drop
    // the other game's section on the next write. Preserve the file aside, then start fresh.
    if (existed && !parsed) {
        std::filesystem::rename(path, path.string() + ".corrupt-" + std::to_string(std::time(nullptr)), ec);
    }
    // COMBO_RELEASE_VERSION gate (major.minor only; patch releases keep saves): a container from a
    // different release is outdated. Back it up aside and start fresh; record the slot so OOT can
    // surface a popup on its main thread.
    if (parsed) {
        auto rel = j.find("comboRelease");
        if (rel == j.end() || !rel->is_string() ||
            ComboReleaseMajorMinor(rel->get<std::string>()) != ComboReleaseMajorMinor(COMBO_RELEASE_VERSION)) {
            std::filesystem::rename(path, path.string() + "-" + std::to_string(std::time(nullptr)) + ".bak", ec);
            g_evictedSlots.push_back(fileNum); // caller holds g_containerMutex
            parsed = false;
        }
    }
    // ComboShip (#165): one-time migrate SoH's per-save notes into combo.notes. Absent-key gate only,
    // so a deliberately cleared combo note is never re-migrated.
    if (parsed && !(j.contains("combo") && j["combo"].is_object() && j["combo"].contains("notes"))) {
        try {
            auto& pn = j.at("oot").at("sections").at("itemTrackerData").at("data").at("personalNotes");
            if (pn.is_string() && !pn.get<std::string>().empty())
                j["combo"]["notes"] = pn;
        } catch (...) {} // oot section absent/null — nothing to migrate
    }
    if (!parsed)
        j = nlohmann::json{ { "comboVersion", 1 }, { "comboRelease", COMBO_RELEASE_VERSION },
                            { "slot", fileNum },   { "oot", nullptr },
                            { "mm", nullptr },     { "combo", nlohmann::json::object() } };
    return g_containerCache.emplace(fileNum, std::move(j)).first->second;
}

// Hold g_containerMutex. Serialize the cached container to a temp file, then atomic rename over it.
static void FlushContainer(int fileNum) {
    auto it = g_containerCache.find(fileNum);
    if (it == g_containerCache.end())
        return;
    std::error_code ec;
    auto path = ComboContainerPath(fileNum);
    std::filesystem::create_directories(path.parent_path(), ec);
    auto tmp = path;
    tmp += ".temp";
    {
        std::ofstream out(tmp, std::ios::trunc | std::ios::binary);
        if (!out.is_open())
            return;
        it->second["comboRelease"] = COMBO_RELEASE_VERSION; // every write carries the current release
        out << it->second.dump();
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { // some filesystems won't replace-on-rename — remove then retry
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
}

// OOT (main thread) polls this each frame via SOH_SetOutdatedSaveNotice: pops the next slot whose
// container was backed up for a release mismatch, or -1 if none. Mirrors the SOH_SetCopyContainer wiring.
int Combo_TakeEvictionNotice() {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    if (g_evictedSlots.empty())
        return -1;
    int slot = g_evictedSlots.front();
    g_evictedSlots.erase(g_evictedSlots.begin());
    return slot;
}

void ComboEraseSlotStorage(int slot) {
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        g_containerCache.erase(slot);
        // MM's dormant save is now stale: leave it marked resident and the next load skips re-reading it,
        // and any dormant MM write would put the erased save back into the slot.
        if (g_MmSaveInMemorySlot == slot)
            g_MmSaveInMemorySlot = -1;
        std::error_code ec;
        std::filesystem::remove(ComboContainerPath(slot), ec);
    }
}

// Copy a whole slot (both games + baked rando) — backs OOT file-select "copy file". Reached through
// the launcher's Combo_CopyContainer wrapper, which notifies the DLLs afterwards.
void ComboCopySlotStorage(int from, int to) {
    {
        std::lock_guard<std::mutex> lk(g_containerMutex);
        nlohmann::json copy = LoadOrCreateContainer(from); // deep copy of the source container
        copy["slot"] = to;
        g_containerCache[to] = std::move(copy);
        if (g_MmSaveInMemorySlot == to)
            g_MmSaveInMemorySlot = -1; // the destination's MM save just changed under us
        FlushContainer(to);
    }
}

// Launcher-provided save IO, pushed into each DLL. game: 0=OOT,1=MM (GameId); fileNum 0-based.
// Returns the section JSON in a thread_local buffer (OOT may read off the main thread), "" if absent.
const char* Combo_ReadGameSave(int game, int fileNum) {
    // No container exists for a sentinel fileNum - see ComboIsValidSlot.
    if (!ComboIsValidSlot(fileNum))
        return "";
    thread_local std::string buf;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const char* key = (game == ComboRando::GAME_OOT) ? "oot" : "mm";
    auto it = c.find(key);
    buf = (it == c.end() || it->is_null()) ? std::string() : it->dump();
    return buf.c_str();
}

// Read-modify-write the FULL container (never re-derived) so the other game's section stays intact
// when e.g. an Anchor MM write lands during OOT play. Malformed inbound leaves the section untouched.
void Combo_WriteGameSave(int game, int fileNum, const char* json) {
    if (!json)
        return;
    // Same guard as the read: a sentinel fileNum must never create a phantom container. Say so once —
    // the session this fires in drops every save, and the load failure may be hours back in the log.
    if (!ComboIsValidSlot(fileNum)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::cerr << "[ComboShip] dropping save write for game " << game << ": no slot loaded (fileNum " << fileNum
                      << ")" << std::endl;
        }
        return;
    }
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const char* key = (game == ComboRando::GAME_OOT) ? "oot" : "mm";
    try {
        c[key] = nlohmann::json::parse(json);
    } catch (...) { return; }
    FlushContainer(fileNum);
}

// Record which game the player is now in, so a quit-and-reload resumes there. Set at the two
// transitions only — NOT from save writes: loading an OOT save itself writes sections (rando and
// check-tracker OnLoadGame handlers), which would stamp OOT over the MM the player actually left in.
void Combo_SetLastGame(int fileNum, int game) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    if (c.value("combo", nlohmann::json::object()).value("lastGame", -1) == game)
        return; // already correct — don't rewrite the container for nothing
    std::cout << "[ComboShip] lastGame <- " << game << " (slot " << fileNum << ")" << std::endl;
    c["combo"]["lastGame"] = game;
    FlushContainer(fileNum);
}

// Which game a slot was last saved in (GameId). Absent => OOT, so pre-lastGame saves resume as before.
int Combo_GetLastGame(int fileNum) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    int g = c.value("combo", nlohmann::json::object()).value("lastGame", (int)ComboRando::GAME_OOT);
    return (g == ComboRando::GAME_MM) ? ComboRando::GAME_MM : ComboRando::GAME_OOT;
}

// ComboShip (#165): the slot's cross-game personal notes. One note per slot, editable from either
// game. Returned in a thread_local buffer (same lifetime contract as Combo_ReadGameSave).
const char* Combo_GetNotes(int fileNum) {
    thread_local std::string buf;
    if (!ComboIsValidSlot(fileNum)) {
        buf.clear();
        return buf.c_str();
    }
    std::lock_guard<std::mutex> lk(g_containerMutex);
    buf.clear();
    // find()-based: never throws (comboui calls these by raw fn-ptr) and never copies combo.rando.
    auto& c = LoadOrCreateContainer(fileNum);
    auto combo = c.find("combo");
    if (combo != c.end() && combo->is_object()) {
        auto n = combo->find("notes");
        if (n != combo->end() && n->is_string())
            buf = n->get<std::string>();
    }
    return buf.c_str();
}

void Combo_SetNotes(int fileNum, const char* text) {
    if (!text || !ComboIsValidSlot(fileNum))
        return;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(fileNum);
    const std::string* cur = nullptr;
    auto combo = c.find("combo");
    if (combo != c.end() && combo->is_object()) {
        auto n = combo->find("notes");
        if (n != combo->end() && n->is_string())
            cur = &n->get_ref<const std::string&>();
    }
    // Unchanged — don't rewrite the container on every debounce (absent/non-string compares as "").
    if (cur ? *cur == text : !*text)
        return;
    c["combo"]["notes"] = text;
    FlushContainer(fileNum);
}

// ---------- typed slot accessors (see the contract in ComboContainer.h) ----------

ComboSlotGoalState ComboReadGoalState(int slot) {
    ComboSlotGoalState s;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    auto combo = c.value("combo", nlohmann::json::object());
    auto comp = combo.value("completion", nlohmann::json::object());
    s.ootDone = comp.value("oot", false);
    s.mmDone = comp.value("mm", false);
    s.triforceDone = comp.value("triforce", false);
    // The goal is seed-bound: it rides the slot's baked combo.rando, not the live menu CVars.
    auto rando = combo.value("rando", nlohmann::json::object());
    auto goal = rando.value("goal", nlohmann::json::object());
    s.hunt = goal.value("type", std::string("bosses")) == "triforceHunt";
    s.required = s.hunt ? goal.value("requiredPieces", 0) : 0;
    s.total = goal.value("totalPieces", -1); // absent on seeds made before the combined total
    // Same for the starting game (#135) — old seeds have no field and started in OOT.
    s.startingGameMM = rando.value("startingGame", std::string("OOT")) == "MM";
    return s;
}

void ComboWriteCompletion(int slot, bool oot, bool mm, bool triforce) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    c["combo"]["completion"]["oot"] = oot;
    c["combo"]["completion"]["mm"] = mm;
    c["combo"]["completion"]["triforce"] = triforce;
    FlushContainer(slot);
}

ComboHintSlice ComboReadHintSlice(int slot) {
    ComboHintSlice out;
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    const auto combo = c.value("combo", nlohmann::json::object());
    out.hints = combo.value("rando", nlohmann::json::object()).value("hints", nlohmann::json::object()).dump();
    out.read = combo.value("hintsRead", nlohmann::json::object()).dump();
    return out;
}

bool ComboInsertHintRead(int slot, const char* bucket, const nlohmann::json& value, const char* matchField) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    nlohmann::json& arr = c["combo"]["hintsRead"][bucket];
    if (!arr.is_array())
        arr = nlohmann::json::array();
    for (auto& e : arr) {
        if (matchField ? e.value(matchField, std::string()) == value.value(matchField, std::string()) : e == value)
            return false;
    }
    arr.push_back(value);
    FlushContainer(slot);
    return true;
}

void ComboResetSlotForNewFile(int slot) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    c["combo"]["notes"] = "";
    if (c["combo"].contains("hintsRead"))
        c["combo"].erase("hintsRead");
    FlushContainer(slot);
}

void ComboBakeSeed(int slot, const nlohmann::json& seed) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    if (!seed.is_null())
        c["combo"]["rando"] = seed;
    if (c.contains("combo") && c["combo"].is_object())
        c["combo"].erase("completion");
    FlushContainer(slot);
}

std::string ComboReadBakedRando(int slot) {
    std::lock_guard<std::mutex> lk(g_containerMutex);
    auto& c = LoadOrCreateContainer(slot);
    auto r = c.value("combo", nlohmann::json::object()).value("rando", nlohmann::json());
    return r.is_null() ? std::string() : r.dump();
}
