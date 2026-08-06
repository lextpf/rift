#include "CameraController.hpp"

#include "Logger.hpp"

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Camera";
}  // namespace

void CameraController::Initialize(glm::vec2 playerVisualCenter, float viewWidth, float viewHeight)
{
    // position is the viewport's top-left corner, so centring on a point means
    // subtracting half the view size. No map clamp here: the first Update() applies
    // it, which keeps this callable before the tilemap size is known.
    m_State.position = playerVisualCenter - glm::vec2(viewWidth / 2.0f, viewHeight / 2.0f);
    m_State.followTarget = m_State.position;
    m_State.hasFollowTarget = false;
}

void CameraController::Update(const CameraUpdateParams& params)
{
    float worldWidth = params.baseWorldWidth / m_State.zoom;
    float worldHeight = params.baseWorldHeight / m_State.zoom;
    bool arrowKeysPressed =
        params.arrowUp || params.arrowDown || params.arrowLeft || params.arrowRight;

    // Camera movement modes:
    // - Free camera (Space toggle): Arrow keys pan freely, camera ignores player
    // - Manual pan: Arrow keys override player follow temporarily
    // - Auto follow: Camera smoothly tracks player's tile center position
    if (m_State.freeMode)
    {
        if (arrowKeysPressed)
        {
            // Base speed scales with zoom (faster when zoomed out for easier map navigation)
            float cameraSpeed = CAMERA_PAN_SPEED / m_State.zoom;

            // Shift modifier for faster panning (2.5x)
            if (params.shiftHeld)
            {
                cameraSpeed *= 2.5f;
            }

            glm::vec2 cameraMove(0.0f);

            if (params.arrowUp)
                cameraMove.y -= cameraSpeed * params.deltaTime;
            if (params.arrowDown)
                cameraMove.y += cameraSpeed * params.deltaTime;
            if (params.arrowLeft)
                cameraMove.x -= cameraSpeed * params.deltaTime;
            if (params.arrowRight)
                cameraMove.x += cameraSpeed * params.deltaTime;

            m_State.position += cameraMove;
        }
        else
        {
            // Smoothly snap to tile grid when not moving, so a hand-panned editor
            // camera comes to rest on whole tiles instead of a fractional offset.
            float tileW = static_cast<float>(params.tileWidth);
            float tileH = static_cast<float>(params.tileHeight);
            glm::vec2 snappedPos;
            snappedPos.x = std::round(m_State.position.x / tileW) * tileW;
            snappedPos.y = std::round(m_State.position.y / tileH) * tileH;

            // Settles in 0.5s rather than CAMERA_SETTLE_TIME's 0.6s: this is a
            // sub-tile correction the user should not have to wait out.
            float alpha = rift::ExpApproachAlpha(params.deltaTime, 0.5f);
            glm::vec2 newPos = m_State.position + (snappedPos - m_State.position) * alpha;

            if (glm::length(snappedPos - newPos) < CAMERA_SNAP_THRESHOLD)
            {
                m_State.position = snappedPos;
            }
            else
            {
                m_State.position = newPos;
            }
        }
        m_State.hasFollowTarget = false;
    }
    else if (arrowKeysPressed)
    {
        // Manual camera control with arrow keys
        float cameraSpeed = CAMERA_PAN_SPEED / m_State.zoom;

        if (params.shiftHeld)
        {
            cameraSpeed *= 2.5f;
        }

        glm::vec2 cameraMove(0.0f);

        if (params.arrowUp)
        {
            cameraMove.y -= cameraSpeed * params.deltaTime;
        }
        if (params.arrowDown)
        {
            cameraMove.y += cameraSpeed * params.deltaTime;
        }
        if (params.arrowLeft)
        {
            cameraMove.x -= cameraSpeed * params.deltaTime;
        }
        if (params.arrowRight)
        {
            cameraMove.x += cameraSpeed * params.deltaTime;
        }

        m_State.position += cameraMove;

        // When user manually pans, cancel any automatic follow smoothing
        m_State.hasFollowTarget = false;
    }
    else
    {
        // No manual camera input.
        // If player is moving with WASD, establish a follow target.
        if (params.playerMoving || m_State.hasFollowTarget)
        {
            // Lead the player by up to m_LookAheadDistance in the direction of travel,
            // scaled by speed so a walk leads less than a run.
            glm::vec2 lead(0.0f);
            float vlen = glm::length(params.playerVelocity);
            if (vlen > 1e-3f && m_LookAheadDistance > 0.0f)
            {
                float mag = std::min(1.0f, vlen / LOOKAHEAD_REF_SPEED);
                lead = (params.playerVelocity / vlen) * (m_LookAheadDistance * mag);
            }
            m_State.followTarget = params.playerFollowTarget + lead;
            m_State.hasFollowTarget = true;
        }

        // Smoothly move the camera toward the follow target when one is set.
        if (m_State.hasFollowTarget)
        {
            float alpha = rift::ExpApproachAlpha(params.deltaTime, CAMERA_SETTLE_TIME);

            glm::vec2 newPos = m_State.position + (m_State.followTarget - m_State.position) * alpha;

            if (glm::length(m_State.followTarget - newPos) < CAMERA_SNAP_THRESHOLD)
            {
                m_State.position = m_State.followTarget;
                m_State.hasFollowTarget = false;
            }
            else
            {
                m_State.position = newPos;
            }
        }
    }

    // Clamp camera to map bounds (skip in editor free-camera mode)
    if (!params.skipMapClamping)
    {
        ClampToMapBounds(worldWidth, worldHeight, params.mapPixelWidth, params.mapPixelHeight);
    }
}

