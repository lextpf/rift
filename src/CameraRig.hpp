#pragma once

#include "EnumTraits.hpp"
#include "MathConstants.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>

/**
 * @brief Orbit-camera geometry: view/projection construction, unprojection and
 *        ground-plane queries.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Renderer-free by construction (no GL/Vulkan types, no @c IRenderer) so it is
 * unit-testable without a graphics context, matching the @c rift_tests rule.
 * Nothing here is stateful. @c Game owns the live orbit state (preset, yaw,
 * pitch) and rebuilds a @ref RigParams every frame in @c Game::BuildCameraRig,
 * which reinterprets the flat @ref CameraController position - a viewport
 * top-left - as a ground focus point.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 *     G["Game: m_CameraPreset, m_CameraYaw, m_CameraPitch"]
 *     C["CameraController: position, zoom"]
 *     B["Game::BuildCameraRig"]
 *     R["RigParams<br/>rebuilt every frame, never stored"]
 *     G --> B
 *     C --> B
 *     B --> R
 *     R --> VP["BuildViewProjection"]
 *     VP --> SVP["IRenderer::SetViewProjection"]
 *     VP --> FF["frustum::FromViewProjection"]
 *     FF --> IS["per-tile IntersectsSphere"]
 *     R --> GF["GroundFootprintAabb"]
 *     GF --> TR["Tilemap::ComputeTileRange"]
 *     R --> OR["billboard::Orient"]
 *     OR --> DR["character and upright-tile quads"]
 * </pre>
 * @endhtmlonly
 *
 * Every transform is a matrix with an inverse, so a picking path built on
 * @ref ScreenToGround is guaranteed to agree with what @ref BuildViewProjection
 * drew.
 * @note The editor is not ported yet. It still picks through the flat
 * screen-to-world formula, so picking is wrong at any non-zero yaw.
 *
 * The camera looks at a focus point on the ground plane and orbits it at
 * @c distance, aimed by two angles. Yaw is the eye's compass bearing around the
 * focus - a rotation about scene +Y - shown here looking straight down, in the
 * `(-pi, pi]` range @ref WrapYaw maintains:
 * @verbatim
 *                       yaw = pi
 *                    north (map -Y)
 *                          |
 *                          |
 *   yaw = -pi/2  ----------+----------  yaw = +pi/2
 *   west (map -X)          |            east (map +X)
 *                          |
 *                    south (map +Y)
 *                       yaw = 0
 * @endverbatim
 * So yaw 0 puts the eye due south of the focus, looking north. Along that
 * bearing the eye stands `distance * cos(pitch)` out from the focus and
 * `distance * sin(pitch)` above the plane, so pitch pi/2 is straight overhead.
 * Scene space is map space plus height: scene +X = map +X (east), scene +Z =
 * map +Y (south), scene +Y = up.
 *
 * At @c yaw 0, @c pitch pi/2 and @ref ProjectionKind::Orthographic the image is
 * the flat path's top-down view exactly: screen +X is world +X and screen +Y is
 * world +Y. That is the @ref Preset::Classic regression baseline.
 *
 * @see sceneMath, billboard, frustum, CameraController
 */
