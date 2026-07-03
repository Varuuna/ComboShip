// libultraship/src/ship/resource/CrossRMRegistry.cpp
// ComboShip: implementation lives in libultraship.dll so the single map instance is shared
// across all modules (soh.dll, 2ship.dll) — a header-only static would give each DLL its own
// copy and registrations from one game would be invisible to the interpreter in another.
#include "ship/resource/CrossRMRegistry.h"
#include "ship/Context.h"

#include <shared_mutex>

namespace Ship {

// Get() is called from the graph thread AND the audio threads (pinned audio loads), so reads
// need a lock against boot/deinit-time Register/Unregister.
static std::shared_mutex& CrossRMMutex() {
    static std::shared_mutex sMutex;
    return sMutex;
}

static std::unordered_map<std::string, std::shared_ptr<ResourceManager>>& CrossRMMap() {
    static std::unordered_map<std::string, std::shared_ptr<ResourceManager>> sMap;
    return sMap;
}

void CrossRMRegistry::Register(const std::string& name, std::shared_ptr<ResourceManager> rm) {
    std::unique_lock lock(CrossRMMutex());
    CrossRMMap()[name] = std::move(rm);
}

// Games unregister at deinit so the RM is destroyed on the main thread (its thread pool joins its
// workers in the destructor); leaving it in this static map defers destruction to DLL unload,
// where joining under the loader lock deadlocks.
void CrossRMRegistry::Unregister(const std::string& name) {
    std::shared_ptr<ResourceManager> removed;
    {
        std::unique_lock lock(CrossRMMutex());
        auto it = CrossRMMap().find(name);
        if (it != CrossRMMap().end()) {
            removed = std::move(it->second);
            CrossRMMap().erase(it);
        }
    }
    // `removed` drops outside the lock: a last-ref ~ResourceManager joins workers that may call
    // Get(), which would deadlock against the held unique_lock.
}

std::shared_ptr<ResourceManager> CrossRMRegistry::Get(const std::string& name) {
    std::shared_lock lock(CrossRMMutex());
    auto it = CrossRMMap().find(name);
    return it != CrossRMMap().end() ? it->second : nullptr;
}

std::shared_ptr<ResourceManager> CrossRMRegistry::GetOrActive(const std::string& name) {
    auto rm = Get(name);
    if (rm) {
        return rm;
    }
    auto ctx = Context::GetInstance();
    return ctx ? ctx->GetResourceManager() : nullptr;
}

OwnRMScope::OwnRMScope(const char* name) {
    auto ctx = Context::GetInstance();
    auto target = CrossRMRegistry::Get(name);
    if (ctx && target) {
        mPrevious = ctx->GetResourceManager();
        if (mPrevious != target) {
            ctx->SetResourceManager(target);
            mSwapped = true;
            mCtx = ctx.get();
        }
    }
}

OwnRMScope::~OwnRMScope() {
    if (mSwapped) {
        if (auto ctx = Context::GetInstance(); ctx && ctx.get() == mCtx) {
            ctx->SetResourceManager(mPrevious);
        }
    }
}

} // namespace Ship
