#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cmath>

/**
 * @brief View-frustum extraction and containment tests for scene-space culling.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * The one cull test for the 3D path, meant to replace the five inflated-rectangle
 * checks the flat path spreads it across (renderer viewport expansion, world
 * render cull, particle cull, editor overlay cull, no-projection structure cull).
 * A rotating camera has no axis-aligned footprint, so there is nothing to inflate
 * and no pad factor to tune: the six clip planes are the honest test.
 *
 * Only the tile pass is ported so far - see @c Game::RenderFrame3D. Particles,
 * editor overlays and no-projection structures are still drawn by the flat
 * pipeline and have no frustum counterpart yet.
 *
 * Planes are extracted straight from a view-projection matrix by the standard
 * Gribb-Hartmann method: each clip-space inequality @f$ -w \le c \le w @f$
 * rearranges into a plane whose coefficients are a sum or difference of two rows
 * of the matrix. Stored as @c vec4(a,b,c,d) with the convention that
 * @f$ ax + by + cz + d \ge 0 @f$ means *inside*.
 *
 * All tests are conservative: they never reject something visible, but may
 * accept something just outside a corner. That is the correct bias for culling.
 */
namespace frustum
{

/// @brief Indices into a @c Frustum's plane array.
enum PlaneIndex
{
    PLANE_LEFT = 0,
    PLANE_RIGHT = 1,
    PLANE_BOTTOM = 2,
    PLANE_TOP = 3,
    PLANE_NEAR = 4,
    PLANE_FAR = 5,
    PLANE_COUNT = 6
};

/// @brief Six normalized clip planes, inside-positive.
struct Frustum
{
    std::array<glm::vec4, PLANE_COUNT> planes{};
};

/**
 * @brief Extract the six clip planes from a view-projection matrix.
 *
 * Assumes an OpenGL-style clip volume with @f$ z \in [-w, w] @f$, which is what
 * GLM produces by default and what both Rift backends already consume (the
 * Vulkan path applies its own Y flip downstream of this).
 *
 * @param viewProj Combined view-projection matrix (column-major, as GLM builds).
 */
inline Frustum FromViewProjection(const glm::mat4& viewProj)
{
    // GLM is column-major (m[column][row]), so row i is the i-th component of
    // each of the four columns.
    const glm::vec4 rowX{viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]};
    const glm::vec4 rowY{viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]};
    const glm::vec4 rowZ{viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]};
    const glm::vec4 rowW{viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]};

    Frustum f;
    f.planes[PLANE_LEFT] = rowW + rowX;
    f.planes[PLANE_RIGHT] = rowW - rowX;
    f.planes[PLANE_BOTTOM] = rowW + rowY;
    f.planes[PLANE_TOP] = rowW - rowY;
    f.planes[PLANE_NEAR] = rowW + rowZ;
    f.planes[PLANE_FAR] = rowW - rowZ;

    for (glm::vec4& p : f.planes)
    {
        const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len > 1e-6f)
        {
            p /= len;
        }
    }
    return f;
}

/// @brief Signed distance from @p point to @p plane; positive is inside.
inline float SignedDistance(const glm::vec4& plane, glm::vec3 point)
{
    return plane.x * point.x + plane.y * point.y + plane.z * point.z + plane.w;
}

/// @brief Whether a point lies inside all six planes.
inline bool ContainsPoint(const Frustum& f, glm::vec3 point)
{
    for (const glm::vec4& p : f.planes)
    {
        if (SignedDistance(p, point) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether a sphere intersects the frustum.
 *
 * Rejects only when the center is further outside a single plane than the
 * radius, so a sphere straddling a corner is conservatively kept.
 */
inline bool IntersectsSphere(const Frustum& f, glm::vec3 center, float radius)
{
    for (const glm::vec4& p : f.planes)
    {
        if (SignedDistance(p, center) < -radius)
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Whether an axis-aligned box intersects the frustum.
 *
 * Uses the positive-vertex test: for each plane, only the box corner furthest
 * along the plane normal can be inside, so if that corner is outside the whole
 * box is.
 */
inline bool IntersectsAabb(const Frustum& f, glm::vec3 minCorner, glm::vec3 maxCorner)
{
    for (const glm::vec4& p : f.planes)
    {
        const glm::vec3 positiveVertex{p.x >= 0.0f ? maxCorner.x : minCorner.x,
                                       p.y >= 0.0f ? maxCorner.y : minCorner.y,
                                       p.z >= 0.0f ? maxCorner.z : minCorner.z};
        if (SignedDistance(p, positiveVertex) < 0.0f)
        {
            return false;
        }
    }
    return true;
}

}  // namespace frustum
