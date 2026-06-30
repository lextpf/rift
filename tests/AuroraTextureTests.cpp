// Renderer-free tests for the procedural aurora texture builders. These assert
// the *shape* of the alpha envelope (feathered edges, profile) without a GPU.
#include <gtest/gtest.h>

#include <algorithm>

#include "AuroraTextures.hpp"

namespace
{
// Alpha (channel 3) of pixel (x,y) in a width*height*4 RGBA8 buffer.
int A(const std::vector<unsigned char>& px, int width, int x, int y)
{
    return px[(static_cast<size_t>(y) * width + x) * 4 + 3];
}

// Highest alpha found anywhere on the 1-pixel border ring (top/bottom rows +
// left/right columns). A seamless, feathered texture must reach ~0 all the way
// around so segment quads dissolve into the sky with no hard seam on any edge.
int MaxBorderAlpha(const std::vector<unsigned char>& px, int width, int height)
{
    int maxA = 0;
    for (int x = 0; x < width; ++x)
    {
        maxA = std::max(maxA, std::max(A(px, width, x, 0), A(px, width, x, height - 1)));
    }
    for (int y = 0; y < height; ++y)
    {
        maxA = std::max(maxA, std::max(A(px, width, 0, y), A(px, width, width - 1, y)));
    }
    return maxA;
}
}  // namespace

TEST(AuroraTextureTests, CurtainSizeAndOpaqueWhiteRGB)
{
    auto px = AuroraTextures::BuildCurtainPixels(128, 256);
    ASSERT_EQ(px.size(), static_cast<size_t>(128) * 256 * 4);
    EXPECT_EQ(px[0], 255);  // R
    EXPECT_EQ(px[1], 255);  // G
    EXPECT_EQ(px[2], 255);  // B
}

TEST(AuroraTextureTests, CurtainHorizontalEdgesAreFeathered)
{
    const int w = 128, h = 256;
    auto px = AuroraTextures::BuildCurtainPixels(w, h);
    const int midRow = h * 6 / 10;                            // near the vertical peak
    EXPECT_LT(A(px, w, 0, midRow), 12);                       // left border ~transparent
    EXPECT_LT(A(px, w, w - 1, midRow), 12);                   // right border ~transparent
    EXPECT_GT(A(px, w, w / 2, midRow), A(px, w, 2, midRow));  // center brighter than edge
}

TEST(AuroraTextureTests, BeamIsSoftFeatheredVerticalOval)
{
    const int w = 64, h = 256;
    auto px = AuroraTextures::BuildBeamPixels(w, h);
    // Brightest near the vertical center, feathering to ~0 at BOTH ends (floats,
    // no hard base) with oval sides that fade out.
    const int center = A(px, w, w / 2, h / 2);
    EXPECT_GT(center, A(px, w, w / 2, 4));      // fades toward the top
    EXPECT_GT(center, A(px, w, w / 2, h - 5));  // fades toward the bottom (soft base)
    EXPECT_LT(A(px, w, w / 2, 4), 40);          // top feathered
    EXPECT_LT(A(px, w, w / 2, h - 5), 40);      // base feathered (no hard cut)
    EXPECT_LT(A(px, w, 0, h / 2), 12);          // oval side ~transparent
    EXPECT_LT(A(px, w, w - 1, h / 2), 12);
}

TEST(AuroraTextureTests, CurtainEntireBorderFeathersToZero)
{
    const int w = 128, h = 256;
    auto px = AuroraTextures::BuildCurtainPixels(w, h);
    // Every edge - including the top and bottom rows - must fade to ~0 so the
    // ribbon's silhouette blends seamlessly into the sky with no hard cut.
    EXPECT_LE(MaxBorderAlpha(px, w, h), 3);
    EXPECT_LT(A(px, w, w / 2, 0), 4);       // top-center feathered (was a hard ~25)
    EXPECT_LT(A(px, w, w / 2, h - 1), 4);   // bottom-center feathered
    EXPECT_GT(A(px, w, w / 2, h / 2), 60);  // interior stays bright (feather didn't blank it)
}

TEST(AuroraTextureTests, BeamEntireBorderFeathersToZero)
{
    const int w = 64, h = 256;
    auto px = AuroraTextures::BuildBeamPixels(w, h);
    // The beam floats free, so all four borders - top and bottom included -
    // must reach ~0; no edge should read as a hard line against the sky.
    EXPECT_LE(MaxBorderAlpha(px, w, h), 3);
    EXPECT_LT(A(px, w, w / 2, 0), 4);       // top-center feathered (was a hard ~15)
    EXPECT_LT(A(px, w, w / 2, h - 1), 4);   // bottom-center feathered
    EXPECT_GT(A(px, w, w / 2, h / 2), 60);  // interior stays bright
}
