#include "AnimationState.hpp"
#include "Appearance.hpp"
#include "CharacterConstants.hpp"
#include "CharacterKinematics.hpp"
#include "CollisionGeometry.hpp"
#include "Dialogue.hpp"
#include "DialogueStore.hpp"
#include "Elevation.hpp"
#include "Facing.hpp"
#include "Game.hpp"
#include "Identity.hpp"
#include "KeyToggle.hpp"
#include "Logger.hpp"
#include "NpcTag.hpp"
#include "PlayerModes.hpp"
#include "PlayerMovementSystem.hpp"
#include "PlayerSystem.hpp"
#include "TileMath.hpp"
#include "Transform.hpp"
#include "WorldServices.hpp"

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
}  // namespace

void Game::ProcessInput(float deltaTime)
{
    // Console toggle is checked unconditionally so F12 both opens and closes
    // it. When open, the console consumes all subsequent input.
    if (m_KeyConsole.JustPressed(m_Window))
    {
        m_Console.Toggle();
    }
    if (m_Console.IsOpen())
    {
        PumpConsoleKeys();
        return;  // Suppress player movement, editor toggles, F-keys, etc.
    }

    // Title and Pause have their own input handlers and consume all non-console input.
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
        bool inAnyDialogue =
            m_DialogueUi.inDialogue || m_DialogueManager.IsActive() || m_DialogueUi.snap.active;
        bool escJustPressed = m_KeyEscape.JustPressed(m_Window);
        if (!inAnyDialogue && escJustPressed)
        {
            m_GameMode = GameMode::Paused;
            m_PauseMenu.enabled.assign(2, true);
            m_PauseMenu.selected = 0;
            // Reset menu-mouse state so the first pause frame ignores a stale
            // cursor parked over a menu item, and a held click doesn't fire confirm.
            m_MenuLastMouseX = -1.0;
            m_MenuLastMouseY = -1.0;
            m_MenuMouseLeftPrev = true;
            return;
        }
    }

    glm::vec2 moveDirection(0.0f);

    // Shift runs (1.75x base speed; see CharacterConstants::RUN_SPEED_MULTIPLIER).
    bool isRunning = (glfwGetKey(m_Window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                      glfwGetKey(m_Window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

    // Drop any copied NPC appearance when starting to run.
    if (isRunning && m_World.get<Appearance>(m_PlayerEntity).usingCopiedAppearance)
    {
        PlayerSystem::RestoreOriginalAppearance(m_World, m_PlayerEntity);
        PlayerSystem::UploadTextures(m_World, m_PlayerEntity, *m_Renderer);
    }

    m_World.get<PlayerModes>(m_PlayerEntity).isRunning = isRunning;

    // WASD 8-directional. Y increases downward (top-left origin), so W = -Y, S = +Y.
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

    if (m_Editor.IsActive())
    {
        m_Editor.ProcessInput(deltaTime, MakeEditorContext());
    }

    // Z resets camera zoom to 1.0x and recenters on player; in editor mode
    // also resets tile picker zoom/pan.
    if (m_KeyZ.JustPressed(m_Window))
    {
        if (!m_Editor.IsActive())
        {
            float worldWidth = static_cast<float>(m_TilesVisibleWidth * TILE_PIXEL_SIZE);
            float worldHeight = static_cast<float>(m_TilesVisibleHeight * TILE_PIXEL_SIZE);

            glm::vec2 playerAnchorTileCenter = PlayerMovementSystem::CurrentTileCenter(
                m_World.get<Transform>(m_PlayerEntity).position, 16.0f);
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

        if (m_Editor.IsActive())
        {
            m_Editor.ResetTilePickerState();
        }
    }

    // Space toggles free camera (camera stops following player; WASD/Arrows
    // pan the camera while player still moves with WASD).
    if (!m_DialogueUi.inDialogue && !m_DialogueManager.IsActive() && !m_DialogueUi.snap.active &&
        !m_Editor.IsActive())
    {
        if (m_KeySpaceFreeCamera.JustPressed(m_Window))
        {
            m_Camera.GetState().freeMode = !m_Camera.GetState().freeMode;
            Logger::InfoF(
                LOG_SUBSYSTEM, "Free Camera Mode: {}", m_Camera.GetState().freeMode ? "ON" : "OFF");
        }
    }

    // B toggles bicycle mode (2.25x base speed; see CharacterConstants::BICYCLE_SPEED_MULTIPLIER),
    // center-only collision, may use a different sprite sheet.
    if (!m_Editor.IsActive() && m_KeyB.JustPressed(m_Window))
    {
        bool currentBicycling = m_World.get<PlayerModes>(m_PlayerEntity).isBicycling;
        bool newBicycling = !currentBicycling;

        // Drop any copied NPC appearance when starting to bicycle.
        if (newBicycling && m_World.get<Appearance>(m_PlayerEntity).usingCopiedAppearance)
        {
            PlayerSystem::RestoreOriginalAppearance(m_World, m_PlayerEntity);
            PlayerSystem::UploadTextures(m_World, m_PlayerEntity, *m_Renderer);
        }

        m_World.get<PlayerModes>(m_PlayerEntity).isBicycling = newBicycling;
        Logger::InfoF(LOG_SUBSYSTEM, "Bicycle: {}", newBicycling ? "ON" : "OFF");
    }

    // Debug mode: X toggles corner cutting on the collision tile under the
    // cursor (the corner nearest the cursor within the tile).
    if (m_Editor.IsDebugMode() && m_KeyX.JustPressed(m_Window))
    {
        double mouseX, mouseY;
        glfwGetCursorPos(m_Window, &mouseX, &mouseY);

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
                // Pick the corner nearest the cursor inside this tile.
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

    // F starts dialogue with an NPC when:
    //   1. Player is within INTERACTION_RANGE, AND
    //   2. NPC is in front of player, OR
    //   3. NPC hitbox is overlapping player hitbox.
    if (!m_Editor.IsActive() && !m_DialogueUi.inDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueUi.snap.active && m_KeyF.JustPressed(m_Window))
    {
        glm::vec2 playerPos = m_World.get<Transform>(m_PlayerEntity).position;
        Direction playerDir = m_World.get<Facing>(m_PlayerEntity).dir;

        int playerTileX = TileMath::TileIndex(playerPos.x, static_cast<float>(TILE_PIXEL_SIZE));
        int playerTileY =
            TileMath::StandingTileRow(playerPos.y, static_cast<float>(TILE_PIXEL_SIZE));

        // Tile directly in front of the player.
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

        // First NPC in range + roughly in front triggers the dialogue snap. each<>
        // can't break, so a flag short-circuits the rest once one is chosen
        // (iteration order matches the old EntityStore::Entities pool order).
        bool dialogueStarted = false;
        m_World.each<const Transform, const NpcTag>(
            [&](ecs::entity npcE, const Transform& npcTransform)
            {
                if (dialogueStarted)
                    return;
                glm::vec2 npcPos = npcTransform.position;
                float distance = glm::length(npcPos - playerPos);

                if (distance <= INTERACTION_RANGE)
                {
                    int npcTileX =
                        TileMath::TileIndex(npcPos.x, static_cast<float>(TILE_PIXEL_SIZE));
                    int npcTileY =
                        TileMath::StandingTileRow(npcPos.y, static_cast<float>(TILE_PIXEL_SIZE));

                    bool isColliding =
                        CollisionGeometry::FeetBoxesOverlap(playerPos,
                                                            npcPos,
                                                            CharacterConstants::HALF_HITBOX_WIDTH,
                                                            CharacterConstants::HITBOX_HEIGHT,
                                                            CharacterConstants::COLLISION_EPS);

                    bool isOnFrontTile = (npcTileX == frontTileX && npcTileY == frontTileY);

                    // Cardinal-adjacent fallback (NPC one tile away, axis-aligned).
                    int tileDistX = std::abs(playerTileX - npcTileX);
                    int tileDistY = std::abs(playerTileY - npcTileY);
                    bool isCardinalAdjacent =
                        (tileDistX == 1 && tileDistY == 0) || (tileDistX == 0 && tileDistY == 1);
                    bool isSameTile = (tileDistX == 0 && tileDistY == 0);

                    // Cardinal-adjacent only counts if the NPC is in player's facing direction.
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

                    // Very-close, roughly-in-front fallback (more lenient direction check).
                    bool isVeryClose = (distance <= COLLISION_DISTANCE);
                    glm::vec2 toNPC = npcPos - playerPos;
                    bool isRoughlyInFront = false;
                    if (isVeryClose)
                    {
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

                    // Start dialogue if: colliding, on front tile, cardinal-adjacent in
                    // facing direction, or very close + roughly in front.
                    if (isColliding || isOnFrontTile || isInCorrectDirection ||
                        (isVeryClose && isRoughlyInFront))
                    {
                        // Delay dialogue activation until the alignment snap completes.
                        const Dialogue& npcDialogue = m_World.get<Dialogue>(npcE);
                        const WorldServices* npcSvc = m_World.globals().find<WorldServices>();
                        m_DialogueUi.npcId = m_World.get<Identity>(npcE).instanceId;
                        m_DialogueUi.page = 0;
                        m_DialogueUi.snap.prefersTree = npcSvc != nullptr &&
                                                        npcSvc->dialogue != nullptr &&
                                                        npcSvc->dialogue->HasTree(npcDialogue.tree);
                        m_DialogueUi.snap.fallbackText = npcDialogue.text;

                        playerPos = m_World.get<Transform>(m_PlayerEntity).position;
                        npcPos = m_World.get<Transform>(npcE).position;

                        // Snap NPC to center of its tile. X anchor is centered so
                        // floor(x/TILE) already gives the right tile; Y anchor is at
                        // the bottom, so subtract TILE to find the standing tile.
                        int snapTileY = static_cast<int>(
                            std::round((npcPos.y - TILE_PIXEL_SIZE) / TILE_PIXEL_SIZE));

                        m_DialogueUi.snap.npcTileX = npcTileX;
                        m_DialogueUi.snap.npcTileY = snapTileY;
                        glm::vec2 npcTargetPos(
                            static_cast<float>(m_DialogueUi.snap.npcTileX * TILE_PIXEL_SIZE +
                                               TILE_PIXEL_SIZE / 2),
                            static_cast<float>(m_DialogueUi.snap.npcTileY * TILE_PIXEL_SIZE +
                                               TILE_PIXEL_SIZE));

                        // Recompute player tile from fresh position.
                        playerTileX =
                            TileMath::TileIndex(playerPos.x, static_cast<float>(TILE_PIXEL_SIZE));
                        playerTileY = TileMath::StandingTileRow(
                            playerPos.y, static_cast<float>(TILE_PIXEL_SIZE));

                        // Use the new NPC tile coordinates so we look for a snap spot
                        // relative to where the NPC ended up.
                        npcTileY = snapTileY;

                        // Direction NPC -> player.
                        int dx = playerTileX - npcTileX;
                        int dy = playerTileY - npcTileY;

                        // Diagonal snaps to the dominant cardinal axis.
                        // TODO: Extract this (cardinal-aligned + diagonal->cardinal
                        // fallback) into a FindDialogueSnapTile helper next to the
                        // DialogueSnapState code so the snap rules live with their state.
                        int finalDx = 0;
                        int finalDy = 0;
                        if (dx != 0 && dy != 0)
                        {
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
                            finalDx = (dx > 0) ? 1 : -1;
                            finalDy = 0;
                        }
                        else if (dy != 0)
                        {
                            finalDx = 0;
                            finalDy = (dy > 0) ? 1 : -1;
                        }
                        else
                        {
                            // Same tile: default to down.
                            finalDx = 0;
                            finalDy = 1;
                        }

                        // Round, not floor, so the player doesn't snap when slightly off-center.
                        int currentPlayerTileX = static_cast<int>(
                            std::round((playerPos.x - TILE_PIXEL_SIZE / 2) / TILE_PIXEL_SIZE));
                        int currentPlayerTileY = static_cast<int>(
                            std::round((playerPos.y - TILE_PIXEL_SIZE) / TILE_PIXEL_SIZE));

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

                        // NPC faces player; player faces NPC.
                        glm::vec2 npcToPlayer = playerTargetPos - npcTargetPos;
                        Direction npcFacing = CardinalFromDelta(npcToPlayer.x, npcToPlayer.y);

                        glm::vec2 playerToNPC = npcTargetPos - playerTargetPos;
                        Direction playerFacing = CardinalFromDelta(playerToNPC.x, playerToNPC.y);

                        // Freeze both and begin smooth alignment.
                        assert(!m_Editor.IsActive() &&
                               "Dialogue cannot start while editor is active");
                        PlayerSystem::Stop(m_World, m_PlayerEntity);
                        m_World.get<NpcIdle>(npcE).isStopped = true;
                        CharacterKinematics::ResetAnimation(m_World.get<AnimationState>(npcE));

                        m_DialogueUi.snap.active = true;
                        m_DialogueUi.snap.timer = 0.0f;
                        m_DialogueUi.snap.duration = 0.42f;
                        m_DialogueUi.snap.playerStart = playerPos;
                        m_DialogueUi.snap.playerTarget = playerTargetPos;
                        m_DialogueUi.snap.npcStart = npcPos;
                        m_DialogueUi.snap.npcTarget = npcTargetPos;
                        m_DialogueUi.snap.playerTileX =
                            hasPlayerTileTarget
                                ? playerTileXFinal
                                : static_cast<int>(std::round(
                                      (playerTargetPos.x - TILE_PIXEL_SIZE / 2) / TILE_PIXEL_SIZE));
                        m_DialogueUi.snap.playerTileY =
                            hasPlayerTileTarget
                                ? playerTileYFinal
                                : static_cast<int>(std::round(
                                      (playerTargetPos.y - TILE_PIXEL_SIZE) / TILE_PIXEL_SIZE));
                        m_DialogueUi.snap.hasPlayerTile = hasPlayerTileTarget;
                        m_DialogueUi.snap.playerFacing = playerFacing;
                        m_DialogueUi.snap.npcFacing = npcFacing;

                        Logger::InfoF(LOG_SUBSYSTEM,
                                      "Starting dialogue snap with NPC: {} target NPC tile ({}, "
                                      "{}), target player tile ({}, {})",
                                      m_World.get<Dialogue>(npcE).type,
                                      m_DialogueUi.snap.npcTileX,
                                      m_DialogueUi.snap.npcTileY,
                                      m_DialogueUi.snap.playerTileX,
                                      m_DialogueUi.snap.playerTileY);
                        dialogueStarted = true;
                        return;
                    }
                }
            });
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
    if (m_DialogueManager.IsActive())
    {
        // Up/Down or W/S navigate options; Enter/Space confirm; Esc force-closes.
        if (m_KeyDialogueUp.JustPressed(m_Window))
            m_DialogueManager.SelectPrevious();

        if (m_KeyDialogueDown.JustPressed(m_Window))
            m_DialogueManager.SelectNext();

        if (m_KeyDialogueEnterTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        if (m_KeyDialogueSpaceTree.JustPressed(m_Window))
            ConfirmOrAdvanceTreeDialogue();

        if (m_KeyDialogueEscapeTree.JustPressed(m_Window))
            ForceCloseTreeDialogue();
    }

    if (m_DialogueUi.inDialogue)
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
    // Gate on: not in editor, not in dialogue, console closed.
    if (!m_Editor.IsActive() && !m_DialogueUi.inDialogue && !m_DialogueManager.IsActive() &&
        !m_DialogueUi.snap.active && !m_Console.IsOpen())
    {
        const glm::vec2 beforeMove = m_World.get<Transform>(m_PlayerEntity).position;

        // Reuses the pre-allocated member (no per-frame alloc). The ECS analog
        // is world.each<const Transform, const NpcTag>.
        BuildNpcFeet(m_World, m_NpcPositions);

        // No-clip bypasses tile + NPC collision via null pointers; Move() and
        // CollisionSystem::HandleStuckRecovery both handle null safely.
        const Tilemap* tilemap =
            m_World.get<PlayerModes>(m_PlayerEntity).noClip ? nullptr : &m_Tilemap;
        const std::vector<glm::vec2>* npcPositions =
            m_World.get<PlayerModes>(m_PlayerEntity).noClip ? nullptr : &m_NpcPositions;
        PlayerSystem::Move(
            m_World, m_PlayerEntity, moveDirection, deltaTime, tilemap, npcPositions);

        // Derive the player's logical plane from this frame's move, mirroring the NPC
        // path in NpcAiSystem::UpdateAll. Uses m_Tilemap directly (not the null no-clip
        // collision tilemap) so elevation still tracks while no-clipping.
        CharacterKinematics::DerivePlane(m_World.get<Elevation>(m_PlayerEntity),
                                         beforeMove,
                                         m_World.get<Transform>(m_PlayerEntity).position,
                                         m_Tilemap);
    }
    else if (m_DialogueUi.inDialogue || m_DialogueUi.snap.active)
    {
        PlayerSystem::Stop(m_World, m_PlayerEntity);
    }
}

void Game::ScrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
    Game* game = static_cast<Game*>(glfwGetWindowUserPointer(window));
    if (!game)
    {
        return;
    }

    // Console takes scroll exclusively while open (scrollback navigation).
    if (game->m_Console.IsOpen())
    {
        // Suggestion dropdown gets first crack: if the cursor is over it, the
        // wheel scrolls suggestions instead of scrollback.
        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        if (!game->m_Console.TryScrollDropdown(mx, my, yoffset))
        {
            game->m_Console.OnScroll(yoffset);
        }
        return;
    }

    if (game->m_Editor.IsActive())
    {
        game->m_Editor.HandleScroll(yoffset, game->MakeEditorContext());
        // Tile picker swallows all scroll when open.
        if (game->m_Editor.IsShowTilePicker())
        {
            return;
        }
    }

    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

    // Ctrl+scroll zooms the camera.
    if (ctrlPressed)
    {
        float baseWorldWidth =
            static_cast<float>(game->m_TilesVisibleWidth * game->m_Tilemap.GetTileWidth());
        float baseWorldHeight =
            static_cast<float>(game->m_TilesVisibleHeight * game->m_Tilemap.GetTileHeight());

        glm::vec2 playerPos = game->m_World.get<Transform>(game->m_PlayerEntity).position;
        glm::vec2 playerVisualCenter =
            playerPos - glm::vec2(0.0f, CharacterConstants::HITBOX_HEIGHT * 0.5f);

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
    game->m_Console.OnChar(codepoint);
}

void Game::PumpConsoleKeys()
{
    // Function-local statics for the keys the console consumes - only allocate
    // once the console is open.
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
            m_Console.OnBackspaceWord();
        else
            m_Console.OnBackspace();
    }
    if (kDelete.JustPressed(m_Window))
        m_Console.OnDelete();
    if (kTab.JustPressed(m_Window))
    {
        // Empty input: Tab toggles Half <-> Full so the user can resize the
        // console for longer sessions. Non-empty input: Tab is autocomplete
        // (cycles through suggestions, same as before).
        if (m_Console.Buffer().Input().empty())
            m_Console.ToggleFullscreen();
        else
            m_Console.OnTab();
    }
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

    // Suggestion dropdown: hover keeps the highlight under the cursor; click
    // splices the chosen suggestion into the input (same path as Tab).
    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_Window, &mouseX, &mouseY);
    m_Console.OnMouseHover(mouseX, mouseY);
    const bool mouseDown = (glfwGetMouseButton(m_Window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
    if (mouseDown && !m_ConsoleMouseLeftPrev)
    {
        m_Console.OnMouseClick(mouseX, mouseY);
    }
    m_ConsoleMouseLeftPrev = mouseDown;
}

void Game::ReleaseDialogueNPC()
{
    if (const ecs::entity dialogueNpc = FindNPCById(m_DialogueUi.npcId))
    {
        m_World.get<NpcIdle>(dialogueNpc).isStopped = false;
    }
    m_DialogueUi.npcId = 0;
}

void Game::CloseSimpleDialogue()
{
    m_DialogueUi.inDialogue = false;
    ReleaseDialogueNPC();
    m_DialogueUi.text.clear();
}

void Game::ConfirmOrAdvanceTreeDialogue()
{
    // If the typewriter is still revealing, skip to full reveal first.
    if (m_DialogueUi.charReveal >= 0.0f)
    {
        m_DialogueUi.charReveal = -1.0f;
        return;
    }

    if (!IsDialogueOnLastPage())
    {
        m_DialogueUi.page++;
        m_DialogueUi.charReveal = 0.0f;
    }
    else
    {
        m_DialogueUi.page = 0;
        m_DialogueUi.charReveal = 0.0f;
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
    m_DialogueUi.page = 0;
    ReleaseDialogueNPC();
}

glm::ivec2 Game::FindDialogueSnapTile(int npcTileX,
                                      int npcTileY,
                                      int playerTileX,
                                      int playerTileY,
                                      int preferredDx,
                                      int preferredDy) const
{
    // Valid: in bounds, not the NPC's tile, not blocked.
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

    // If the player is already on a valid cardinal-adjacent tile, stay put.
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

    // Ensure non-zero preferred direction (default: down).
    if (preferredDx == 0 && preferredDy == 0)
    {
        preferredDx = 0;
        preferredDy = 1;
    }

    // Try preferred direction first, then all four cardinals.
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

    // No cardinal worked: fall back to the current player tile if it's valid.
    if (isValidSnapTile(playerTileX, playerTileY))
    {
        return glm::ivec2(playerTileX, playerTileY);
    }

    // No safe tile found.
    return glm::ivec2(-1, -1);
}
