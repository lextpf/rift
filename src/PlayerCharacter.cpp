#include "PlayerCharacter.h"
#include "IRenderer.h"
#include "Tilemap.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

namespace
{
// Width of each player sprite frame in pixels.
constexpr float SPRITE_WIDTH_F = 32.0f;

// Height of each player sprite frame in pixels.
constexpr float SPRITE_HEIGHT_F = 32.0f;

// Half the sprite height (for split rendering).
constexpr float SPRITE_HALF_HEIGHT = 16.0f;

// Number of walking animation frames per direction.
constexpr int WALK_FRAMES = 3;
}  // namespace

/* Animation frame duration in seconds (time per frame). */
const float PlayerCharacter::ANIMATION_SPEED = 0.15f;

/* Static registry mapping (CharacterType, spriteType) -> asset path. */
std::map<PlayerCharacter::CharacterAssetKey, std::string> PlayerCharacter::s_CharacterAssets;

PlayerCharacter::PlayerCharacter()
    : GameCharacter(),
      m_AnimationType(AnimationType::IDLE),
      m_IsMoving(false),
      m_IsRunning(false),
      m_IsBicycling(false),
      m_IsUsingCopiedAppearance(false),
      m_CharacterType(CharacterType::BW1_MALE),
      m_LastSafeTileCenter(200.0f, 150.0f),
      m_LastMovementDirection(0.0f, 0.0f),
      m_SlideHysteresisDir(0.0f, 0.0f),
      m_SlideCommitTimer(0.0f),
      m_AxisPreference(0),
      m_AxisCommitTimer(0.0f),
      m_SnapStartPos(0.0f),
      m_SnapTargetPos(0.0f),
      m_SnapProgress(1.0f),
      m_LastInputX(0),
      m_LastInputY(0),
      m_Collision(*this)
{
    m_Position = glm::vec2(200.0f, 150.0f);
    m_Speed = 80.0f;
}

PlayerCharacter::~PlayerCharacter() = default;

PlayerCharacter::PlayerCharacter(PlayerCharacter&& other) noexcept
    : GameCharacter(std::move(other)),
      m_SpriteSheet(std::move(other.m_SpriteSheet)),
      m_RunningSpriteSheet(std::move(other.m_RunningSpriteSheet)),
      m_BicycleSpriteSheet(std::move(other.m_BicycleSpriteSheet)),
      m_IsRunning(other.m_IsRunning),
      m_IsBicycling(other.m_IsBicycling),
      m_IsUsingCopiedAppearance(other.m_IsUsingCopiedAppearance),
      m_LastSafeTileCenter(other.m_LastSafeTileCenter),
      m_LastMovementDirection(other.m_LastMovementDirection),
      m_SlideHysteresisDir(other.m_SlideHysteresisDir),
      m_SlideCommitTimer(other.m_SlideCommitTimer),
      m_AxisPreference(other.m_AxisPreference),
      m_AxisCommitTimer(other.m_AxisCommitTimer),
      m_SnapStartPos(other.m_SnapStartPos),
      m_SnapTargetPos(other.m_SnapTargetPos),
      m_SnapProgress(other.m_SnapProgress),
      m_IsMoving(other.m_IsMoving),
      m_LastInputX(other.m_LastInputX),
      m_LastInputY(other.m_LastInputY),
      m_AnimationType(other.m_AnimationType),
      m_CharacterType(other.m_CharacterType),
      m_Collision(*this)  // Rebind to this instance, not the moved-from one
{
}

PlayerCharacter& PlayerCharacter::operator=(PlayerCharacter&& other) noexcept
{
    if (this != &other)
    {
        GameCharacter::operator=(std::move(other));
        m_SpriteSheet = std::move(other.m_SpriteSheet);
        m_RunningSpriteSheet = std::move(other.m_RunningSpriteSheet);
        m_BicycleSpriteSheet = std::move(other.m_BicycleSpriteSheet);
        m_IsRunning = other.m_IsRunning;
        m_IsBicycling = other.m_IsBicycling;
        m_IsUsingCopiedAppearance = other.m_IsUsingCopiedAppearance;
        m_LastSafeTileCenter = other.m_LastSafeTileCenter;
        m_LastMovementDirection = other.m_LastMovementDirection;
        m_SlideHysteresisDir = other.m_SlideHysteresisDir;
        m_SlideCommitTimer = other.m_SlideCommitTimer;
        m_AxisPreference = other.m_AxisPreference;
        m_AxisCommitTimer = other.m_AxisCommitTimer;
        m_SnapStartPos = other.m_SnapStartPos;
        m_SnapTargetPos = other.m_SnapTargetPos;
        m_SnapProgress = other.m_SnapProgress;
        m_IsMoving = other.m_IsMoving;
        m_LastInputX = other.m_LastInputX;
        m_LastInputY = other.m_LastInputY;
        m_AnimationType = other.m_AnimationType;
        m_CharacterType = other.m_CharacterType;
        m_Collision.Rebind(*this);  // Keep pointing to this instance
    }
    return *this;
}

