// libultraship/src/ship/resource/CrossRMRegistry.cpp
// ComboShip: implementation lives in libultraship.dll so the single map instance is shared
// across all modules (soh.dll, 2ship.dll) — a header-only static would give each DLL its own
// copy and registrations from one game would be invisible to the interpreter in another.
#include "ship/resource/CrossRMRegistry.h"

namespace Ship {

static std::unordered_map<std::string, std::shared_ptr<ResourceManager>>& CrossRMMap() {
    static std::unordered_map<std::string, std::shared_ptr<ResourceManager>> sMap;
    return sMap;
}

void CrossRMRegistry::Register(const std::string& name, std::shared_ptr<ResourceManager> rm) {
    CrossRMMap()[name] = std::move(rm);
}

std::shared_ptr<ResourceManager> CrossRMRegistry::Get(const std::string& name) {
    auto it = CrossRMMap().find(name);
    return it != CrossRMMap().end() ? it->second : nullptr;
}

} // namespace Ship
