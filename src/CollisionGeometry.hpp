#pragma once

#include <glm/glm.hpp>

/**
 * @brief Shared feet-anchored AABB helpers for entity collision tests.
 * @author Alex (https://github.com/lextpf)
 * @ingroup World
 *
 * Every character hitbox in Rift is a bottom-center (feet) anchored box: it
 * extends @c halfWidth left/right of the feet and @c boxHeight straight up.
 * These helpers are the single source for building that box and testing two of
 * them for overlap, replacing ~6 hand-inlined copies that had drifted on the
 * epsilon-shrink convention. @p eps shrinks the box inward on every side to
 * avoid edge-on-edge false positives; pass 0 for an exact (non-shrunk) test.
 */

namespace CollisionGeometry
{
/**
 * @brief Axis-aligned box in world space.
 */
struct Aabb
{
    float minX, maxX, minY, maxY;
};

/**
 * @brief Feet-anchored hitbox dimensions: @c halfWidth left/right of the feet,
 * @c height straight up.
 *
 * Lets the collision pipeline run for any entity size instead of a hardcoded
 * player hitbox.
 */
struct Hitbox
{
    float halfWidth;
    float height;
};

/**
 * @brief Build a feet-anchored (bottom-center) AABB, optionally epsilon-shrunk.
 */
inline Aabb MakeFeetAabb(glm::vec2 feet, float halfWidth, float boxHeight, float eps = 0.0f)
{
    return Aabb{
        feet.x - halfWidth + eps, feet.x + halfWidth - eps, feet.y - boxHeight + eps, feet.y - eps};
}

/**
 * @brief Separating-axis overlap test for two AABBs.
 */
inline bool AabbOverlap(const Aabb& a, const Aabb& b)
{
    return a.minX < b.maxX && a.maxX > b.minX && a.minY < b.maxY && a.maxY > b.minY;
}

/**
 * @brief Overlap test for two same-size feet-anchored boxes (eps-shrunk).
 */
inline bool FeetBoxesOverlap(glm::vec2 a, glm::vec2 b, float halfWidth, float boxHeight, float eps)
{
    return AabbOverlap(MakeFeetAabb(a, halfWidth, boxHeight, eps),
                       MakeFeetAabb(b, halfWidth, boxHeight, eps));
}
}  // namespace CollisionGeometry
