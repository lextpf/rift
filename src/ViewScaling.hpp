#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

/// Pure, renderer-free helpers mapping window pixels to the visible world extent
/// and to proportional UI / title-world scaling. Deliberately free of GL/Vulkan
/// so they are unit-testable without a graphics context (rift_tests constraint).
/// Namespace style mirrors the sibling math helper `perspectiveTransform`.
namespace viewScaling
{
/// Visible world extent in world pixels at zoom 1.0. This is the single source
/// of truth the ortho projection in Game::Render already uses
/// (screenPixels / PIXEL_SCALE) -- every camera/particle consumer should match it.
inline glm::vec2 VisibleWorldSize(int screenWidth, int screenHeight, int pixelScale)
{
    const float scale = static_cast<float>(std::max(1, pixelScale));
    return {static_cast<float>(screenWidth) / scale, static_cast<float>(screenHeight) / scale};
}

/// Visible world extent after camera zoom (>1 shows less world). Guards zoom.
inline glm::vec2 VisibleWorldSizeZoomed(int screenWidth,
                                        int screenHeight,
                                        int pixelScale,
                                        float zoom)
{
    const float z = std::max(zoom, 0.001f);
    return VisibleWorldSize(screenWidth, screenHeight, pixelScale) / z;
}

/// Uniform UI scale for menu metrics. Scales with the window so the menu stays
/// proportional, but is clamped by the narrower axis so text never overflows a
/// narrow window. referenceWidth/Height = the design resolution the raw pixel
/// constants were authored against (the default window size). Returns 1.0 there.
inline float MenuUiScale(int screenWidth,
                         int screenHeight,
                         float referenceWidth,
                         float referenceHeight)
{
    const float wRatio = static_cast<float>(screenWidth) / std::max(referenceWidth, 1.0f);
    const float hRatio = static_cast<float>(screenHeight) / std::max(referenceHeight, 1.0f);
    return std::min(wRatio, hRatio);
}

/// Tiles the title world needs to fully cover the visible viewport (+ margin),
/// never smaller than the base map. Lets the title grass + particle zones fill
/// any window instead of running past the finite map edge.
inline glm::ivec2 RequiredTitleWorldTiles(int screenWidth,
                                          int screenHeight,
                                          int pixelScale,
                                          int tileWidth,
                                          int tileHeight,
                                          float zoom,
                                          int marginTiles,
                                          int minTilesWide,
                                          int minTilesTall)
{
    const glm::vec2 world = VisibleWorldSizeZoomed(screenWidth, screenHeight, pixelScale, zoom);
    const int tw = std::max(1, tileWidth);
    const int th = std::max(1, tileHeight);
    const int needW =
        static_cast<int>(std::ceil(world.x / static_cast<float>(tw))) + 2 * marginTiles;
    const int needH =
        static_cast<int>(std::ceil(world.y / static_cast<float>(th))) + 2 * marginTiles;
    return {std::max(minTilesWide, needW), std::max(minTilesTall, needH)};
}
}  // namespace viewScaling
