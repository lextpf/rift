#pragma once

#include "EnumTraits.hpp"
#include "SceneMath.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <iterator>
#include <string_view>

/**
 * @brief Damped billboard orientation for upright 2D artwork in the 3D scene.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Ground artwork lies flat (see @ref sceneMath::MakeGroundQuad). Everything that
 * should read as *standing up* - characters, trees, fences, signs - is a quad
 * whose orientation is interpolated between grid-aligned and camera-facing. Two
 * independent damping factors control how much of the camera it follows:
 *
 * - @c yawFollow  in [0,1]: fraction of the camera's yaw the quad turns through.
 *   1 = always square to the camera; 0 = permanently facing map-south.
 * - @c leanFollow in [0,1]: fraction of the camera's elevation the quad tips
 *   through. 1 = exactly perpendicular to the view ray; 0 = bolt upright.
 *
 * @par The lean identity
 * A quad faces the camera exactly when its @c lean (from vertical) equals the
 * camera's @c pitch (from the horizon) - two angles off *different* axes, which is
 * why the answer is not @f$ 90^\circ - pitch @f$. Lay every direction in the
 * camera's vertical plane on one dial, turning up from @c h (south, toward the
 * camera) through @c v (up) round to map-north:
 * @verbatim
 *
 *      0           pitch           90        90 + lean        180
 *      |-------------|-------------|-------------|-------------|
 *    south          eye          v (up)       quad up        north
 *
 *                    |<------- must be 90 ------>|
 *                (90 + lean) - pitch = 90  =>  lean = pitch
 *
 * @endverbatim
 * The quad's up axis has to sit square to the line of sight, and the gap between
 * them is @f$ (90^\circ + lean) - pitch @f$ - so the two angles have to move
 * together. In vector form @f$ \vec{up} \cdot \vec{view} = \sin(lean - pitch) @f$;
 * @ref ApparentHeightScale keys off the same difference.
 *
 * Note the quad's top tips *away* from the camera (toward map-north): under an
 * overhead camera a fully-facing quad lies flat on the ground pointing north, not
 * standing up.
 *
 * @par Apparent height
 * A quad of length @c h renders @f$ h \cdot |\cos(lean - pitch)| @f$ tall on
 * screen - see @ref ApparentHeightScale. Under a camera 51.3 degrees up, a bolt
 * upright quad is squashed to 62.5%; a fully leaning one keeps 100%.
 *
 * @see sceneMath, CameraRig
 */
