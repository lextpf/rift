#include "SurfaceSystem.hpp"

#include "CharacterConstants.hpp"
#include "ElevationAxis.hpp"
#include "Tilemap.hpp"

#include <algorithm>
#include <cmath>

namespace
{
bool MovementMatchesAxis(ElevationAxis axis, int moveDx, int moveDy)
{
    // A ramp connector is an edge in the support graph, not a loose directional
    // hint. Crossing its side or its diagonal corner must not engage it.
    return (axis == ElevationAxis::X && moveDx != 0 && moveDy == 0) ||
           (axis == ElevationAxis::Y && moveDy != 0 && moveDx == 0);
}

SurfaceTransition ResolveBoundary(SupportState current,
                                  glm::vec2 from,
                                  glm::vec2 to,
                                  const Tilemap& tilemap)
{
    int sourceX = 0;
    int sourceY = 0;
    int destinationX = 0;
    int destinationY = 0;
    tilemap.WorldToTileCoord(from.x, from.y, sourceX, sourceY);
    tilemap.WorldToTileCoord(to.x, to.y, destinationX, destinationY);

    if (sourceX == destinationX && sourceY == destinationY)
    {
        return {current, true};
    }

    const int moveDx = (destinationX > sourceX) ? 1 : (destinationX < sourceX) ? -1 : 0;
    const int moveDy = (destinationY > sourceY) ? 1 : (destinationY < sourceY) ? -1 : 0;
    const int sourceHeight = tilemap.GetElevation(sourceX, sourceY);
    const int destinationHeight = tilemap.GetElevation(destinationX, destinationY);

    if (current.surface == SupportSurface::Ground)
    {
        // Ground is continuous underneath every elevated footprint. Enter the
        // elevation surface only from a genuinely ground-only source cell into
        // the low end of a ramp along its authored/derived run.
        const bool entersFromBareGround = sourceHeight == 0 && destinationHeight != 0;
        const bool stepIsReachable =
            std::abs(destinationHeight) <= CharacterConstants::MAX_STEP_HEIGHT;
        const bool entersAlongRamp = MovementMatchesAxis(
            tilemap.GetElevationAxisAt(destinationX, destinationY), moveDx, moveDy);

        if (entersFromBareGround && stepIsReachable && entersAlongRamp)
        {
            return {{SupportSurface::Elevation, destinationHeight}, true};
        }

        return {{SupportSurface::Ground, 0}, true};
    }

    if (destinationHeight != 0)
    {
        // Once supported by elevation, adjacency supplies the topology. Height
        // may vary along a ramp, but an abrupt change is not a connected edge.
        if (std::abs(destinationHeight - current.height) > CharacterConstants::MAX_STEP_HEIGHT)
        {
            return {current, false};
        }
        return {{SupportSurface::Elevation, destinationHeight}, true};
    }

    // Leave elevation only through the low end of its ramp. Deck edges without
    // a ramp are disconnected instead of silently dropping the character.
    const bool exitIsReachable = std::abs(sourceHeight) <= CharacterConstants::MAX_STEP_HEIGHT;
    const bool exitsAlongRamp =
        MovementMatchesAxis(tilemap.GetElevationAxisAt(sourceX, sourceY), moveDx, moveDy);
    if (sourceHeight != 0 && exitIsReachable && exitsAlongRamp)
    {
        return {{SupportSurface::Ground, 0}, true};
    }

    return {current, false};
}
}  // namespace

namespace SurfaceSystem
{
SurfaceTransition ResolveMove(SupportState current,
                              glm::vec2 from,
                              glm::vec2 to,
                              const Tilemap& tilemap)
{
    const float tileWidth = static_cast<float>(tilemap.GetTileWidth());
    const float tileHeight = static_cast<float>(tilemap.GetTileHeight());
    const float stepLength = std::max(1.0f, std::min(tileWidth, tileHeight) * 0.45f);
    const glm::vec2 delta = to - from;
    const float longestAxis = std::max(std::abs(delta.x), std::abs(delta.y));
    const int steps = std::max(1, static_cast<int>(std::ceil(longestAxis / stepLength)));

    glm::vec2 cursor = from;
    SupportState support = current;
    for (int step = 1; step <= steps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const glm::vec2 next = from + delta * t;
        const SurfaceTransition transition = ResolveBoundary(support, cursor, next, tilemap);
        if (!transition.connected)
        {
            return transition;
        }
        support = transition.support;
        cursor = next;
    }

    return {support, true};
}

bool CollisionBelongsTo(const Tilemap& tilemap, int tileX, int tileY, SupportState support)
{
    const int collisionHeight = tilemap.GetElevation(tileX, tileY);
    if (support.surface == SupportSurface::Ground)
    {
        return collisionHeight == 0;
    }
    return collisionHeight != 0 && collisionHeight == support.height;
}
}  // namespace SurfaceSystem
