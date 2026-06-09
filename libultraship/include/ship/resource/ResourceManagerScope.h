// libultraship/include/ship/resource/ResourceManagerScope.h
// ComboShip: temporarily make a specific ResourceManager the Context's active one,
// restoring the previous one on scope exit. Safe because render and game loop are
// sequential on one thread (no concurrent active-RM access). See
// docs/superpowers/specs/2026-06-09-comboship-owns-menus-design.md.
#pragma once

#include <memory>
#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"

namespace Ship {
class ResourceManagerScope {
  public:
    explicit ResourceManagerScope(std::shared_ptr<ResourceManager> target) {
        auto ctx = Context::GetInstance();
        if (ctx && target) {
            mPrevious = ctx->GetResourceManager();
            if (mPrevious != target) {
                ctx->SetResourceManager(target);
                mSwapped = true;
                // Capture the Context identity (non-owning — must NOT extend Context lifetime)
                // so the dtor only restores into the same Context we swapped. The single-thread
                // model is the norm; this is a cheap mechanical guard against ever writing
                // mPrevious into a different Context if the active Context were swapped in-scope.
                mCtx = ctx.get();
            }
        }
    }
    ~ResourceManagerScope() {
        if (mSwapped) {
            if (auto ctx = Context::GetInstance(); ctx && ctx.get() == mCtx) {
                ctx->SetResourceManager(mPrevious);
            }
        }
    }
    ResourceManagerScope(const ResourceManagerScope&) = delete;
    ResourceManagerScope& operator=(const ResourceManagerScope&) = delete;
    // Strictly a stack-scoped RAII guard: not movable (no need to transfer the restore).
    ResourceManagerScope(ResourceManagerScope&&) = delete;
    ResourceManagerScope& operator=(ResourceManagerScope&&) = delete;

  private:
    std::shared_ptr<ResourceManager> mPrevious;
    const Context* mCtx = nullptr;
    bool mSwapped = false;
};
} // namespace Ship