bool PlayerCharacter::LoadSpriteSheet(const std::string& path)
{
    return m_SpriteSheet.LoadFromFile(path);
}

bool PlayerCharacter::LoadRunningSpriteSheet(const std::string& path)
{
    return m_RunningSpriteSheet.LoadFromFile(path);
}

bool PlayerCharacter::LoadBicycleSpriteSheet(const std::string& path)
{
    return m_BicycleSpriteSheet.LoadFromFile(path);
}

void PlayerCharacter::UploadTextures(IRenderer& renderer)
{
    // Upload all sprite textures to the renderer
    // This is needed when switching renderers to recreate textures in the new context
    renderer.UploadTexture(m_SpriteSheet);
    renderer.UploadTexture(m_RunningSpriteSheet);
    renderer.UploadTexture(m_BicycleSpriteSheet);
}

void PlayerCharacter::SetCharacterAsset(CharacterType characterType,
                                        const std::string& spriteType,
                                        const std::string& path)
{
    s_CharacterAssets[CharacterAssetKey{characterType, spriteType}] = path;
}

bool PlayerCharacter::SwitchCharacter(CharacterType characterType)
{
    auto typeName = EnumTraits<CharacterType>::ToString(characterType);

    // Lambda: Resolve asset path from registry
    auto getAssetPath = [characterType, typeName](const std::string& spriteType) -> std::string
    {
        auto key = CharacterAssetKey{characterType, spriteType};
        auto it = s_CharacterAssets.find(key);
        if (it != s_CharacterAssets.end())
            return it->second;

        // Asset not registered
        std::cerr << "No asset registered for " << typeName << " " << spriteType << std::endl;
        return "";
    };

    // Lambda: Attempt load with fallback to parent directory
    auto tryLoad = [](Texture& target, const std::string& path) -> bool
    {
        if (path.empty())
            return false;

        if (target.LoadFromFile(path))
            return true;
        return target.LoadFromFile("../" + path);  // Try parent directory
    };

    Texture newWalking;
    Texture newRunning;
    Texture newBicycle;

    // Load all sprite sheets into temporaries so state only changes on success
    bool walkingLoaded = tryLoad(newWalking, getAssetPath("Walking"));
    bool runningLoaded = tryLoad(newRunning, getAssetPath("Running"));
    bool bicycleLoaded = tryLoad(newBicycle, getAssetPath("Bicycle"));

    // Validate required sprites loaded
    if (!walkingLoaded || !runningLoaded)
    {
        std::cerr << "Failed to load character sprites for " << typeName << std::endl;
        return false;
    }

    if (!bicycleLoaded)
        std::cout << "Warning: Bicycle sprite not found for " << typeName << std::endl;

    m_SpriteSheet = std::move(newWalking);
    m_RunningSpriteSheet = std::move(newRunning);
    if (bicycleLoaded)
    {
        m_BicycleSpriteSheet = std::move(newBicycle);
    }
    m_CharacterType = characterType;

    std::cout << "Switched to " << typeName << std::endl;
    return true;
}

bool PlayerCharacter::CopyAppearanceFrom(const std::string& spritePath)
{
    // Load into temporaries first so the player's existing sprites are
    // preserved if any load fails. This prevents a partial state where
    // the walk sheet is replaced but run/bicycle are stale.
    auto tryLoad = [](Texture& target, const std::string& path) -> bool
    {
        if (target.LoadFromFile(path))
        {
            return true;
        }
        return target.LoadFromFile("../" + path);
    };

    Texture newWalk;

    if (!tryLoad(newWalk, spritePath))
    {
        std::cerr << "Failed to copy appearance from: " << spritePath << std::endl;
        return false;
    }

    // NPC sprite sheets have only a walking sprite, so reuse it for all animation types.
    // Running and bicycle modes will automatically restore the original appearance anyway,
    // since NPCs don't have separate run/bike sheets.

    // Commit the change -- only the walking sheet is replaced.
    m_SpriteSheet = std::move(newWalk);
    m_IsUsingCopiedAppearance = true;
    std::cout << "Copied appearance from: " << spritePath << std::endl;
    return true;
}

