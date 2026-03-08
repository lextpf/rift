#include "NonPlayerCharacter.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <random>

namespace
{
std::mt19937& GetNpcRng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

// Width of each NPC sprite frame in pixels.
constexpr int NPC_SPRITE_WIDTH = 32;

// Height of each NPC sprite frame in pixels.
constexpr int NPC_SPRITE_HEIGHT = 32;

// Number of walking animation frames per direction.
constexpr int NPC_WALK_FRAMES = 3;

// Time between animation frame changes (seconds).
constexpr float NPC_ANIM_SPEED = 0.15f;

// NPC hitbox half-width for collision detection.
constexpr float NPC_HALF_WIDTH = 8.0f;

// NPC hitbox height for collision detection.
constexpr float NPC_HITBOX_HEIGHT = 16.0f;

// Distance threshold for reaching a waypoint (pixels).
constexpr float WAYPOINT_REACH_THRESHOLD = 0.5f;

// Minimum movement distance to avoid division by zero.
constexpr float MIN_MOVEMENT_DIST = 0.001f;
}  // namespace

NonPlayerCharacter::NonPlayerCharacter()
    : GameCharacter(),
      m_TileX(0),
      m_TileY(0),
      m_TargetTileX(0),
      m_TargetTileY(0),
      m_WaitTimer(0.0f),
      m_IsStopped(false),
      m_StandingStill(false),
      m_LookAroundTimer(0.0f),
      m_RandomStandStillCheckTimer(0.0f),
      m_RandomStandStillTimer(0.0f),
      m_Dialogue("Hello! How are you today?")
{
    m_Speed = 25.0f;
}

bool NonPlayerCharacter::Load(const std::string& relativePath)
{
    // Extract NPC type from filename
    size_t lastSlash = relativePath.find_last_of('/');
    std::string filename =
        (lastSlash != std::string::npos) ? relativePath.substr(lastSlash + 1) : relativePath;

    // Remove .png extension if present
    if (filename.size() > 4 && filename.substr(filename.size() - 4) == ".png")
    {
        m_Type = filename.substr(0, filename.size() - 4);
    }
    else
    {
        m_Type = filename;
    }

    // Try loading from given path
    std::string path = relativePath;
    if (!m_SpriteSheet.LoadFromFile(path))
    {
        // Fallback: try parent directory
        std::string altPath = "../" + path;
        if (!m_SpriteSheet.LoadFromFile(altPath))
        {
            std::cerr << "Failed to load NPC sprite sheet: " << path << " or " << altPath
                      << std::endl;
            return false;
        }
    }
    return true;
}

void NonPlayerCharacter::UploadTextures(IRenderer& renderer)
{
    renderer.UploadTexture(m_SpriteSheet);
}

void NonPlayerCharacter::SetTilePosition(int tileX, int tileY, int tileSize, bool preserveRoute)
{
    m_TileX = tileX;
    m_TileY = tileY;

    // Position at bottom-center of tile
    m_Position.x = tileX * tileSize + tileSize * 0.5f;
    m_Position.y = tileY * tileSize + static_cast<float>(tileSize);

    m_TargetTileX = tileX;
    m_TargetTileY = tileY;

    if (!preserveRoute)
    {
        m_PatrolRoute.Reset();
    }
}

glm::vec2 NonPlayerCharacter::GetSpriteCoords(int frame, NPCDirection dir) const
{
    int spriteX = (frame % NPC_WALK_FRAMES) * NPC_SPRITE_WIDTH;
    int spriteY = 0;

    switch (dir)
    {
        case NPCDirection::DOWN:
            spriteY = 2 * NPC_SPRITE_HEIGHT;
            break;
        case NPCDirection::UP:
            spriteY = 3 * NPC_SPRITE_HEIGHT;
            break;
        case NPCDirection::LEFT:
            spriteY = 1 * NPC_SPRITE_HEIGHT;
            break;
        case NPCDirection::RIGHT:
            spriteY = 0 * NPC_SPRITE_HEIGHT;
            break;
    }

    return glm::vec2(static_cast<float>(spriteX), static_cast<float>(spriteY));
}