namespace cameraRig
{

/// @brief Whether the projection converges (perspective) or not (orthographic).
enum class ProjectionKind
{
    Orthographic = 0,
    Perspective = 1
};

/// @brief Named camera configurations.
enum class Preset
{
    /// Flat top-down orthographic. Pixel-identical to the pre-overhaul view.
    Classic = 0,
    /// A long lens at a distance is very nearly orthographic, which is what
    /// keeps tile artwork readable instead of visibly keystoning.
    DS = 1,
    /// User-driven orbit; angles come from mouse drag rather than the preset.
    Free = 2
};

/// @brief Number of entries in @ref ProjectionKind.
inline constexpr std::size_t PROJECTION_KIND_COUNT = 2;
/// @brief Number of entries in @ref Preset.
inline constexpr std::size_t PRESET_COUNT = 3;

/// @name Angle and framing limits
/// @{
/**
 * @brief Shallowest elevation the camera may orbit to.
 *
 * Below this the ground plane is nearly edge-on: the visible footprint runs away
 * toward the horizon and ground-plane picking becomes numerically useless.
 */
inline constexpr float MIN_PITCH_RADIANS = 10.0f * rift::PiF / 180.0f;
/// Straight overhead. Also the @ref Preset::Classic value.
inline constexpr float MAX_PITCH_RADIANS = rift::PiF * 0.5f;
/// Elevation of the real DS overworld camera: atan(35 / 28).
inline constexpr float DS_PITCH_RADIANS = 51.34f * rift::PiF / 180.0f;
/// Vertical FOV of the real DS overworld camera.
inline constexpr float DS_FOV_RADIANS = 15.0f * rift::PiF / 180.0f;
/**
 * @brief Wider FOV intended for the editor's 3D view, where seeing more context
 *        beats matching the game.
 *
 * @note Not wired up. No caller reads it, and @ref ApplyPreset overwrites
 * @c fovYRadians for @ref Preset::Classic and @ref Preset::DS.
 */
inline constexpr float EDITOR_FOV_RADIANS = 30.0f * rift::PiF / 180.0f;
/// @}

/**
 * @struct RigParams
 * @brief Everything needed to build a view and a projection.
 */
struct RigParams
{
    /// Ground focus point in world pixels (look-at point).
    glm::vec2 target{0.0f};
    float focusHeight = 0.0f;  ///< Scene height of the ground plane under the focus.
    float yawRadians = 0.0f;   ///< 0 places the eye due map-south of the focus.
    /// Elevation above the horizon; pi/2 is straight down. See @ref ClampPitch.
    float pitchRadians = MAX_PITCH_RADIANS;
    /// Vertical field of view. Also sets the orbit distance, and hence the depth slab,
    /// under @ref ProjectionKind::Orthographic - where it has no effect on the image.
    float fovYRadians = DS_FOV_RADIANS;
    ProjectionKind kind = ProjectionKind::Orthographic;
    /**
     * @brief Visible world extent in world pixels after zoom, i.e. what
     *        `viewScaling::VisibleWorldSizeZoomed` already returns.
     *
     * Drives the orthographic half-extents and, through
     * @ref DistanceForVisibleHeight, the perspective orbit distance - so `zoom`
     * keeps its existing meaning under both projections.
     */
    glm::vec2 visibleWorldSize{320.0f, 180.0f};
    /// Half-extent of scene content to keep inside the depth range, in world
    /// pixels. Normally about the map diagonal.
    float sceneRadius = 4096.0f;
};

/**
 * @struct Basis
 * @brief Derived camera frame in scene space.
 */
struct Basis
{
    glm::vec3 eye{0.0f};      ///< Camera position.
    glm::vec3 focus{0.0f};    ///< Look-at point on the ground plane.
    glm::vec3 forward{0.0f};  ///< Unit vector from @c eye toward @c focus.
    glm::vec3 right{0.0f};    ///< Unit vector to the camera's right.
    glm::vec3 up{0.0f};       ///< Unit vector up in the image.
    float distance = 0.0f;    ///< Orbit distance from @c focus to @c eye.
};

/// @brief A scene-space ray, used for picking.
struct Ray
{
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};  ///< Normalized.
};

/**
 * @struct GroundBounds
 * @brief Axis-aligned world-pixel bounds of the camera's ground footprint.
 */
struct GroundBounds
{
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    /**
     * @brief Whether @c min / @c max are the true footprint or a stand-in.
     *
     * The function must return a finite box, but with the horizon on screen the
     * visible ground genuinely runs to infinity, so corner rays that miss the
     * plane are cut off.
     */
    bool complete = true;
};

/// @brief Orbit angles a drag produces.
struct OrbitAngles
{
    float yawRadians = 0.0f;
    float pitchRadians = MAX_PITCH_RADIANS;
};

/**
 * @brief How far a full-window mouse sweep rotates the camera.
 *
 * A drag across the whole window turns 100 degrees on either axis, matching the
 * orbit feel of established tile-map editors: slow enough to frame a shot
 * precisely without needing a modifier key for fine control.
 */
inline constexpr float DRAG_SWEEP_RADIANS = 100.0f * rift::PiF / 180.0f;