void PlayerCharacter::RestoreOriginalAppearance()
{
    if (!m_IsUsingCopiedAppearance)
        return;

    // Reload original character sprites - only clear the flag on success
    if (SwitchCharacter(m_CharacterType))
    {
        m_IsUsingCopiedAppearance = false;
        std::cout << "Restored original appearance" << std::endl;
    }
    else
    {
        std::cerr << "Failed to restore original appearance" << std::endl;
    }
}

void PlayerCharacter::SetRunning(bool running)
{
    m_IsRunning = running;
}
void PlayerCharacter::SetBicycling(bool bicycling)
{
    m_IsBicycling = bicycling;
}

void PlayerCharacter::Update(float deltaTime)
{
    m_AnimationTime += deltaTime;

    // Running animation is 50% faster than walking
    float animSpeed =
        (m_AnimationType == AnimationType::RUN) ? ANIMATION_SPEED * 0.5f : ANIMATION_SPEED;

    if (m_AnimationTime >= animSpeed)
    {
        m_AnimationTime = std::fmod(m_AnimationTime, animSpeed);

        if (m_AnimationType == AnimationType::IDLE)
        {
            ResetAnimation();
        }
        else
        {
            AdvanceWalkAnimation();
        }
    }

    UpdateElevation(deltaTime);
}

void PlayerCharacter::Render(IRenderer& renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    // Pass renderer's Y-flip requirement for correct sprite sheet row selection
    glm::vec2 spriteCoords =
        GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation BEFORE projection (moves sprite up on stairs)
    bottomCenter.y -= m_ElevationOffset;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    // Convert from bottom-center to render position (top-left)
    glm::vec2 renderPos = bottomCenter - glm::vec2(SPRITE_WIDTH_F / 2.0f, SPRITE_HEIGHT_F);

    // Select sprite sheet based on movement mode
    const Texture& sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Suspend perspective - we already projected the position, don't double-project
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(sheet,
                                  renderPos,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HEIGHT_F),
                                  spriteCoords,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HEIGHT_F),
                                  0.0f,
                                  glm::vec3(1.0f),
                                  false);
    }
}

void PlayerCharacter::RenderBottomHalf(IRenderer& renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    glm::vec2 spriteCoords =
        GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation to bottom-center BEFORE projection
    bottomCenter.y -= m_ElevationOffset;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    // Convert from bottom-center position to render position (top-left)
    glm::vec2 renderPos = bottomCenter - glm::vec2(SPRITE_WIDTH_F / 2.0f, SPRITE_HEIGHT_F);

    // Apply sprint visual offset
    /*bool isSprinting = (m_AnimationType == AnimationType::RUN && m_IsRunning);
    if (isSprinting)
    {
        constexpr float offsetAmount = 2.0f;
        switch (m_Direction)
        {
            case Direction::DOWN:  renderPos.y -= offsetAmount; break;
            case Direction::UP:    renderPos.y += offsetAmount; break;
            case Direction::RIGHT: renderPos.x -= offsetAmount; break;
            case Direction::LEFT:  renderPos.x += offsetAmount; break;
        }
    }*/

    // Select sprite sheet based on movement mode
    const Texture& sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Bottom half: lower 16 pixels of the sprite
    // Render position is offset to show only the bottom half
    glm::vec2 bottomRenderPos = renderPos + glm::vec2(0.0f, SPRITE_HALF_HEIGHT);
    glm::vec2 bottomSpriteCoords = spriteCoords;

    // Suspend perspective - we already projected the position, don't double-project
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(sheet,
                                  bottomRenderPos,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                                  bottomSpriteCoords,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                                  0.0f,
                                  glm::vec3(1.0f),
                                  false);
    }
}

void PlayerCharacter::RenderTopHalf(IRenderer& renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    glm::vec2 spriteCoords =
        GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation to bottom-center BEFORE projection
    bottomCenter.y -= m_ElevationOffset;

    bottomCenter = renderer.ProjectPointSafe(bottomCenter);

    // Convert from bottom-center position to render position (top-left)
    glm::vec2 renderPos = bottomCenter - glm::vec2(SPRITE_WIDTH_F / 2.0f, SPRITE_HEIGHT_F);

    // Select sprite sheet based on movement mode
    const Texture& sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Top half: upper 16 pixels of the sprite (head/torso area)
    glm::vec2 topSpriteCoords = spriteCoords + glm::vec2(0.0f, SPRITE_HALF_HEIGHT);

    // Suspend perspective - we already projected the position, don't double-project
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        renderer.DrawSpriteRegion(sheet,
                                  renderPos,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                                  topSpriteCoords,
                                  glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                                  0.0f,
                                  glm::vec3(1.0f),
                                  false);
    }
}

