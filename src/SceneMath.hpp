#pragma once

#include <glm/glm.hpp>

#include <algorithm>

/**
 * @brief The world-pixel <-> 3D scene-space seam.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Rendering
 *
 * Rift's simulation is 2D: world positions are pixels with X growing right and
 * Y growing *down* the map. The 3D path renders that same world in scene space.
 * This header owns the single lift between the two, so no call site open-codes
 * the conversion.
 *
 * The mapping is a pure relabelling of axes plus a height component:
 * @verbatim
 *   world (2D, Y-down)              scene (3D, Y-up, right-handed)
 *
 *      +--------> +X (east)              +Y (up)
 *      |                                  |
 *      |                                  |    +Z (south, toward the camera
 *      v                                  |   /      at yaw 0)
 *     +Y (south)                          |  /
 *                                         | /
 *                                         +-------> +X (east)
 *
 *   scene = (worldX, height, worldY)
 * @endverbatim
 *
 * @par Why this handedness
 * `east x up = south`, so (X=east, Y=up, Z=south) is right-handed and matches
 * OpenGL's convention of a camera looking down @c -Z. At yaw 0 the camera
 * therefore sits south of its focus looking north, which is exactly the classic
 * top-down RPG orientation - and at pitch 90 degrees the projected image has
 * screen +X = world +X and screen +Y = world +Y, i.e. it reproduces the flat
 * path's orthographic view pixel for pixel. That equality is the regression
 * baseline for the whole 3D overhaul.
 *
 * @par Units
 * 1 world pixel == 1 scene unit. Nothing is rescaled, so gameplay distances,
 * hitboxes and speeds keep their existing numeric meaning. Tile size still comes
 * from @c ProjectManifest and is never assumed to be 16.
 *
 * @par Elevation is a height, but only for layers that opt in
 * @c Tilemap::GetElevation is a per-cell pixel height driving collision,
 * navigation and the support graph. It becomes a scene height for a layer only
 * where that layer's @ref ElevationRole says so - see
 * @ref elevationRole::SurfaceHeight.
 *
 * The opt-in is per layer on purpose. A bridge cell has one elevation, but the
 * water painted beneath the deck must stay on the ground; lifting the whole cell
 * would carry the water up with the bridge.
 *
 * Characters move vertically by @c Elevation::offset, which tracks the same pixel
 * value 1:1, so an actor and the surface it stands on agree without a scale factor.
 *
 * @see CameraRig, Billboard, TileMath
 */
