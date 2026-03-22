#pragma once

#include "IRenderer.h"
#include "MathUtils.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

/**
 * @struct CameraState
 * @brief Camera position, following, and perspective state.
 * @author Alex (https://github.com/lextpf)
 */
struct CameraState
{
    glm::vec2 position{0.0f};          ///< Current camera world position (rendered position)
    glm::vec2 followTarget{0.0f};      ///< Target position camera is smoothing toward
    bool hasFollowTarget = false;      ///< True = smooth follow mode, false = instant snap
    float zoom = 1.0f;                 ///< Zoom multiplier (1.0 = 100%)
    float tilt = 0.2f;                 ///< Tilt angle for 3D effect
    bool enable3DEffect = false;       ///< Whether 3D tilt effect is active
    float globeSphereRadius = 200.0f;  ///< Globe projection radius
    bool freeMode = false;             ///< Free camera mode (decoupled from player)
};

/**
 * @struct CameraUpdateParams
 * @brief Per-frame input parameters for camera update logic.
 * @author Alex (https://github.com/lextpf)
 *
 * Passed by value to CameraController::Update() each frame.
 * All fields are snapshots of the current frame's state.
 */
struct CameraUpdateParams
{
    float deltaTime = 0.0f;
    glm::vec2 playerFollowTarget{0.0f};  ///< Camera centering target derived from player position
    bool playerMoving = false;           ///< Whether WASD input is active this frame
    bool arrowUp = false;
    bool arrowDown = false;
    bool arrowLeft = false;
    bool arrowRight = false;
    bool shiftHeld = false;
    float baseWorldWidth = 0.0f;   ///< tilesVisibleWidth * tileWidth (unzoomed)
    float baseWorldHeight = 0.0f;  ///< tilesVisibleHeight * tileHeight (unzoomed)
    float mapPixelWidth = 0.0f;    ///< mapWidth * tileWidth
    float mapPixelHeight = 0.0f;   ///< mapHeight * tileHeight
    bool skipMapClamping = false;  ///< True when editor free-camera mode is active
    int tileWidth = 16;            ///< Tile width in pixels (for free-mode snap)
    int tileHeight = 16;           ///< Tile height in pixels (for free-mode snap)
};

/**
 * @class CameraController
 * @brief Manages camera position, following, zoom, and perspective projection.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Extracted from Game to reduce the Game class's responsibilities.
 * Owns all camera state and implements camera following, panning,
 * zooming, and 3D globe perspective configuration.
 *
 * @see CameraState, CameraUpdateParams, EditorContext
 */
class CameraController
{
public:
    CameraController() = default;

    /**
     * @brief Initialize camera centered on the player.
     * @param playerVisualCenter Player's visual center in world space.
     * @param viewWidth Viewport width in world pixels.
     * @param viewHeight Viewport height in world pixels.
     */
    void Initialize(glm::vec2 playerVisualCenter, float viewWidth, float viewHeight);

    /**
     * @brief Update camera position based on input and player state.
     *
     * Handles three modes:
     * - Free mode: Arrow keys pan freely
     * - Manual pan: Arrow keys override follow
     * - Auto follow: Smooth tracking of player position
     *
     * @param params Per-frame input and world state.
     */
    void Update(const CameraUpdateParams& params);

    /**
     * @brief Configure renderer perspective based on current 3D effect settings.
     * @param renderer Renderer to configure.
     * @param width Zoomed viewport width.
     * @param height Zoomed viewport height.
     */
    void ConfigurePerspective(IRenderer& renderer, float width, float height) const;

    /**
     * @brief Get orthographic projection matrix.
     * @param width Viewport width.
     * @param height Viewport height.
     * @return Orthographic projection with top-left origin.
     */
    static glm::mat4 GetOrthoProjection(float width, float height);

    /// @brief Toggle the 3D globe effect on/off.
    void Toggle3DEffect();

    /**
     * @brief Handle scroll wheel zoom.
     * @param yoffset Scroll delta (positive = zoom in).
     * @param playerVisualCenter Player's visual center for zoom anchoring.
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
     * @brief Reset zoom to 1.0x and recenter on player.
     * @param playerVisualCenter Player's visual center.
     * @param worldWidth Viewport width at 1.0x zoom.
     * @param worldHeight Viewport height at 1.0x zoom.
     * @param mapPixelWidth Map bounds width.
     * @param mapPixelHeight Map bounds height.
     * @param skipMapClamping True to skip clamping.
     */
    void ResetZoom(glm::vec2 playerVisualCenter,
                   float worldWidth,
                   float worldHeight,
                   float mapPixelWidth,
                   float mapPixelHeight,
                   bool skipMapClamping);

    /// @name Accessors
    /// @{
    CameraState& GetState() { return m_State; }
    const CameraState& GetState() const { return m_State; }
    const glm::vec2& GetPosition() const { return m_State.position; }
    float GetZoom() const { return m_State.zoom; }
    bool Is3DEnabled() const { return m_State.enable3DEffect; }
    bool IsFreeMode() const { return m_State.freeMode; }
    /// @}

private:
    CameraState m_State;

    /// @name Constants
    /// @{
    static constexpr float CAMERA_PAN_SPEED = 600.0f;
    static constexpr float CAMERA_SETTLE_TIME = 0.6f;
    static constexpr float CAMERA_SNAP_THRESHOLD = 0.1f;
    /// @}

    /// @brief Clamp camera position to map bounds.
    void ClampToMapBounds(float worldWidth, float worldHeight, float mapW, float mapH);
};