// Collision methods have been moved to CollisionResolver.cpp

void PlayerCharacter::Move(glm::vec2 direction,
                           float deltaTime,
                           const Tilemap* tilemap,
                           const std::vector<glm::vec2>* npcPositions)
{
    // === No input: handle idle state ===
    if (glm::length(direction) < 0.1f)
    {
        m_Collision.HandleIdleSnap(deltaTime, tilemap, npcPositions);
        return;
    }

    // Decay commit timers
    if (m_SlideCommitTimer > 0.0f)
        m_SlideCommitTimer -= deltaTime;
    if (m_AxisCommitTimer > 0.0f)
        m_AxisCommitTimer -= deltaTime;

    glm::vec2 normalizedDir = glm::normalize(direction);

    bool curHorizontal = std::abs(normalizedDir.x) > std::abs(normalizedDir.y);
    bool lastHorizontal = std::abs(m_LastMovementDirection.x) > std::abs(m_LastMovementDirection.y);
    if (curHorizontal != lastHorizontal && m_SlideCommitTimer <= 0.0f)
        m_SlideHysteresisDir = glm::vec2(0.0f);

    // Convert continuous direction to discrete signs with deadzone
    // Deadzone prevents accidental diagonal input from slight analog drift
    auto signWithDeadzone = [](float v, float dz = 0.2f) -> int
    {
        return (v > dz) ? 1 : (v < -dz) ? -1 : 0;
    };
    auto signStep = [](float v) -> int { return (v > 1e-4f) ? 1 : (v < -1e-4f) ? -1 : 0; };
    int moveDx = signWithDeadzone(direction.x);
    int moveDy = signWithDeadzone(direction.y);
    bool diagonalInput = (moveDx != 0 && moveDy != 0);

    // Update last input direction
    if (moveDx != 0)
        m_LastInputX = moveDx;
    if (moveDy != 0)
        m_LastInputY = moveDy;

    // Update facing direction
    if (std::abs(normalizedDir.x) > std::abs(normalizedDir.y))
        m_Direction = (normalizedDir.x > 0) ? Direction::RIGHT : Direction::LEFT;
    else
        m_Direction = (normalizedDir.y > 0) ? Direction::DOWN : Direction::UP;

    // Start or update animation
    AnimationType targetAnim =
        (m_IsRunning || m_IsBicycling) ? AnimationType::RUN : AnimationType::WALK;
    if (!m_IsMoving)
    {
        m_IsMoving = true;
        m_AnimationType = targetAnim;
        m_WalkSequenceIndex = 0;
        m_CurrentFrame = 1;
        m_AnimationTime = 0.0f;
    }
    else if (m_AnimationType != targetAnim)
    {
        m_AnimationType = targetAnim;
    }

    // Calculate speed and movement
    float currentSpeed = m_Speed;
    if (m_IsBicycling)
        currentSpeed *= 2.0f;
    else if (m_IsRunning)
        currentSpeed *= 1.9f;

    bool sprintMode = (m_IsRunning || m_IsBicycling);
    glm::vec2 desiredMovement = normalizedDir * currentSpeed * deltaTime;
    const float requestedMoveLen = glm::length(desiredMovement);

    if (tilemap)
    {
        // Track last safe position
        if (!m_Collision.CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false))
            m_LastSafeTileCenter =
                GetCurrentTileCenter(static_cast<float>(tilemap->GetTileWidth()));

        // Try full movement first
        glm::vec2 testPos = m_Position + desiredMovement;
        bool npcBlocked = m_Collision.CollidesWithNPC(testPos, npcPositions);
        bool tileBlocked = sprintMode ? m_Collision.CollidesWithTilesCenter(testPos, tilemap)
                                      : m_Collision.CollidesWithTilesStrict(
                                            testPos, tilemap, moveDx, moveDy, diagonalInput);
        bool initiallyTileBlocked = tileBlocked;

        bool didCornerSlide = false;

        if (npcBlocked)
        {
            // NPC collision: stop completely
            desiredMovement = glm::vec2(0.0f);
        }
        else if (tileBlocked)
        {
            // 1) Try the real corner/slide solver FIRST (it can do slide+forward at corners)
            glm::vec2 slideMovement = m_Collision.TrySlideMovement(desiredMovement,
                                                                   normalizedDir,
                                                                   deltaTime,
                                                                   currentSpeed,
                                                                   tilemap,
                                                                   npcPositions,
                                                                   sprintMode,
                                                                   moveDx,
                                                                   moveDy,
                                                                   diagonalInput);

            if (glm::length(slideMovement) > 0.001f)
            {
                desiredMovement = slideMovement;
                didCornerSlide = true;
                tileBlocked = false;  // we produced a non-blocked alternative
            }
            else
            {
                // 2) Only if slide solver found nothing: fall back to axis-separated movement for
                // diagonal input
                if (diagonalInput)
                {
                    glm::vec2 moveX(desiredMovement.x, 0.0f);
                    glm::vec2 moveY(0.0f, desiredMovement.y);

                    bool okX = !m_Collision.CollidesAt(
                        m_Position + moveX, tilemap, npcPositions, sprintMode, moveDx, 0, false);
                    bool okY = !m_Collision.CollidesAt(
                        m_Position + moveY, tilemap, npcPositions, sprintMode, 0, moveDy, false);

                    if (okX && !okY)
                    {
                        desiredMovement = moveX;
                        moveDy = 0;
                        diagonalInput = false;
                        tileBlocked = false;
                    }
                    else if (okY && !okX)
                    {
                        desiredMovement = moveY;
                        moveDx = 0;
                        diagonalInput = false;
                        tileBlocked = false;
                    }
                    // else: keep blocked -> later escape logic can try
                }

                // (keep your existing GetCornerSlideDirection escape fallback here if you want)
            }
        }

        // Apply lane snapping (perpendicular alignment to tile centers)
        // Skip if we're at a collision - let the player deal with that first
        int effDx = signStep(desiredMovement.x);
        int effDy = signStep(desiredMovement.y);
        bool effDiagonal = (effDx != 0 && effDy != 0);
        if (!effDiagonal && !didCornerSlide && !initiallyTileBlocked)
        {
            desiredMovement = m_Collision.ApplyLaneSnapping(desiredMovement,
                                                            normalizedDir,
                                                            deltaTime,
                                                            tilemap,
                                                            npcPositions,
                                                            sprintMode,
                                                            effDx,
                                                            effDy);
        }

        // Final collision check
        if (m_Collision.CollidesAt(m_Position + desiredMovement,
                                   tilemap,
                                   npcPositions,
                                   sprintMode,
                                   effDx,
                                   effDy,
                                   effDiagonal))
        {
            // Try axis-separated movement
            glm::vec2 tryX = m_Position + glm::vec2(desiredMovement.x, 0.0f);
            glm::vec2 tryY = m_Position + glm::vec2(0.0f, desiredMovement.y);

            bool okX =
                !m_Collision.CollidesAt(tryX, tilemap, npcPositions, sprintMode, moveDx, 0, false);
            bool okY =
                !m_Collision.CollidesAt(tryY, tilemap, npcPositions, sprintMode, 0, moveDy, false);

            if (okX && okY)
            {
                // Both axes work - use hysteresis to avoid jitter at corners
                bool preferX;
                if (m_AxisCommitTimer > 0.0f && m_AxisPreference != 0)
                {
                    // Committed to a direction - keep it
                    preferX = (m_AxisPreference > 0);
                }
                else
                {
                    // No commitment - pick based on primary direction with deadzone
                    float xMag = std::abs(normalizedDir.x);
                    float yMag = std::abs(normalizedDir.y);
                    float diff = xMag - yMag;

                    // Only change preference if there's a significant difference
                    if (std::abs(diff) > 0.15f)
                    {
                        preferX = (diff > 0.0f);
                        m_AxisPreference = preferX ? 1 : -1;
                        m_AxisCommitTimer = 0.15f;  // Commit for 150ms
                    }
                    else
                    {
                        // Close to 45 degrees - use last preference or default
                        preferX = (m_AxisPreference > 0) || (m_AxisPreference == 0 && xMag > yMag);
                    }
                }

                if (preferX)
                    desiredMovement.y = 0.0f;
                else
                    desiredMovement.x = 0.0f;
            }
            else if (okX)
                desiredMovement.y = 0.0f;
            else if (okY)
                desiredMovement.x = 0.0f;
            else
                desiredMovement = glm::vec2(0.0f);
        }

        // Momentum preservation: if our resolved movement is shorter than the requested
        // length, try to extend along the chosen direction until a collision is found.
        if (requestedMoveLen > 0.001f && glm::length(desiredMovement) > 0.001f)
        {
            glm::vec2 dir = glm::normalize(desiredMovement);
            float lo = glm::length(desiredMovement);
            float hi = requestedMoveLen;

            if (hi > lo + 1e-3f)
            {
                int finalDx = signStep(dir.x);
                int finalDy = signStep(dir.y);
                bool finalDiag = (finalDx != 0 && finalDy != 0);

                for (int i = 0; i < 6; ++i)
                {
                    float mid = (lo + hi) * 0.5f;
                    glm::vec2 tryPos = m_Position + dir * mid;
                    if (!m_Collision.CollidesAt(
                            tryPos, tilemap, npcPositions, sprintMode, finalDx, finalDy, finalDiag))
                        lo = mid;
                    else
                        hi = mid;
                }

                desiredMovement = dir * lo;
            }
        }

        // If sprint center-collision left us wedged in a corner pocket, shove out using strict
        // collision. High FPS fix: Also check when current position OR target position has corner
        // penetration, not just when movement is zero. At high FPS, small movements pass center
        // collision repeatedly, letting the player creep into corner pockets where hitbox edges
        // overlap tiles but center doesn't.
        if (sprintMode && diagonalInput)
        {
            glm::vec2 targetPos = m_Position + desiredMovement;
            bool currentlyStuck =
                m_Collision.IsCornerPenetration(m_Position, tilemap) ||
                m_Collision.CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false);
            bool wouldBeStuck = m_Collision.IsCornerPenetration(targetPos, tilemap);

            if (glm::length(desiredMovement) < 0.001f || currentlyStuck || wouldBeStuck)
            {
                glm::vec2 cornerEject =
                    m_Collision.ComputeSprintCornerEject(tilemap, npcPositions, normalizedDir);
                if (glm::length(cornerEject) > 0.001f)
                    desiredMovement = cornerEject;
            }
        }

        if (glm::length(desiredMovement) > 0.001f)
            m_LastMovementDirection = glm::normalize(desiredMovement);
    }

    m_Position += desiredMovement;
}

