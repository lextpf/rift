#include "Game.h"
#include "KeyToggle.h"
#include "Logger.h"

#include <glad/glad.h>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Game";

constexpr float INTERACTION_RANGE = 32.0f;   ///< NPC interaction range in pixels (2 tiles)
constexpr float COLLISION_DISTANCE = 20.0f;  ///< Very close = colliding with NPC
constexpr float DIRECTION_LENIENCY = 8.0f;   ///< Pixels of directional leniency when very close
constexpr float TILE_POSITION_EPS = 0.1f;    ///< Epsilon for tile coordinate calculation
}  // namespace

void Game::ProcessInput(float deltaTime)
{
    // Console toggle is checked unconditionally so `~` can both open and
    // close it. When the console is open it consumes all subsequent input.
    if (m_KeyConsole.JustPressed(m_Window))
    {
        m_Console.Toggle();
    }
    if (m_Console.IsOpen())
    {
        PumpConsoleKeys();
        return;  // Suppress player movement, editor toggles, F-keys, etc.
    }

    // Top-level mode dispatch: Title and Pause have their own input handlers
    // and consume all non-console input.
    if (m_GameMode == GameMode::Title)
    {
        ProcessTitleInput();
        return;
    }
    if (m_GameMode == GameMode::Paused)
    {
        ProcessPauseInput();
        return;
    }

    // Esc enters Pause from Playing, but yields to dialogue's own Esc handler
    // so a single press can't both close the dialogue and pause the game.
    // Always call JustPressed to keep the toggle's edge state advancing.
    {
        bool inAnyDialogue = m_InDialogue || m_DialogueManager.IsActive() || m_DialogueSnap.active;
        bool escJustPressed = m_KeyEscape.JustPressed(m_Window);
        if (!inAnyDialogue && escJustPressed)
        {
            m_GameMode = GameMode::Paused;
            m_PauseMenu.enabled.assign(2, true);
            m_PauseMenu.selected = 0;
            return;
        }
    }

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

    // Delegate all editor-specific key input to Editor
    if (m_Editor.IsActive())
    {
        m_Editor.ProcessInput(deltaTime, MakeEditorContext());
    }

    // --- Remainder of ProcessInput: keys that stay in Game ---
    // The following sections handle: Z, F6, Space, B, X (debug-only corner cut), F,
    // dialogue, player movement.
    // Engine controls (editor toggle, renderer switch, debug overlays, time-of-day,
    // 3D globe, character cycle, NPC appearance copy) are exposed as console commands.

    // Resets camera zoom to 1.0x and recenters on player.
    // In editor mode, also resets tile picker zoom and pan.
    if (m_KeyZ.JustPressed(m_Window))
    {
        // Recenter camera on player in gameplay mode
        if (!m_Editor.IsActive())
        {
            float worldWidth = static_cast<float>(m_TilesVisibleWidth * TILE_PIXEL_SIZE);
            float worldHeight = static_cast<float>(m_TilesVisibleHeight * TILE_PIXEL_SIZE);

            glm::vec2 playerAnchorTileCenter = m_Player.GetCurrentTileCenter();
            glm::vec2 playerVisualCenter =
                glm::vec2(playerAnchorTileCenter.x, playerAnchorTileCenter.y - TILE_PIXEL_SIZE);

            float mapWidth = static_cast<float>(m_Tilemap.GetMapWidth() * m_Tilemap.GetTileWidth());
            float mapHeight =
                static_cast<float>(m_Tilemap.GetMapHeight() * m_Tilemap.GetTileHeight());

            m_Camera.ResetZoom(playerVisualCenter,
                               worldWidth,
                               worldHeight,
                               mapWidth,
                               mapHeight,
                               m_Editor.IsActive() && m_Camera.IsFreeMode());
        }
        else
        {
            m_Camera.GetState().zoom = 1.0f;
            Logger::Info(LOG_SUBSYSTEM, "Camera zoom reset to 1.0x");
        }

        // Reset tile picker state in editor mode
        if (m_Editor.IsActive())
        {
            m_Editor.ResetTilePickerState();
        }
    }

    // Toggle FPS cap (0 = uncapped, 500 = capped)
    if (m_KeyF6.JustPressed(m_Window))
    {
        if (m_Fps.targetFps <= 0.0f)
        {
            m_Fps.targetFps = 500.0f;
            Logger::Info(LOG_SUBSYSTEM, "FPS capped at 500");
        }
        else
        {
            m_Fps.targetFps = 0.0f;
            Logger::Info(LOG_SUBSYSTEM, "FPS uncapped");
        }
    }

    // Toggle free camera mode (Space) - camera stops following player
    // WASD/Arrows can then pan camera while player still moves with WASD
    if (!m_InDialogue && !m_DialogueManager.IsActive() && !m_DialogueSnap.active &&
        !m_Editor.IsActive())
    {
        if (m_KeySpaceFreeCamera.JustPressed(m_Window))
        {
            m_Camera.GetState().freeMode = !m_Camera.GetState().freeMode;
            Logger::InfoF(
                LOG_SUBSYSTEM, "Free Camera Mode: {}", m_Camera.GetState().freeMode ? "ON" : "OFF");
        }
    }

    // Toggles bicycle mode on/off. When bicycling:
    //   - Movement speed is 2.0x base speed
    //   - Uses center-only collision detection
    //   - Different sprite sheet may be used
    if (!m_Editor.IsActive() && m_KeyB.JustPressed(m_Window))
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
        Logger::InfoF(LOG_SUBSYSTEM, "Bicycle: {}", newBicycling ? "ON" : "OFF");
    }

    // In debug mode, X key toggles corner cutting on the collision tile under cursor
    // The corner nearest to the mouse cursor within the tile is toggled
    if (m_Editor.IsDebugMode() && m_KeyX.JustPressed(m_Window))
    {
        double mouseX, mouseY;
        glfwGetCursorPos(m_Window, &mouseX, &mouseY);

        // Calculate world coordinates from mouse position
        float baseWorldWidth = static_cast<float>(m_TilesVisibleWidth * m_Tilemap.GetTileWidth());
        float baseWorldHeight =
            static_cast<float>(m_TilesVisibleHeight * m_Tilemap.GetTileHeight());
        float worldWidth = baseWorldWidth / m_Camera.GetState().zoom;
        float worldHeight = baseWorldHeight / m_Camera.GetState().zoom;

        float worldX =
            (static_cast<float>(mouseX) / static_cast<float>(m_ScreenWidth)) * worldWidth +
            m_Camera.GetState().position.x;
        float worldY =
            (static_cast<float>(mouseY) / static_cast<float>(m_ScreenHeight)) * worldHeight +
            m_Camera.GetState().position.y;

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
                Logger::InfoF(LOG_SUBSYSTEM,
                              "Corner cutting {} at ({}, {}): {}",
                              cornerName,
                              tileX,
                              tileY,
                              !currentlyBlocked ? "BLOCKED" : "ALLOWED");
            }
            else
            {
                Logger::InfoF(LOG_SUBSYSTEM,
                              "Tile ({}, {}) has no collision - corner cutting N/A",
                              tileX,
                              tileY);
            }
        }
    }

    // Initiates dialogue with an NPC when
    //   1. Player is within INTERACTION_RANGE and
    //   2. NPC is in front of player or
    //   3. NPC hitbox is overlapping player hitbox
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueSnap.active && m_KeyF.JustPressed(m_Window))
    {
        glm::vec2 playerPos = m_Player.GetPosition();
        Direction playerDir = m_Player.GetDirection();

        // Calculate player's tile position
        int playerTileX = static_cast<int>(std::floor(playerPos.x / TILE_PIXEL_SIZE));
        int playerTileY =
            static_cast<int>(std::floor((playerPos.y - TILE_POSITION_EPS) / TILE_PIXEL_SIZE));

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
                int npcTileY =
                    static_cast<int>(std::floor((npcPos.y - TILE_POSITION_EPS) / TILE_PIXEL_SIZE));

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
                            isRoughlyInFront = (toNPC.y > -DIRECTION_LENIENCY);
                            break;
                        case Direction::UP:
                            isRoughlyInFront = (toNPC.y < DIRECTION_LENIENCY);
                            break;
                        case Direction::LEFT:
                            isRoughlyInFront = (toNPC.x < DIRECTION_LENIENCY);
                            break;
                        case Direction::RIGHT:
                            isRoughlyInFront = (toNPC.x > -DIRECTION_LENIENCY);
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
                    playerTileY = static_cast<int>(
                        std::floor((playerPos.y - TILE_POSITION_EPS) / TILE_PIXEL_SIZE));

                    // Use the new NPC tile coordinates for direction calculation
                    // This ensures we look for a spot relative to where the NPC ended up
                    npcTileY = snapTileY;

                    // Calculate direction from NPC to player
                    int dx = playerTileX - npcTileX;
                    int dy = playerTileY - npcTileY;

                    // If diagonal, snap to nearest cardinal direction
                    // Prefer the direction with larger absolute value
                    // TODO: Extract this dialogue-snap direction calculation
                    // (cardinal-aligned + diagonal->cardinal fallback) into a
                    // FindDialogueSnapTile helper alongside the existing
                    // DialogueSnapState code so the snap rules live next to
                    // the state they drive.
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

                    // Find a valid tile for the player to stand on during dialogue
                    glm::ivec2 snapTile = FindDialogueSnapTile(npcTileX,
                                                               npcTileY,
                                                               currentPlayerTileX,
                                                               currentPlayerTileY,
                                                               finalDx,
                                                               finalDy);
                    int playerTileXFinal = snapTile.x;
                    int playerTileYFinal = snapTile.y;

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
                    assert(!m_Editor.IsActive() && "Dialogue cannot start while editor is active");
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

                    Logger::InfoF(LOG_SUBSYSTEM,
                                  "Starting dialogue snap with NPC: {} target NPC tile ({}, "
                                  "{}), target player tile ({}, {})",
                                  npc.GetType(),
                                  m_DialogueSnap.npcTileX,
                                  m_DialogueSnap.npcTileY,
                                  m_DialogueSnap.playerTileX,
                                  m_DialogueSnap.playerTileY);
                    break;
                }
            }
        }
    }

    ProcessDialogueInput();
    ProcessPlayerMovement(moveDirection, deltaTime);

    // Process mouse input for editor
    if (m_Editor.IsActive())
    {
        m_Editor.ProcessMouseInput(MakeEditorContext());
    }
}