void NonPlayerCharacter::Update(float deltaTime,
                                const Tilemap* tilemap,
                                const glm::vec2* playerPosition)
{
    if (!tilemap)
        return;

    // Smooth elevation transition (must run regardless of movement state)
    UpdateElevation(deltaTime);

    bool isCollidingWithPlayer = false;
    if (playerPosition)
    {
        // Build NPC hitbox (16x16, centered on feet)
        float npcMinX = m_Position.x - NPC_HALF_WIDTH + COLLISION_EPS;
        float npcMaxX = m_Position.x + NPC_HALF_WIDTH - COLLISION_EPS;
        float npcMaxY = m_Position.y - COLLISION_EPS;
        float npcMinY = m_Position.y - NPC_HITBOX_HEIGHT + COLLISION_EPS;

        // Build player hitbox (16x16, centered on feet)
        float playerMinX = playerPosition->x - NPC_HALF_WIDTH + COLLISION_EPS;
        float playerMaxX = playerPosition->x + NPC_HALF_WIDTH - COLLISION_EPS;
        float playerMaxY = playerPosition->y - COLLISION_EPS;
        float playerMinY = playerPosition->y - NPC_HITBOX_HEIGHT + COLLISION_EPS;

        // AABB intersection test
        if (npcMinX < playerMaxX && npcMaxX > playerMinX && npcMinY < playerMaxY &&
            npcMaxY > playerMinY)
        {
            isCollidingWithPlayer = true;
            m_WaitTimer = 0.5f;
        }
    }

    if (m_IsStopped || isCollidingWithPlayer)
    {
        ResetAnimation();
        return;
    }

    if (m_StandingStill)
    {
        ResetAnimation();

        // Random pause: Count down timer
        if (m_RandomStandStillTimer > 0.0f)
        {
            m_RandomStandStillTimer -= deltaTime;
            if (m_RandomStandStillTimer <= 0.0f)
            {
                m_StandingStill = false;
                m_RandomStandStillTimer = 0.0f;
            }
            else
            {
                // Look around while paused
                UpdateLookAround(deltaTime);
                return;
            }
        }
        else
        {
            // No path available: Look around indefinitely
            UpdateLookAround(deltaTime);
            return;
        }
    }

    const int tileWidth = tilemap->GetTileWidth();
    const int tileHeight = tilemap->GetTileHeight();
    if (tileWidth <= 0 || tileHeight <= 0)
        return;

    m_TileX = static_cast<int>(std::floor(m_Position.x / static_cast<float>(tileWidth)));
    m_TileY = static_cast<int>(std::floor((m_Position.y - 0.1f) / static_cast<float>(tileHeight)));

    if (m_WaitTimer > 0.0f)
    {
        m_WaitTimer -= deltaTime;
        if (m_WaitTimer < 0.0f)
            m_WaitTimer = 0.0f;
    }

    m_AnimationTime += deltaTime;
    if (m_AnimationTime >= NPC_ANIM_SPEED)
    {
        m_AnimationTime -= NPC_ANIM_SPEED;
        AdvanceWalkAnimation();
    }

    if (m_WaitTimer > 0.0f)
        return;

    if (m_PatrolRoute.IsValid() && m_RandomStandStillCheckTimer > 0.0f)
    {
        m_RandomStandStillCheckTimer -= deltaTime;
    }

    glm::vec2 targetPos(
        m_TargetTileX * static_cast<float>(tileWidth) + static_cast<float>(tileWidth) * 0.5f,
        m_TargetTileY * static_cast<float>(tileHeight) + static_cast<float>(tileHeight));

    glm::vec2 toTarget = targetPos - m_Position;
    float dist = glm::length(toTarget);

    // Check if we've reached the current waypoint
    if (dist < WAYPOINT_REACH_THRESHOLD)
    {
        m_Position = targetPos;

        // Initialize patrol route if needed
        if (!m_PatrolRoute.IsValid())
        {
            if (!m_PatrolRoute.Initialize(m_TileX, m_TileY, tilemap, 100))
            {
                EnterStandingStillMode(false);
                return;
            }
            else
            {
                m_StandingStill = false;
                m_RandomStandStillTimer = 0.0f;
                m_RandomStandStillCheckTimer = 5.0f + (GetNpcRng()() % 500) / 100.0f;
            }
        }

        // Random pause check (30% chance when timer expires at waypoint)
        if (m_PatrolRoute.IsValid() && m_RandomStandStillCheckTimer <= 0.0f)
        {
            m_RandomStandStillCheckTimer = 5.0f + (GetNpcRng()() % 500) / 100.0f;
            if ((GetNpcRng()() % 100) < 30)
            {
                float duration = 2.0f + (GetNpcRng()() % 300) / 100.0f;
                EnterStandingStillMode(true, duration);
                return;
            }
        }

        // Get next waypoint
        int nextX, nextY;
        if (m_PatrolRoute.GetNextWaypoint(nextX, nextY))
        {
            m_TargetTileX = nextX;
            m_TargetTileY = nextY;
            UpdateDirectionFromMovement(m_TargetTileX - m_TileX, m_TargetTileY - m_TileY);
        }
        else
        {
            m_WaitTimer = 1.0f;
        }
        return;
    }

    if (dist > MIN_MOVEMENT_DIST)
    {
        glm::vec2 dir = toTarget / dist;
        glm::vec2 newPosition = m_Position + dir * m_Speed * deltaTime;

        bool wouldCollide = CheckPlayerCollision(newPosition, playerPosition);

        if (!wouldCollide)
        {
            m_Position = newPosition;
            UpdateDirectionFromMovement(static_cast<int>(dir.x > 0) - static_cast<int>(dir.x < 0),
                                        static_cast<int>(dir.y > 0) - static_cast<int>(dir.y < 0));
        }
        else
        {
            m_WaitTimer = 0.5f;
        }
    }
}