namespace billboard
{

/**
 * @enum Role
 * @brief Which damping profile a piece of upright artwork uses.
 *
 * Flat ground artwork has no role - it is not a billboard at all and goes
 * through @ref sceneMath::MakeGroundQuad instead.
 */
enum class Role
{
    Scenery = 0,   ///< Tile artwork that stands up: trees, fences, signs, buildings.
    Character = 1  ///< Player and NPC sprites.
};

/// @brief Per-role damping factors, both in [0,1].
struct Damping
{
    float yawFollow = 1.0f;   ///< Fraction of camera yaw the quad turns through.
    float leanFollow = 1.0f;  ///< Fraction of camera elevation the quad tips through.
};

/// @brief Number of entries in @ref Role.
inline constexpr std::size_t ROLE_COUNT = 2;

/**
 * @brief Default damping per @ref Role, indexed by the enumerator's value.
 *
 * Scenery deliberately under-follows in yaw, which is what keeps a row of fences
 * from visibly pivoting in lockstep as the camera orbits. Characters follow yaw
 * fully so a sprite is never seen edge-on, but under-lean slightly so they keep a
 * little upright presence when the camera looks steeply down.
 */
inline constexpr Damping DEFAULT_DAMPING[ROLE_COUNT] = {
    /* Scenery   */ {0.50f, 0.80f},
    /* Character */ {1.00f, 0.90f},
};

/// @brief Damping profile for @p role.
inline constexpr Damping DefaultDamping(Role role)
{
    return DEFAULT_DAMPING[static_cast<std::size_t>(role)];
}

/**
 * @brief On-screen height multiplier for a quad leaning @p leanRadians under a
 *        camera @p pitchRadians above the horizon.
 *
 * Exposed so callers can compensate (or deliberately not) for the foreshortening
 * that under-leaning introduces, and so tests can pin the identity directly.
 */
inline float ApparentHeightScale(float leanRadians, float pitchRadians)
{
    return std::abs(std::cos(leanRadians - pitchRadians));
}

/**
 * @struct Orientation
 * @brief The orthonormal in-plane axes of an oriented billboard quad.
 */
struct Orientation
{
    glm::vec3 right{1.0f, 0.0f, 0.0f};  ///< Unit vector along the quad's width.
    glm::vec3 up{0.0f, 1.0f, 0.0f};     ///< Unit vector along the quad's height.
    float yawRadians = 0.0f;            ///< Damped yaw actually applied.
    float leanRadians = 0.0f;           ///< Damped lean from vertical actually applied.
};

/**
 * @brief Resolve the damped orientation for a billboard under a given camera.
 *
 * @param cameraYawRadians   Camera yaw; 0 places the camera due map-south.
 * @param cameraPitchRadians Camera elevation above the horizon; pi/2 is top-down.
 * @param damping            Damping factors, normally @ref DefaultDamping.
 */
inline Orientation Orient(float cameraYawRadians, float cameraPitchRadians, Damping damping)
{
    Orientation o;
    o.yawRadians = damping.yawFollow * cameraYawRadians;
    o.leanRadians = damping.leanFollow * cameraPitchRadians;

    const float sy = std::sin(o.yawRadians);
    const float cy = std::cos(o.yawRadians);
    const float sl = std::sin(o.leanRadians);
    const float cl = std::cos(o.leanRadians);

    // Horizontal axis across the quad, and the horizontal direction pointing
    // away from the camera (map-north at yaw 0). The top edge tips along the
    // latter, so a fully-leaning quad lies flat pointing north.
    o.right = {cy, 0.0f, -sy};
    const glm::vec3 awayFromCamera{-sy, 0.0f, -cy};

    o.up = cl * glm::vec3(0.0f, 1.0f, 0.0f) + sl * awayFromCamera;
    return o;
}

/**
 * @brief Build an upright quad standing on the ground at @p footCenter.
 *
 * @param footCenter      Scene position of the quad's bottom-center (the
 *                        sprite's feet, matching Rift's feet-anchored @c Hitbox
 *                        convention).
 * @param size            Quad width and height in world pixels.
 * @param o               Orientation from @ref Orient.
 * @param outCorners      Receives 4 corners in @ref sceneMath::QuadCorner order.
 * @param rotationDegrees Artwork rotation within the quad's own plane, same
 *                        convention as the flat path's per-tile rotation.
 *                        Characters leave this at 0.
 */
inline void MakeQuad(glm::vec3 footCenter,
                     glm::vec2 size,
                     const Orientation& o,
                     glm::vec3 outCorners[sceneMath::QUAD_CORNER_COUNT],
                     float rotationDegrees = 0.0f)
{
    // Routed through the shared primitive so per-tile rotation behaves the same
    // for upright artwork as for flat. Note axisDown is -up: the quad's own
    // "down the image" points from its head toward its feet.
    const glm::vec3 centre = footCenter + o.up * (size.y * 0.5f);
    sceneMath::MakeOrientedQuad(centre, size, o.right, -o.up, rotationDegrees, outCorners);
}

/// @brief Convenience overload orienting and building in one step.
inline void MakeQuad(glm::vec3 footCenter,
                     glm::vec2 size,
                     float cameraYawRadians,
                     float cameraPitchRadians,
                     Damping damping,
                     glm::vec3 outCorners[sceneMath::QUAD_CORNER_COUNT])
{
    MakeQuad(footCenter, size, Orient(cameraYawRadians, cameraPitchRadians, damping), outCorners);
}

}  // namespace billboard

/// @brief Reflection for @ref billboard::Role: name/value round-tripping for tests
/// and diagnostics. No console command reads it.
template <>
struct EnumTraits<billboard::Role> : EnumTraitsBase<billboard::Role, EnumTraits<billboard::Role>>
{
    static constexpr std::size_t Count = billboard::ROLE_COUNT;
    static constexpr std::string_view Names[] = {"Scenery", "Character"};
};

static_assert(std::size(EnumTraits<billboard::Role>::Names) == billboard::ROLE_COUNT,
              "billboard::Role names must stay in step with ROLE_COUNT");
