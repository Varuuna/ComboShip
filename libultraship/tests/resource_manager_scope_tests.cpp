#include <gtest/gtest.h>
#include <memory>

#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceManagerScope.h"

namespace {

// The harness never boots a Context, so GetRawInstance() is null by default. Stand up a minimal,
// uninitialized one (libultraship owns it) to exercise the guard's swap/restore for real without
// starting any subsystems. ~Context tolerates the null Window/Config this leaves behind.
Ship::Context& TestContext() {
    static Ship::Context* ctx = Ship::Context::CreateUninitializedInstance("LusTest", "lustest", "");
    return *ctx;
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
