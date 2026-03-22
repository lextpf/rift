#include "CameraController.h"
#include "MathConstants.h"

void CameraController::Initialize(glm::vec2 playerVisualCenter, float viewWidth, float viewHeight)
{
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
            // Smoothly snap to tile grid when not moving
            float tileW = static_cast<float>(params.tileWidth);
            float tileH = static_cast<float>(params.tileHeight);
            glm::vec2 snappedPos;
            snappedPos.x = std::round(m_State.position.x / tileW) * tileW;
            snappedPos.y = std::round(m_State.position.y / tileH) * tileH;

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
            m_State.followTarget = params.playerFollowTarget;
            m_State.hasFollowTarget = true;
        }

        // Smoothly move camera towards follow target if we have one
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

void CameraController::ConfigurePerspective(IRenderer& renderer, float width, float height) const
{
    if (m_State.enable3DEffect)
    {
        float horizonY = -height * m_State.tilt * 0.20f;
        float horizonScale = 0.75f + (1.0f - m_State.tilt) * 0.10f;

        float viewportDiagonal = std::sqrt(width * width + height * height);
        float baseRadius = m_State.globeSphereRadius / m_State.zoom;
        float minRadius = viewportDiagonal / static_cast<float>(rift::Pi * 2.0);
        float effectiveSphereRadius = std::max(baseRadius, minRadius);

        renderer.SetFisheyePerspective(
            true, effectiveSphereRadius, horizonY, horizonScale, width, height);
    }
    else
    {
        renderer.SetVanishingPointPerspective(false, 0.0f, 1.0f, width, height);
    }
}

glm::mat4 CameraController::GetOrthoProjection(float width, float height)
{
    return glm::ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
}

void CameraController::Toggle3DEffect()
{
    m_State.enable3DEffect = !m_State.enable3DEffect;
    std::cout << "3D Effect: " << (m_State.enable3DEffect ? "ON" : "OFF")
              << " (Radius: " << m_State.globeSphereRadius << ")" << std::endl;
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

    // Editor mode allows zooming out further (0.1x) to see entire map
    float minZoom = editorFreeMode ? 0.1f : 0.4f;
    m_State.zoom = std::max(minZoom, std::min(4.0f, m_State.zoom));
    // Snap to 0.1 increments
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

    std::cout << "Camera zoom: " << m_State.zoom << "x" << std::endl;
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
    std::cout << "Camera zoom reset to 1.0x" << std::endl;
}

void CameraController::ClampToMapBounds(float worldWidth, float worldHeight, float mapW, float mapH)
{
    m_State.position.x = std::max(0.0f, std::min(m_State.position.x, mapW - worldWidth));
    m_State.position.y = std::max(0.0f, std::min(m_State.position.y, mapH - worldHeight));
}
