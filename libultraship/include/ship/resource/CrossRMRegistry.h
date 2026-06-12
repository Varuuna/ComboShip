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
    static void Register(const std::string& name, std::shared_ptr<ResourceManager> rm);
    static void Unregister(const std::string& name);
    static std::shared_ptr<ResourceManager> Get(const std::string& name);
};
} // namespace Ship