void NonPlayerCharacter::UpdateLookAround(float deltaTime)
{
    m_LookAroundTimer -= deltaTime;
    if (m_LookAroundTimer <= 0.0f)
    {
        static const NPCDirection directions[] = {
            NPCDirection::LEFT, NPCDirection::RIGHT, NPCDirection::UP, NPCDirection::DOWN};
        m_Direction = directions[GetNpcRng()() % 4];
        m_LookAroundTimer = 2.0f;
    }
}

void NonPlayerCharacter::EnterStandingStillMode(bool isRandom, float duration)
{
    m_StandingStill = true;
    m_RandomStandStillTimer = isRandom ? duration : 0.0f;
    m_LookAroundTimer = 2.0f;
    ResetAnimation();

    static const NPCDirection directions[] = {
        NPCDirection::LEFT, NPCDirection::RIGHT, NPCDirection::UP, NPCDirection::DOWN};
    m_Direction = directions[GetNpcRng()() % 4];
}

void NonPlayerCharacter::UpdateDirectionFromMovement(int dx, int dy)
{
    if (std::abs(dx) > std::abs(dy))
    {
        m_Direction = (dx > 0) ? NPCDirection::RIGHT : NPCDirection::LEFT;
    }
    else if (dy != 0)
    {
        m_Direction = (dy > 0) ? NPCDirection::DOWN : NPCDirection::UP;
    }
}

bool NonPlayerCharacter::CheckPlayerCollision(const glm::vec2& newPosition,
                                              const glm::vec2* playerPos) const
{
    if (!playerPos)
        return false;

    float npcMinX = newPosition.x - NPC_HALF_WIDTH + COLLISION_EPS;
    float npcMaxX = newPosition.x + NPC_HALF_WIDTH - COLLISION_EPS;
    float npcMaxY = newPosition.y - COLLISION_EPS;
    float npcMinY = newPosition.y - NPC_HITBOX_HEIGHT + COLLISION_EPS;

    float playerMinX = playerPos->x - NPC_HALF_WIDTH + COLLISION_EPS;
    float playerMaxX = playerPos->x + NPC_HALF_WIDTH - COLLISION_EPS;
    float playerMaxY = playerPos->y - COLLISION_EPS;
    float playerMinY = playerPos->y - NPC_HITBOX_HEIGHT + COLLISION_EPS;

    return npcMinX < playerMaxX && npcMaxX > playerMinX && npcMinY < playerMaxY &&
           npcMaxY > playerMinY;
}

