#pragma once

#include "IRenderer.hpp"
#include "MathUtils.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

/**
 * @struct CameraState
 * @brief Camera position, following, and zoom state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * @warning @c position is the viewport's top-left corner in world pixels, not the
 * camera center. Anything re-centring on a point subtracts half the view size first,
 * as @ref CameraController::Initialize, @ref CameraController::HandleZoomScroll and
 * @ref CameraController::ResetZoom all do. The console `*.goto` commands do not, so
 * they land their target at the corner rather than the middle.
 *
 * @verbatim
 *   (0,0) map origin
 *     +--------------------------------------------+
 *     |                                            |
 *     | position -> +--------------+               |
 *     |             |              |               |
 *     |             |   viewport   | worldHeight   |
 *     |             |              |               |
 *     |             +--------------+               |
 *     |                worldWidth                  |
 *     +--------------------------------------------+
 *                                                (w,h) map size
 *
 *   clamp band: [0, mapPixels - worldSize] per axis, skipped in editor free mode.
 *   Zooming out grows worldSize, so the band shrinks - and collapses to {0} once
 *   the viewport is wider than the map.
 * @endverbatim
 *
 * World Y grows downward, so a larger @c position.y scrolls further down the map.
 */
struct CameraState
{
    /// Viewport top-left corner in world pixels. @c Game::Render overwrites this with a
    /// pixel-snapped value for the span of the draw, then restores it.
    glm::vec2 position{0.0f};
    /// Top-left corner the auto-follow is easing toward; same frame as @c position.
    glm::vec2 followTarget{0.0f};
    /// True while an auto-follow ease is in flight. Cleared on arrival, on arrow-key
    /// panning, in free mode, and by any explicit re-center.
    bool hasFollowTarget = false;
    /// Zoom multiplier (1.0 = 100%). @ref CameraController::HandleZoomScroll holds the wheel
    /// to [0.4, 4.0]; `camera.zoom` accepts the wider [0.1, 10.0] and rejects anything outside.
    float zoom = 1.0f;
    bool freeMode = false;  ///< Free camera mode (decoupled from the player).
};

/**
 * @struct CameraUpdateParams
 * @brief Per-frame input parameters for camera update logic.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Snapshots of the current frame, passed by const reference to
 * @ref CameraController::Update once per gameplay frame - @c Game::Update returns early
 * in the Title and Paused modes, so the camera does not move there.
 */
struct CameraUpdateParams
{
    float deltaTime = 0.0f;  ///< Seconds since the previous frame.
    /**
     * @brief Desired viewport top-left corner, already reduced from the player's visual
     * center by half the view size (@c Game::Update does that subtraction).
     *
     * Tracks the live player position while WASD is held and the player's tile center
     * when idle, so the view settles onto the grid at rest.
     */
    glm::vec2 playerFollowTarget{0.0f};
    bool playerMoving = false;       ///< Whether WASD input is active this frame.
    glm::vec2 playerVelocity{0.0f};  ///< Player velocity (px/s) for camera look-ahead.
    /// Arrow-key pan input, this frame's polled state. Opposing keys cancel the motion
    /// but still read as manual input, which freezes the camera.
    bool arrowUp = false;
    bool arrowDown = false;
    bool arrowLeft = false;
    bool arrowRight = false;
    bool shiftHeld = false;  ///< Shift multiplies the pan speed by 2.5x.
    /**
     * @brief Unzoomed viewport width: `tilesVisibleWidth * tileWidth`.
     *
     * @note This is the truncated tile-count extent, not the rendered extent
     * (@c viewScaling::VisibleWorldSize, i.e. screen pixels / @c PIXEL_SCALE).
     * @ref CameraController::ClampToMapBounds uses it, so the clamp band matches the
     * drawn viewport only while the window is a whole number of tiles wide and the
     * tileset tile is @c TILE_PIXEL_SIZE. Off that path the camera clamps against a
     * narrower viewport than it draws, which lets off-map area onto screen.
     */
    float baseWorldWidth = 0.0f;
    /// Unzoomed viewport height: `tilesVisibleHeight * tileHeight`. Same truncation
    /// caveat as @c baseWorldWidth.
    float baseWorldHeight = 0.0f;
    float mapPixelWidth = 0.0f;    ///< mapWidth * tileWidth.
    float mapPixelHeight = 0.0f;   ///< mapHeight * tileHeight.
    bool skipMapClamping = false;  ///< True when editor free-camera mode is active.
    int tileWidth = 16;            ///< Tile width in px (for the free-mode grid snap).
    int tileHeight = 16;           ///< Tile height in px (for the free-mode grid snap).
};

