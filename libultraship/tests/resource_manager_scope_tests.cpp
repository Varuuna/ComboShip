#include <gtest/gtest.h>
#include <memory>

#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceManagerScope.h"

namespace {

// The libultraship test harness never calls Context::CreateInstance(), so
// GetInstance() returns null by default. Context exposes a public constructor
// plus SetInstance(), so we stand up a minimal, uninitialized Context to
// genuinely exercise the guard's swap/restore semantics without booting any
// subsystems.
//
// NOTE: ~Context() unconditionally calls GetWindow()->SaveWindowToConfig(),
// which would dereference the null Window of an uninitialized Context and crash.
// We therefore deliberately never destruct this Context: it is heap-allocated and
// intentionally leaked (held by a control block that outlives the process), and
// registered via SetInstance(), which only stores a weak_ptr. This matters because
// ctest runs each TEST in its own process, so a static shared_ptr would otherwise
// run ~Context() at process exit and segfault during teardown.
Ship::Context& TestContext() {
    static std::shared_ptr<Ship::Context>* ctx = [] {
        auto* held = new std::shared_ptr<Ship::Context>(
            std::make_shared<Ship::Context>("LusTest", "lustest", ""));
        Ship::Context::SetInstance(*held);
        return held; // never deleted on purpose — keeps the Context alive past process exit
    }();
    return **ctx;
}

// These tests use ResourceManagers purely as identity tokens (pointer equality),
// so we skip Init(): a default-constructed ResourceManager is a valid, destructible
// object (ctor is empty, ~ResourceManager only logs) and never needs its subsystems.
std::shared_ptr<Ship::ResourceManager> MakeRm() {
    return std::make_shared<Ship::ResourceManager>();
}

} // namespace

// Verifies the active RM is swapped within scope and restored after.
TEST(ResourceManagerScope, SwapsAndRestores) {
    auto& ctx = TestContext();

    auto original = MakeRm();
    ctx.SetResourceManager(original);
    ASSERT_EQ(ctx.GetResourceManager(), original);

    auto other = MakeRm();
    {
        Ship::ResourceManagerScope scope(other);
        EXPECT_EQ(ctx.GetResourceManager(), other);
    }
    EXPECT_EQ(ctx.GetResourceManager(), original);
}

// Constructing the guard with a null target must be a no-op (no swap, no crash).
TEST(ResourceManagerScope, NullTargetIsNoOp) {
    auto& ctx = TestContext();

    auto original = MakeRm();
    ctx.SetResourceManager(original);

    {
        Ship::ResourceManagerScope scope(nullptr);
        EXPECT_EQ(ctx.GetResourceManager(), original);
    }
    EXPECT_EQ(ctx.GetResourceManager(), original);
}
