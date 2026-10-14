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
    // Modules that borrow shared_ptrs from a registered RM (the foreign-anim caches) register a
    // teardown listener; every Unregister fires ALL listeners before dropping the RM, so each
    // module releases its cross-module refs while every game DLL is still mapped. Idempotent per
    // pointer; listeners stay registered, so a re-fire must be a safe no-op (clearing an empty
    // cache). Raw fn pointers into game DLLs: valid because Unregister only runs during deinit,
    // before any FreeLibrary (see docs/deviations/boot-shutdown.md).
    static void RegisterTeardownListener(void (*listener)());
    static std::shared_ptr<ResourceManager> Get(const std::string& name);
    // Get(name), falling back to the Context's active RM when not registered. For code that must
    // always use its own game's RM (e.g. audio threads racing active-RM swaps on other threads).
    static std::shared_ptr<ResourceManager> GetOrActive(const std::string& name);
};

// RAII: pin the named game's RM as active, restoring on exit. Like ResourceManagerScope, but
// out-of-line so this light header is safe in widely-included game headers (no Context/SDL chain).
class OwnRMScope {
  public:
    explicit OwnRMScope(const char* name);
    ~OwnRMScope();
    OwnRMScope(const OwnRMScope&) = delete;
    OwnRMScope& operator=(const OwnRMScope&) = delete;
    OwnRMScope(OwnRMScope&&) = delete;
    OwnRMScope& operator=(OwnRMScope&&) = delete;

  private:
    std::shared_ptr<ResourceManager> mPrevious;
    const void* mCtx = nullptr; // Context identity guard (non-owning)
    bool mSwapped = false;
};
} // namespace Ship
