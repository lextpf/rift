#include "Game.h"
#include "KeyToggle.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <iostream>

void Game::ProcessInput(float deltaTime)
{
    glm::vec2 moveDirection(0.0f);

    // Check if shift is pressed for running (1.5x movement speed)
    bool isRunning = (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

    // Reset copied NPC appearance when starting to run
    if (isRunning && m_Player.IsUsingCopiedAppearance())
    {
        m_Player.RestoreOriginalAppearance();
        m_Player.UploadTextures(*m_Renderer);
    }

    m_Player.SetRunning(isRunning);

    // Standard WASD layout for 8-directional movement
    // Y increases downward in screen space (top-left origin), so W = -Y, S = +Y
    if (glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS)
    {
        moveDirection.y -= 1.0f;  // Up
    }
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS)
    {
        moveDirection.x -= 1.0f;  // Left
    }
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS)
    {
        moveDirection.y += 1.0f;  // Down
    }
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS)
    {
        moveDirection.x += 1.0f;  // Right
    }

    // Toggles between gameplay and editor mode.
    static KeyToggle<GLFW_KEY_E> eKey;
    if (eKey.JustPressed(m_Window))
    {
        m_Editor.SetActive(!m_Editor.IsActive());
        std::cout << "Editor mode: " << (m_Editor.IsActive() ? "ON" : "OFF") << std::endl;
        if (m_Editor.IsActive())
        {
            std::cout << "Press T to toggle tile picker visibility" << std::endl;
        }
    }

    // Delegate all editor-specific key input to Editor
    if (m_Editor.IsActive())
    {
        m_Editor.ProcessInput(deltaTime, MakeEditorContext());
    }

    // --- Remainder of ProcessInput: keys that stay in Game ---
    // (E key is above, editor delegation just above)
    // The following sections handle: Z, F1-F6, Space, PageUp/Down, C, B, X, F, dialogue, player
    // movement

    // Resets camera zoom to 1.0x and recenters on player.
    // In editor mode, also resets tile picker zoom and pan.
    static KeyToggle<GLFW_KEY_Z> zKey;
    if (zKey.JustPressed(m_Window))
    {
        m_Camera.zoom = 1.0f;
        std::cout << "Camera zoom reset to 1.0x" << std::endl;

        // Recenter camera on player in gameplay mode
        if (!m_Editor.IsActive())
        {
            // Calculate viewport dimensions at 1.0x zoom
            float worldWidth = static_cast<float>(m_TilesVisibleWidth * TILE_PIXEL_SIZE);
            float worldHeight = static_cast<float>(m_TilesVisibleHeight * TILE_PIXEL_SIZE);

            // Calculate player's visual center
            glm::vec2 playerAnchorTileCenter = m_Player.GetCurrentTileCenter();
            glm::vec2 playerVisualCenter =
                glm::vec2(playerAnchorTileCenter.x, playerAnchorTileCenter.y - TILE_PIXEL_SIZE);

            // Position camera so player is centered
            m_Camera.position =
                playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

            // Clamp to map bounds (skip in editor free-camera mode)
            if (!(m_Editor.IsActive() && m_Camera.freeMode))
            {
                float mapWidth =
                    static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
                float mapHeight =
                    static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());
                m_Camera.position.x =
                    std::max(0.0f, std::min(m_Camera.position.x, mapWidth - worldWidth));
                m_Camera.position.y =
                    std::max(0.0f, std::min(m_Camera.position.y, mapHeight - worldHeight));
            }

            // Disable smooth follow to prevent drift after reset
            m_Camera.hasFollowTarget = false;
        }

        // Reset tile picker state in editor mode
        if (m_Editor.IsActive())
        {
            m_Editor.ResetTilePickerState();
        }
    }

    // Toggle between OpenGL and Vulkan renderers at runtime
    static KeyToggle<GLFW_KEY_F1> f1Key;
    if (f1Key.JustPressed(m_Window))
    {
        // Toggle between OpenGL and Vulkan
        RendererAPI newApi =
            (m_RendererAPI == RendererAPI::OpenGL) ? RendererAPI::Vulkan : RendererAPI::OpenGL;
        SwitchRenderer(newApi);
    }

    // Toggles FPS and position information display
    static KeyToggle<GLFW_KEY_F2> f2Key;
    if (f2Key.JustPressed(m_Window))
    {
        m_Editor.ToggleShowDebugInfo();
    }

    // Enables visual debug overlays including:
    //   - Collision tiles
    //   - Player collision tolerance zones
    //   - Navigation tiles
    //   - NPC information
    //   - All tile layers visible
    static KeyToggle<GLFW_KEY_F3> f3Key;
    if (f3Key.JustPressed(m_Window))
    {
        m_Editor.ToggleDebugMode();
    }

    // Cycle through all 8 time periods
    static KeyToggle<GLFW_KEY_F4> f4Key;
    static int timeOfDayCycle = 0;
    if (f4Key.JustPressed(m_Window))
    {
        timeOfDayCycle = (timeOfDayCycle + 1) % 8;
        const char* periodName = "";
        switch (timeOfDayCycle)
        {
            case 0:  // Dawn (05:00-07:00)
                m_TimeManager.SetTime(6.0f);
                periodName = "Dawn (06:00)";
                break;
            case 1:  // Morning (07:00-10:00)
                m_TimeManager.SetTime(8.5f);
                periodName = "Morning (08:30)";
                break;
            case 2:  // Midday (10:00-16:00)
                m_TimeManager.SetTime(13.0f);
                periodName = "Midday (13:00)";
                break;
            case 3:  // Afternoon (16:00-18:00)
                m_TimeManager.SetTime(17.0f);
                periodName = "Afternoon (17:00)";
                break;
            case 4:  // Dusk (18:00-20:00)
                m_TimeManager.SetTime(19.0f);
                periodName = "Dusk (19:00)";
                break;
            case 5:  // Evening (20:00-22:00)
                m_TimeManager.SetTime(21.0f);
                periodName = "Evening (21:00)";
                break;
            case 6:  // Night (22:00-04:00)
                m_TimeManager.SetTime(1.0f);
                periodName = "Night (01:00)";
                break;
            case 7:  // LateNight (04:00-05:00)
                m_TimeManager.SetTime(4.5f);
                periodName = "Late Night (04:30)";
                break;
        }
        std::cout << "Time of day: " << periodName << std::endl;
    }

    // Toggles the 3D globe effect for an isometric-like view
    static KeyToggle<GLFW_KEY_F5> f5Key;
    if (f5Key.JustPressed(m_Window))
    {
        Toggle3DEffect();
    }

    // Toggle FPS cap (0 = uncapped, 500 = capped)
    static KeyToggle<GLFW_KEY_F6> f6Key;
    if (f6Key.JustPressed(m_Window))
    {
        if (m_Fps.targetFps <= 0.0f)
        {
            m_Fps.targetFps = 500.0f;
            std::cout << "FPS capped at 500" << std::endl;
        }
        else
        {
            m_Fps.targetFps = 0.0f;
            std::cout << "FPS uncapped" << std::endl;
        }
    }

    // Toggle free camera mode (Space) - camera stops following player
    // WASD/Arrows can then pan camera while player still moves with WASD
    static KeyToggle<GLFW_KEY_SPACE> spaceKeyFreeCamera;
    if (!m_InDialogue && !m_DialogueManager.IsActive() && !m_DialogueSnap.active &&
        !m_Editor.IsActive())
    {
        if (spaceKeyFreeCamera.JustPressed(m_Window))
        {
            m_Camera.freeMode = !m_Camera.freeMode;
            std::cout << "Free Camera Mode: " << (m_Camera.freeMode ? "ON" : "OFF") << std::endl;
        }
    }

    // Adjusts 3D effect parameters when enabled:
    //   - Page Up/Down adjusts globe radius and tilt
    static KeyToggle<GLFW_KEY_PAGE_UP> pageUpKey;
    static KeyToggle<GLFW_KEY_PAGE_DOWN> pageDownKey;
    if (m_Camera.enable3DEffect)
    {
        // Globe effect parameter adjustment
        if (pageUpKey.JustPressed(m_Window))
        {
            m_Camera.globeSphereRadius = std::min(500.0f, m_Camera.globeSphereRadius + 10.0f);
            m_Camera.tilt = std::max(0.0f, m_Camera.tilt - 0.05f);
            std::cout << "3D Effect - Radius: " << m_Camera.globeSphereRadius
                      << ", Tilt: " << m_Camera.tilt << std::endl;
        }

        if (pageDownKey.JustPressed(m_Window))
        {
            m_Camera.globeSphereRadius = std::max(50.0f, m_Camera.globeSphereRadius - 10.0f);
            m_Camera.tilt = std::min(1.0f, m_Camera.tilt + 0.05f);
            std::cout << "3D Effect - Radius: " << m_Camera.globeSphereRadius
                      << ", Tilt: " << m_Camera.tilt << std::endl;
        }
    }
    // Cycles through available player character sprites.
    // Each character type has its own sprite sheet loaded from assets.
    static KeyToggle<GLFW_KEY_C> cKey;
    if (cKey.JustPressed(m_Window))
    {
        CharacterType newType = NextEnum(m_Player.GetCharacterType());

        // Attempt to load and switch to new character
        if (m_Player.SwitchCharacter(newType))
        {
            std::cout << "Character switched to: " << EnumTraits<CharacterType>::ToString(newType)
                      << std::endl;
        }
    }

    // Toggles bicycle mode on/off. When bicycling:
    //   - Movement speed is 2.0x base speed
    //   - Uses center-only collision detection
    //   - Different sprite sheet may be used
    static KeyToggle<GLFW_KEY_B> bKey;
    if (!m_Editor.IsActive() && bKey.JustPressed(m_Window))
    {
        bool currentBicycling = m_Player.IsBicycling();
        bool newBicycling = !currentBicycling;

        // Reset copied NPC appearance when starting to bicycle
        if (newBicycling && m_Player.IsUsingCopiedAppearance())
        {
            m_Player.RestoreOriginalAppearance();
            m_Player.UploadTextures(*m_Renderer);
        }

        m_Player.SetBicycling(newBicycling);
        std::cout << "Bicycle: " << (newBicycling ? "ON" : "OFF") << std::endl;
    }

    // Copies the appearance of a nearby NPC, transforming the player.
    // Press X again to restore original appearance.
    // Note: Running or bicycling will automatically restore original appearance
    //       since NPCs don't have running/bicycle sprites.
    static bool xKeyPressed = false;
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueSnap.active && glfwGetKey(m_Window, GLFW_KEY_X) == GLFW_PRESS && !xKeyPressed)
    {
        if (m_Player.IsUsingCopiedAppearance())
        {
            // Restore original appearance
            m_Player.RestoreOriginalAppearance();
            m_Player.UploadTextures(*m_Renderer);
            std::cout << "Restored original appearance (X)" << std::endl;
        }
        else
        {
            // Try to copy appearance from nearby NPC
            glm::vec2 playerPos = m_Player.GetPosition();
            const float COPY_RANGE = 32.0f;  // 2 tiles

            NonPlayerCharacter* nearestNPC = nullptr;
            float nearestDist = COPY_RANGE + 1.0f;

            for (auto& npc : m_NPCs)
            {
                glm::vec2 npcPos = npc.GetPosition();
                float dist = glm::length(npcPos - playerPos);
                if (dist < nearestDist && dist <= COPY_RANGE)
                {
                    nearestDist = dist;
                    nearestNPC = &npc;
                }
            }

            if (nearestNPC != nullptr)
            {
                std::string spritePath = nearestNPC->GetSpritePath();
                if (m_Player.CopyAppearanceFrom(spritePath))
                {
                    m_Player.UploadTextures(*m_Renderer);
                    std::cout << "Copied appearance from: " << nearestNPC->GetType() << " (X)"
                              << std::endl;
                }
            }
            else
            {
                std::cout << "No NPC nearby to copy (X)" << std::endl;
            }
        }
        xKeyPressed = true;
    }
    // In debug mode, X key toggles corner cutting on the collision tile under cursor
    // The corner nearest to the mouse cursor within the tile is toggled
    if (m_Editor.IsDebugMode() && glfwGetKey(m_Window, GLFW_KEY_X) == GLFW_PRESS && !xKeyPressed)
    {
        double mouseX, mouseY;
        glfwGetCursorPos(m_Window, &mouseX, &mouseY);

        // Calculate world coordinates from mouse position
        float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
        float baseWorldHeight =
            static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
        float worldWidth = baseWorldWidth / m_Camera.zoom;
        float worldHeight = baseWorldHeight / m_Camera.zoom;

        float worldX =
            (static_cast<float>(mouseX) / static_cast<float>(m_ScreenWidth)) * worldWidth +
            m_Camera.position.x;
        float worldY =
            (static_cast<float>(mouseY) / static_cast<float>(m_ScreenHeight)) * worldHeight +
            m_Camera.position.y;

        int tileWidth = m_Tilemap.GetTileWidth();
        int tileHeight = m_Tilemap.GetTileHeight();
        int tileX = static_cast<int>(worldX / tileWidth);
        int tileY = static_cast<int>(worldY / tileHeight);

        if (tileX >= 0 && tileY >= 0 && tileX < m_Tilemap.GetMapWidth() &&
            tileY < m_Tilemap.GetMapHeight())
        {
            if (m_Tilemap.GetTileCollision(tileX, tileY))
            {
                // Determine which corner is nearest to mouse position within the tile
                float localX = worldX - (tileX * tileWidth);
                float localY = worldY - (tileY * tileHeight);
                float halfTile = tileWidth * 0.5f;

                Tilemap::Corner corner;
                const char* cornerName;
                if (localX < halfTile && localY < halfTile)
                {
                    corner = Tilemap::CORNER_TL;
                    cornerName = "top-left";
                }
                else if (localX >= halfTile && localY < halfTile)
                {
                    corner = Tilemap::CORNER_TR;
                    cornerName = "top-right";
                }
                else if (localX < halfTile && localY >= halfTile)
                {
                    corner = Tilemap::CORNER_BL;
                    cornerName = "bottom-left";
                }
                else
                {
                    corner = Tilemap::CORNER_BR;
                    cornerName = "bottom-right";
                }

                bool currentlyBlocked = m_Tilemap.IsCornerCutBlocked(tileX, tileY, corner);
                m_Tilemap.SetCornerCutBlocked(tileX, tileY, corner, !currentlyBlocked);
                std::cout << "Corner cutting " << cornerName << " at (" << tileX << ", " << tileY
                          << "): " << (!currentlyBlocked ? "BLOCKED" : "ALLOWED") << std::endl;
            }
            else
            {
                std::cout << "Tile (" << tileX << ", " << tileY
                          << ") has no collision - corner cutting N/A" << std::endl;
            }
        }
        xKeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_X) == GLFW_RELEASE)
    {
        xKeyPressed = false;
    }

    // Initiates dialogue with an NPC when
    //   1. Player is within INTERACTION_RANGE and
    //   2. NPC is in front of player or
    //   3. NPC hitbox is overlapping player hitbox
    static KeyToggle<GLFW_KEY_F> fKey;
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueSnap.active && fKey.JustPressed(m_Window))
    {
        glm::vec2 playerPos = m_Player.GetPosition();
        Direction playerDir = m_Player.GetDirection();

        // Calculate player's tile position
        const float EPS = 0.1f;
        int playerTileX = static_cast<int>(std::floor(playerPos.x / TILE_PIXEL_SIZE));
        int playerTileY = static_cast<int>(std::floor((playerPos.y - EPS) / TILE_PIXEL_SIZE));

        // Calculate tile position in front of player
        int frontTileX = playerTileX;
        int frontTileY = playerTileY;

        switch (playerDir)
        {
            case Direction::DOWN:
                frontTileY += 1;
                break;
            case Direction::UP:
                frontTileY -= 1;
                break;
            case Direction::LEFT:
                frontTileX -= 1;
                break;
            case Direction::RIGHT:
                frontTileX += 1;
                break;
        }

        // Interaction thresholds
        const float INTERACTION_RANGE = 32.0f;   // 2 tiles for easier interaction
        const float COLLISION_DISTANCE = 20.0f;  // Very close = colliding

        // Hitbox dimensions for AABB collision (tile-sized)
        const float PLAYER_HALF_W = TILE_PIXEL_SIZE * 0.5f;
        const float PLAYER_BOX_H = static_cast<float>(TILE_PIXEL_SIZE);
        const float NPC_HALF_W = TILE_PIXEL_SIZE * 0.5f;
        const float NPC_BOX_H = static_cast<float>(TILE_PIXEL_SIZE);
        const float COLLISION_EPS = 0.05f;  // Small margin for floating-point

        for (size_t npcIdx = 0; npcIdx < m_NPCs.size(); ++npcIdx)
        {
            auto& npc = m_NPCs[npcIdx];
            glm::vec2 npcPos = npc.GetPosition();
            float distance = glm::length(npcPos - playerPos);

            // Check if NPC is within interaction range
            if (distance <= INTERACTION_RANGE)
            {
                // Calculate NPC's tile position
                int npcTileX = static_cast<int>(std::floor(npcPos.x / TILE_PIXEL_SIZE));
                int npcTileY = static_cast<int>(std::floor((npcPos.y - EPS) / TILE_PIXEL_SIZE));

                // Check for AABB collision between player and NPC
                float playerMinX = playerPos.x - PLAYER_HALF_W + COLLISION_EPS;
                float playerMaxX = playerPos.x + PLAYER_HALF_W - COLLISION_EPS;
                float playerMaxY = playerPos.y - COLLISION_EPS;
                float playerMinY = playerPos.y - PLAYER_BOX_H + COLLISION_EPS;

                float npcMinX = npcPos.x - NPC_HALF_W + COLLISION_EPS;
                float npcMaxX = npcPos.x + NPC_HALF_W - COLLISION_EPS;
                float npcMaxY = npcPos.y - COLLISION_EPS;
                float npcMinY = npcPos.y - NPC_BOX_H + COLLISION_EPS;

                bool isColliding = (playerMinX < npcMaxX && playerMaxX > npcMinX &&
                                    playerMinY < npcMaxY && playerMaxY > npcMinY);

                // Check if NPC is on the tile in front of the player
                bool isOnFrontTile = (npcTileX == frontTileX && npcTileY == frontTileY);

                // Also check if NPC is on cardinal-adjacent tiles
                int tileDistX = std::abs(playerTileX - npcTileX);
                int tileDistY = std::abs(playerTileY - npcTileY);
                bool isCardinalAdjacent =
                    (tileDistX == 1 && tileDistY == 0) || (tileDistX == 0 && tileDistY == 1);
                bool isSameTile = (tileDistX == 0 && tileDistY == 0);

                // Verify NPC is in the correct direction
                bool isInCorrectDirection = false;
                if (isCardinalAdjacent)
                {
                    switch (playerDir)
                    {
                        case Direction::DOWN:
                            isInCorrectDirection =
                                (npcTileY > playerTileY && npcTileX == playerTileX);
                            break;
                        case Direction::UP:
                            isInCorrectDirection =
                                (npcTileY < playerTileY && npcTileX == playerTileX);
                            break;
                        case Direction::LEFT:
                            isInCorrectDirection =
                                (npcTileX < playerTileX && npcTileY == playerTileY);
                            break;
                        case Direction::RIGHT:
                            isInCorrectDirection =
                                (npcTileX > playerTileX && npcTileY == playerTileY);
                            break;
                    }
                }

                // Check if NPC is very close and roughly in front
                bool isVeryClose = (distance <= COLLISION_DISTANCE);
                glm::vec2 toNPC = npcPos - playerPos;
                bool isRoughlyInFront = false;
                if (isVeryClose)
                {
                    // When very close, be more lenient with direction check
                    switch (playerDir)
                    {
                        case Direction::DOWN:
                            isRoughlyInFront = (toNPC.y > -8.0f);  // NPC is below or at same level
                            break;
                        case Direction::UP:
                            isRoughlyInFront = (toNPC.y < 8.0f);  // NPC is above or at same level
                            break;
                        case Direction::LEFT:
                            isRoughlyInFront =
                                (toNPC.x < 8.0f);  // NPC is to the left or at same level
                            break;
                        case Direction::RIGHT:
                            isRoughlyInFront =
                                (toNPC.x > -8.0f);  // NPC is to the right or at same level
                            break;
                    }
                }

                // Start dialogue if:
                // 1. NPC is colliding with player or
                // 2. NPC is on front tile or
                // 3. NPC is cardinal-adjacent in correct direction or
                // 4. NPC is very close and roughly in front
                if (isColliding || isOnFrontTile || isInCorrectDirection ||
                    (isVeryClose && isRoughlyInFront))
                {
                    // Delay dialogue activation until the alignment snap completes.
                    m_DialogueNPCIndex = static_cast<int>(npcIdx);
                    m_DialoguePage = 0;
                    m_DialogueSnap.prefersTree = npc.HasDialogueTree();
                    m_DialogueSnap.fallbackText = npc.GetDialogue();

                    // Get current positions
                    playerPos = m_Player.GetPosition();
                    npcPos = npc.GetPosition();

                    // Snap NPC to center of current tile
                    // X anchor is horizontally centered, so floor(x/TILE) already gives correct
                    // tile Y anchor is at bottom, so subtract TILE to find the tile the NPC stands
                    // on
                    int snapTileY = static_cast<int>(
                        std::round((npcPos.y - TILE_PIXEL_SIZE) / TILE_PIXEL_SIZE));

                    m_DialogueSnap.npcTileX = npcTileX;
                    m_DialogueSnap.npcTileY = snapTileY;
                    glm::vec2 npcTargetPos(
                        static_cast<float>(m_DialogueSnap.npcTileX * TILE_PIXEL_SIZE +
                                           TILE_PIXEL_SIZE / 2),
                        static_cast<float>(m_DialogueSnap.npcTileY * TILE_PIXEL_SIZE +
                                           TILE_PIXEL_SIZE));

                    // Recalculate player tile position after getting fresh position
                    playerTileX = static_cast<int>(std::floor(playerPos.x / TILE_PIXEL_SIZE));
                    playerTileY =
                        static_cast<int>(std::floor((playerPos.y - EPS) / TILE_PIXEL_SIZE));

                    // Use the new NPC tile coordinates for direction calculation
                    // This ensures we look for a spot relative to where the NPC ended up
                    npcTileY = snapTileY;

                    // Calculate direction from NPC to player
                    int dx = playerTileX - npcTileX;
                    int dy = playerTileY - npcTileY;

                    // If diagonal, snap to nearest cardinal direction
                    // Prefer the direction with larger absolute value
                    // TODO: Refactor, this logic is crazy messy
                    int finalDx = 0;
                    int finalDy = 0;
                    if (dx != 0 && dy != 0)
                    {
                        // Diagonal, snap to nearest cardinal
                        if (std::abs(dx) > std::abs(dy))
                        {
                            finalDx = (dx > 0) ? 1 : -1;
                            finalDy = 0;
                        }
                        else
                        {
                            finalDx = 0;
                            finalDy = (dy > 0) ? 1 : -1;
                        }
                    }
                    else if (dx != 0)
                    {
                        // Horizontal only
                        finalDx = (dx > 0) ? 1 : -1;
                        finalDy = 0;
                    }
                    else if (dy != 0)
                    {
                        // Vertical only
                        finalDx = 0;
                        finalDy = (dy > 0) ? 1 : -1;
                    }
                    else
                    {
                        // Same tile, default to down
                        finalDx = 0;
                        finalDy = 1;
                    }

                    // Find nearest tile (round instead of floor so player doesn't snap when
                    // slightly off-center)
                    int currentPlayerTileX = static_cast<int>(
                        std::round((playerPos.x - TILE_PIXEL_SIZE / 2) / TILE_PIXEL_SIZE));
                    int currentPlayerTileY = static_cast<int>(
                        std::round((playerPos.y - TILE_PIXEL_SIZE) / TILE_PIXEL_SIZE));

                    // Check if player is already on a valid cardinal-adjacent tile
                    bool playerAlreadyValid = false;
                    if (currentPlayerTileX != npcTileX || currentPlayerTileY != npcTileY)
                    {
                        // Check if current position is valid
                        if (currentPlayerTileX >= 0 && currentPlayerTileY >= 0 &&
                            currentPlayerTileX < m_Tilemap.GetMapWidth() &&
                            currentPlayerTileY < m_Tilemap.GetMapHeight() &&
                            !m_Tilemap.GetTileCollision(currentPlayerTileX, currentPlayerTileY))
                        {
                            // Check if it's cardinal-adjacent to NPC
                            int tileDistX = std::abs(currentPlayerTileX - npcTileX);
                            int tileDistY = std::abs(currentPlayerTileY - npcTileY);
                            bool isCardinalAdjacent = (tileDistX == 1 && tileDistY == 0) ||
                                                      (tileDistX == 0 && tileDistY == 1);
                            if (isCardinalAdjacent)
                            {
                                playerAlreadyValid = true;
                            }
                        }
                    }

                    int playerTileXFinal = currentPlayerTileX;
                    int playerTileYFinal = currentPlayerTileY;
                    auto isValidSnapTile = [this, npcTileX, npcTileY](int tx, int ty)
                    {
                        if (tx < 0 || ty < 0 || tx >= m_Tilemap.GetMapWidth() ||
                            ty >= m_Tilemap.GetMapHeight())
                            return false;
                        if (tx == npcTileX && ty == npcTileY)
                            return false;
                        return !m_Tilemap.GetTileCollision(tx, ty);
                    };

                    // Only snap if player is not already in a valid position
                    if (!playerAlreadyValid)
                    {
                        // Ensure finalDx and finalDy are not both zero
                        // This handles the edge case where player and NPC might be on same tile due
                        // to floating point precision
                        if (finalDx == 0 && finalDy == 0)
                        {
                            // Default to down if somehow on same tile
                            finalDx = 0;
                            finalDy = 1;
                        }

                        // Try to position player one tile away in the chosen direction
                        // If that position is invalid, try other cardinal directions
                        struct CardinalDir
                        {
                            int dx, dy;
                        };
                        CardinalDir cardinals[] = {
                            {finalDx, finalDy},  // Preferred direction
                            {0, 1},              // Down
                            {0, -1},             // Up
                            {1, 0},              // Right
                            {-1, 0}              // Left
                        };

                        bool foundValidPosition = false;

                        for (const auto& dir : cardinals)
                        {
                            int testTileX = npcTileX + dir.dx;
                            int testTileY = npcTileY + dir.dy;

                            // Safety check, never place player on NPC's tile
                            if (testTileX == npcTileX && testTileY == npcTileY)
                            {
                                continue;
                            }

                            // Check bounds
                            if (testTileX < 0 || testTileY < 0 ||
                                testTileX >= m_Tilemap.GetMapWidth() ||
                                testTileY >= m_Tilemap.GetMapHeight())
                            {
                                continue;
                            }

                            // Check collision
                            if (m_Tilemap.GetTileCollision(testTileX, testTileY))
                            {
                                continue;
                            }

                            // Valid position found
                            playerTileXFinal = testTileX;
                            playerTileYFinal = testTileY;
                            foundValidPosition = true;
                            break;
                        }

                        // If no valid position found, try the preferred direction
                        if (!foundValidPosition)
                        {
                            int safeTileX = npcTileX + finalDx;
                            int safeTileY = npcTileY + finalDy;

                            if (isValidSnapTile(safeTileX, safeTileY))
                            {
                                playerTileXFinal = safeTileX;
                                playerTileYFinal = safeTileY;
                            }
                            else if (isValidSnapTile(npcTileX, npcTileY + 1))
                            {
                                // Fallback, use down direction if preferred would place on same
                                // tile
                                playerTileXFinal = npcTileX;
                                playerTileYFinal = npcTileY + 1;
                            }
                        }

                        if (!isValidSnapTile(playerTileXFinal, playerTileYFinal))
                        {
                            if (isValidSnapTile(currentPlayerTileX, currentPlayerTileY))
                            {
                                playerTileXFinal = currentPlayerTileX;
                                playerTileYFinal = currentPlayerTileY;
                            }
                            else
                            {
                                // Keep current world position if no safe snap tile is available.
                                playerTileXFinal = -1;
                                playerTileYFinal = -1;
                            }
                        }
                    }
                    else
                    {
                        // Player is already valid, but ensure they're exactly at tile center
                        playerTileXFinal = currentPlayerTileX;
                        playerTileYFinal = currentPlayerTileY;
                    }

                    glm::vec2 playerTargetPos = playerPos;
                    bool hasPlayerTileTarget = (playerTileXFinal >= 0 && playerTileYFinal >= 0);
                    if (hasPlayerTileTarget)
                    {
                        playerTargetPos =
                            glm::vec2(static_cast<float>(playerTileXFinal * TILE_PIXEL_SIZE +
                                                         TILE_PIXEL_SIZE / 2),
                                      static_cast<float>(playerTileYFinal * TILE_PIXEL_SIZE +
                                                         TILE_PIXEL_SIZE));
                    }

                    // Make NPC face the player
                    glm::vec2 npcToPlayer = playerTargetPos - npcTargetPos;
                    NPCDirection npcFacing = NPCDirection::DOWN;
                    if (std::abs(npcToPlayer.x) > std::abs(npcToPlayer.y))
                    {
                        // Horizontal direction
                        npcFacing = (npcToPlayer.x > 0) ? NPCDirection::RIGHT : NPCDirection::LEFT;
                    }
                    else
                    {
                        // Vertical direction
                        npcFacing = (npcToPlayer.y > 0) ? NPCDirection::DOWN : NPCDirection::UP;
                    }

                    // Make player face the NPC
                    glm::vec2 playerToNPC = npcTargetPos - playerTargetPos;
                    Direction playerFacing = Direction::DOWN;
                    if (std::abs(playerToNPC.x) > std::abs(playerToNPC.y))
                    {
                        // Horizontal direction
                        playerFacing = (playerToNPC.x > 0) ? Direction::RIGHT : Direction::LEFT;
                    }
                    else
                    {
                        // Vertical direction
                        playerFacing = (playerToNPC.y > 0) ? Direction::DOWN : Direction::UP;
                    }

                    // Freeze both and begin smooth alignment.
                    m_Player.Stop();
                    npc.SetStopped(true);
                    npc.ResetAnimationToIdle();

                    m_DialogueSnap.active = true;
                    m_DialogueSnap.timer = 0.0f;
                    m_DialogueSnap.duration = 0.42f;
                    m_DialogueSnap.playerStart = playerPos;
                    m_DialogueSnap.playerTarget = playerTargetPos;
                    m_DialogueSnap.npcStart = npcPos;
                    m_DialogueSnap.npcTarget = npcTargetPos;
                    m_DialogueSnap.playerTileX =
                        hasPlayerTileTarget
                            ? playerTileXFinal
                            : static_cast<int>(std::round(
                                  (playerTargetPos.x - TILE_PIXEL_SIZE / 2) / TILE_PIXEL_SIZE));
                    m_DialogueSnap.playerTileY =
                        hasPlayerTileTarget
                            ? playerTileYFinal
                            : static_cast<int>(std::round((playerTargetPos.y - TILE_PIXEL_SIZE) /
                                                          TILE_PIXEL_SIZE));
                    m_DialogueSnap.hasPlayerTile = hasPlayerTileTarget;
                    m_DialogueSnap.playerFacing = playerFacing;
                    m_DialogueSnap.npcFacing = npcFacing;

                    std::cout << "Starting dialogue snap with NPC: " << npc.GetType()
                              << " target NPC tile (" << m_DialogueSnap.npcTileX << ", "
                              << m_DialogueSnap.npcTileY << ")"
                              << ", target player tile (" << m_DialogueSnap.playerTileX << ", "
                              << m_DialogueSnap.playerTileY << ")" << std::endl;
                    break;
                }
            }
        }
    }

    // Handle branching dialogue tree input
    if (m_DialogueManager.IsActive())
    {
        static KeyToggle<GLFW_KEY_UP, GLFW_KEY_W> upKey;
        static KeyToggle<GLFW_KEY_DOWN, GLFW_KEY_S> downKey;
        static KeyToggle<GLFW_KEY_ENTER> enterKeyTree;
        static KeyToggle<GLFW_KEY_SPACE> spaceKeyTree;
        static KeyToggle<GLFW_KEY_ESCAPE> escapeKeyTree;

        // Navigate options with Up/Down or W/S
        if (upKey.JustPressed(m_Window))
            m_DialogueManager.SelectPrevious();

        if (downKey.JustPressed(m_Window))
            m_DialogueManager.SelectNext();

        // Confirm selection with Enter or Space
        if (enterKeyTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        if (spaceKeyTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        // Escape to force-close dialogue
        if (escapeKeyTree.JustPressed(m_Window))
            ForceCloseTreeDialogue();
    }

    // Close simple dialogue
    if (m_InDialogue)
    {
        static KeyToggle<GLFW_KEY_ENTER> enterKey;
        static KeyToggle<GLFW_KEY_SPACE> spaceKey;
        static KeyToggle<GLFW_KEY_ESCAPE> escapeKey;

        if (enterKey.JustPressed(m_Window))
            CloseSimpleDialogue();

        if (spaceKey.JustPressed(m_Window))
            CloseSimpleDialogue();

        if (escapeKey.JustPressed(m_Window))
            CloseSimpleDialogue();
    }

    // Only process player movement if not in editor mode and not in dialogue
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueSnap.active)
    {
        // Remember previous position for resolving collisions with NPCs
        m_PlayerPreviousPosition = m_Player.GetPosition();

        // Collect NPC positions for collision checking (pre-allocated member avoids per-frame
        // alloc)
        m_NpcPositions.clear();
        for (const auto& npc : m_NPCs)
        {
            m_NpcPositions.push_back(npc.GetPosition());
        }

        m_Player.Move(moveDirection, deltaTime, &m_Tilemap, &m_NpcPositions);
    }
    else if (m_InDialogue || m_DialogueSnap.active)
    {
        // Stop player movement during dialogue
        m_Player.Stop();
    }

    // Process mouse input for editor
    if (m_Editor.IsActive())
    {
        m_Editor.ProcessMouseInput(MakeEditorContext());
    }
}