glm::vec2 PlayerCharacter::GetCurrentTileCenter(float tileSize) const
{
    if (tileSize <= 0.0f)
        return glm::vec2(0.0f);

    constexpr float EPS = 0.001f;  // Small epsilon to handle edge cases

    // Calculate tile indices
    int tileX = static_cast<int>(std::floor(m_Position.x / tileSize));
    int tileY = static_cast<int>(std::floor((m_Position.y - tileSize * 0.5f - EPS) / tileSize));

    // Return bottom-center position at tile center
    return glm::vec2(tileX * tileSize + tileSize * 0.5f,  // Horizontal center of tile
                     tileY * tileSize + tileSize          // Bottom of tile
    );
}

/**
 * @brief Stop all movement and reset to idle animation state.
 */
void PlayerCharacter::Stop()
{
    m_IsMoving = false;
    m_AnimationType = AnimationType::IDLE;
    m_CurrentFrame = 0;
    m_WalkSequenceIndex = 0;
    m_AnimationTime = 0.0f;
}

glm::vec2 PlayerCharacter::GetSpriteCoords(int frame,
                                           Direction dir,
                                           AnimationType anim,
                                           bool requiresYFlip)
{
    if (anim != AnimationType::WALK && anim != AnimationType::IDLE && anim != AnimationType::RUN)
        return glm::vec2(0, 0);

    int clampedFrame = frame % 3;
    int spriteX = clampedFrame * SPRITE_WIDTH;

    // Map direction to logical row index
    int dirRow = 0;
    switch (dir)
    {
        case Direction::DOWN:
            dirRow = 0;
            break;
        case Direction::UP:
            dirRow = 1;
            break;
        case Direction::LEFT:
            dirRow = 2;
            break;
        case Direction::RIGHT:
            dirRow = 3;
            break;
    }

    if (requiresYFlip)
    {
        // OpenGL: invert row order due to bottom-up texture coordinate system
        static const int glRowMap[] = {2, 3, 1, 0};
        dirRow = glRowMap[dirRow];
    }

    return glm::vec2(spriteX, dirRow * SPRITE_HEIGHT);
}
