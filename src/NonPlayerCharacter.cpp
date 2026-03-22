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

// All four cardinal directions for random look-around selection.
constexpr NPCDirection ALL_DIRECTIONS[] = {
    NPCDirection::LEFT, NPCDirection::RIGHT, NPCDirection::UP, NPCDirection::DOWN};

// AABB overlap test between two entities using feet-based hitboxes.
bool TestHitboxOverlap(
    const glm::vec2& posA, const glm::vec2& posB, float halfWidth, float hitboxHeight, float eps)
{
    float aMinX = posA.x - halfWidth + eps;
    float aMaxX = posA.x + halfWidth - eps;
    float aMaxY = posA.y - eps;
    float aMinY = posA.y - hitboxHeight + eps;

    float bMinX = posB.x - halfWidth + eps;
    float bMaxX = posB.x + halfWidth - eps;
    float bMaxY = posB.y - eps;
    float bMinY = posB.y - hitboxHeight + eps;

    return aMinX < bMaxX && aMaxX > bMinX && aMinY < bMaxY && aMaxY > bMinY;
}
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
    size_t lastSlash = relativePath.find_last_of("/\\");
    std::string filename =
        (lastSlash != std::string::npos) ? relativePath.substr(lastSlash + 1) : relativePath;

    // Remove .png extension if present (case-insensitive)
    if (filename.size() > 4)
    {
        std::string ext = filename.substr(filename.size() - 4);
        std::transform(ext.begin(),
                       ext.end(),
                       ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        m_Type = (ext == ".png") ? filename.substr(0, filename.size() - 4) : filename;
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
        default:
            spriteY = 2 * NPC_SPRITE_HEIGHT;
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
        if (TestHitboxOverlap(
                m_Position, *playerPosition, NPC_HALF_WIDTH, NPC_HITBOX_HEIGHT, COLLISION_EPS))
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
    // Subtract 0.1px so an NPC standing exactly on a tile boundary registers
    // as belonging to the tile above, not the one below (matches player logic).
    m_TileY = static_cast<int>(std::floor((m_Position.y - 0.1f) / static_cast<float>(tileHeight)));

    if (m_WaitTimer > 0.0f)
    {
        m_WaitTimer -= deltaTime;
        if (m_WaitTimer < 0.0f)
            m_WaitTimer = 0.0f;
    }

    if (m_WaitTimer > 0.0f)
        return;

    m_AnimationTime += deltaTime;
    if (m_AnimationTime >= NPC_ANIM_SPEED)
    {
        m_AnimationTime -= NPC_ANIM_SPEED;
        AdvanceWalkAnimation();
    }

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
        // Verify the target tile is still walkable before snapping
        int targetTileX = static_cast<int>(targetPos.x / static_cast<float>(tileWidth));
        int targetTileY = static_cast<int>((targetPos.y - static_cast<float>(tileHeight) * 0.5f) /
                                           static_cast<float>(tileHeight));
        if (tilemap->GetTileCollision(targetTileX, targetTileY))
        {
            // Target blocked - stop and invalidate route to trigger re-initialization
            EnterStandingStillMode(false);
            m_PatrolRoute = PatrolRoute();
            return;
        }

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
                // Wait 5-9.99 seconds before the next random-pause roll.
                // The range prevents NPCs from all pausing in sync.
                m_RandomStandStillCheckTimer =
                    5.0f + std::uniform_int_distribution<int>(0, 499)(GetNpcRng()) / 100.0f;
            }
        }

        // 30% chance to pause at each waypoint when the cooldown expires.
        // This breaks up the mechanical look of constant patrol walking.
        if (m_PatrolRoute.IsValid() && m_RandomStandStillCheckTimer <= 0.0f)
        {
            m_RandomStandStillCheckTimer =
                5.0f + std::uniform_int_distribution<int>(0, 499)(GetNpcRng()) / 100.0f;
            if (std::uniform_int_distribution<int>(0, 99)(GetNpcRng()) < 30)
            {
                // Pause for 2-4.99 seconds - long enough to look natural,
                // short enough not to stall gameplay.
                float duration =
                    2.0f + std::uniform_int_distribution<int>(0, 299)(GetNpcRng()) / 100.0f;
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
        m_Direction = ALL_DIRECTIONS[std::uniform_int_distribution<int>(0, 3)(GetNpcRng())];
        m_LookAroundTimer = 2.0f;
    }
}

void NonPlayerCharacter::EnterStandingStillMode(bool isRandom, float duration)
{
    m_StandingStill = true;
    // When not random (e.g. no patrol route found), timer stays at 0 so the
    // NPC stays in standing-still/look-around mode indefinitely until a route
    // is assigned.
    m_RandomStandStillTimer = isRandom ? duration : 0.0f;
    m_LookAroundTimer = 2.0f;
    ResetAnimation();

    m_Direction = ALL_DIRECTIONS[std::uniform_int_distribution<int>(0, 3)(GetNpcRng())];
}

void NonPlayerCharacter::UpdateDirectionFromMovement(int dx, int dy)
{
    // Prefer horizontal facing when both axes have equal magnitude.
    // This matches player character behavior and looks more natural for
    // diagonal movement on a 2D top-down map.
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
    {
        return false;
    }

    return TestHitboxOverlap(
        newPosition, *playerPos, NPC_HALF_WIDTH, NPC_HITBOX_HEIGHT, COLLISION_EPS);
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
        m_RandomStandStillCheckTimer =
            5.0f + std::uniform_int_distribution<int>(0, 499)(GetNpcRng()) / 100.0f;
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
    bottomCenter.y -= m_ElevationOffset;

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
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(m_SpriteSheet,
                                  renderPos + glm::vec2(0.0f, halfHeight),
                                  glm::vec2(spriteWidth, halfHeight),
                                  bottomHalfCoords,
                                  glm::vec2(spriteWidth, halfHeight),
                                  0.0f,
                                  glm::vec3(1.0f),
                                  false);
    }
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

    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(m_SpriteSheet,
                                  renderPos,
                                  glm::vec2(spriteWidth, halfHeight),
                                  topHalfCoords,
                                  glm::vec2(spriteWidth, halfHeight),
                                  0.0f,
                                  glm::vec3(1.0f),
                                  false);
    }
}