/**
 * @class CameraController
 * @brief Owns camera position, following, and zoom.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Keeps camera state and its three movement modes in one place rather than spread
 * across @c Game. @ref Update resolves the modes each frame; @ref HandleZoomScroll
 * and @ref ResetZoom drive zoom.
 *
 * @par Smoothing
 * Both eases go through @ref rift::ExpApproachAlpha and are therefore frame-rate
 * independent: the distance still left after a given wall-clock time does not depend
 * on how many frames that time was split into. Auto-follow settles over
 * @c CAMERA_SETTLE_TIME, the free-mode grid snap over a shorter 0.5s.
 *
 * @par Look-ahead
 * In auto-follow only, the target leads the player in the direction of travel:
 * @verbatim
 *   lead = normalize(v) * lookAheadDistance * min(1, |v| / LOOKAHEAD_REF_SPEED)
 * @endverbatim
 * The lead is recomputed every frame, so at a constant speed the camera holds a
 * steady offset of lead plus exponential lag - it is not a transient.
 *
 * @warning Nothing re-acquires the player. Leaving free mode or releasing the arrow
 * keys leaves @c hasFollowTarget false, and @ref Update only re-targets while the
 * player is moving - so a camera parked off-player stays there until the next WASD
 * press or an explicit `camera.follow on`.
 *
 * @note This drives the default flat 2.5D path only. With the `world3d` console
 * toggle on, @c Game::BuildCameraRig re-reads @c position and @c zoom as an orbit
 * focus plus a visible extent and builds its matrices in @ref cameraRig, so
 * @ref GetOrthoProjection is bypassed. Keep both readings of @ref CameraState in
 * mind before changing follow or clamp behavior.
 *
 * @see CameraState, CameraUpdateParams, rift::ExpApproachAlpha
 */
class CameraController
{
public:
    CameraController() = default;

    /**
     * @brief Center the view on @p playerVisualCenter without easing.
     *
     * Sets @c position and @c followTarget to the top-left corner that puts the point
     * in the middle of the view, and clears @c hasFollowTarget. Deliberately does not
     * clamp to the map - the first @ref Update does that, which keeps this callable
     * before the tilemap size is known.
     *
     * @param playerVisualCenter Point to center the view on, in world pixels.
     * @param viewWidth Viewport width in world pixels.
     * @param viewHeight Viewport height in world pixels.
     */
    void Initialize(glm::vec2 playerVisualCenter, float viewWidth, float viewHeight);

    /**
     * @brief Resolve one frame of camera movement.
     *
     * Modes in priority order - free mode wins over the arrow keys, which win over
     * auto-follow:
     * - Free mode: arrow keys pan. With no arrow held it eases onto the tile grid
     *   every frame (a continuous correction, not a one-shot on release), and it snaps
     *   the top-left corner, so an odd-sized viewport rests half a tile off-center.
     * - Manual pan: arrow keys move @c position directly and clear @c hasFollowTarget.
     * - Auto-follow: exponential approach to @c playerFollowTarget plus look-ahead.
     *
     * Finishes by clamping to the map bounds unless @c params.skipMapClamping.
     *
     * @param params Per-frame input and world state.
     */
    void Update(const CameraUpdateParams& params);

    /**
     * @brief Orthographic projection with a top-left origin.
     *
     * Carries no camera transform at all - it ignores every member. Callers apply the
     * camera themselves, by subtracting @c position per drawable and by passing a view
     * size they have already divided by @c zoom. There is deliberately no view matrix.
     *
     * @param width Viewport width, already zoom-adjusted.
     * @param height Viewport height, already zoom-adjusted.
     * @return Projection over [0,width] x [0,height] with y growing downward.
     */
    static glm::mat4 GetOrthoProjection(float width, float height);