/**
 * @brief Apply a mouse drag to the orbit angles.
 *
 * Dragging right swings the camera west so the ground appears to follow the
 * cursor, and dragging down lifts the camera toward top-down as though pulling
 * the ground toward you. Pitch is clamped and yaw wrapped, so repeated dragging
 * cannot walk the camera under the map or drift the angle unbounded.
 *
 * @param current      Angles before the drag.
 * @param dragPixels   Cursor delta this frame, in framebuffer pixels (Y down).
 * @param viewportSize Framebuffer size, so the sweep is resolution independent.
 */
OrbitAngles ApplyOrbitDrag(OrbitAngles current, glm::vec2 dragPixels, glm::vec2 viewportSize);

/// @brief Clamp an elevation into `[MIN_PITCH_RADIANS, MAX_PITCH_RADIANS]`.
float ClampPitch(float pitchRadians);

/// @brief Wrap a yaw into `(-pi, pi]` so it stays finite under repeated dragging.
float WrapYaw(float yawRadians);

/**
 * @brief Orbit distance that frames @p visibleWorldHeight at the focus depth.
 *
 * @f[ d = \frac{h/2}{\tan(fov_y/2)} @f]
 *
 * Measured perpendicular to the view ray at the focus, so at @c pitch pi/2 it is
 * exactly the visible ground height and the perspective and orthographic images
 * agree at the center of the screen. At shallower elevations the ground
 * footprint stretches away from the camera, which is the intended effect.
 */
float DistanceForVisibleHeight(float visibleWorldHeight, float fovYRadians);

/// @brief Unit vector from the focus toward the eye.
glm::vec3 EyeDirection(float yawRadians, float pitchRadians);

/**
 * @brief Camera up vector for the given angles.
 *
 * Derived analytically rather than by orthogonalising against world-up, which
 * would be degenerate at @c pitch pi/2 - precisely the @ref Preset::Classic case.
 */
glm::vec3 UpVector(float yawRadians, float pitchRadians);

/// @brief Resolve the full camera frame.
Basis MakeBasis(const RigParams& params);

/**
 * @brief Near and far plane distances for @p params.
 *
 * Two branches with different contracts. Orthographic returns a symmetric slab
 * about the orbit distance, so @p outNear may be negative - legal without a
 * perspective divide, where depth precision is uniform. Perspective instead
 * returns `max(0.5, 0.05 * distance)` and `distance + 2 * sceneRadius`, which
 * spends precision where the map is rather than on the empty space in front of a
 * dollied-back eye.
 *
 * @param params   Rig to measure. Orbit distance comes from @c visibleWorldSize.y
 *                 and @c fovYRadians under both projections.
 * @param outNear  Receives the near plane distance.
 * @param outFar   Receives the far plane distance.
 */
void DepthRange(const RigParams& params, float& outNear, float& outFar);

/// @brief View matrix (scene space -> camera space).
glm::mat4 BuildView(const RigParams& params);

/// @brief Projection matrix, orthographic or perspective per @c params.kind.
glm::mat4 BuildProjection(const RigParams& params);

/// @brief Combined `projection * view`.
glm::mat4 BuildViewProjection(const RigParams& params);

/**
 * @brief Build a picking ray through a viewport pixel.
 *
 * @warning A singular @p invViewProj, or a degenerate near-to-far delta, yields
 * the default @ref Ray: origin at the scene origin, pointing straight down. That
 * is indistinguishable from a real ray, and @ref IntersectGroundPlane then reports
 * a confident hit at world (0,0). Validate the matrix before calling rather than
 * testing the result.
 *
 * @param pixel        Cursor position in framebuffer pixels, Y measured down
 *                     from the top (GLFW's convention).
 * @param viewportSize Framebuffer size in pixels.
 * @param invViewProj  Inverse of @ref BuildViewProjection.
 * @return             Scene-space ray with a normalized direction.
 */
Ray ScreenToRay(glm::vec2 pixel, glm::vec2 viewportSize, const glm::mat4& invViewProj);

/**
 * @brief Intersect a ray with the horizontal plane at @p planeHeight.
 * @return World-pixel hit position, or @c nullopt when the ray is parallel to
 *         the plane or would only meet it behind the camera (i.e. the cursor is
 *         on or above the horizon).
 */
