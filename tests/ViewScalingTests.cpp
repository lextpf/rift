// Pure-math regression guard for the window->world scaling helpers. No GL/Vulkan
// context is created here (see the rift_tests constraint in CMakeLists.txt).
#include "../src/ViewScaling.hpp"

#include <gtest/gtest.h>

namespace
{
constexpr float kTol = 1e-3f;
}  // namespace

TEST(ViewScalingTest, VisibleWorldSize_DefaultWindow)
{
    // 1520x800 at PIXEL_SCALE 5 -> 304x160 world pixels.
    const glm::vec2 v = viewScaling::VisibleWorldSize(1520, 800, 5);
    EXPECT_NEAR(v.x, 304.0f, kTol);
    EXPECT_NEAR(v.y, 160.0f, kTol);
}

TEST(ViewScalingTest, VisibleWorldSize_WideWindowSeesMoreWorld)
{
    // Twice as wide -> twice the world width, same height (reveal-more model).
    const glm::vec2 v = viewScaling::VisibleWorldSize(3040, 800, 5);
    EXPECT_NEAR(v.x, 608.0f, kTol);
    EXPECT_NEAR(v.y, 160.0f, kTol);
}

TEST(ViewScalingTest, VisibleWorldSize_NonTileAlignedKeepsFraction)
{
    // 1601 px wide is NOT a multiple of 80; the helper keeps the fraction
    // (the old truncated tile-count formula dropped it).
    const glm::vec2 v = viewScaling::VisibleWorldSize(1601, 800, 5);
    EXPECT_NEAR(v.x, 320.2f, kTol);
}

TEST(ViewScalingTest, VisibleWorldSizeZoomed_HalvesAtZoomTwo)
{
    const glm::vec2 v = viewScaling::VisibleWorldSizeZoomed(1520, 800, 5, 2.0f);
    EXPECT_NEAR(v.x, 152.0f, kTol);
    EXPECT_NEAR(v.y, 80.0f, kTol);
}

TEST(ViewScalingTest, VisibleWorldSizeZoomed_GuardsZeroZoom)
{
    // Must not divide by zero; clamps to a tiny positive zoom.
    const glm::vec2 v = viewScaling::VisibleWorldSizeZoomed(1520, 800, 5, 0.0f);
    EXPECT_GT(v.x, 0.0f);
    EXPECT_TRUE(std::isfinite(v.x));
}

TEST(ViewScalingTest, VisibleWorldSize_GuardsZeroPixelScale)
{
    // pixelScale < 1 clamps to 1 (no div-by-zero); world == screen pixels.
    const glm::vec2 v = viewScaling::VisibleWorldSize(1520, 800, 0);
    EXPECT_NEAR(v.x, 1520.0f, kTol);
    EXPECT_NEAR(v.y, 800.0f, kTol);
}

TEST(ViewScalingTest, MenuUiScale_OneAtReference)
{
    EXPECT_NEAR(viewScaling::MenuUiScale(1520, 800, 1520.0f, 800.0f), 1.0f, kTol);
}

TEST(ViewScalingTest, MenuUiScale_GrowsWithBothDimensions)
{
    // Double both -> scale 2; the menu (spacing + fonts) grows proportionally.
    EXPECT_NEAR(viewScaling::MenuUiScale(3040, 1600, 1520.0f, 800.0f), 2.0f, kTol);
}

TEST(ViewScalingTest, MenuUiScale_ClampedByNarrowerAxis)
{
    // Tall but reference-width window: width ratio (1.0) binds, so text won't
    // overflow horizontally even though height doubled.
    EXPECT_NEAR(viewScaling::MenuUiScale(1520, 1600, 1520.0f, 800.0f), 1.0f, kTol);
    // Narrow window shrinks the whole menu so it still fits.
    EXPECT_NEAR(viewScaling::MenuUiScale(760, 800, 1520.0f, 800.0f), 0.5f, kTol);
}

TEST(ViewScalingTest, RequiredTitleWorldTiles_SmallWindowKeepsBaseSize)
{
    // Default window fits inside the 32x24 base map -> stay at the minimum.
    const glm::ivec2 t = viewScaling::RequiredTitleWorldTiles(
        1520, 800, 5, 16, 16, 1.0f, /*marginTiles=*/2, 32, 24);
    EXPECT_EQ(t.x, 32);
    EXPECT_EQ(t.y, 24);
}

TEST(ViewScalingTest, RequiredTitleWorldTiles_WideWindowGrowsToCover)
{
    // 5120 px wide @ scale 5 -> 1024 world px / 16 = 64 tiles, +2*2 margin = 68.
    const glm::ivec2 t = viewScaling::RequiredTitleWorldTiles(
        5120, 800, 5, 16, 16, 1.0f, /*marginTiles=*/2, 32, 24);
    EXPECT_EQ(t.x, 68);
    EXPECT_EQ(t.y, 24);  // height still fits the base 24.
}