    /**
     * @brief Apply one scroll notch of zoom, then re-center on the player.
     *
     * Multiplies @c zoom by 1.1 (in) or 0.9 (out), clamps to
     * `[editorFreeMode ? 0.1 : 0.4, 4.0]`, then rounds to a 0.1 increment - the steps
     * are multiplicative, so unrounded an in/out pair would not return to where it
     * started (1.1 * 0.9 = 0.99). Because the rounding comes last the wheel bottoms
     * out at 0.5x in both modes; anything below that is reachable only through the
     * `camera.zoom` console command. All five numbers are literals in the .cpp.
     *
     * @warning Callers disagree on the player's visual center: @c Game::Update and
     * @c Game::LoadGameWorld use `pos.y - HITBOX_HEIGHT`, @c Game::ScrollCallback uses
     * half of that, and the Z-key reset spells it `tileCenter.y - TILE_PIXEL_SIZE`. So
     * Ctrl+scroll re-centers 8 px lower than auto-follow does, and since this function
     * leaves @c hasFollowTarget alone the offset persists until the player moves again.
     *
     * @param yoffset Scroll delta (positive = zoom in).
     * @param playerVisualCenter Point to re-center on, in world pixels.
     * @param baseWorldWidth Unzoomed viewport width.
     * @param baseWorldHeight Unzoomed viewport height.
     * @param mapPixelWidth Map bounds width.
     * @param mapPixelHeight Map bounds height.
     * @param skipMapClamping True to allow zooming beyond map bounds.
     * @param editorFreeMode True when in editor free-camera mode (wider zoom range).
     */
    void HandleZoomScroll(double yoffset,
                          glm::vec2 playerVisualCenter,
                          float baseWorldWidth,
                          float baseWorldHeight,
                          float mapPixelWidth,
                          float mapPixelHeight,
                          bool skipMapClamping,
                          bool editorFreeMode);

    /**
     * @brief Reset zoom to 1.0x and re-center on the player.
     *
     * Clears @c hasFollowTarget, so the re-center is instant rather than eased.
     *
     * @param playerVisualCenter Point to re-center on, in world pixels.
     * @param worldWidth Viewport width at 1.0x zoom.
     * @param worldHeight Viewport height at 1.0x zoom.
     * @param mapPixelWidth Map bounds width.
     * @param mapPixelHeight Map bounds height.
     * @param skipMapClamping True to allow the re-center to leave the map bounds.
     */
    void ResetZoom(glm::vec2 playerVisualCenter,
                   float worldWidth,
                   float worldHeight,
                   float mapPixelWidth,
                   float mapPixelHeight,
                   bool skipMapClamping);

    /// @name Accessors
    /// @{
    /// Mutable access to the whole state. Every external writer - console commands,
    /// input handlers, the renderer's pixel snap - comes through here, past the clamps.
    CameraState& GetState() { return m_State; }
    const CameraState& GetState() const { return m_State; }
    const glm::vec2& GetPosition() const { return m_State.position; }
    float GetZoom() const { return m_State.zoom; }
    bool IsFreeMode() const { return m_State.freeMode; }
    /// Set the camera look-ahead distance in world pixels; 0 disables it.
    void SetLookAheadDistance(float d) { m_LookAheadDistance = d; }
    /// Current camera look-ahead distance (world pixels).
    float GetLookAheadDistance() const { return m_LookAheadDistance; }
    /// @}

private:
    CameraState m_State;
    float m_LookAheadDistance = 12.0f;  ///< Camera lead in the direction of travel (px).

    /// @name Constants
    /// @{
    /// Arrow-key pan speed in world px/s at 1.0x zoom, divided by @c zoom so a pan
    /// covers the same on-screen distance per second at any zoom. Diagonals are 1.41x.
    static constexpr float CAMERA_PAN_SPEED = 600.0f;
    /// Auto-follow settle time: after this long, 1% of the distance is left.
    static constexpr float CAMERA_SETTLE_TIME = 0.6f;
    /// World-pixel distance below which an ease snaps to its target, killing jitter.
    static constexpr float CAMERA_SNAP_THRESHOLD = 0.1f;
    /**
     * @brief Speed at which the look-ahead lead saturates (px/s).
     *
     * Hand-copied from `PLAYER_BASE_SPEED * RUN_SPEED_MULTIPLIER` (50 * 1.75), so a
     * full run reaches full lead and bicycle mode (50 * 2.25 = 112.5) stays there.
     * Nothing enforces the equality - retuning either character constant needs this
     * one updated by hand.
     */
    static constexpr float LOOKAHEAD_REF_SPEED = 87.5f;
    /// @}

    /// Clamp @c position into `[0, mapSize - worldSize]` per axis; a negative upper
    /// bound (viewport wider than the map) collapses the band to 0.
    void ClampToMapBounds(float worldWidth, float worldHeight, float mapW, float mapH);
};
