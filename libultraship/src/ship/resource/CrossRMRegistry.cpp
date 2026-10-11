// libultraship/src/ship/resource/CrossRMRegistry.cpp
// ComboShip: implementation lives in libultraship.dll so the single map instance is shared
// across all modules (soh.dll, 2ship.dll) — a header-only static would give each DLL its own
// copy and registrations from one game would be invisible to the interpreter in another.
#include "ship/resource/CrossRMRegistry.h"
#include "ship/Context.h"

#include <algorithm>
#include <shared_mutex>
#include <vector>

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

static std::vector<void (*)()>& CrossRMTeardownListeners() {
    static std::vector<void (*)()> sListeners;
    return sListeners;
}

void CrossRMRegistry::Register(const std::string& name, std::shared_ptr<ResourceManager> rm) {
    std::unique_lock lock(CrossRMMutex());
    CrossRMMap()[name] = std::move(rm);
}

void CrossRMRegistry::RegisterTeardownListener(void (*listener)()) {
    if (listener == nullptr) {
        return;
    }
    std::unique_lock lock(CrossRMMutex());
    auto& listeners = CrossRMTeardownListeners();
    if (std::find(listeners.begin(), listeners.end(), listener) == listeners.end()) {
        listeners.push_back(listener);
    }
}

// Games unregister at deinit so the RM is destroyed on the main thread (its thread pool joins its
// workers in the destructor); leaving it in this static map defers destruction to DLL unload,
// where joining under the loader lock deadlocks.
void CrossRMRegistry::Unregister(const std::string& name) {
    // Fire teardown listeners FIRST: each module drops shared_ptrs whose control blocks live in
    // another game DLL (all still mapped during deinit) before that RM's resources go away.
    // Copied out of the lock — a released resource's destructor may re-enter Get().
    std::vector<void (*)()> listeners;
    {
        std::shared_lock lock(CrossRMMutex());
        listeners = CrossRMTeardownListeners();
    }
    for (auto listener : listeners) {
        listener();
    }
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
    auto ctx = Context::GetRawInstance();
    return ctx ? ctx->GetResourceManager() : nullptr;
}

OwnRMScope::OwnRMScope(const char* name) {
    auto ctx = Context::GetRawInstance();
    auto target = CrossRMRegistry::Get(name);
    if (ctx && target) {
        mPrevious = ctx->GetResourceManager();
        if (mPrevious != target) {
            ctx->SetResourceManager(target);
            mSwapped = true;
            mCtx = ctx;
        }
    }
}

OwnRMScope::~OwnRMScope() {
    if (mSwapped) {
        if (auto ctx = Context::GetRawInstance(); ctx && ctx == mCtx) {
            ctx->SetResourceManager(mPrevious);
        }
    }
}

} // namespace Ship