std::optional<glm::vec2> IntersectGroundPlane(const Ray& ray, float planeHeight);

/**
 * @brief Unproject a viewport pixel onto the ground plane.
 *
 * The one screen-to-world inverse for the 3D path. Every caller must handle the
 * empty result: a cursor pointing at the sky genuinely has no world position.
 */
std::optional<glm::vec2> ScreenToGround(glm::vec2 pixel,
                                        glm::vec2 viewportSize,
                                        const glm::mat4& invViewProj,
                                        float planeHeight = 0.0f);

/**
 * @brief Project a scene point to framebuffer pixels.
 * @return Pixel position with Y measured down, or @c nullopt when the point is
 *         behind the camera.
 */
std::optional<glm::vec2> WorldToScreen(glm::vec3 scenePoint,
                                       const glm::mat4& viewProj,
                                       glm::vec2 viewportSize);

/**
 * @brief Axis-aligned world bounds of the ground area the camera can see.
 *
 * Casts rays through the four viewport corners. Particle spawning, tile range
 * computation and overlay culling all need a world-space extent, and once the
 * camera can yaw that extent is a rotated trapezoid - so it is measured here
 * rather than assumed to be a camera-aligned rectangle.
 */
GroundBounds GroundFootprintAabb(const RigParams& params, float planeHeight = 0.0f);

/**
 * @brief Apply a preset's angles, projection kind and field of view to @p params.
 *
 * Framing (@c target, @c focusHeight, @c visibleWorldSize, @c sceneRadius) is left
 * untouched. @ref Preset::Free leaves the angles alone as well - it only re-clamps
 * the pitch and re-wraps the yaw - so switching to it keeps whatever the user last
 * dragged to.
 *
 * @warning @ref Preset::Classic and @ref Preset::DS overwrite @c fovYRadians with
 * @ref DS_FOV_RADIANS. Only @ref Preset::Free preserves a caller-set FOV.
 */
void ApplyPreset(RigParams& params, Preset preset);

/**
 * @brief Clamp the ground focus so the camera stays over the map.
 *
 * The flat pipeline clamped a viewport rectangle into `[0, mapSize - viewSize]`,
 * which has no meaning once the visible footprint is a rotated trapezoid, so this
 * clamps the focus point instead.
 *
 * @note Not on the live path yet. @c Game::BuildCameraRig derives its focus from
 * the flat camera, which @ref CameraController::ClampToMapBounds has already
 * clamped with the old viewport rule.
 *
 * @param focus         Desired focus in world pixels.
 * @param mapPixelSize  Map size in world pixels.
 */
glm::vec2 ClampFocusToMap(glm::vec2 focus, glm::vec2 mapPixelSize);

}  // namespace cameraRig

/// @brief Reflection for @ref cameraRig::ProjectionKind.
template <>
struct EnumTraits<cameraRig::ProjectionKind>
    : EnumTraitsBase<cameraRig::ProjectionKind, EnumTraits<cameraRig::ProjectionKind>>
{
    static constexpr std::size_t Count = cameraRig::PROJECTION_KIND_COUNT;
    static constexpr std::string_view Names[] = {"Orthographic", "Perspective"};
};

/// @brief Reflection for @ref cameraRig::Preset (the `cam.preset` command).
template <>
struct EnumTraits<cameraRig::Preset>
    : EnumTraitsBase<cameraRig::Preset, EnumTraits<cameraRig::Preset>>
{
    static constexpr std::size_t Count = cameraRig::PRESET_COUNT;
    static constexpr std::string_view Names[] = {"Classic", "DS", "Free"};
};

static_assert(std::size(EnumTraits<cameraRig::ProjectionKind>::Names) ==
                  cameraRig::PROJECTION_KIND_COUNT,
              "cameraRig::ProjectionKind names must stay in step with PROJECTION_KIND_COUNT");
static_assert(std::size(EnumTraits<cameraRig::Preset>::Names) == cameraRig::PRESET_COUNT,
              "cameraRig::Preset names must stay in step with PRESET_COUNT");