void Game::ProcessDialogueInput()
{
    // Handle branching dialogue tree input
    if (m_DialogueManager.IsActive())
    {
        // Navigate options with Up/Down or W/S
        if (m_KeyDialogueUp.JustPressed(m_Window))
            m_DialogueManager.SelectPrevious();

        if (m_KeyDialogueDown.JustPressed(m_Window))
            m_DialogueManager.SelectNext();

        // Confirm selection with Enter or Space
        if (m_KeyDialogueEnterTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        if (m_KeyDialogueSpaceTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        // Escape to force-close dialogue
        if (m_KeyDialogueEscapeTree.JustPressed(m_Window))
            ForceCloseTreeDialogue();
    }

    // Close simple dialogue
    if (m_InDialogue)
    {
        if (m_KeyDialogueEnter.JustPressed(m_Window))
            CloseSimpleDialogue();

        if (m_KeyDialogueSpace.JustPressed(m_Window))
            CloseSimpleDialogue();

        if (m_KeyDialogueEscape.JustPressed(m_Window))
            CloseSimpleDialogue();
    }
}

void Game::ProcessPlayerMovement(glm::vec2 moveDirection, float deltaTime)
{
    // Only process player movement if not in editor mode, not in dialogue,
    // and the developer console is closed.
    if (!m_Editor.IsActive() && !m_InDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueSnap.active && !m_Console.IsOpen())
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

        // No-clip (developer console): bypass tile + NPC collision by passing
        // null pointers; PlayerCharacter::Move integrates raw input when the
        // tilemap is null. CollisionResolver::HandleIdleSnap also handles
        // null gracefully (PlayerCharacter.cpp:563, CollisionResolver.cpp:1270).
        const Tilemap* tilemap = m_Player.IsNoClip() ? nullptr : &m_Tilemap;
        const std::vector<glm::vec2>* npcPositions =
            m_Player.IsNoClip() ? nullptr : &m_NpcPositions;
        m_Player.Move(moveDirection, deltaTime, tilemap, npcPositions);
    }
    else if (m_InDialogue || m_DialogueSnap.active)
    {
        // Stop player movement during dialogue
        m_Player.Stop();
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

    // Console takes scroll exclusively while open (scrollback navigation).
    if (game->m_Console.IsOpen())
    {
        game->m_Console.OnScroll(yoffset);
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
        float baseWorldWidth =
            static_cast<float>(game->m_TilesVisibleWidth * game->m_Tilemap.GetTileWidth());
        float baseWorldHeight =
            static_cast<float>(game->m_TilesVisibleHeight * game->m_Tilemap.GetTileHeight());

        glm::vec2 playerPos = game->m_Player.GetPosition();
        glm::vec2 playerVisualCenter =
            playerPos - glm::vec2(0.0f, PlayerCharacter::HITBOX_HEIGHT * 0.5f);

        float mapWidth =
            static_cast<float>(game->m_Tilemap.GetMapWidth() * game->m_Tilemap.GetTileWidth());
        float mapHeight =
            static_cast<float>(game->m_Tilemap.GetMapHeight() * game->m_Tilemap.GetTileHeight());
        bool editorFreeMode = game->m_Editor.IsActive() && game->m_Camera.IsFreeMode();

        game->m_Camera.HandleZoomScroll(yoffset,
                                        playerVisualCenter,
                                        baseWorldWidth,
                                        baseWorldHeight,
                                        mapWidth,
                                        mapHeight,
                                        editorFreeMode,
                                        editorFreeMode);
    }
}

void Game::CharCallback(GLFWwindow* window, unsigned int codepoint)
{
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game)
    {
        return;
    }
    // Filtering happens by codepoint inside Console::OnChar. Don't gate on
    // glfwGetKey(GRAVE_ACCENT) here: on dead-key layouts (German ^, French
    // U+00B2, Polish ^) the next key's CHAR event composes with the dead key
    // and arrives while the toggle key is still physically held, which
    // would otherwise eat the user's first real keystroke after toggling.
    game->m_Console.OnChar(codepoint);
}

void Game::PumpConsoleKeys()
{
    // Local debounced toggles for the keys the console consumes. Held inside
    // this method (function-local statics) so they only allocate when the
    // console is actually open.
    static KeyToggle<GLFW_KEY_ENTER> kEnter;
    static KeyToggle<GLFW_KEY_BACKSPACE> kBackspace;
    static KeyToggle<GLFW_KEY_DELETE> kDelete;
    static KeyToggle<GLFW_KEY_TAB> kTab;
    static KeyToggle<GLFW_KEY_UP> kUp;
    static KeyToggle<GLFW_KEY_DOWN> kDown;
    static KeyToggle<GLFW_KEY_LEFT> kLeft;
    static KeyToggle<GLFW_KEY_RIGHT> kRight;
    static KeyToggle<GLFW_KEY_HOME> kHome;
    static KeyToggle<GLFW_KEY_END> kEnd;
    static KeyToggle<GLFW_KEY_ESCAPE> kEscape;

    if (kEnter.JustPressed(m_Window))
        m_Console.OnEnter();
    if (kBackspace.JustPressed(m_Window))
    {
        const bool ctrl = glfwGetKey(m_Window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                          glfwGetKey(m_Window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        if (ctrl)
            m_Console.OnClearLine();
        else
            m_Console.OnBackspace();
    }
    if (kDelete.JustPressed(m_Window))
        m_Console.OnDelete();
    if (kTab.JustPressed(m_Window))
        m_Console.OnTab();
    if (kUp.JustPressed(m_Window))
        m_Console.OnUp();
    if (kDown.JustPressed(m_Window))
        m_Console.OnDown();
    if (kLeft.JustPressed(m_Window))
        m_Console.OnLeft();
    if (kRight.JustPressed(m_Window))
        m_Console.OnRight();
    if (kHome.JustPressed(m_Window))
        m_Console.OnHome();
    if (kEnd.JustPressed(m_Window))
        m_Console.OnEnd();
    if (kEscape.JustPressed(m_Window))
        m_Console.OnEscape();
}

void Game::ReleaseDialogueNPC()
{
    if (m_DialogueNPCIndex >= 0 && m_DialogueNPCIndex < static_cast<int>(m_NPCs.size()))
    {
        GetDialogueNPC().SetStopped(false);
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

glm::ivec2 Game::FindDialogueSnapTile(int npcTileX,
                                      int npcTileY,
                                      int playerTileX,
                                      int playerTileY,
                                      int preferredDx,
                                      int preferredDy) const
{
    // Validate a candidate tile: must be in bounds, not the NPC's tile, not blocked.
    auto isValidSnapTile = [&](int tx, int ty)
    {
        if (tx < 0 || ty < 0 || tx >= m_Tilemap.GetMapWidth() || ty >= m_Tilemap.GetMapHeight())
        {
            return false;
        }
        if (tx == npcTileX && ty == npcTileY)
        {
            return false;
        }
        return !m_Tilemap.GetTileCollision(tx, ty);
    };

    // Check if player is already on a valid cardinal-adjacent tile
    if (playerTileX != npcTileX || playerTileY != npcTileY)
    {
        if (isValidSnapTile(playerTileX, playerTileY))
        {
            int tileDistX = std::abs(playerTileX - npcTileX);
            int tileDistY = std::abs(playerTileY - npcTileY);
            bool isCardinalAdjacent =
                (tileDistX == 1 && tileDistY == 0) || (tileDistX == 0 && tileDistY == 1);
            if (isCardinalAdjacent)
            {
                return glm::ivec2(playerTileX, playerTileY);
            }
        }
    }

    // Ensure we have a non-zero preferred direction
    if (preferredDx == 0 && preferredDy == 0)
    {
        preferredDx = 0;
        preferredDy = 1;
    }

    // Try preferred direction first, then all four cardinals
    struct CardinalDir
    {
        int dx, dy;
    };
    CardinalDir cardinals[] = {
        {preferredDx, preferredDy},
        {0, 1},
        {0, -1},
        {1, 0},
        {-1, 0},
    };

    for (const auto& dir : cardinals)
    {
        int testX = npcTileX + dir.dx;
        int testY = npcTileY + dir.dy;
        if (testX == npcTileX && testY == npcTileY)
        {
            continue;
        }
        if (isValidSnapTile(testX, testY))
        {
            return glm::ivec2(testX, testY);
        }
    }

    // No cardinal direction worked. Fall back to current player tile if valid.
    if (isValidSnapTile(playerTileX, playerTileY))
    {
        return glm::ivec2(playerTileX, playerTileY);
    }

    // No safe tile found at all.
    return glm::ivec2(-1, -1);
}