glm::mat4 CameraController::GetOrthoProjection(float width, float height)
{
    return glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
}

void CameraController::HandleZoomScroll(double yoffset,
                                        glm::vec2 playerVisualCenter,
                                        float baseWorldWidth,
                                        float baseWorldHeight,
                                        float mapPixelWidth,
                                        float mapPixelHeight,
                                        bool skipMapClamping,
                                        bool editorFreeMode)
{
    float oldZoom = m_State.zoom;

    float zoomDelta = yoffset > 0 ? 1.1f : 0.9f;
    m_State.zoom *= zoomDelta;

    // Two floors, one shared 4.0x ceiling: gameplay stops at 0.4x, while editor
    // free-camera mode goes down to 0.1x so an entire map fits on screen.
    float minZoom = editorFreeMode ? 0.1f : 0.4f;
    m_State.zoom = std::max(minZoom, std::min(4.0f, m_State.zoom));
    // Snap to 0.1 increments. The steps are multiplicative, so without rounding an
    // in/out pair would not return to where it started (1.1 * 0.9 = 0.99) and the
    // zoom level would drift with every wheel notch.
    m_State.zoom = std::round(m_State.zoom * 10.0f) / 10.0f;

    float newWorldWidth = baseWorldWidth / m_State.zoom;
    float newWorldHeight = baseWorldHeight / m_State.zoom;

    // Adjust camera position to keep player centered
    m_State.position = playerVisualCenter - glm::vec2(newWorldWidth * 0.5f, newWorldHeight * 0.5f);

    if (!skipMapClamping)
    {
        ClampToMapBounds(newWorldWidth, newWorldHeight, mapPixelWidth, mapPixelHeight);
    }

    // Also update the follow target so camera doesn't snap back
    m_State.followTarget = m_State.position;

    Logger::InfoF(LOG_SUBSYSTEM, "Camera zoom: {}x", m_State.zoom);
}

void CameraController::ResetZoom(glm::vec2 playerVisualCenter,
                                 float worldWidth,
                                 float worldHeight,
                                 float mapPixelWidth,
                                 float mapPixelHeight,
                                 bool skipMapClamping)
{
    m_State.zoom = 1.0f;

    m_State.position = playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

    if (!skipMapClamping)
    {
        ClampToMapBounds(worldWidth, worldHeight, mapPixelWidth, mapPixelHeight);
    }

    m_State.hasFollowTarget = false;
    Logger::Info(LOG_SUBSYSTEM, "Camera zoom reset to 1.0x");
}

void CameraController::ClampToMapBounds(float worldWidth, float worldHeight, float mapW, float mapH)
{
    // position is the viewport's top-left corner, so the legal band is
    // [0, mapSize - viewSize]. When the viewport is larger than the map that upper
    // bound goes negative and the max(0, ...) wins.
    m_State.position.x = std::max(0.0f, std::min(m_State.position.x, mapW - worldWidth));
    m_State.position.y = std::max(0.0f, std::min(m_State.position.y, mapH - worldHeight));
}