void Game::ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
    // Retrieve Game instance from window user pointer
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game)
    {
        return;
    }

    // Delegate editor-specific scroll handling
    if (game->m_Editor.IsActive())
    {
        game->m_Editor.HandleScroll(yoffset, game->MakeEditorContext());
        // If tile picker is open, editor handles all scroll
        if (game->m_Editor.IsShowTilePicker())
        {
            return;
        }
    }

    // Check for Ctrl modifier
    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    // Camera zoom with Ctrl+scroll
    if (ctrlPressed)
    {
        // Zoom centered on player position
        float baseWorldWidth =
            static_cast<float>(game->m_TilesVisibleWidth * game->m_Tilemap.GetTileWidth());
        float baseWorldHeight =
            static_cast<float>(game->m_TilesVisibleHeight * game->m_Tilemap.GetTileHeight());

        float oldZoom = game->m_Camera.zoom;
        float oldWorldWidth = baseWorldWidth / oldZoom;
        float oldWorldHeight = baseWorldHeight / oldZoom;

        // Get the player's visual center
        glm::vec2 playerPos = game->m_Player.GetPosition();
        glm::vec2 playerVisualCenter =
            playerPos - glm::vec2(0.0f, PlayerCharacter::HITBOX_HEIGHT * 0.5f);

        // Apply zoom with snapping to prevent sub-pixel seams
        float zoomDelta = yoffset > 0 ? 1.1f : 0.9f;
        game->m_Camera.zoom *= zoomDelta;

        // Editor mode allows zooming out further (0.1x) to see entire map
        float minZoom = (game->m_Editor.IsActive() && game->m_Camera.freeMode) ? 0.1f : 0.4f;
        game->m_Camera.zoom = std::max(minZoom, std::min(4.0f, game->m_Camera.zoom));
        // Snap to 0.1 increments
        game->m_Camera.zoom = std::round(game->m_Camera.zoom * 10.0f) / 10.0f;

        float newZoom = game->m_Camera.zoom;
        float newWorldWidth = baseWorldWidth / newZoom;
        float newWorldHeight = baseWorldHeight / newZoom;

        // Adjust camera position to keep player centered
        game->m_Camera.position =
            playerVisualCenter - glm::vec2(newWorldWidth * 0.5f, newWorldHeight * 0.5f);

        // Clamp camera to map bounds (skip in editor free-camera mode)
        if (!(game->m_Editor.IsActive() && game->m_Camera.freeMode))
        {
            float mapWidth =
                static_cast<float>(game->m_Tilemap.GetMapWidth() * game->m_Tilemap.GetTileWidth());
            float mapHeight = static_cast<float>(game->m_Tilemap.GetMapHeight() *
                                                 game->m_Tilemap.GetTileHeight());
            game->m_Camera.position.x =
                std::max(0.0f, std::min(game->m_Camera.position.x, mapWidth - newWorldWidth));
            game->m_Camera.position.y =
                std::max(0.0f, std::min(game->m_Camera.position.y, mapHeight - newWorldHeight));
        }

        // Also update the follow target so camera doesn't snap back
        game->m_Camera.followTarget = game->m_Camera.position;

        std::cout << "Camera zoom: " << game->m_Camera.zoom << "x" << std::endl;
    }
}

