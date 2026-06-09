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
            }
        }
    }
    ~ResourceManagerScope() {
        if (mSwapped) {
            if (auto ctx = Context::GetInstance()) {
                ctx->SetResourceManager(mPrevious);
            }
        }
    }
    ResourceManagerScope(const ResourceManagerScope&) = delete;
    ResourceManagerScope& operator=(const ResourceManagerScope&) = delete;

  private:
    std::shared_ptr<ResourceManager> mPrevious;
    bool mSwapped = false;
};
} // namespace Ship
