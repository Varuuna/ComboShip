#include "core/ComboSeedFile.h"

#include <fstream>
#include <iostream>

#include "core/ComboPlatform.h" // GetModuleFileNameW / MAX_PATH
#include "rando/CrossForeign.h"  // ComboRando::ConsolidatedDir

// ComboShip: read a candidate consolidated seed file. True only if it opens, parses and is ours.
bool TryLoadComboSeedFile(const std::filesystem::path& p, nlohmann::json& out) {
    std::ifstream in(p);
    if (!in.is_open())
        return false;
    try {
        nlohmann::json j;
        in >> j;
        if (j.value("fileType", std::string()) != "ComboShipRandomizer") {
            std::cerr << "[ComboShip] reload: " << p.string() << " is not a ComboShip seed file\n";
            return false;
        }
        out = std::move(j);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ComboShip] reload: could not parse " << p.string() << ": " << e.what() << "\n";
        return false;
    }
}

// ComboShip: a stored-relative path (or one from a different CWD) also gets tried next to the exe.
std::filesystem::path ResolveComboSeedPath(const std::string& file) {
    std::filesystem::path p(file);
    std::error_code ec;
    if (std::filesystem::exists(p, ec))
        return p;
#ifdef _WIN32
    // Wide API: the ANSI variant mangles non-ASCII install paths (e.g. accented user names) to '?'.
    wchar_t exe[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        const auto dir = std::filesystem::path(exe).parent_path();
        // A relative path re-rooted at the exe; a moved absolute one by name under the seed dir.
        for (const auto& alt : { dir / p.relative_path(), dir / ComboRando::ConsolidatedDir() / p.filename() })
            if (std::filesystem::exists(alt, ec))
                return alt;
    }
#endif
    return p;
}

// ComboShip: newest readable combo seed in the Randomizer dir — recovers an auto-load when the
// remembered path is lost (e.g. a wiped CVar) while the seed files are still there.
std::filesystem::path FindNewestComboSeed(nlohmann::json& out) {
    std::error_code ec;
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
    for (const auto& dir :
         { ComboRando::ConsolidatedDir(), ResolveComboSeedPath(ComboRando::ConsolidatedDir().string()) }) {
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json")
                continue;
            found.emplace_back(std::filesystem::last_write_time(it->path(), ec), it->path());
        }
        if (!found.empty())
            break;
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (const auto& [t, p] : found)
        if (TryLoadComboSeedFile(p, out))
            return p;
    return {};
}
