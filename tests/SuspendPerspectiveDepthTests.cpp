// Tests for the reference-counted SuspendPerspective state in IRenderer.
//
// The Y-sort entity rendering loop relies on nested PerspectiveSuspendGuard
// instances being free (no flush) inside an outer suspension scope. These
// tests exercise the depth-counter semantics directly through the base class
// so the contract is locked in without instantiating a real renderer.

#include "MockRenderer.hpp"

#include <gtest/gtest.h>

TEST(SuspendPerspectiveDepthTests, InitialStateIsNotSuspended)
{
    MockRenderer renderer;
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

TEST(SuspendPerspectiveDepthTests, SingleGuardSuspendsAndResumes)
{
    MockRenderer renderer;
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

TEST(SuspendPerspectiveDepthTests, NestedGuardsCollapseToSingleSuspension)
{
    MockRenderer renderer;
    {
        IRenderer::PerspectiveSuspendGuard outer(renderer);
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
        {
            IRenderer::PerspectiveSuspendGuard inner1(renderer);
            EXPECT_TRUE(renderer.IsPerspectiveSuspended());
            {
                IRenderer::PerspectiveSuspendGuard inner2(renderer);
                EXPECT_TRUE(renderer.IsPerspectiveSuspended());
            }
            EXPECT_TRUE(renderer.IsPerspectiveSuspended());
        }
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

TEST(SuspendPerspectiveDepthTests, OuterBoolPlusInnerGuardKeepsState)
{
    // Mirrors the Y-sort loop pattern: outer code calls SuspendPerspective(true)
    // directly while inner render methods construct their own guards. The
    // outer state must survive the inner guard's destruction.
    MockRenderer renderer;
    renderer.SuspendPerspective(true);
    EXPECT_TRUE(renderer.IsPerspectiveSuspended());

    {
        IRenderer::PerspectiveSuspendGuard inner(renderer);
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }
    EXPECT_TRUE(renderer.IsPerspectiveSuspended());

    renderer.SuspendPerspective(false);
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

TEST(SuspendPerspectiveDepthTests, MultipleConsecutiveEntityGuards)
{
    // Models the worst-case Y-sort scenario: outer suspension held by the
    // loop, then each entity half constructs an independent guard. None of
    // the inner guards should drop the outer state.
    MockRenderer renderer;
    renderer.SuspendPerspective(true);

    for (int i = 0; i < 10; ++i)
    {
        {
            IRenderer::PerspectiveSuspendGuard bottomHalf(renderer);
            EXPECT_TRUE(renderer.IsPerspectiveSuspended());
        }
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
        {
            IRenderer::PerspectiveSuspendGuard topHalf(renderer);
            EXPECT_TRUE(renderer.IsPerspectiveSuspended());
        }
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }

    renderer.SuspendPerspective(false);
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

TEST(SuspendPerspectiveDepthTests, SeparateGuardsAlternate)
{
    MockRenderer renderer;

    {
        IRenderer::PerspectiveSuspendGuard a(renderer);
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());

    {
        IRenderer::PerspectiveSuspendGuard b(renderer);
        EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    }
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}

#if !defined(NDEBUG) && defined(GTEST_HAS_DEATH_TEST) && GTEST_HAS_DEATH_TEST
TEST(SuspendPerspectiveDepthTests, UnderflowAssertsInDebug)
{
    MockRenderer renderer;
    EXPECT_DEATH({ renderer.SuspendPerspective(false); }, ".*");
}
#else
TEST(SuspendPerspectiveDepthTests, UnderflowClampsInRelease)
{
    // In release builds the assert is compiled out and the underflow path
    // logs once and clamps the depth at 0 rather than crashing.
    MockRenderer renderer;
    renderer.SuspendPerspective(false);
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());

    // Subsequent suspend/resume still works normally after a clamped underflow.
    renderer.SuspendPerspective(true);
    EXPECT_TRUE(renderer.IsPerspectiveSuspended());
    renderer.SuspendPerspective(false);
    EXPECT_FALSE(renderer.IsPerspectiveSuspended());
}
#endif