bool NonPlayerCharacter::ReinitializePatrolRoute(const Tilemap* tilemap)
{
    if (!tilemap)
        return false;

    m_PatrolRoute.Reset();
    bool success = m_PatrolRoute.Initialize(m_TileX, m_TileY, tilemap, 100);

    if (success)
    {
        m_StandingStill = false;
        m_RandomStandStillTimer = 0.0f;
        m_RandomStandStillCheckTimer = 5.0f + (GetNpcRng()() % 500) / 100.0f;
    }
    else
    {
        m_StandingStill = true;
        m_RandomStandStillTimer = 0.0f;
        m_LookAroundTimer = 2.0f;
    }

    return success;
}

void NonPlayerCharacter::ResetAnimationToIdle()
{
    ResetAnimation();
}

void NonPlayerCharacter::Render(IRenderer& renderer, glm::vec2 cameraPos) const
{
    constexpr float spriteWidth = static_cast<float>(NPC_SPRITE_WIDTH);
    constexpr float spriteHeight = static_cast<float>(NPC_SPRITE_HEIGHT);

    // Convert world position to screen space
    glm::vec2 bottomCenter = m_Position - cameraPos;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    // Position sprite with feet at projected point
    glm::vec2 renderPos = bottomCenter - glm::vec2(spriteWidth / 2.0f, spriteHeight);
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction);

    renderer.DrawSpriteRegion(m_SpriteSheet,
                              renderPos,
                              glm::vec2(spriteWidth, spriteHeight),
                              spriteCoords,
                              glm::vec2(spriteWidth, spriteHeight),
                              0.0f,
                              glm::vec3(1.0f),
                              false);
}

void NonPlayerCharacter::RenderBottomHalf(IRenderer& renderer, glm::vec2 cameraPos) const
{
    constexpr float spriteWidth = static_cast<float>(NPC_SPRITE_WIDTH);
    constexpr float spriteHeight = static_cast<float>(NPC_SPRITE_HEIGHT);
    constexpr float halfHeight = 16.0f;

    // Apply elevation before projection
    glm::vec2 bottomCenter = m_Position - cameraPos;
    bottomCenter.y -= m_ElevationOffset;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    glm::vec2 renderPos = bottomCenter - glm::vec2(spriteWidth / 2.0f, spriteHeight);
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction);

    // Draw lower 16 pixels (feet area)
    glm::vec2 bottomHalfCoords = spriteCoords;
    renderer.SuspendPerspective(true);
    renderer.DrawSpriteRegion(m_SpriteSheet,
                              renderPos + glm::vec2(0.0f, halfHeight),
                              glm::vec2(spriteWidth, halfHeight),
                              bottomHalfCoords,
                              glm::vec2(spriteWidth, halfHeight),
                              0.0f,
                              glm::vec3(1.0f),
                              false);
    renderer.SuspendPerspective(false);
}

void NonPlayerCharacter::RenderTopHalf(IRenderer& renderer, glm::vec2 cameraPos) const
{
    constexpr float spriteWidth = static_cast<float>(NPC_SPRITE_WIDTH);
    constexpr float spriteHeight = static_cast<float>(NPC_SPRITE_HEIGHT);
    constexpr float halfHeight = 16.0f;

    // Apply elevation before projection
    glm::vec2 bottomCenter = m_Position - cameraPos;
    bottomCenter.y -= m_ElevationOffset;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    glm::vec2 renderPos = bottomCenter - glm::vec2(spriteWidth / 2.0f, spriteHeight);
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction);

    // Draw upper 16 pixels (head/torso area)
    glm::vec2 topHalfCoords = spriteCoords + glm::vec2(0.0f, halfHeight);

    renderer.SuspendPerspective(true);
    renderer.DrawSpriteRegion(m_SpriteSheet,
                              renderPos,
                              glm::vec2(spriteWidth, halfHeight),
                              topHalfCoords,
                              glm::vec2(spriteWidth, halfHeight),
                              0.0f,
                              glm::vec3(1.0f),
                              false);
    renderer.SuspendPerspective(false);
}
