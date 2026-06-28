#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

/**
 * @namespace viewScaling
 * @brief Renderer-free helpers mapping window pixels to the visible world extent
 *        and to proportional UI scale.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Pure and deliberately free of GL/Vulkan so they are unit-testable without a
 * graphics context (rift_tests constraint). Namespace style mirrors the sibling
 * math helper `perspectiveTransform`.
 */
namespace viewScaling
{
/**
 * @brief Visible world extent in world pixels at zoom 1.0.
 *
 * The single source of truth the ortho projection in Game::Render already uses
 * (screenPixels / PIXEL_SCALE); every camera/particle consumer should match it.
 *
 * @param screenWidth  Window/framebuffer width in pixels.
 * @param screenHeight Window/framebuffer height in pixels.
 * @param pixelScale   Integer pixel-upscale factor (clamped to >= 1).
 * @return Visible world size in world pixels (screen / pixelScale).
 */
inline glm::vec2 VisibleWorldSize(int screenWidth, int screenHeight, int pixelScale)
{
    const float scale = static_cast<float>(std::max(1, pixelScale));
    return {static_cast<float>(screenWidth) / scale, static_cast<float>(screenHeight) / scale};
}

/**
 * @brief Visible world extent after camera zoom (>1 shows less world).
 *
 * @param screenWidth  Window/framebuffer width in pixels.
 * @param screenHeight Window/framebuffer height in pixels.
 * @param pixelScale   Integer pixel-upscale factor (clamped to >= 1).
 * @param zoom         Camera zoom factor (clamped to >= 0.001; >1 shows less world).
 * @return Visible world size at zoom 1.0 divided by the guarded zoom.
 */
inline glm::vec2 VisibleWorldSizeZoomed(int screenWidth,
                                        int screenHeight,
                                        int pixelScale,
                                        float zoom)
{
    const float z = std::max(zoom, 0.001f);
    return VisibleWorldSize(screenWidth, screenHeight, pixelScale) / z;
}

/**
 * @brief Uniform UI scale for menu metrics.
 *
 * Scales with the window so the menu stays proportional, but is clamped by the
 * narrower axis so text never overflows a narrow window.
 *
 * @param screenWidth     Current window width in pixels.
 * @param screenHeight    Current window height in pixels.
 * @param referenceWidth  Design-resolution width the raw pixel constants were
 *                        authored against (the default window size).
 * @param referenceHeight Design-resolution height the raw pixel constants were
 *                        authored against.
 * @return Scale factor (the smaller of the width and height ratios); 1.0 at the
 *         design resolution.
 */
inline float MenuUiScale(int screenWidth,
                         int screenHeight,
                         float referenceWidth,
                         float referenceHeight)
{
    const float wRatio = static_cast<float>(screenWidth) / std::max(referenceWidth, 1.0f);
    const float hRatio = static_cast<float>(screenHeight) / std::max(referenceHeight, 1.0f);
    return std::min(wRatio, hRatio);
}

/**
 * @brief Tile dimensions the title world needs to fully cover the viewport.
 *
 * Sizes the title world to cover the visible viewport plus a margin, never
 * smaller than the base map, so the title grass + particle zones fill any
 * window instead of running past the finite map edge.
 *
 * @param screenWidth  Window/framebuffer width in pixels.
 * @param screenHeight Window/framebuffer height in pixels.
 * @param pixelScale   Integer pixel-upscale factor (clamped to >= 1).
 * @param tileWidth    Tile width in pixels (clamped to >= 1).
 * @param tileHeight   Tile height in pixels (clamped to >= 1).
 * @param zoom         Camera zoom factor (clamped to >= 0.001).
 * @param marginTiles  Extra tiles added on every side beyond the visible area.
 * @param minTilesWide Minimum width in tiles (never returns below this).
 * @param minTilesTall Minimum height in tiles (never returns below this).
 * @return Title-world size in tiles: max(min, ceil(visible / tile) + 2*margin).
 */
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
