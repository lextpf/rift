#include "Game.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <glad/glad.h>

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
        moveDirection.y -= 1.0f; // Up
    }
    if (glfwGetKey(m_Window, GLFW_KEY_A) == GLFW_PRESS)
    {
        moveDirection.x -= 1.0f; // Left
    }
    if (glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS)
    {
        moveDirection.y += 1.0f; // Down
    }
    if (glfwGetKey(m_Window, GLFW_KEY_D) == GLFW_PRESS)
    {
        moveDirection.x += 1.0f; // Right
    }

    // Toggles between gameplay and editor mode.
    static bool eKeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_PRESS && !eKeyPressed)
    {
        m_Editor.SetActive(!m_Editor.IsActive());
        eKeyPressed = true;
        std::cout << "Editor mode: " << (m_Editor.IsActive() ? "ON" : "OFF") << std::endl;
        if (m_Editor.IsActive())
        {
            std::cout << "Press T to toggle tile picker visibility" << std::endl;
        }
    }
    if (glfwGetKey(m_Window, GLFW_KEY_E) == GLFW_RELEASE)
    {
        eKeyPressed = false;
    }

    // Delegate all editor-specific key input to Editor
    if (m_Editor.IsActive())
    {
        m_Editor.ProcessInput(deltaTime, MakeEditorContext());
    }

    // --- Remainder of ProcessInput: keys that stay in Game ---
    // (E key is above, editor delegation just above)
    // The following sections handle: Z, F1-F6, Space, PageUp/Down, C, B, X, F, dialogue, player movement


    // Resets camera zoom to 1.0x and recenters on player.
    // In editor mode, also resets tile picker zoom and pan.
    static bool zKeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_Z) == GLFW_PRESS && !zKeyPressed)
    {
        m_CameraZoom = 1.0f;
        std::cout << "Camera zoom reset to 1.0x" << std::endl;

        // Recenter camera on player in gameplay mode
        if (!m_Editor.IsActive())
        {
            // Calculate viewport dimensions at 1.0x zoom
            float worldWidth = static_cast<float>(m_TilesVisibleWidth * 16);
            float worldHeight = static_cast<float>(m_TilesVisibleHeight * 16);

            // Calculate player's visual center
            glm::vec2 playerAnchorTileCenter = m_Player.GetCurrentTileCenter();
            glm::vec2 playerVisualCenter = glm::vec2(playerAnchorTileCenter.x, playerAnchorTileCenter.y - 16.0f);

            // Position camera so player is centered
            m_CameraPosition = playerVisualCenter - glm::vec2(worldWidth / 2.0f, worldHeight / 2.0f);

            // Clamp to map bounds (skip in editor free-camera mode)
            if (!(m_Editor.IsActive() && m_FreeCameraMode))
            {
                float mapWidth = static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
                float mapHeight = static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());
                m_CameraPosition.x = std::max(0.0f, std::min(m_CameraPosition.x, mapWidth - worldWidth));
                m_CameraPosition.y = std::max(0.0f, std::min(m_CameraPosition.y, mapHeight - worldHeight));
            }

            // Disable smooth follow to prevent drift after reset
            m_HasCameraFollowTarget = false;
        }

        // Reset tile picker state in editor mode
        if (m_Editor.IsActive())
        {
            m_Editor.ResetTilePickerState();
        }
        zKeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_Z) == GLFW_RELEASE)
    {
        zKeyPressed = false;
    }

    // Toggle between OpenGL and Vulkan renderers at runtime
    static bool f1KeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_F1) == GLFW_PRESS && !f1KeyPressed)
    {
        // Toggle between OpenGL and Vulkan
        RendererAPI newApi = (m_RendererAPI == RendererAPI::OpenGL)
                                 ? RendererAPI::Vulkan
                                 : RendererAPI::OpenGL;
        SwitchRenderer(newApi);
        f1KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F1) == GLFW_RELEASE)
    {
        f1KeyPressed = false;
    }

    // Toggles FPS and position information display
    static bool f2KeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_F2) == GLFW_PRESS && !f2KeyPressed)
    {
        m_Editor.ToggleShowDebugInfo();
        f2KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F2) == GLFW_RELEASE)
    {
        f2KeyPressed = false;
    }

    // Enables visual debug overlays including:
    //   - Collision tiles
    //   - Player collision tolerance zones
    //   - Navigation tiles
    //   - NPC information
    //   - All tile layers visible
    static bool f3KeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_F3) == GLFW_PRESS && !f3KeyPressed)
    {
        m_Editor.ToggleDebugMode();
        f3KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F3) == GLFW_RELEASE)
    {
        f3KeyPressed = false;
    }

    // Cycle through all 8 time periods
    static bool f4KeyPressed = false;
    static int timeOfDayCycle = 0;
    if (glfwGetKey(m_Window, GLFW_KEY_F4) == GLFW_PRESS && !f4KeyPressed)
    {
        timeOfDayCycle = (timeOfDayCycle + 1) % 8;
        const char *periodName = "";
        switch (timeOfDayCycle)
        {
        case 0: // Dawn (05:00-07:00)
            m_TimeManager.SetTime(6.0f);
            periodName = "Dawn (06:00)";
            break;
        case 1: // Morning (07:00-10:00)
            m_TimeManager.SetTime(8.5f);
            periodName = "Morning (08:30)";
            break;
        case 2: // Midday (10:00-16:00)
            m_TimeManager.SetTime(13.0f);
            periodName = "Midday (13:00)";
            break;
        case 3: // Afternoon (16:00-18:00)
            m_TimeManager.SetTime(17.0f);
            periodName = "Afternoon (17:00)";
            break;
        case 4: // Dusk (18:00-20:00)
            m_TimeManager.SetTime(19.0f);
            periodName = "Dusk (19:00)";
            break;
        case 5: // Evening (20:00-22:00)
            m_TimeManager.SetTime(21.0f);
            periodName = "Evening (21:00)";
            break;
        case 6: // Night (22:00-04:00)
            m_TimeManager.SetTime(1.0f);
            periodName = "Night (01:00)";
            break;
        case 7: // LateNight (04:00-05:00)
            m_TimeManager.SetTime(4.5f);
            periodName = "Late Night (04:30)";
            break;
        }
        std::cout << "Time of day: " << periodName << std::endl;
        f4KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F4) == GLFW_RELEASE)
    {
        f4KeyPressed = false;
    }

    // Toggles the 3D globe effect for an isometric-like view
    static bool f5KeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_F5) == GLFW_PRESS && !f5KeyPressed)
    {
        Toggle3DEffect();
        f5KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F5) == GLFW_RELEASE)
    {
        f5KeyPressed = false;
    }

    // Toggle FPS cap (0 = uncapped, 60 = capped)
    static bool f6KeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_F6) == GLFW_PRESS && !f6KeyPressed)
    {
        if (m_TargetFps <= 0.0f)
        {
            m_TargetFps = 500.0f;
            std::cout << "FPS capped at 500" << std::endl;
        }
        else
        {
            m_TargetFps = 0.0f;
            std::cout << "FPS uncapped" << std::endl;
        }
        f6KeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F6) == GLFW_RELEASE)
    {
        f6KeyPressed = false;
    }

    // Toggle free camera mode (Space) - camera stops following player
    // WASD/Arrows can then pan camera while player still moves with WASD
    static bool spaceKeyFreeCamera = false;
    if (!m_InDialogue && !m_DialogueManager.IsActive() && !m_Editor.IsActive())
    {
        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS && !spaceKeyFreeCamera)
        {
            m_FreeCameraMode = !m_FreeCameraMode;
            std::cout << "Free Camera Mode: " << (m_FreeCameraMode ? "ON" : "OFF") << std::endl;
            spaceKeyFreeCamera = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_RELEASE)
        {
            spaceKeyFreeCamera = false;
        }
    }

    // Adjusts 3D effect parameters when enabled:
    //   - Page Up/Down adjusts globe radius and tilt
    static bool pageUpPressed = false;
    static bool pageDownPressed = false;
    if (m_Enable3DEffect)
    {
        // Globe effect parameter adjustment
        if (glfwGetKey(m_Window, GLFW_KEY_PAGE_UP) == GLFW_PRESS && !pageUpPressed)
        {
            m_GlobeSphereRadius = std::min(500.0f, m_GlobeSphereRadius + 10.0f);
            m_CameraTilt = std::max(0.0f, m_CameraTilt - 0.05f);
            std::cout << "3D Effect - Radius: " << m_GlobeSphereRadius << ", Tilt: " << m_CameraTilt << std::endl;
            pageUpPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_PAGE_UP) == GLFW_RELEASE)
        {
            pageUpPressed = false;
        }

        if (glfwGetKey(m_Window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS && !pageDownPressed)
        {
            m_GlobeSphereRadius = std::max(50.0f, m_GlobeSphereRadius - 10.0f);
            m_CameraTilt = std::min(1.0f, m_CameraTilt + 0.05f);
            std::cout << "3D Effect - Radius: " << m_GlobeSphereRadius << ", Tilt: " << m_CameraTilt << std::endl;
            pageDownPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_PAGE_DOWN) == GLFW_RELEASE)
        {
            pageDownPressed = false;
        }
    }
    // Cycles through available player character sprites.
    // Each character type has its own sprite sheet loaded from assets.
    static bool cKeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_C) == GLFW_PRESS && !cKeyPressed)
    {
        CharacterType currentType = m_Player.GetCharacterType();
        CharacterType newType;

        // Cycle to next character type
        switch (currentType)
        {
        case CharacterType::BW1_MALE:
            newType = CharacterType::BW1_FEMALE;
            break;
        case CharacterType::BW1_FEMALE:
            newType = CharacterType::BW2_MALE;
            break;
        case CharacterType::BW2_MALE:
            newType = CharacterType::BW2_FEMALE;
            break;
        case CharacterType::BW2_FEMALE:
            newType = CharacterType::CC_FEMALE;
            break;
        case CharacterType::CC_FEMALE:
            newType = CharacterType::BW1_MALE; // Wrap to start
            break;
        default:
            newType = CharacterType::BW1_MALE;
            break;
        }

        // Attempt to load and switch to new character
        if (m_Player.SwitchCharacter(newType))
        {
            const char *name = "BW1_MALE";
            switch (newType)
            {
            case CharacterType::BW1_MALE:
                name = "BW1_MALE";
                break;
            case CharacterType::BW1_FEMALE:
                name = "BW1_FEMALE";
                break;
            case CharacterType::BW2_MALE:
                name = "BW2_MALE";
                break;
            case CharacterType::BW2_FEMALE:
                name = "BW2_FEMALE";
                break;
            case CharacterType::CC_FEMALE:
                name = "CC_FEMALE";
                break;
            }
            std::cout << "Character switched to: " << name << std::endl;
        }

        cKeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_C) == GLFW_RELEASE)
    {
        cKeyPressed = false;
    }

    // Toggles bicycle mode on/off. When bicycling:
    //   - Movement speed is 2.0x base speed
    //   - Uses center-only collision detection
    //   - Different sprite sheet may be used
    static bool bKeyPressed = false;
    if (glfwGetKey(m_Window, GLFW_KEY_B) == GLFW_PRESS && !bKeyPressed && !m_Editor.IsActive())
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
        bKeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_B) == GLFW_RELEASE)
    {
        bKeyPressed = false;
    }

    // Copies the appearance of a nearby NPC, transforming the player.
    // Press X again to restore original appearance.
    // Note: Running or bicycling will automatically restore original appearance
    //       since NPCs don't have running/bicycle sprites.
    static bool xKeyPressed = false;
    if (!m_Editor.IsActive() && !m_InDialogue && glfwGetKey(m_Window, GLFW_KEY_X) == GLFW_PRESS && !xKeyPressed)
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
            const float COPY_RANGE = 32.0f; // 2 tiles

            NonPlayerCharacter *nearestNPC = nullptr;
            float nearestDist = COPY_RANGE + 1.0f;

            for (auto &npc : m_NPCs)
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
                    std::cout << "Copied appearance from: " << nearestNPC->GetType() << " (X)" << std::endl;
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
        float baseWorldHeight = static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
        float worldWidth = baseWorldWidth / m_CameraZoom;
        float worldHeight = baseWorldHeight / m_CameraZoom;

        float worldX = (static_cast<float>(mouseX) / static_cast<float>(m_ScreenWidth)) * worldWidth + m_CameraPosition.x;
        float worldY = (static_cast<float>(mouseY) / static_cast<float>(m_ScreenHeight)) * worldHeight + m_CameraPosition.y;

        int tileWidth = m_Tilemap.GetTileWidth();
        int tileHeight = m_Tilemap.GetTileHeight();
        int tileX = static_cast<int>(worldX / tileWidth);
        int tileY = static_cast<int>(worldY / tileHeight);

        if (tileX >= 0 && tileY >= 0 && tileX < m_Tilemap.GetMapWidth() && tileY < m_Tilemap.GetMapHeight())
        {
            if (m_Tilemap.GetTileCollision(tileX, tileY))
            {
                // Determine which corner is nearest to mouse position within the tile
                float localX = worldX - (tileX * tileWidth);
                float localY = worldY - (tileY * tileHeight);
                float halfTile = tileWidth * 0.5f;

                Tilemap::Corner corner;
                const char *cornerName;
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
                std::cout << "Corner cutting " << cornerName << " at (" << tileX << ", " << tileY << "): "
                          << (!currentlyBlocked ? "BLOCKED" : "ALLOWED") << std::endl;
            }
            else
            {
                std::cout << "Tile (" << tileX << ", " << tileY << ") has no collision - corner cutting N/A" << std::endl;
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
    static bool fKeyPressed = false;
    if (!m_Editor.IsActive() && !m_InDialogue && glfwGetKey(m_Window, GLFW_KEY_F) == GLFW_PRESS && !fKeyPressed)
    {
        glm::vec2 playerPos = m_Player.GetPosition();
        Direction playerDir = m_Player.GetDirection();

        // Calculate player's tile position
        const float EPS = 0.1f;
        int playerTileX = static_cast<int>(std::floor(playerPos.x / 16.0f));
        int playerTileY = static_cast<int>(std::floor((playerPos.y - EPS) / 16.0f));

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
        const float INTERACTION_RANGE = 32.0f;  // 2 tiles for easier interaction
        const float COLLISION_DISTANCE = 20.0f; // Very close = colliding

        // Hitbox dimensions for AABB collision
        const float PLAYER_HALF_W = 16.0f * 0.5f; // Player: 16x16 px hitbox
        const float PLAYER_BOX_H = 16.0f;
        const float NPC_HALF_W = 16.0f * 0.5f; // NPC: 16x16 px hitbox
        const float NPC_BOX_H = 16.0f;
        const float COLLISION_EPS = 0.05f; // Small margin for floating-point

        for (auto &npc : m_NPCs)
        {
            glm::vec2 npcPos = npc.GetPosition();
            float distance = glm::length(npcPos - playerPos);

            // Check if NPC is within interaction range
            if (distance <= INTERACTION_RANGE)
            {
                // Calculate NPC's tile position
                int npcTileX = static_cast<int>(std::floor(npcPos.x / 16.0f));
                int npcTileY = static_cast<int>(std::floor((npcPos.y - EPS) / 16.0f));

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
                bool isCardinalAdjacent = (tileDistX == 1 && tileDistY == 0) || (tileDistX == 0 && tileDistY == 1);
                bool isSameTile = (tileDistX == 0 && tileDistY == 0);

                // Verify NPC is in the correct direction
                bool isInCorrectDirection = false;
                if (isCardinalAdjacent)
                {
                    switch (playerDir)
                    {
                    case Direction::DOWN:
                        isInCorrectDirection = (npcTileY > playerTileY && npcTileX == playerTileX);
                        break;
                    case Direction::UP:
                        isInCorrectDirection = (npcTileY < playerTileY && npcTileX == playerTileX);
                        break;
                    case Direction::LEFT:
                        isInCorrectDirection = (npcTileX < playerTileX && npcTileY == playerTileY);
                        break;
                    case Direction::RIGHT:
                        isInCorrectDirection = (npcTileX > playerTileX && npcTileY == playerTileY);
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
                        isRoughlyInFront = (toNPC.y > -8.0f); // NPC is below or at same level
                        break;
                    case Direction::UP:
                        isRoughlyInFront = (toNPC.y < 8.0f); // NPC is above or at same level
                        break;
                    case Direction::LEFT:
                        isRoughlyInFront = (toNPC.x < 8.0f); // NPC is to the left or at same level
                        break;
                    case Direction::RIGHT:
                        isRoughlyInFront = (toNPC.x > -8.0f); // NPC is to the right or at same level
                        break;
                    }
                }

                // Start dialogue if:
                // 1. NPC is colliding with player or
                // 2. NPC is on front tile or
                // 3. NPC is cardinal-adjacent in correct direction or
                // 4. NPC is very close and roughly in front
                if (isColliding || isOnFrontTile || isInCorrectDirection || (isVeryClose && isRoughlyInFront))
                {
                    // Check if NPC has a dialogue tree
                    if (npc.HasDialogueTree() && m_DialogueManager.StartDialogue(&npc))
                    {
                        // Using branching dialogue system
                        m_DialogueNPC = &npc;
                        m_DialoguePage = 0; // Reset pagination
                    }
                    else
                    {
                        // Fall back to simple dialogue
                        m_InDialogue = true;
                        m_DialogueNPC = &npc;
                        m_DialogueText = npc.GetDialogue();
                    }

                    // Get current positions
                    playerPos = m_Player.GetPosition();
                    npcPos = npc.GetPosition();

                    // Snap NPC to center of current tile
                    // X anchor is horizontally centered, so floor(x/16) already gives correct tile
                    // Y anchor is at bottom, so subtract 16 to find the tile the NPC stands on
                    int snapTileY = static_cast<int>(std::round((npcPos.y - 16.0f) / 16.0f));

                    // Pass true to preserve patrol route so NPC continues where it left off after dialogue
                    npc.SetTilePosition(npcTileX, snapTileY, 16, true);
                    npcPos = npc.GetPosition();

                    // Recalculate player tile position after getting fresh position
                    playerTileX = static_cast<int>(std::floor(playerPos.x / 16.0f));
                    playerTileY = static_cast<int>(std::floor((playerPos.y - EPS) / 16.0f));

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

                    // Find nearest tile (round instead of floor so player doesn't snap when slightly off-center)
                    int currentPlayerTileX = static_cast<int>(std::round((playerPos.x - 8.0f) / 16.0f));
                    int currentPlayerTileY = static_cast<int>(std::round((playerPos.y - 16.0f) / 16.0f));

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
                            bool isCardinalAdjacent = (tileDistX == 1 && tileDistY == 0) || (tileDistX == 0 && tileDistY == 1);
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
                        if (tx < 0 || ty < 0 || tx >= m_Tilemap.GetMapWidth() || ty >= m_Tilemap.GetMapHeight())
                            return false;
                        if (tx == npcTileX && ty == npcTileY)
                            return false;
                        return !m_Tilemap.GetTileCollision(tx, ty);
                    };

                    // Only snap if player is not already in a valid position
                    if (!playerAlreadyValid)
                    {
                        // Ensure finalDx and finalDy are not both zero
                        // This handles the edge case where player and NPC might be on same tile due to floating point precision
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
                            {finalDx, finalDy}, // Preferred direction
                            {0, 1},             // Down
                            {0, -1},            // Up
                            {1, 0},             // Right
                            {-1, 0}             // Left
                        };

                        bool foundValidPosition = false;

                        for (const auto &dir : cardinals)
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
                                // Fallback, use down direction if preferred would place on same tile
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

                        // Snap player to the chosen tile position (ensures tile center)
                        if (playerTileXFinal >= 0 && playerTileYFinal >= 0)
                        {
                            m_Player.SetTilePosition(playerTileXFinal, playerTileYFinal);
                        }
                    }
                    else
                    {
                        // Player is already valid, but ensure they're exactly at tile center
                        m_Player.SetTilePosition(currentPlayerTileX, currentPlayerTileY);
                    }

                    // Stop player movement to prevent getting stuck
                    m_Player.Stop();

                    // Get fresh position after snapping to tile center
                    playerPos = m_Player.GetPosition();

                    // Make NPC face the player
                    glm::vec2 npcToPlayer = playerPos - npcPos;
                    if (std::abs(npcToPlayer.x) > std::abs(npcToPlayer.y))
                    {
                        // Horizontal direction
                        npc.SetDirection(npcToPlayer.x > 0 ? NPCDirection::RIGHT : NPCDirection::LEFT);
                    }
                    else
                    {
                        // Vertical direction
                        npc.SetDirection(npcToPlayer.y > 0 ? NPCDirection::DOWN : NPCDirection::UP);
                    }

                    // Make player face the NPC
                    glm::vec2 playerToNPC = npcPos - playerPos;
                    if (std::abs(playerToNPC.x) > std::abs(playerToNPC.y))
                    {
                        // Horizontal direction
                        m_Player.SetDirection(playerToNPC.x > 0 ? Direction::RIGHT : Direction::LEFT);
                    }
                    else
                    {
                        // Vertical direction
                        m_Player.SetDirection(playerToNPC.y > 0 ? Direction::DOWN : Direction::UP);
                    }

                    // Freeze both in place
                    npc.SetStopped(true);       // Lock NPC in place
                    npc.ResetAnimationToIdle(); // Reset NPC animation to idle frame
                    // Player movement is already disabled by m_InDialogue check in Update()

                    std::cout << "Started dialogue with NPC: " << npc.GetType()
                              << " at tile (" << npcTileX << ", " << npcTileY << ")"
                              << ", player at tile (" << playerTileXFinal << ", " << playerTileYFinal << ")" << std::endl;
                    break;
                }
            }
        }
        fKeyPressed = true;
    }
    if (glfwGetKey(m_Window, GLFW_KEY_F) == GLFW_RELEASE)
    {
        fKeyPressed = false;
    }

    // Handle branching dialogue tree input
    if (m_DialogueManager.IsActive())
    {
        static bool upKeyPressed = false;
        static bool downKeyPressed = false;
        static bool enterKeyTree = false;
        static bool spaceKeyTree = false;
        static bool escapeKeyTree = false;

        // Navigate options with Up/Down or W/S
        if ((glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_PRESS || glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_PRESS) && !upKeyPressed)
        {
            m_DialogueManager.SelectPrevious();
            upKeyPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_UP) == GLFW_RELEASE && glfwGetKey(m_Window, GLFW_KEY_W) == GLFW_RELEASE)
        {
            upKeyPressed = false;
        }

        if ((glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_PRESS || glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_PRESS) && !downKeyPressed)
        {
            m_DialogueManager.SelectNext();
            downKeyPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_DOWN) == GLFW_RELEASE && glfwGetKey(m_Window, GLFW_KEY_S) == GLFW_RELEASE)
        {
            downKeyPressed = false;
        }

        // Confirm selection with Enter or Space
        if (glfwGetKey(m_Window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterKeyTree)
        {
            // Check if we need to advance pages first
            if (!IsDialogueOnLastPage())
            {
                m_DialoguePage++;
            }
            else
            {
                m_DialoguePage = 0; // Reset for next node
                m_DialogueManager.ConfirmSelection();
                // If dialogue ended, release NPC
                if (!m_DialogueManager.IsActive() && m_DialogueNPC)
                {
                    m_DialogueNPC->SetStopped(false);
                    m_DialogueNPC = nullptr;
                }
            }
            enterKeyTree = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_ENTER) == GLFW_RELEASE)
        {
            enterKeyTree = false;
        }

        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS && !spaceKeyTree)
        {
            // Check if we need to advance pages first
            if (!IsDialogueOnLastPage())
            {
                m_DialoguePage++;
            }
            else
            {
                m_DialoguePage = 0; // Reset for next node
                m_DialogueManager.ConfirmSelection();
                if (!m_DialogueManager.IsActive() && m_DialogueNPC)
                {
                    m_DialogueNPC->SetStopped(false);
                    m_DialogueNPC = nullptr;
                }
            }
            spaceKeyTree = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_RELEASE)
        {
            spaceKeyTree = false;
        }

        // Escape to force-close dialogue
        if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapeKeyTree)
        {
            m_DialogueManager.EndDialogue();
            m_DialoguePage = 0; // Reset pagination
            if (m_DialogueNPC)
            {
                m_DialogueNPC->SetStopped(false);
                m_DialogueNPC = nullptr;
            }
            escapeKeyTree = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
        {
            escapeKeyTree = false;
        }
    }

    // Close simple dialogue
    if (m_InDialogue)
    {
        static bool enterKeyPressed = false;
        static bool spaceKeyPressed = false;
        static bool escapeKeyPressed = false;

        if (glfwGetKey(m_Window, GLFW_KEY_ENTER) == GLFW_PRESS && !enterKeyPressed)
        {
            m_InDialogue = false;
            if (m_DialogueNPC)
            {
                m_DialogueNPC->SetStopped(false);
            }
            m_DialogueNPC = nullptr;
            m_DialogueText = "";
            enterKeyPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_ENTER) == GLFW_RELEASE)
        {
            enterKeyPressed = false;
        }

        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_PRESS && !spaceKeyPressed)
        {
            m_InDialogue = false;
            if (m_DialogueNPC)
            {
                m_DialogueNPC->SetStopped(false);
            }
            m_DialogueNPC = nullptr;
            m_DialogueText = "";
            spaceKeyPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_SPACE) == GLFW_RELEASE)
        {
            spaceKeyPressed = false;
        }

        if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS && !escapeKeyPressed)
        {
            m_InDialogue = false;
            if (m_DialogueNPC)
            {
                m_DialogueNPC->SetStopped(false);
            }
            m_DialogueNPC = nullptr;
            m_DialogueText = "";
            escapeKeyPressed = true;
        }
        if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_RELEASE)
        {
            escapeKeyPressed = false;
        }
    }

    // Only process player movement if not in editor mode and not in dialogue
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive())
    {
        // Remember previous position for resolving collisions with NPCs
        m_PlayerPreviousPosition = m_Player.GetPosition();

        // Collect NPC positions for collision checking
        std::vector<glm::vec2> npcPositions;
        for (const auto &npc : m_NPCs)
        {
            npcPositions.push_back(npc.GetPosition());
        }

        m_Player.Move(moveDirection, deltaTime, &m_Tilemap, &npcPositions);
    }
    else if (m_InDialogue)
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

