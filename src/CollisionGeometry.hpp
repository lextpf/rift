#pragma once

#include <glm/glm.hpp>

/**
 * @brief Shared feet-anchored AABB helpers for entity collision tests.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Every character hitbox is anchored at bottom-center: it extends @c halfWidth to each
 * side of the feet and @c boxHeight straight up. These helpers are the single source for building
 * that box and testing two of them for overlap, so the epsilon-shrink convention stays consistent
 * across every call site.
 */
namespace CollisionGeometry
{
/**
 * @struct Aabb
 * @brief Axis-aligned box in world space.
 *
 * World pixels with +Y down, so @ref minY is the top edge and @ref maxY the bottom.
 * @ref MakeFeetAabb builds these rather than call sites aggregate-initializing them. No invariant
 * is enforced: two equal-size boxes inverted by the same over-large epsilon can never overlap, but
 * an inverted box still overlaps a larger normal one, so inversion is not a safety net.
 */
struct Aabb
{
    float minX;  ///< Left edge, in world pixels.
    float maxX;  ///< Right edge, in world pixels.
    float minY;  ///< Top edge, the smaller Y, in world pixels.
    float maxY;  ///< Bottom edge, the larger Y, in world pixels.
};

/**
 * @struct Hitbox
 * @brief Feet-anchored hitbox dimensions.
 *
 * @warning Unused: nothing in the tree constructs or consumes it, and the name shadows the live
 * per-entity ECS component ::Hitbox in Hitbox.hpp. The helpers below take loose @c halfWidth and
 * @c boxHeight floats rather than either struct. Do not build on this one.
 */
struct Hitbox
{
    float halfWidth;  ///< Half-width to each side of the feet, in world pixels.
    float height;     ///< Box height above the feet, in world pixels.
};

/**
 * @brief Build a feet-anchored AABB, anchored at bottom-center.
 *
 * @param feet       Feet position in world pixels.
 * @param halfWidth  Half-width to each side of the feet.
 * @param boxHeight  Box height above the feet.
 * @param eps        Amount to shrink the box inward on every side, which avoids edge-on-edge
 *                   false positives. Pass 0 for an exact box.
 * @return           The resulting box.
 */
inline Aabb MakeFeetAabb(glm::vec2 feet, float halfWidth, float boxHeight, float eps = 0.0f)
{
    return Aabb{
        feet.x - halfWidth + eps, feet.x + halfWidth - eps, feet.y - boxHeight + eps, feet.y - eps};
}

/// @brief Test two axis-aligned boxes for overlap on both axes.
inline bool AabbOverlap(const Aabb& a, const Aabb& b)
{
    return a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY && a.maxY > b.minY;
}

/**
 * @brief Test two same-size feet-anchored boxes for overlap.
 *
 * @param a          Feet position of the first box.
 * @param b          Feet position of the second box.
 * @param halfWidth  Half-width shared by both boxes.
 * @param boxHeight  Height shared by both boxes.
 * @param eps        Inward shrink applied to both boxes before the test.
 * @return           True when the shrunk boxes overlap.
 */
inline bool FeetBoxesOverlap(glm::vec2 a, glm::vec2 b, float halfWidth, float boxHeight, float eps)
{
    return AabbOverlap(MakeFeetAabb(a, halfWidth, boxHeight, eps),
                       MakeFeetAabb(b, halfWidth, boxHeight, eps));
}
}  // namespace CollisionGeometry