namespace sceneMath
{

/// @brief Lift a world position onto the scene ground plane (or to @p height).
inline glm::vec3 ToScene(glm::vec2 world, float height = 0.0f)
{
    return {world.x, height, world.y};
}

/// @brief Drop a scene position back to world pixels, discarding its height.
inline glm::vec2 ToWorld(glm::vec3 scene)
{
    return {scene.x, scene.z};
}

/**
 * @brief Corner order every quad in the 3D path uses: top-left, top-right,
 *        bottom-right, bottom-left.
 *
 * Matches the ordering the 2D sprite path already took, so texture coordinate
 * derivation in the renderers is unchanged.
 */
enum QuadCorner
{
    QUAD_TOP_LEFT = 0,
    QUAD_TOP_RIGHT = 1,
    QUAD_BOTTOM_RIGHT = 2,
    QUAD_BOTTOM_LEFT = 3,
    QUAD_CORNER_COUNT = 4
};

/**
 * @brief Build a quad from its centre and a pair of in-plane axes, applying the
 *        artwork's own rotation.
 *
 * The single primitive behind both flat ground tiles and upright billboards, so
 * per-tile rotation behaves identically for either. The axes are named for what
 * they mean on screen rather than in the world:
 *
 * - @p axisRight is the artwork's +X, pointing right across the image.
 * - @p axisDown is the artwork's +Y, pointing DOWN the image.
 *
 * Naming them that way is what lets the rotation match the flat pipeline
 * exactly. @c IRenderer::RotateCorners rotates screen-space corners in a Y-down
 * frame, so reusing its formula only gives the same result if the basis is
 * Y-down too:
 * @f[
 *   \vec{u}' = \cos\theta\,\vec{u} + \sin\theta\,\vec{v}, \qquad
 *   \vec{v}' = -\sin\theta\,\vec{u} + \cos\theta\,\vec{v}
 * @f]
 * Getting @p axisDown's sign wrong mirrors every rotated tile.
 *
 * @param centre           Quad centre in scene space.
 * @param size             Quad width and height in world pixels.
 * @param axisRight        Unit vector along the artwork's +X.
 * @param axisDown         Unit vector along the artwork's +Y (downward).
 * @param rotationDegrees  Per-tile rotation, same convention as the 2D path.
 * @param outCorners       Receives 4 corners in @ref QuadCorner order.
 */
inline void MakeOrientedQuad(glm::vec3 centre,
                             glm::vec2 size,
                             glm::vec3 axisRight,
                             glm::vec3 axisDown,
                             float rotationDegrees,
                             glm::vec3 outCorners[QUAD_CORNER_COUNT])
{
    glm::vec3 u = axisRight;
    glm::vec3 v = axisDown;

    if (std::abs(rotationDegrees) > 1e-6f)
    {
        const float rad = rotationDegrees * 3.14159265f / 180.0f;
        const float cosR = std::cos(rad);
        const float sinR = std::sin(rad);
        const glm::vec3 rotatedU = cosR * axisRight + sinR * axisDown;
        const glm::vec3 rotatedV = -sinR * axisRight + cosR * axisDown;
        u = rotatedU;
        v = rotatedV;
    }

    const glm::vec3 halfW = u * (size.x * 0.5f);
    const glm::vec3 halfH = v * (size.y * 0.5f);

    outCorners[QUAD_TOP_LEFT] = centre - halfW - halfH;
    outCorners[QUAD_TOP_RIGHT] = centre + halfW - halfH;
    outCorners[QUAD_BOTTOM_RIGHT] = centre + halfW + halfH;
    outCorners[QUAD_BOTTOM_LEFT] = centre - halfW + halfH;
}

/**
 * @brief Build a tile-sized quad lying flat on the ground plane.
 *
 * @p worldTopLeft is the tile's top-left corner in world pixels, matching how
 * @c Tilemap already addresses tiles (`tileX * tileWidth`, `tileY * tileHeight`).
 * Because neighboring tiles derive their shared edge from the identical
 * expression, adjacent quads agree exactly and no seam-hiding inflation is
 * needed - a per-quad fudge factor would only be required if each quad were
 * transformed independently.
 *
 * On the ground plane the artwork's +Y runs south (scene +Z), because that is
 * the direction that reads as "down the screen" in a top-down view.
 *
 * @param worldTopLeft    Tile top-left in world pixels.
 * @param size            Tile size in world pixels.
 * @param height          Scene height of the ground under this tile.
 * @param rotationDegrees Per-tile artwork rotation.
 * @param outCorners      Receives 4 corners in @ref QuadCorner order.
 */
inline void MakeGroundQuad(glm::vec2 worldTopLeft,
                           glm::vec2 size,
                           float height,
                           float rotationDegrees,
                           glm::vec3 outCorners[QUAD_CORNER_COUNT])
{
    const glm::vec3 centre = ToScene(worldTopLeft + size * 0.5f, height);
    MakeOrientedQuad(centre,
                     size,
                     glm::vec3(1.0f, 0.0f, 0.0f),  // artwork +X = world east
                     glm::vec3(0.0f, 0.0f, 1.0f),  // artwork +Y = world south
                     rotationDegrees,
                     outCorners);
}

/**
 * @brief Re-sample a ground quad's corner heights across a sloping cell.
 *
 * Applied after @ref MakeGroundQuad, which has already placed the corners in XZ
 * and applied per-tile rotation. Heights are sampled from each corner's own world
 * position rather than assigned by corner index, which is what keeps a rotated
 * ramp sloping along the world axis instead of rotating the slope with the artwork.
 *
 * A consequence of sampling by position: a rotated quad's corners can fall outside the
 * cell itself, so @c t leaves [0,1] and the height extrapolates past @p heightMinus /
 * @p heightPlus rather than clamping. That is why a rotated ramp does not seam-close with
 * its neighbors the way an unrotated one does - acceptable, because rotated ground tiles
 * already overhang their cells for the same reason, but worth stating rather than
 * discovering.
 *
 * @p heightMinus and @p heightPlus are equal for a level cell, which collapses the
 * interpolation to a constant - so @c Ground and @c Raised come through this
 * function bit-identical to a flat quad and need no separate path.
 *
 * @param worldTopLeft  Cell top-left in world pixels.
 * @param size          Cell size in world pixels.
 * @param heightMinus   Scene height at the low-coordinate edge (west or north).
 * @param heightPlus    Scene height at the high-coordinate edge (east or south).
 * @param alongZ        true to slope north-south (scene Z), false east-west (X).
 * @param outCorners    Corners to adjust in place, in @ref QuadCorner order.
 */
inline void ApplySlopeHeights(glm::vec2 worldTopLeft,
                              glm::vec2 size,
                              float heightMinus,
                              float heightPlus,
                              bool alongZ,
                              glm::vec3 outCorners[QUAD_CORNER_COUNT])
{
    const float span = alongZ ? size.y : size.x;
    const float origin = alongZ ? worldTopLeft.y : worldTopLeft.x;
    for (int i = 0; i < QUAD_CORNER_COUNT; ++i)
    {
        const float along = alongZ ? outCorners[i].z : outCorners[i].x;
        const float t = (span != 0.0f) ? (along - origin) / span : 0.0f;
        outCorners[i].y = heightMinus + (heightPlus - heightMinus) * t;
    }
}

}  // namespace sceneMath
