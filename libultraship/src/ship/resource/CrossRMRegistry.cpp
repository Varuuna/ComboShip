// libultraship/src/ship/resource/CrossRMRegistry.cpp
// ComboShip: implementation lives in libultraship.dll so the single map instance is shared
// across all modules (soh.dll, 2ship.dll) — a header-only static would give each DLL its own
// copy and registrations from one game would be invisible to the interpreter in another.
#include "ship/resource/CrossRMRegistry.h"

namespace Ship {

// Thread-safety: Register() runs only at game-boot/transition time on the main thread, before
// frame processing; Get() runs from the interpreter on the same thread. No concurrent
// modification+read → no mutex needed. If a future caller registers after boot (hot reload),
// add a std::shared_mutex here.
static std::unordered_map<std::string, std::shared_ptr<ResourceManager>>& CrossRMMap() {
    static std::unordered_map<std::string, std::shared_ptr<ResourceManager>> sMap;
    return sMap;
}

void CrossRMRegistry::Register(const std::string& name, std::shared_ptr<ResourceManager> rm) {
    CrossRMMap()[name] = std::move(rm);
}

// Games unregister at deinit so the RM is destroyed on the main thread (its thread pool joins its
// workers in the destructor); leaving it in this static map defers destruction to DLL unload,
// where joining under the loader lock deadlocks.
void CrossRMRegistry::Unregister(const std::string& name) {
    CrossRMMap().erase(name);
}

std::shared_ptr<ResourceManager> CrossRMRegistry::Get(const std::string& name) {
    auto it = CrossRMMap().find(name);
    return it != CrossRMMap().end() ? it->second : nullptr;
}

} // namespace Ship