void Game::ScrollCallback(GLFWwindow *window, double /*xoffset*/, double yoffset)
{
    // Retrieve Game instance from window user pointer
    Game *game = static_cast<Game *>(glfwGetWindowUserPointer(window));
    if (!game)
    {
        return;
    }

    // Delegate editor-specific scroll handling
    if (game->m_Editor.IsActive())
    {
        game->m_Editor.HandleScroll(yoffset, game->MakeEditorContext());
        // If tile picker is open, editor handles all scroll
        if (game->m_Editor.ShowTilePicker())
        {
            return;
        }
    }

    // Check for Ctrl modifier
    int ctrlState = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) | glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL);

    // Camera zoom with Ctrl+scroll
    if (ctrlState == GLFW_PRESS)
    {
        // Zoom centered on player position
        float baseWorldWidth = static_cast<float>(game->m_TilesVisibleWidth * game->m_Tilemap.GetTileWidth());
        float baseWorldHeight = static_cast<float>(game->m_TilesVisibleHeight * game->m_Tilemap.GetTileHeight());

        float oldZoom = game->m_CameraZoom;
        float oldWorldWidth = baseWorldWidth / oldZoom;
        float oldWorldHeight = baseWorldHeight / oldZoom;

        // Get the player's visual center
        glm::vec2 playerPos = game->m_Player.GetPosition();
        glm::vec2 playerVisualCenter = playerPos - glm::vec2(0.0f, PlayerCharacter::HITBOX_HEIGHT * 0.5f);

        // Apply zoom with snapping to prevent sub-pixel seams
        float zoomDelta = yoffset > 0 ? 1.1f : 0.9f;
        game->m_CameraZoom *= zoomDelta;

        // Editor mode allows zooming out further (0.1x) to see entire map
        float minZoom = (game->m_Editor.IsActive() && game->m_FreeCameraMode) ? 0.1f : 0.4f;
        game->m_CameraZoom = std::max(minZoom, std::min(4.0f, game->m_CameraZoom));
        // Snap to 0.1 increments
        game->m_CameraZoom = std::round(game->m_CameraZoom * 10.0f) / 10.0f;

        float newZoom = game->m_CameraZoom;
        float newWorldWidth = baseWorldWidth / newZoom;
        float newWorldHeight = baseWorldHeight / newZoom;

        // Adjust camera position to keep player centered
        game->m_CameraPosition = playerVisualCenter - glm::vec2(newWorldWidth * 0.5f, newWorldHeight * 0.5f);

        // Clamp camera to map bounds (skip in editor free-camera mode)
        if (!(game->m_Editor.IsActive() && game->m_FreeCameraMode))
        {
            float mapWidth = static_cast<float>(game->m_Tilemap.GetMapWidth() * game->m_Tilemap.GetTileWidth());
            float mapHeight = static_cast<float>(game->m_Tilemap.GetMapHeight() * game->m_Tilemap.GetTileHeight());
            game->m_CameraPosition.x = std::max(0.0f, std::min(game->m_CameraPosition.x, mapWidth - newWorldWidth));
            game->m_CameraPosition.y = std::max(0.0f, std::min(game->m_CameraPosition.y, mapHeight - newWorldHeight));
        }

        // Also update the follow target so camera doesn't snap back
        game->m_CameraFollowTarget = game->m_CameraPosition;

        std::cout << "Camera zoom: " << game->m_CameraZoom << "x" << std::endl;
    }
}