void Game::ReleaseDialogueNPC()
{
    if (m_DialogueNPCIndex >= 0 && m_DialogueNPCIndex < static_cast<int>(m_NPCs.size()))
    {
        m_NPCs[m_DialogueNPCIndex].SetStopped(false);
    }
    m_DialogueNPCIndex = -1;
}

void Game::CloseSimpleDialogue()
{
    m_InDialogue = false;
    ReleaseDialogueNPC();
    m_DialogueText.clear();
}

void Game::ConfirmOrAdvanceTreeDialogue()
{
    // If typewriter is still revealing text, skip to full reveal first
    if (m_DialogueCharReveal >= 0.0f)
    {
        m_DialogueCharReveal = -1.0f;
        return;
    }

    if (!IsDialogueOnLastPage())
    {
        m_DialoguePage++;
        m_DialogueCharReveal = 0.0f;
    }
    else
    {
        m_DialoguePage = 0;
        m_DialogueCharReveal = 0.0f;
        m_DialogueManager.ConfirmSelection();
        if (!m_DialogueManager.IsActive())
        {
            ReleaseDialogueNPC();
        }
    }
}

void Game::ForceCloseTreeDialogue()
{
    m_DialogueManager.EndDialogue();
    m_DialoguePage = 0;
    ReleaseDialogueNPC();
}
