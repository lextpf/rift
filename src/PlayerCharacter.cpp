#include "PlayerCharacter.h"
#include "Tilemap.h"
#include "IRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <glm/gtc/matrix_transform.hpp>

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

    // NPC hitbox height for player-NPC collision checks.
    constexpr float NPC_HITBOX_HEIGHT = 16.0f;

    // Maximum slide distance for corner-cutting (pixels).
    constexpr float MAX_SLIDE_DISTANCE = 16.0f;
}

/* Animation frame duration in seconds (time per frame). */
const float PlayerCharacter::ANIMATION_SPEED = 0.15f;

/* Static registry mapping (CharacterType, spriteType) -> asset path. */
std::map<std::pair<CharacterType, std::string>, std::string> PlayerCharacter::s_CharacterAssets;

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
      m_LastInputY(0)
{
    m_Position = glm::vec2(200.0f, 150.0f);
    m_Speed = 80.0f;
}

PlayerCharacter::~PlayerCharacter() = default;

bool PlayerCharacter::LoadSpriteSheet(const std::string &path)
{
    return m_SpriteSheet.LoadFromFile(path);
}

bool PlayerCharacter::LoadRunningSpriteSheet(const std::string &path)
{
    return m_RunningSpriteSheet.LoadFromFile(path);
}

bool PlayerCharacter::LoadBicycleSpriteSheet(const std::string &path)
{
    return m_BicycleSpriteSheet.LoadFromFile(path);
}

void PlayerCharacter::UploadTextures(IRenderer &renderer)
{
    // Upload all sprite textures to the renderer
    // This is needed when switching renderers to recreate textures in the new context
    renderer.UploadTexture(m_SpriteSheet);
    renderer.UploadTexture(m_RunningSpriteSheet);
    renderer.UploadTexture(m_BicycleSpriteSheet);
}

void PlayerCharacter::SetCharacterAsset(CharacterType characterType, const std::string &spriteType, const std::string &path)
{
    s_CharacterAssets[std::make_pair(characterType, spriteType)] = path;
}

bool PlayerCharacter::SwitchCharacter(CharacterType characterType)
{
    // Character type names for logging
    static const char *typeNames[] = {"BW1_MALE", "BW1_FEMALE", "BW2_MALE", "BW2_FEMALE", "CC_FEMALE"};
    int typeIdx = static_cast<int>(characterType);
    const char *typeName = (typeIdx >= 0 && typeIdx < 5) ? typeNames[typeIdx] : "UNKNOWN";

    // Lambda: Resolve asset path from registry
    auto getAssetPath = [characterType, typeName](const std::string &spriteType) -> std::string
    {
        auto key = std::make_pair(characterType, spriteType);
        auto it = s_CharacterAssets.find(key);
        if (it != s_CharacterAssets.end())
            return it->second;

        // Asset not registered
        std::cerr << "No asset registered for " << typeName << " " << spriteType << std::endl;
        return "";
    };

    // Lambda: Attempt load with fallback to parent directory
    auto tryLoad = [](Texture &target, const std::string &path) -> bool
    {
        if (path.empty())
            return false;

        if (target.LoadFromFile(path))
            return true;
        return target.LoadFromFile("../" + path); // Try parent directory
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

bool PlayerCharacter::CopyAppearanceFrom(const std::string &spritePath)
{
    // Load the NPC sprite sheet as all player sprites
    // NPC sprites use the same layout as player sprites
    bool walkLoaded = m_SpriteSheet.LoadFromFile(spritePath);
    if (!walkLoaded)
    {
        // Try parent directory
        walkLoaded = m_SpriteSheet.LoadFromFile("../" + spritePath);
    }

    if (!walkLoaded)
    {
        std::cerr << "Failed to copy appearance from: " << spritePath << std::endl;
        return false;
    }

    // Use the same sprite for running and bicycle modes
    if (!m_RunningSpriteSheet.LoadFromFile(spritePath))
    {
        m_RunningSpriteSheet.LoadFromFile("../" + spritePath);
    }

    if (!m_BicycleSpriteSheet.LoadFromFile(spritePath))
    {
        m_BicycleSpriteSheet.LoadFromFile("../" + spritePath);
    }

    m_IsUsingCopiedAppearance = true;
    std::cout << "Copied appearance from: " << spritePath << std::endl;
    return true;
}

void PlayerCharacter::RestoreOriginalAppearance()
{
    if (!m_IsUsingCopiedAppearance)
        return;

    // Reload original character sprites
    SwitchCharacter(m_CharacterType);
    m_IsUsingCopiedAppearance = false;
    std::cout << "Restored original appearance" << std::endl;
}

void PlayerCharacter::SetRunning(bool running) { m_IsRunning = running; }
void PlayerCharacter::SetBicycling(bool bicycling) { m_IsBicycling = bicycling; }

void PlayerCharacter::Update(float deltaTime)
{
    m_AnimationTime += deltaTime;

    // Running animation is 50% faster than walking
    float animSpeed = (m_AnimationType == AnimationType::RUN) ? ANIMATION_SPEED * 0.5f : ANIMATION_SPEED;

    if (m_AnimationTime >= animSpeed)
    {
        m_AnimationTime = 0.0f;

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

void PlayerCharacter::Render(IRenderer &renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    // Pass renderer's Y-flip requirement for correct sprite sheet row selection
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation BEFORE projection (moves sprite up on stairs)
    bottomCenter.y -= m_ElevationOffset;

    // Only use ProjectPoint if inside the expanded 3D viewport (prevents globe wrap-around artifacts)
    auto perspState = renderer.GetPerspectiveState();
    if (perspState.enabled)
    {
        // Calculate expanded viewport bounds for 3D mode
        float safeHorizonScale = std::max(perspState.horizonScale, 0.001f);
        float expansion = 1.0f / safeHorizonScale;
        float expandedWidth = perspState.viewWidth * expansion * 1.5f;
        float expandedHeight = perspState.viewHeight * expansion;
        float widthPadding = (expandedWidth - perspState.viewWidth) * 0.5f;
        float heightPadding = (expandedHeight - perspState.viewHeight) * 0.5f;

        if (bottomCenter.x >= -widthPadding && bottomCenter.x <= perspState.viewWidth + widthPadding &&
            bottomCenter.y >= -heightPadding && bottomCenter.y <= perspState.viewHeight + heightPadding)
        {
            // Position-only perspective: project the bottom-center through 3D transformation
            // This places the player at the correct depth in the scene without scaling the sprite
            bottomCenter = renderer.ProjectPoint(bottomCenter);
        }
    }

    // Convert from bottom-center to render position (top-left)
    glm::vec2 renderPos = bottomCenter - glm::vec2(SPRITE_WIDTH_F / 2.0f, SPRITE_HEIGHT_F);

    // Apply sprint visual offset (sprite leans into movement direction)
    // Applied after projection so it's in screen-space
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
    const Texture &sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Suspend perspective - we already projected the position, don't double-project
    renderer.SuspendPerspective(true);
    renderer.DrawSpriteRegion(sheet, renderPos, glm::vec2(SPRITE_WIDTH_F, SPRITE_HEIGHT_F),
                              spriteCoords, glm::vec2(SPRITE_WIDTH_F, SPRITE_HEIGHT_F), 0.0f, glm::vec3(1.0f), false);
    renderer.SuspendPerspective(false);
}

void PlayerCharacter::RenderBottomHalf(IRenderer &renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation to bottom-center BEFORE projection
    bottomCenter.y -= m_ElevationOffset;

    // Only use ProjectPoint if inside the expanded 3D viewport (prevents globe wrap-around artifacts)
    auto perspState = renderer.GetPerspectiveState();
    if (perspState.enabled)
    {
        // Calculate expanded viewport bounds for 3D mode
        float safeHorizonScale = std::max(perspState.horizonScale, 0.001f);
        float expansion = 1.0f / safeHorizonScale;
        float expandedWidth = perspState.viewWidth * expansion * 1.5f;
        float expandedHeight = perspState.viewHeight * expansion;
        float widthPadding = (expandedWidth - perspState.viewWidth) * 0.5f;
        float heightPadding = (expandedHeight - perspState.viewHeight) * 0.5f;

        if (bottomCenter.x >= -widthPadding && bottomCenter.x <= perspState.viewWidth + widthPadding &&
            bottomCenter.y >= -heightPadding && bottomCenter.y <= perspState.viewHeight + heightPadding)
        {
            bottomCenter = renderer.ProjectPoint(bottomCenter);
        }
    }

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
    const Texture &sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Bottom half: lower 16 pixels of the sprite
    // Render position is offset to show only the bottom half
    glm::vec2 bottomRenderPos = renderPos + glm::vec2(0.0f, SPRITE_HALF_HEIGHT);
    glm::vec2 bottomSpriteCoords = spriteCoords + glm::vec2(0.0f, SPRITE_HALF_HEIGHT);

    // Suspend perspective - we already projected the position, don't double-project
    renderer.SuspendPerspective(true);
    renderer.DrawSpriteRegion(sheet, bottomRenderPos, glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                              bottomSpriteCoords, glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT), 0.0f, glm::vec3(1.0f), false);
    renderer.SuspendPerspective(false);
}

void PlayerCharacter::RenderTopHalf(IRenderer &renderer, glm::vec2 cameraPos)
{
    // Get UV coordinates for current animation frame
    glm::vec2 spriteCoords = GetSpriteCoords(m_CurrentFrame, m_Direction, m_AnimationType, renderer.RequiresYFlip());

    // Screen-space bottom-center position
    glm::vec2 bottomCenter = m_Position - cameraPos;

    // Apply elevation to bottom-center BEFORE projection
    bottomCenter.y -= m_ElevationOffset;

    // Only use ProjectPoint if inside the expanded 3D viewport (prevents globe wrap-around artifacts)
    auto perspState = renderer.GetPerspectiveState();
    if (perspState.enabled)
    {
        // Calculate expanded viewport bounds for 3D mode
        float safeHorizonScale = std::max(perspState.horizonScale, 0.001f);
        float expansion = 1.0f / safeHorizonScale;
        float expandedWidth = perspState.viewWidth * expansion * 1.5f;
        float expandedHeight = perspState.viewHeight * expansion;
        float widthPadding = (expandedWidth - perspState.viewWidth) * 0.5f;
        float heightPadding = (expandedHeight - perspState.viewHeight) * 0.5f;

        if (bottomCenter.x >= -widthPadding && bottomCenter.x <= perspState.viewWidth + widthPadding &&
            bottomCenter.y >= -heightPadding && bottomCenter.y <= perspState.viewHeight + heightPadding)
        {
            bottomCenter = renderer.ProjectPoint(bottomCenter);
        }
    }

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
    const Texture &sheet = m_IsBicycling                             ? m_BicycleSpriteSheet
                           : (m_AnimationType == AnimationType::RUN) ? m_RunningSpriteSheet
                                                                     : m_SpriteSheet;

    // Top half: upper 16 pixels of the sprite (head/torso area)
    glm::vec2 topSpriteCoords = spriteCoords;

    // Suspend perspective - we already projected the position, don't double-project
    renderer.SuspendPerspective(true);
    renderer.DrawSpriteRegion(sheet, renderPos, glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT),
                              topSpriteCoords, glm::vec2(SPRITE_WIDTH_F, SPRITE_HALF_HEIGHT), 0.0f, glm::vec3(1.0f), false);
    renderer.SuspendPerspective(false);
}

float PlayerCharacter::CalculateFollowAlpha(float deltaTime, float settleTime, float epsilon)
{
    deltaTime = std::max(0.0f, deltaTime);
    settleTime = std::max(1e-5f, settleTime); // Prevent division by zero
    float alpha = 1.0f - std::pow(epsilon, deltaTime / settleTime);
    return std::clamp(alpha, 0.0f, 1.0f);
}

bool PlayerCharacter::CollidesWithNPC(const glm::vec2 &bottomCenterPos, const std::vector<glm::vec2> *npcPositions) const
{
    if (!npcPositions || npcPositions->empty())
        return false;

    // Hitbox dimensions
    constexpr float NPC_HALF_W = 8.0f; // 8 pixels from center (16px wide)
    constexpr float NPC_BOX_H = 16.0f; // 16 pixels up from bottomCenterPos

    // Epsilon-shrinking: Reduce AABB bounds by a small margin to prevent
    // false positives when entities are exactly touching but not overlapping.
    // This avoids "stuck" states from edge-on-edge collision detection.
    constexpr float EPS = 0.05f;

    // Calculate player AABB (shrunk by epsilon for tolerance)
    float playerMinX = bottomCenterPos.x - HALF_HITBOX_WIDTH + EPS;
    float playerMaxX = bottomCenterPos.x + HALF_HITBOX_WIDTH - EPS;
    float playerMaxY = bottomCenterPos.y - EPS;
    float playerMinY = bottomCenterPos.y - HITBOX_HEIGHT + EPS;

    // Test against each NPC
    for (const auto &npcbottomCenterPos : *npcPositions)
    {
        // Calculate NPC AABB (shrunk by epsilon)
        float npcMinX = npcbottomCenterPos.x - NPC_HALF_W + EPS;
        float npcMaxX = npcbottomCenterPos.x + NPC_HALF_W - EPS;
        float npcMaxY = npcbottomCenterPos.y - EPS;
        float npcMinY = npcbottomCenterPos.y - NPC_BOX_H + EPS;

        // AABB overlap test
        if (playerMinX < npcMaxX && playerMaxX > npcMinX &&
            playerMinY < npcMaxY && playerMaxY > npcMinY)
            return true;
    }
    return false;
}

bool PlayerCharacter::CollidesWithTilesCenter(const glm::vec2 &bottomCenterPos, const Tilemap *tilemap) const
{
    if (!tilemap)
        return false;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());

    // Small offset to avoid boundary ambiguity when standing exactly on tile edge
    constexpr float EPS = 0.05f;

    // Calculate hitbox center position
    glm::vec2 centerPos(bottomCenterPos.x, bottomCenterPos.y - HITBOX_HEIGHT * 0.5f);

    // Convert to tile coordinates
    int tileX = static_cast<int>(std::floor(centerPos.x / TILE_W));
    int tileY = static_cast<int>(std::floor((centerPos.y - EPS) / TILE_H));

    // Bounds check, out of map = no collision (allows walking off edge)
    if (tileX < 0 || tileY < 0 || tileX >= tilemap->GetMapWidth() || tileY >= tilemap->GetMapHeight())
        return false;

    return tilemap->GetTileCollision(tileX, tileY);
}

bool PlayerCharacter::CollidesWithTilesStrict(const glm::vec2 &bottomCenterPos, const Tilemap *tilemap,
                                              int moveDx, int moveDy, bool diagonalInput) const
{
    if (!tilemap)
        return false;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());
    constexpr float HALF_W = HITBOX_WIDTH * 0.5f;
    constexpr float BOX_H = HITBOX_HEIGHT;
    constexpr float EPS = 0.05f;
    constexpr float CORNER_OVERLAP_THRESHOLD = 0.20f;

    // Small overlaps with side walls are tolerated when moving along a corridor
    constexpr float SIDE_WALL_TOLERANCE = 0.15f;

    // Calculate player AABB bounds
    float minX = bottomCenterPos.x - HALF_W + EPS;
    float maxX = bottomCenterPos.x + HALF_W - EPS;
    float maxY = bottomCenterPos.y - EPS;
    float minY = bottomCenterPos.y - BOX_H + EPS;

    glm::vec2 hitboxCenter(bottomCenterPos.x, bottomCenterPos.y - BOX_H * 0.5f);
    // Calculate tile range that overlaps hitbox
    int tileX0 = static_cast<int>(std::floor(minX / TILE_W));
    int tileX1 = static_cast<int>(std::floor(maxX / TILE_W));
    int tileY0 = static_cast<int>(std::floor(minY / TILE_H));
    int tileY1 = static_cast<int>(std::floor(maxY / TILE_H));

    int playerTileX = static_cast<int>(std::floor(bottomCenterPos.x / TILE_W));
    int playerTileY = static_cast<int>(std::floor((bottomCenterPos.y - TILE_H * 0.5f - EPS) / TILE_H));

    auto inBounds = [&](int x, int y)
    {
        return x >= 0 && y >= 0 && x < tilemap->GetMapWidth() && y < tilemap->GetMapHeight();
    };

    auto tileBlocked = [&](int x, int y)
    {
        return !inBounds(x, y) || tilemap->GetTileCollision(x, y);
    };

    float hitboxArea = (maxX - minX) * (maxY - minY);

    for (int ty = tileY0; ty <= tileY1; ++ty)
    {
        for (int tx = tileX0; tx <= tileX1; ++tx)
        {
            if (!inBounds(tx, ty) || !tilemap->GetTileCollision(tx, ty))
                continue;

            float tileMinX = tx * TILE_W, tileMaxX = (tx + 1) * TILE_W;
            float tileMinY = ty * TILE_H, tileMaxY = (ty + 1) * TILE_H;

            float overlapW = std::max(0.0f, std::min(maxX, tileMaxX) - std::max(minX, tileMinX));
            float overlapH = std::max(0.0f, std::min(maxY, tileMaxY) - std::max(minY, tileMinY));
            float overlapRatio = (overlapW * overlapH) / hitboxArea;

            {
                bool cardinalMove = ((moveDx != 0) ^ (moveDy != 0)); // exactly one axis non-zero
                if (cardinalMove && !diagonalInput)
                {
                    int dxT = tx - playerTileX;
                    int dyT = ty - playerTileY;

                    // Only diagonally-adjacent tiles
                    if (std::abs(dxT) == 1 && std::abs(dyT) == 1)
                    {
                        // How deep we penetrated into the diagonal tile along the forward axis
                        float forwardPenetration = (moveDy != 0) ? overlapH : overlapW;

                        // Range knob: increase to shrink corner influence range more.
                        constexpr float DIAGONAL_CORNER_ACTIVATION_PX = 4.0f;

                        if (forwardPenetration < DIAGONAL_CORNER_ACTIVATION_PX)
                            continue; // too far -> don't let this diagonal corner tile influence us yet
                    }
                }
            }

            {
                const bool hasMotion = (moveDx != 0) || (moveDy != 0);
                if (hasMotion && !diagonalInput && overlapW > 0.0f && overlapH > 0.0f)
                {
                    float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
                    float tileCenterY = (tileMinY + tileMaxY) * 0.5f;

                    bool tileAbove = tileCenterY < hitboxCenter.y;
                    bool tileBelow = tileCenterY > hitboxCenter.y;
                    bool tileLeft = tileCenterX < hitboxCenter.x;
                    bool tileRight = tileCenterX > hitboxCenter.x;

                    // Determine which axis is the penetration axis
                    bool penetrationIsY = (overlapH <= overlapW);
                    float penetrationPx = penetrationIsY ? overlapH : overlapW;

                    constexpr float PASSIVE_PENETRATION_PX = 5.0f;

                    bool movingInto = false;
                    if (penetrationIsY)
                    {
                        // Y+ is down in our world
                        if (tileAbove)
                            movingInto = (moveDy < 0); // moving up into top wall
                        if (tileBelow)
                            movingInto = (moveDy > 0); // moving down into bottom wall
                        // moveDy == 0 is OK (sliding sideways while scraping)
                    }
                    else
                    {
                        if (tileLeft)
                            movingInto = (moveDx < 0); // moving left into left wall
                        if (tileRight)
                            movingInto = (moveDx > 0); // moving right into right wall
                        // moveDx == 0 is OK (sliding vertically while scraping)
                    }

                    constexpr float FACE_CONTACT_MIN_PX = 4.0f;
                    float faceOverlap = penetrationIsY ? overlapW : overlapH;

                    // Only allow passive tolerance when we're clearly alongside a wall face.
                    // Near corners, faceOverlap gets small -> do NOT suppress collision there.
                    if (!movingInto && penetrationPx <= PASSIVE_PENETRATION_PX && faceOverlap >= FACE_CONTACT_MIN_PX)
                        continue;
                }
            }

            // ========== Corner Cutting Algorithm ==========
            // Allows smooth movement around convex corners of collision tiles.
            //
            // A "true corner" exists when a blocking tile has two adjacent empty
            // tiles (e.g., empty above AND empty left = top-left corner exposed).
            //
            // When the player clips a true corner with small overlap (<20% hitbox),
            // we check if there's an "escape route" - open space perpendicular to
            // movement direction. If so, allow the overlap to enable smooth sliding.
            //
            // This prevents the "stuck on corners" problem common in tile-based games
            // while still preventing players from clipping through solid walls.
            // ================================================

            // Check adjacent tiles to identify exposed corners
            bool emptyAbove = !tileBlocked(tx, ty - 1);
            bool emptyBelow = !tileBlocked(tx, ty + 1);
            bool emptyLeft = !tileBlocked(tx - 1, ty);
            bool emptyRight = !tileBlocked(tx + 1, ty);

            // Check if corner cutting is blocked for each corner
            bool tlBlocked = tilemap->IsCornerCutBlocked(tx, ty, Tilemap::CORNER_TL);
            bool trBlocked = tilemap->IsCornerCutBlocked(tx, ty, Tilemap::CORNER_TR);
            bool blBlocked = tilemap->IsCornerCutBlocked(tx, ty, Tilemap::CORNER_BL);
            bool brBlocked = tilemap->IsCornerCutBlocked(tx, ty, Tilemap::CORNER_BR);

            bool isTopLeftCorner = emptyAbove && emptyLeft && !tlBlocked;
            bool isTopRightCorner = emptyAbove && emptyRight && !trBlocked;
            bool isBottomLeftCorner = emptyBelow && emptyLeft && !blBlocked;
            bool isBottomRightCorner = emptyBelow && emptyRight && !brBlocked;

            bool isTrueCorner = isTopLeftCorner || isTopRightCorner ||
                                isBottomLeftCorner || isBottomRightCorner;

            // When moving horizontally, tolerate small overlaps with tiles above/below
            // When moving vertically, tolerate small overlaps with tiles left/right
            // This prevents getting stuck in narrow corridors after corner cutting
            if (!isTrueCorner && overlapRatio <= SIDE_WALL_TOLERANCE && overlapRatio > 0.01f)
            {
                float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
                float tileCenterY = (tileMinY + tileMaxY) * 0.5f;

                bool tileIsAboveOrBelow = std::abs(hitboxCenter.y - tileCenterY) > std::abs(hitboxCenter.x - tileCenterX);
                bool tileIsLeftOrRight = !tileIsAboveOrBelow;

                // Moving horizontally and tile is above/below = side wall, tolerate
                if (moveDx != 0 && moveDy == 0 && tileIsAboveOrBelow)
                    continue;

                // Moving vertically and tile is left/right = side wall, tolerate
                if (moveDy != 0 && moveDx == 0 && tileIsLeftOrRight)
                    continue;
            }

            if (isTrueCorner)
            {
                float tileCenterX = (tileMinX + tileMaxX) * 0.5f;
                float tileCenterY = (tileMinY + tileMaxY) * 0.5f;

                // Deadzone around the tile center so 1px wobble doesn't flip the quadrant.
                // IMPORTANT: never allow both "left" and "right" to be true at the same time.
                constexpr float CORNER_QUAD_EPS = 4.0f;

                auto sideSign = [](float v, float eps) -> int
                {
                    if (v > eps)
                        return 1;
                    if (v < -eps)
                        return -1;
                    return 0; // near center
                };

                float dx = hitboxCenter.x - tileCenterX;
                float dy = hitboxCenter.y - tileCenterY;

                int sx = sideSign(dx, CORNER_QUAD_EPS);
                int sy = sideSign(dy, CORNER_QUAD_EPS);

                // Tie-break when we're near the center using movement direction (approach direction).
                // If we're moving right, we are approaching the blocking tile from the left, etc.
                if (sx == 0)
                {
                    if (moveDx > 0)
                        sx = -1;
                    else if (moveDx < 0)
                        sx = 1;
                }
                if (sy == 0)
                {
                    // Y+ is down in our world
                    if (moveDy > 0)
                        sy = -1; // moving down -> approaching from above
                    else if (moveDy < 0)
                        sy = 1; // moving up -> approaching from below
                }

                bool playerLeftOfTile = (sx < 0);
                bool playerRightOfTile = (sx > 0);
                bool playerAboveTile = (sy < 0);
                bool playerBelowTile = (sy > 0);

                // If both movement axes are pushing directly into blocked faces (solid rectangle corner),
                // do not allow corner cutting - force a collision so we slide instead of clipping through.
                bool movingIntoClosedCorner = diagonalInput &&
                                              ((moveDx > 0 && !emptyRight) || (moveDx < 0 && !emptyLeft)) &&
                                              ((moveDy > 0 && !emptyBelow) || (moveDy < 0 && !emptyAbove));
                if (movingIntoClosedCorner)
                    return true;

                bool canCutThisCorner = false;

                // Check if the escape route in the perpendicular direction is clear
                // by looking at adjacent tiles to the PLAYER, not to the collision tile

                auto hasEscapeRoute = [&](int escapeX, int escapeY) -> bool
                {
                    // Check if moving in the escape direction leads to open space
                    glm::vec2 escapePos = bottomCenterPos + glm::vec2(escapeX * TILE_W * 0.5f, escapeY * TILE_H * 0.5f);

                    float escMinX = escapePos.x - HALF_W + EPS;
                    float escMaxX = escapePos.x + HALF_W - EPS;
                    float escMaxY = escapePos.y - EPS;
                    float escMinY = escapePos.y - BOX_H + EPS;

                    int escTileX0 = static_cast<int>(std::floor(escMinX / TILE_W));
                    int escTileX1 = static_cast<int>(std::floor(escMaxX / TILE_W));
                    int escTileY0 = static_cast<int>(std::floor(escMinY / TILE_H));
                    int escTileY1 = static_cast<int>(std::floor(escMaxY / TILE_H));

                    for (int ety = escTileY0; ety <= escTileY1; ++ety)
                    {
                        for (int etx = escTileX0; etx <= escTileX1; ++etx)
                        {
                            if (tileBlocked(etx, ety))
                            {
                                // Check overlap at escape position
                                float etMinX = etx * TILE_W, etMaxX = (etx + 1) * TILE_W;
                                float etMinY = ety * TILE_H, etMaxY = (ety + 1) * TILE_H;

                                float eOverlapW = std::max(0.0f, std::min(escMaxX, etMaxX) - std::max(escMinX, etMinX));
                                float eOverlapH = std::max(0.0f, std::min(escMaxY, etMaxY) - std::max(escMinY, etMinY));

                                // Significant overlap at escape position = blocked
                                if (eOverlapW > 2.0f && eOverlapH > 2.0f)
                                    return false;
                            }
                        }
                    }
                    return true;
                };

                if (isTopLeftCorner && playerAboveTile && playerLeftOfTile)
                {
                    if (hasEscapeRoute(0, -1) || hasEscapeRoute(-1, 0))
                        canCutThisCorner = true;
                }
                if (isTopRightCorner && playerAboveTile && playerRightOfTile)
                {
                    if (hasEscapeRoute(0, -1) || hasEscapeRoute(1, 0))
                        canCutThisCorner = true;
                }
                if (isBottomLeftCorner && playerBelowTile && playerLeftOfTile)
                {
                    if (hasEscapeRoute(0, 1) || hasEscapeRoute(-1, 0))
                        canCutThisCorner = true;
                }
                if (isBottomRightCorner && playerBelowTile && playerRightOfTile)
                {
                    if (hasEscapeRoute(0, 1) || hasEscapeRoute(1, 0))
                        canCutThisCorner = true;
                }

                if (diagonalInput && !canCutThisCorner)
                {
                    if (isTopLeftCorner && playerAboveTile && playerLeftOfTile && moveDx > 0 && moveDy > 0)
                    {
                        if (hasEscapeRoute(0, -1) || hasEscapeRoute(-1, 0))
                            canCutThisCorner = true;
                    }
                    if (isTopRightCorner && playerAboveTile && playerRightOfTile && moveDx < 0 && moveDy > 0)
                    {
                        if (hasEscapeRoute(0, -1) || hasEscapeRoute(1, 0))
                            canCutThisCorner = true;
                    }
                    if (isBottomLeftCorner && playerBelowTile && playerLeftOfTile && moveDx > 0 && moveDy < 0)
                    {
                        if (hasEscapeRoute(0, 1) || hasEscapeRoute(-1, 0))
                            canCutThisCorner = true;
                    }
                    if (isBottomRightCorner && playerBelowTile && playerRightOfTile && moveDx < 0 && moveDy < 0)
                    {
                        if (hasEscapeRoute(0, 1) || hasEscapeRoute(1, 0))
                            canCutThisCorner = true;
                    }
                }

                if (canCutThisCorner)
                {
                    // For cardinal movement, judge "corner scrape" by perpendicular penetration (px),
                    // not by overlap area (which gets harsh when you're 1px off).
                    bool cardinalMove = ((moveDx != 0) ^ (moveDy != 0)) && !diagonalInput;
                    if (cardinalMove)
                    {
                        float perpPenPx = (moveDx != 0) ? overlapH : overlapW; // moving horizontal -> perp is Y overlap
                        constexpr float CORNER_PERP_PX = 4.0f;
                        if (perpPenPx <= CORNER_PERP_PX)
                            continue;
                    }

                    // Fallback for diagonal/etc.
                    if (overlapRatio <= CORNER_OVERLAP_THRESHOLD)
                        continue;
                }
            }

            if (overlapRatio > 0.01f)
                return true;
        }
    }
    return false;
}

bool PlayerCharacter::CollidesAt(const glm::vec2 &bottomCenterPos, const Tilemap *tilemap,
                                 const std::vector<glm::vec2> *npcPositions, bool sprintMode,
                                 int moveDx, int moveDy, bool diagonalInput) const
{
    bool tileCollision = false;

    if (sprintMode)
    {
        bool centerHit = CollidesWithTilesCenter(bottomCenterPos, tilemap);
        bool cornerPocket = diagonalInput && IsCornerPenetration(bottomCenterPos, tilemap);
        tileCollision = centerHit || cornerPocket;
    }
    else
    {
        tileCollision = CollidesWithTilesStrict(bottomCenterPos, tilemap, moveDx, moveDy, diagonalInput);
    }

    return tileCollision || CollidesWithNPC(bottomCenterPos, npcPositions);
}

bool PlayerCharacter::IsCornerPenetration(const glm::vec2 &bottomCenterPos, const Tilemap *tilemap) const
{
    if (!tilemap)
        return false;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());
    constexpr float EPS = COLLISION_EPS;

    float minX = bottomCenterPos.x - HALF_HITBOX_WIDTH + EPS;
    float maxX = bottomCenterPos.x + HALF_HITBOX_WIDTH - EPS;
    float maxY = bottomCenterPos.y - EPS;
    float minY = bottomCenterPos.y - HITBOX_HEIGHT + EPS;

    int tileX0 = static_cast<int>(std::floor(minX / TILE_W));
    int tileX1 = static_cast<int>(std::floor(maxX / TILE_W));
    int tileY0 = static_cast<int>(std::floor(minY / TILE_H));
    int tileY1 = static_cast<int>(std::floor(maxY / TILE_H));

    bool hasRowDiff = false;
    bool hasColDiff = false;
    int firstRow = std::numeric_limits<int>::max();
    int firstCol = std::numeric_limits<int>::max();
    bool foundAny = false;

    for (int ty = tileY0; ty <= tileY1; ++ty)
    {
        for (int tx = tileX0; tx <= tileX1; ++tx)
        {
            if (tx < 0 || ty < 0 || tx >= tilemap->GetMapWidth() || ty >= tilemap->GetMapHeight())
                continue;
            if (!tilemap->GetTileCollision(tx, ty))
                continue;

            float tileMinX = tx * TILE_W, tileMaxX = (tx + 1) * TILE_W;
            float tileMinY = ty * TILE_H, tileMaxY = (ty + 1) * TILE_H;

            float overlapW = std::min(maxX, tileMaxX) - std::max(minX, tileMinX);
            float overlapH = std::min(maxY, tileMaxY) - std::max(minY, tileMinY);

            if (overlapW <= 0.0f || overlapH <= 0.0f)
                continue;

            if (!foundAny)
            {
                firstRow = ty;
                firstCol = tx;
                foundAny = true;
            }
            else
            {
                if (ty != firstRow)
                    hasRowDiff = true;
                if (tx != firstCol)
                    hasColDiff = true;

                if (hasRowDiff && hasColDiff)
                    return true;
            }
        }
    }

    return hasRowDiff && hasColDiff;
}

glm::vec2 PlayerCharacter::ComputeSprintCornerEject(const Tilemap *tilemap,
                                                    const std::vector<glm::vec2> *npcPositions,
                                                    glm::vec2 normalizedDir) const
{
    if (!tilemap)
        return glm::vec2(0.0f);
    bool strictlyColliding = CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false);
    if (!strictlyColliding && !IsCornerPenetration(m_Position, tilemap))
        return glm::vec2(0.0f);

    if (glm::length(normalizedDir) < 0.001f)
        normalizedDir = glm::vec2(0.0f, -1.0f); // default bias upward to avoid zero

    // Search for the nearest offset (within a small radius) that is clear in STRICT mode.
    const int MAX_STEP = 8; // half a tile
    float bestScore = std::numeric_limits<float>::max();
    glm::vec2 bestOffset(0.0f);

    auto clearStrict = [&](const glm::vec2 &pos)
    {
        return !CollidesWithTilesStrict(pos, tilemap, 0, 0, false) &&
               !CollidesWithNPC(pos, npcPositions);
    };

    for (int dy = -MAX_STEP; dy <= MAX_STEP; ++dy)
    {
        for (int dx = -MAX_STEP; dx <= MAX_STEP; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            glm::vec2 offset(static_cast<float>(dx), static_cast<float>(dy));
            float dist2 = glm::dot(offset, offset);
            if (dist2 < 0.5f || dist2 > static_cast<float>(MAX_STEP * MAX_STEP))
                continue;

            glm::vec2 candidatePos = m_Position + offset;
            if (!clearStrict(candidatePos))
                continue;

            glm::vec2 offsetDir = glm::normalize(offset);
            // Prefer offsets that move against the incoming direction when sprinting into a wall.
            float forwardPenalty = std::max(0.0f, glm::dot(offsetDir, normalizedDir));
            float score = dist2 + forwardPenalty * 20.0f;

            if (score < bestScore)
            {
                bestScore = score;
                bestOffset = offset;
            }
        }
    }

    return bestOffset;
}

glm::vec2 PlayerCharacter::GetCornerSlideDirection(
    const glm::vec2 &testPos,
    const Tilemap *tilemap,
    int /*moveDirX*/,
    int /*moveDirY*/)
{
    if (!tilemap)
        return glm::vec2(0.0f);

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());

    auto signi = [](float v) -> int
    { return (v > 0.001f) ? 1 : (v < -0.001f) ? -1
                                              : 0; };

    glm::vec2 step = testPos - m_Position;
    bool horizontalPrimary = std::abs(step.x) >= std::abs(step.y);

    // Use a fixed 1-pixel forward probe distance for corner detection.
    // This makes detection frame-rate independent - we're just checking IF a path exists,
    // not how far we can move this frame. The actual movement distance is handled separately.
    float forwardSign = horizontalPrimary ? (step.x >= 0 ? 1.0f : -1.0f) : (step.y >= 0 ? 1.0f : -1.0f);
    glm::vec2 forward = horizontalPrimary ? glm::vec2(forwardSign, 0.0f)
                                          : glm::vec2(0.0f, forwardSign);

    auto tileBlocked = [&](int tx, int ty) -> bool
    {
        if (tx < 0 || ty < 0 || tx >= tilemap->GetMapWidth() || ty >= tilemap->GetMapHeight())
            return true;
        return tilemap->GetTileCollision(tx, ty);
    };

    // === Detect corner type based on the CLOSEST ACTUAL CORNER to player's center ===
    // For multi-tile walls, only the END tiles are corners - middle tiles have no perpendicular opening
    // We need to find the nearest tile that actually has a corner (perpendicular opening)
    bool cornerEmptyAbove = false;
    bool cornerEmptyBelow = false;
    bool cornerEmptyLeft = false;
    bool cornerEmptyRight = false;
    {
        float hitboxCenterX = testPos.x;
        float hitboxCenterY = testPos.y - HITBOX_HEIGHT * 0.5f;

        int forwardTileX, forwardTileY;
        int bestTileX = 0, bestTileY = 0;
        float bestCornerDist = std::numeric_limits<float>::max();
        bool foundAnyCorner = false;
        bool foundAnyBlocked = false;

        if (horizontalPrimary)
        {
            // Moving horizontally - find the closest CORNER tile in the forward column
            forwardTileX = (step.x < 0)
                               ? static_cast<int>(std::floor((testPos.x - HALF_HITBOX_WIDTH) / TILE_W))
                               : static_cast<int>(std::floor((testPos.x + HALF_HITBOX_WIDTH) / TILE_W));

            int hitboxTopTileY = static_cast<int>(std::floor((testPos.y - HITBOX_HEIGHT) / TILE_H));
            int hitboxBottomTileY = static_cast<int>(std::floor((testPos.y - 0.01f) / TILE_H));

            for (int ty = hitboxTopTileY; ty <= hitboxBottomTileY; ++ty)
            {
                if (!tileBlocked(forwardTileX, ty))
                    continue;
                foundAnyBlocked = true;

                // Check if this tile has a perpendicular opening (is a corner)
                bool hasOpenAbove = !tileBlocked(forwardTileX, ty - 1);
                bool hasOpenBelow = !tileBlocked(forwardTileX, ty + 1);
                if (!hasOpenAbove && !hasOpenBelow)
                    continue; // This is a middle wall tile, not a corner

                foundAnyCorner = true;
                float tileCenterY = (ty + 0.5f) * TILE_H;
                float dist = std::abs(hitboxCenterY - tileCenterY);
                if (dist < bestCornerDist)
                {
                    bestCornerDist = dist;
                    bestTileX = forwardTileX;
                    bestTileY = ty;
                }
            }
        }
        else
        {
            // Moving vertically - find the closest CORNER tile in the forward row
            forwardTileY = (step.y < 0)
                               ? static_cast<int>(std::floor((testPos.y - HITBOX_HEIGHT) / TILE_H))
                               : static_cast<int>(std::floor(testPos.y / TILE_H));

            int hitboxLeftTileX = static_cast<int>(std::floor((testPos.x - HALF_HITBOX_WIDTH) / TILE_W));
            int hitboxRightTileX = static_cast<int>(std::floor((testPos.x + HALF_HITBOX_WIDTH - 0.01f) / TILE_W));

            for (int tx = hitboxLeftTileX; tx <= hitboxRightTileX; ++tx)
            {
                if (!tileBlocked(tx, forwardTileY))
                    continue;
                foundAnyBlocked = true;

                // Check if this tile has a perpendicular opening (is a corner)
                bool hasOpenLeft = !tileBlocked(tx - 1, forwardTileY);
                bool hasOpenRight = !tileBlocked(tx + 1, forwardTileY);
                if (!hasOpenLeft && !hasOpenRight)
                    continue; // This is a middle wall tile, not a corner

                foundAnyCorner = true;
                float tileCenterX = (tx + 0.5f) * TILE_W;
                float dist = std::abs(hitboxCenterX - tileCenterX);
                if (dist < bestCornerDist)
                {
                    bestCornerDist = dist;
                    bestTileX = tx;
                    bestTileY = forwardTileY;
                }
            }
        }

        if (!foundAnyBlocked)
        {
            // No blocked tiles - shouldn't happen, but just in case
            return glm::vec2(0.0f);
        }

        if (!foundAnyCorner)
        {
            // All blocked tiles are middle wall tiles with no perpendicular openings
            // This is a flat wall - don't slide
            if (m_SlideCommitTimer <= 0.0f)
                m_SlideHysteresisDir = glm::vec2(0.0f);
            return glm::vec2(0.0f);
        }

        // Don't slide if the closest corner is too far away
        // This prevents pulling toward distant corners when facing the middle of a long wall
        float maxCornerDist = horizontalPrimary ? (TILE_H * 0.75f) : (TILE_W * 0.75f);
        if (bestCornerDist > maxCornerDist)
        {
            if (m_SlideCommitTimer <= 0.0f)
                m_SlideHysteresisDir = glm::vec2(0.0f);
            return glm::vec2(0.0f);
        }

        // Use ONLY the closest corner tile's info
        bool emptyAbove = !tileBlocked(bestTileX, bestTileY - 1);
        bool emptyBelow = !tileBlocked(bestTileX, bestTileY + 1);
        bool emptyLeft = !tileBlocked(bestTileX - 1, bestTileY);
        bool emptyRight = !tileBlocked(bestTileX + 1, bestTileY);

        cornerEmptyAbove = emptyAbove;
        cornerEmptyBelow = emptyBelow;
        cornerEmptyLeft = emptyLeft;
        cornerEmptyRight = emptyRight;
    }

    // IMPORTANT: do NOT call CollidesWithTilesStrict with (0,0) here,
    // or your SIDE_WALL_TOLERANCE never runs.
    auto hardTileBlocked = [&](const glm::vec2 &p, int dx, int dy) -> bool
    {
        return CollidesWithTilesStrict(p, tilemap, dx, dy, /*diagonalInput*/ false);
    };

    struct Eval
    {
        glm::vec2 dir{0.0f};
        bool canForward = false;
        bool canSlideOnly = false;
        float bestMag = std::numeric_limits<float>::infinity();
    };

    // Limit probe distance to prevent sliding toward distant corners
    // Only probe a short distance (about half a tile) to find nearby corners
    constexpr float MAX_PROBE = 10.0f;

    auto evalDir = [&](const glm::vec2 &dir, float maxProbe) -> Eval
    {
        Eval e;
        e.dir = dir;

        int sdx = signi(dir.x);
        int sdy = signi(dir.y);
        int fdx = signi(forward.x);
        int fdy = signi(forward.y);

        for (int magInt = 1; magInt <= static_cast<int>(maxProbe); ++magInt)
        {
            float mag = static_cast<float>(magInt);
            glm::vec2 offset = dir * mag;

            // slide step must be safe
            if (hardTileBlocked(m_Position + offset, sdx, sdy))
                continue;

            e.canSlideOnly = true;

            // slide + forward must be safe
            if (!hardTileBlocked(m_Position + offset + forward, fdx, fdy))
            {
                e.canForward = true;
                e.bestMag = mag;
                break;
            }
        }
        return e;
    };

    glm::vec2 dNeg, dPos;
    if (horizontalPrimary)
    {
        dNeg = {0.0f, -1.0f};
        dPos = {0.0f, 1.0f};
    }
    else
    {
        dNeg = {-1.0f, 0.0f};
        dPos = {1.0f, 0.0f};
    }

    // Check if only ONE direction is geometrically valid
    bool bothDirectionsOpen = horizontalPrimary
                                  ? (cornerEmptyAbove && cornerEmptyBelow)
                                  : (cornerEmptyLeft && cornerEmptyRight);

    // Calculate player's offset from wall center to use as tiebreaker
    float playerOffset = 0.0f; // Negative = toward dNeg, Positive = toward dPos
    {
        float hitboxCenterY = testPos.y - HITBOX_HEIGHT * 0.5f;
        int wallTileX, wallTileY;

        if (horizontalPrimary)
        {
            wallTileX = (step.x < 0)
                            ? static_cast<int>(std::floor((testPos.x - HALF_HITBOX_WIDTH) / TILE_W))
                            : static_cast<int>(std::floor((testPos.x + HALF_HITBOX_WIDTH) / TILE_W));
            wallTileY = static_cast<int>(std::floor(hitboxCenterY / TILE_H));
            float wallCenterY = (wallTileY + 0.5f) * TILE_H;
            playerOffset = hitboxCenterY - wallCenterY; // Negative = above center (toward dNeg/up)
        }
        else
        {
            wallTileY = (step.y < 0)
                            ? static_cast<int>(std::floor((testPos.y - HITBOX_HEIGHT) / TILE_H))
                            : static_cast<int>(std::floor(testPos.y / TILE_H));
            wallTileX = static_cast<int>(std::floor(testPos.x / TILE_W));
            float wallCenterX = (wallTileX + 0.5f) * TILE_W;
            playerOffset = testPos.x - wallCenterX; // Negative = left of center (toward dNeg/left)
        }
    }

    // Priority order:
    // 1. If only ONE direction leads to open space, use it (no choice)
    // 2. If BOTH directions are open, use player's offset from wall center
    // 3. Then hysteresis/last input as tiebreaker
    // 4. Counter-clockwise as final fallback
    auto preferredFirst = [&]() -> std::array<glm::vec2, 2>
    {
        if (horizontalPrimary)
        {
            // dNeg = up (y=-1), dPos = down (y=+1)
            if (cornerEmptyAbove && !cornerEmptyBelow)
                return {dNeg, dPos};
            if (cornerEmptyBelow && !cornerEmptyAbove)
                return {dPos, dNeg};

            // Both directions open - use player offset as tiebreaker only if significantly off-center
            // Threshold of 4.0 pixels ensures minor hitbox misalignment doesn't pull toward corners
            if (playerOffset < -4.0f)
                return {dNeg, dPos}; // Player above center -> prefer up
            if (playerOffset > 4.0f)
                return {dPos, dNeg}; // Player below center -> prefer down

            // Nearly centered - use hysteresis/last input
            if (m_SlideHysteresisDir.y < 0.0f)
                return {dNeg, dPos};
            if (m_SlideHysteresisDir.y > 0.0f)
                return {dPos, dNeg};
            if (m_LastInputY < 0)
                return {dNeg, dPos};
            if (m_LastInputY > 0)
                return {dPos, dNeg};

            // Counter-clockwise as last resort
            if (forward.x > 0.0f)
                return {dNeg, dPos};
            else
                return {dPos, dNeg};
        }
        else
        {
            // dNeg = left (x=-1), dPos = right (x=+1)
            if (cornerEmptyLeft && !cornerEmptyRight)
                return {dNeg, dPos};
            if (cornerEmptyRight && !cornerEmptyLeft)
                return {dPos, dNeg};

            // Both directions open - use player offset as tiebreaker only if significantly off-center
            // Threshold of 4.0 pixels ensures minor hitbox misalignment doesn't pull toward corners
            if (playerOffset < -4.0f)
                return {dNeg, dPos}; // Player left of center -> prefer left
            if (playerOffset > 4.0f)
                return {dPos, dNeg}; // Player right of center -> prefer right

            // Nearly centered - use hysteresis/last input
            if (m_SlideHysteresisDir.x < 0.0f)
                return {dNeg, dPos};
            if (m_SlideHysteresisDir.x > 0.0f)
                return {dPos, dNeg};
            if (m_LastInputX < 0)
                return {dNeg, dPos};
            if (m_LastInputX > 0)
                return {dPos, dNeg};

            // Counter-clockwise as last resort
            if (forward.y > 0.0f)
                return {dPos, dNeg};
            else
                return {dNeg, dPos};
        }
    };

    auto dirs = preferredFirst();

    Eval a = evalDir(dirs[0], MAX_PROBE);

    // Only evaluate second direction if BOTH directions are geometrically open
    Eval b;
    if (bothDirectionsOpen)
    {
        b = evalDir(dirs[1], MAX_PROBE);
    }

    auto pick = [&](const Eval &e1, const Eval &e2) -> glm::vec2
    {
        // If only one direction was geometrically valid, only consider e1
        if (!bothDirectionsOpen)
        {
            if (e1.canForward)
                return e1.dir;
            if (e1.canSlideOnly)
                return e1.dir;
            return glm::vec2(0.0f); // Preferred direction failed -> stop
        }

        // Both directions geometrically valid
        if (e1.canForward && !e2.canForward)
            return e1.dir;
        if (e2.canForward && !e1.canForward)
            return e2.dir;

        if (e1.canForward && e2.canForward)
        {
            // Both work - prefer the one matching player's offset (e1 is already preferred)
            return e1.dir;
        }

        if (e1.canSlideOnly && !e2.canSlideOnly)
            return e1.dir;
        if (e2.canSlideOnly && !e1.canSlideOnly)
            return e2.dir;

        return glm::vec2(0.0f);
    };

    glm::vec2 chosen = pick(a, b);

    // Check if corner cutting is blocked for the corner we would be sliding around
    // We need to find the blocking tile and determine which corner would be cut
    if (glm::length(chosen) > 0.001f)
    {
        // Find the blocking tile we're sliding around
        float hitboxCenterY = testPos.y - HITBOX_HEIGHT * 0.5f;
        int blockTileX, blockTileY;

        if (horizontalPrimary)
        {
            // Moving horizontally - blocking tile is in the forward column
            blockTileX = (step.x < 0)
                             ? static_cast<int>(std::floor((testPos.x - HALF_HITBOX_WIDTH) / TILE_W))
                             : static_cast<int>(std::floor((testPos.x + HALF_HITBOX_WIDTH) / TILE_W));
            // Find the tile we're actually sliding around (closest to hitbox center)
            int hitboxTopTileY = static_cast<int>(std::floor((testPos.y - HITBOX_HEIGHT) / TILE_H));
            int hitboxBottomTileY = static_cast<int>(std::floor((testPos.y - 0.01f) / TILE_H));
            blockTileY = hitboxTopTileY;
            float bestDist = std::numeric_limits<float>::max();
            for (int ty = hitboxTopTileY; ty <= hitboxBottomTileY; ++ty)
            {
                if (tileBlocked(blockTileX, ty))
                {
                    float tileCenterY = (ty + 0.5f) * TILE_H;
                    float dist = std::abs(hitboxCenterY - tileCenterY);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        blockTileY = ty;
                    }
                }
            }
        }
        else
        {
            // Moving vertically - blocking tile is in the forward row
            blockTileY = (step.y < 0)
                             ? static_cast<int>(std::floor((testPos.y - HITBOX_HEIGHT) / TILE_H))
                             : static_cast<int>(std::floor(testPos.y / TILE_H));
            int hitboxLeftTileX = static_cast<int>(std::floor((testPos.x - HALF_HITBOX_WIDTH) / TILE_W));
            int hitboxRightTileX = static_cast<int>(std::floor((testPos.x + HALF_HITBOX_WIDTH - 0.01f) / TILE_W));
            blockTileX = hitboxLeftTileX;
            float bestDist = std::numeric_limits<float>::max();
            for (int tx = hitboxLeftTileX; tx <= hitboxRightTileX; ++tx)
            {
                if (tileBlocked(tx, blockTileY))
                {
                    float tileCenterX = (tx + 0.5f) * TILE_W;
                    float dist = std::abs(testPos.x - tileCenterX);
                    if (dist < bestDist)
                    {
                        bestDist = dist;
                        blockTileX = tx;
                    }
                }
            }
        }

        // Determine which corner would be cut based on forward and slide directions
        // The corner is on the side the player approaches from, in the direction they slide
        // Example: Moving RIGHT (from left) and sliding UP -> top-left corner (TL)
        Tilemap::Corner cornerToCut;
        if (horizontalPrimary)
        {
            // Moving horizontally
            if (forward.x > 0)
            {
                // Moving RIGHT into tile (approaching from left side)
                cornerToCut = (chosen.y < 0) ? Tilemap::CORNER_TL : Tilemap::CORNER_BL;
            }
            else
            {
                // Moving LEFT into tile (approaching from right side)
                cornerToCut = (chosen.y < 0) ? Tilemap::CORNER_TR : Tilemap::CORNER_BR;
            }
        }
        else
        {
            // Moving vertically
            if (forward.y > 0)
            {
                // Moving DOWN into tile (approaching from top)
                cornerToCut = (chosen.x < 0) ? Tilemap::CORNER_TL : Tilemap::CORNER_TR;
            }
            else
            {
                // Moving UP into tile (approaching from bottom)
                cornerToCut = (chosen.x < 0) ? Tilemap::CORNER_BL : Tilemap::CORNER_BR;
            }
        }

        // Check if this corner has cutting blocked
        if (tilemap->IsCornerCutBlocked(blockTileX, blockTileY, cornerToCut))
        {
            // Corner cutting is blocked - don't slide
            return glm::vec2(0.0f);
        }
    }

    // Update hysteresis and commit timer
    // Only set commit timer when direction actually changes to a new non-zero direction
    if (glm::length(chosen) > 0.001f)
    {
        if (glm::length(m_SlideHysteresisDir) < 0.001f ||
            glm::dot(chosen, m_SlideHysteresisDir) < 0.5f) // Direction changed significantly
        {
            m_SlideCommitTimer = 0.12f; // Commit to this direction for 120ms
        }
        m_SlideHysteresisDir = chosen;
    }

    return chosen;
}

glm::vec2 PlayerCharacter::FindClosestSafeTileCenter(const Tilemap *tilemap,
                                                     const std::vector<glm::vec2> *npcPositions) const
{
    if (!tilemap)
        return m_Position;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());

    int baseTileX = static_cast<int>(std::floor(m_Position.x / TILE_W));
    int baseTileY = static_cast<int>(std::floor((m_Position.y - TILE_H * 0.5f) / TILE_H));

    float bestDist2 = std::numeric_limits<float>::infinity();
    glm::vec2 bestCenter = m_Position;

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            int tx = baseTileX + dx;
            int ty = baseTileY + dy;

            if (tx < 0 || ty < 0 || tx >= tilemap->GetMapWidth() || ty >= tilemap->GetMapHeight())
                continue;

            glm::vec2 bottomCenterPos(tx * TILE_W + TILE_W * 0.5f, ty * TILE_H + TILE_H);

            if (!CollidesWithTilesStrict(bottomCenterPos, tilemap, 0, 0, false) &&
                !CollidesWithNPC(bottomCenterPos, npcPositions))
            {
                float dist2 = glm::dot(bottomCenterPos - m_Position, bottomCenterPos - m_Position);
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestCenter = bottomCenterPos;
                }
            }
        }
    }
    return bestCenter;
}

void PlayerCharacter::Move(glm::vec2 direction, float deltaTime, const Tilemap *tilemap,
                           const std::vector<glm::vec2> *npcPositions)
{
    // === No input: handle idle state ===
    if (glm::length(direction) < 0.1f)
    {
        HandleIdleSnap(deltaTime, tilemap, npcPositions);
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
        return (v > dz) ? 1 : (v < -dz) ? -1
                                        : 0;
    };
    auto signStep = [](float v) -> int
    { return (v > 1e-4f) ? 1 : (v < -1e-4f) ? -1
                                            : 0; };
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
    AnimationType targetAnim = (m_IsRunning || m_IsBicycling) ? AnimationType::RUN : AnimationType::WALK;
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
        if (!CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false))
            m_LastSafeTileCenter = GetCurrentTileCenter(static_cast<float>(tilemap->GetTileWidth()));

        // Try full movement first
        glm::vec2 testPos = m_Position + desiredMovement;
        bool npcBlocked = CollidesWithNPC(testPos, npcPositions);
        bool tileBlocked = sprintMode
                               ? CollidesWithTilesCenter(testPos, tilemap)
                               : CollidesWithTilesStrict(testPos, tilemap, moveDx, moveDy, diagonalInput);
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
            glm::vec2 slideMovement = TrySlideMovement(
                desiredMovement, normalizedDir, deltaTime, currentSpeed,
                tilemap, npcPositions, sprintMode, moveDx, moveDy, diagonalInput);

            if (glm::length(slideMovement) > 0.001f)
            {
                desiredMovement = slideMovement;
                didCornerSlide = true;
                tileBlocked = false; // we produced a non-blocked alternative
            }
            else
            {
                // 2) Only if slide solver found nothing: fall back to axis-separated movement for diagonal input
                if (diagonalInput)
                {
                    glm::vec2 moveX(desiredMovement.x, 0.0f);
                    glm::vec2 moveY(0.0f, desiredMovement.y);

                    bool okX = !CollidesAt(m_Position + moveX, tilemap, npcPositions, sprintMode, moveDx, 0, false);
                    bool okY = !CollidesAt(m_Position + moveY, tilemap, npcPositions, sprintMode, 0, moveDy, false);

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
            desiredMovement = ApplyLaneSnapping(
                desiredMovement, normalizedDir, deltaTime,
                tilemap, npcPositions, sprintMode, effDx, effDy);
        }

        // Final collision check
        if (CollidesAt(m_Position + desiredMovement, tilemap, npcPositions, sprintMode, effDx, effDy, effDiagonal))
        {
            // Try axis-separated movement
            glm::vec2 tryX = m_Position + glm::vec2(desiredMovement.x, 0.0f);
            glm::vec2 tryY = m_Position + glm::vec2(0.0f, desiredMovement.y);

            bool okX = !CollidesAt(tryX, tilemap, npcPositions, sprintMode, moveDx, 0, false);
            bool okY = !CollidesAt(tryY, tilemap, npcPositions, sprintMode, 0, moveDy, false);

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
                        m_AxisCommitTimer = 0.15f; // Commit for 150ms
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
                    if (!CollidesAt(tryPos, tilemap, npcPositions, sprintMode, finalDx, finalDy, finalDiag))
                        lo = mid;
                    else
                        hi = mid;
                }

                desiredMovement = dir * lo;
            }
        }

        // If sprint center-collision left us wedged in a corner pocket, shove out using strict collision.
        // High FPS fix: Also check when current position OR target position has corner penetration,
        // not just when movement is zero. At high FPS, small movements pass center collision repeatedly,
        // letting the player creep into corner pockets where hitbox edges overlap tiles but center doesn't.
        if (sprintMode && diagonalInput)
        {
            glm::vec2 targetPos = m_Position + desiredMovement;
            bool currentlyStuck = IsCornerPenetration(m_Position, tilemap) ||
                                  CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false);
            bool wouldBeStuck = IsCornerPenetration(targetPos, tilemap);

            if (glm::length(desiredMovement) < 0.001f || currentlyStuck || wouldBeStuck)
            {
                glm::vec2 cornerEject = ComputeSprintCornerEject(tilemap, npcPositions, normalizedDir);
                if (glm::length(cornerEject) > 0.001f)
                    desiredMovement = cornerEject;
            }
        }

        if (glm::length(desiredMovement) > 0.001f)
            m_LastMovementDirection = glm::normalize(desiredMovement);
    }

    m_Position += desiredMovement;
}

glm::vec2 PlayerCharacter::TrySlideMovement(glm::vec2 desiredMovement, glm::vec2 normalizedDir,
                                            float deltaTime, float currentSpeed,
                                            const Tilemap *tilemap, const std::vector<glm::vec2> *npcPositions,
                                            bool sprintMode, int moveDx, int moveDy, bool diagonalInput)
{
    // When sprinting and cutting corners diagonally, use strict collision to avoid over-lenient center checks
    bool slideSprintMode = (sprintMode && diagonalInput) ? false : sprintMode;
    const float maxSlide = currentSpeed * deltaTime;

    // Test if desired movement is already valid
    glm::vec2 testPos = m_Position + desiredMovement;

    if (!CollidesAt(testPos, tilemap, npcPositions, slideSprintMode, moveDx, moveDy, diagonalInput) &&
        !CollidesWithNPC(testPos, npcPositions))
    {
        // Only reset hysteresis if commit timer expired (prevents jitter at corners)
        if (m_SlideCommitTimer <= 0.0f)
            m_SlideHysteresisDir = glm::vec2(0.0f);
        return desiredMovement; // No collision - use original movement
    }

    // NPC collision: don't slide, just stop
    if (CollidesWithNPC(testPos, npcPositions))
    {
        m_SlideHysteresisDir = glm::vec2(0.0f);
        m_SlideCommitTimer = 0.0f;
        return glm::vec2(0.0f);
    }

    // Tile collision: find slide direction away from obstacle
    glm::vec2 slideDir = GetCornerSlideDirection(testPos, tilemap, moveDx, moveDy);

    if (glm::length(slideDir) < 0.001f)
    {
        // Only reset hysteresis if commit timer expired
        if (m_SlideCommitTimer <= 0.0f)
            m_SlideHysteresisDir = glm::vec2(0.0f);
        return glm::vec2(0.0f); // No valid slide direction
    }
    /*
    // Determine primary movement axis
    bool horizontalPrimary = std::abs(desiredMovement.x) > std::abs(desiredMovement.y);

    // Try increasing slide amounts until we find a valid position
    for (float slideAmount = 1.0f; slideAmount <= 16.0f; slideAmount += 1.0f)
    {
        glm::vec2 slideOffset;
        glm::vec2 forwardMove;

        if (horizontalPrimary)
        {
            slideOffset = glm::vec2(0.0f, slideDir.y * slideAmount);
            forwardMove = glm::vec2(desiredMovement.x, 0.0f);
        }
        else
        {
            slideOffset = glm::vec2(slideDir.x * slideAmount, 0.0f);
            forwardMove = glm::vec2(0.0f, desiredMovement.y);
        }

        // Test slide + full forward movement
        glm::vec2 testSlideForward = m_Position + slideOffset + forwardMove;
        if (!CollidesAt(testSlideForward, tilemap, npcPositions, sprintMode, moveDx, moveDy, false) &&
            !CollidesWithNPC(testSlideForward, npcPositions))
        {
            // Found valid slide - clamp to speed limit
            float clampedSlide = std::min(slideAmount, maxSlide);
            glm::vec2 clampedOffset;
            if (horizontalPrimary)
                clampedOffset = glm::vec2(0.0f, slideDir.y * clampedSlide);
            else
                clampedOffset = glm::vec2(slideDir.x * clampedSlide, 0.0f);

            // After computing clampedOffset
            if (CollidesAt(m_Position + clampedOffset, tilemap, npcPositions, sprintMode,
                (int)slideDir.x, (int)slideDir.y, false))
            {
            continue; // clamped step isn't safe; try a different slideAmount
            }

            // Binary search for maximum forward progress
            float lo = 0.0f, hi = 1.0f;
            for (int i = 0; i < 8; ++i)
            {
                float mid = (lo + hi) * 0.5f;
                glm::vec2 tryPos = m_Position + clampedOffset + forwardMove * mid;
                if (!CollidesAt(tryPos, tilemap, npcPositions, sprintMode, moveDx, moveDy, false))
                    lo = mid;
                else
                    hi = mid;
            }

            return clampedOffset + forwardMove * lo;
        }

        // Test slide only (no forward progress this frame)
        glm::vec2 testSlideOnly = m_Position + slideOffset;
        if (!CollidesAt(testSlideOnly, tilemap, npcPositions, sprintMode,
                        (int)slideDir.x, (int)slideDir.y, false))
        {
            float clampedSlide = std::min(slideAmount, maxSlide);
            if (horizontalPrimary)
                return glm::vec2(0.0f, slideDir.y * clampedSlide);
            else
                return glm::vec2(slideDir.x * clampedSlide, 0.0f);
        }
    }

    m_SlideHysteresisDir = glm::vec2(0.0f);
    return glm::vec2(0.0f);  // No valid slide found
    */
    slideDir = GetCornerSlideDirection(testPos, tilemap, moveDx, moveDy);
    if (glm::length(slideDir) < 0.001f)
        return glm::vec2(0.0f);

    auto attemptDir = [&](glm::vec2 dir) -> glm::vec2
    {
        bool horizontalPrimary = std::abs(desiredMovement.x) > std::abs(desiredMovement.y);

        // Use fixed 1-pixel forward probe for detection (frame-rate independent)
        // The actual movement uses desiredMovement which is frame-rate dependent
        glm::vec2 forwardProbe = horizontalPrimary
                                     ? glm::vec2(desiredMovement.x >= 0 ? 1.0f : -1.0f, 0.0f)
                                     : glm::vec2(0.0f, desiredMovement.y >= 0 ? 1.0f : -1.0f);

        glm::vec2 forwardMove = horizontalPrimary
                                    ? glm::vec2(desiredMovement.x, 0.0f)
                                    : glm::vec2(0.0f, desiredMovement.y);

        for (int slideAmountInt = 1; slideAmountInt <= 16; ++slideAmountInt)
        {
            float slideAmount = static_cast<float>(slideAmountInt);
            glm::vec2 slideOffset = horizontalPrimary
                                        ? glm::vec2(0.0f, dir.y * slideAmount)
                                        : glm::vec2(dir.x * slideAmount, 0.0f);

            // Use fixed 1-pixel probe for DETECTION of valid corner path
            glm::vec2 testSlideForward = m_Position + slideOffset + forwardProbe;

            if (!CollidesAt(testSlideForward, tilemap, npcPositions, slideSprintMode, moveDx, moveDy, diagonalInput))
            {
                float clampedSlide = std::min(slideAmount, maxSlide);
                glm::vec2 clampedOffset = horizontalPrimary
                                              ? glm::vec2(0.0f, dir.y * clampedSlide)
                                              : glm::vec2(dir.x * clampedSlide, 0.0f);

                // must be safe to apply the perpendicular step
                if (CollidesAt(m_Position + clampedOffset, tilemap, npcPositions, slideSprintMode,
                               (int)dir.x, (int)dir.y, diagonalInput))
                    continue;

                // Limit perpendicular shove so it doesn't exceed 75% of forward distance (prevents violent kicks)
                float forwardMag = glm::length(forwardMove);
                float perpMag = glm::length(clampedOffset);
                if (forwardMag > 0.001f && perpMag > forwardMag * 0.75f)
                {
                    clampedOffset *= (forwardMag * 0.75f) / perpMag;
                }

                float lo = 0.0f, hi = 1.0f;
                for (int i = 0; i < 8; ++i)
                {
                    float mid = (lo + hi) * 0.5f;
                    glm::vec2 tryPos = m_Position + clampedOffset + forwardMove * mid;
                    if (!CollidesAt(tryPos, tilemap, npcPositions, slideSprintMode, moveDx, moveDy, diagonalInput))
                        lo = mid;
                    else
                        hi = mid;
                }

                glm::vec2 slideResult = clampedOffset + forwardMove * lo;

                // Blend toward the original desired movement to smooth the direction change,
                // but only keep it if still collision-free.
                constexpr float SLIDE_BLEND = 0.35f;
                glm::vec2 blended = glm::mix(desiredMovement, slideResult, SLIDE_BLEND);
                if (!CollidesAt(m_Position + blended, tilemap, npcPositions, slideSprintMode, moveDx, moveDy, diagonalInput))
                    return blended;

                return slideResult;
            }

            glm::vec2 testSlideOnly = m_Position + slideOffset;
            if (!CollidesAt(testSlideOnly, tilemap, npcPositions, slideSprintMode, (int)dir.x, (int)dir.y, diagonalInput))
            {
                float clampedSlide = std::min(slideAmount, maxSlide);
                return horizontalPrimary
                           ? glm::vec2(0.0f, dir.y * clampedSlide)
                           : glm::vec2(dir.x * clampedSlide, 0.0f);
            }
        }

        return glm::vec2(0.0f);
    };

    glm::vec2 r = attemptDir(slideDir);
    if (glm::length(r) > 0.001f)
        return r;

    // If preferred side can't work, try the other side:
    return attemptDir(-slideDir);
}

glm::vec2 PlayerCharacter::ApplyLaneSnapping(
    glm::vec2 desiredMovement, glm::vec2 normalizedDir,
    float deltaTime, const Tilemap *tilemap,
    const std::vector<glm::vec2> *npcPositions,
    bool sprintMode, int moveDx, int moveDy)
{
    if (!tilemap)
        return desiredMovement;

    glm::vec2 bottomCenterPos = GetCurrentTileCenter(static_cast<float>(tilemap->GetTileWidth()));
    glm::vec2 offsetToCenter = bottomCenterPos - m_Position;

    constexpr float laneSettleTime = 0.3f;
    float alpha = CalculateFollowAlpha(deltaTime, laneSettleTime);

    bool movingHorizontal = std::abs(normalizedDir.x) > std::abs(normalizedDir.y);

    // Keep correction small per frame so it can ratchet into tight gaps.
    auto clampCorr = [](float c)
    { return std::clamp(c, -1.2f, 1.2f); };

    if (movingHorizontal)
    {
        if (std::abs(desiredMovement.y) > 0.01f)
            return desiredMovement;

        float correction = clampCorr(offsetToCenter.y * alpha);
        if (std::abs(correction) < 0.001f)
            return desiredMovement;

        glm::vec2 testPos = m_Position + glm::vec2(desiredMovement.x, correction);

        // moving horizontally: moveDx matters, moveDy = 0
        if (!CollidesAt(testPos, tilemap, npcPositions, sprintMode, moveDx, 0, false))
        {
            desiredMovement.y += correction;
        }
        else
        {
            // try perpendicular-only with correct direction
            int corrDy = (correction > 0.0f) ? 1 : -1;
            glm::vec2 testPerpOnly = m_Position + glm::vec2(0.0f, correction);

            if (!CollidesAt(testPerpOnly, tilemap, npcPositions, sprintMode, 0, corrDy, false))
                desiredMovement.y += correction;
        }
    }
    else
    {
        if (std::abs(desiredMovement.x) > 0.01f)
            return desiredMovement;

        float correction = clampCorr(offsetToCenter.x * alpha);
        if (std::abs(correction) < 0.001f)
            return desiredMovement;

        glm::vec2 testPos = m_Position + glm::vec2(correction, desiredMovement.y);

        // moving vertically: moveDy matters, moveDx = 0
        if (!CollidesAt(testPos, tilemap, npcPositions, sprintMode, 0, moveDy, false))
        {
            desiredMovement.x += correction;
        }
        else
        {
            int corrDx = (correction > 0.0f) ? 1 : -1;
            glm::vec2 testPerpOnly = m_Position + glm::vec2(correction, 0.0f);

            if (!CollidesAt(testPerpOnly, tilemap, npcPositions, sprintMode, corrDx, 0, false))
                desiredMovement.x += correction;
        }
    }

    return desiredMovement;
}

void PlayerCharacter::HandleIdleSnap(float deltaTime, const Tilemap *tilemap,
                                     const std::vector<glm::vec2> *npcPositions)
{
    float tileSize = tilemap ? static_cast<float>(tilemap->GetTileWidth()) : 16.0f;
    glm::vec2 targetCenter = GetCurrentTileCenter(tileSize);
    float distanceToCenter = glm::length(targetCenter - m_Position);

    // === Stuck Detection: teleport to safety if inside collision ===
    if (tilemap && CollidesWithTilesStrict(m_Position, tilemap, 0, 0, false))
    {
        m_Position = FindClosestSafeTileCenter(tilemap, npcPositions);
        targetCenter = GetCurrentTileCenter(tileSize);
        distanceToCenter = glm::length(targetCenter - m_Position);
    }
    else if (tilemap)
    {
        // Track last known safe position for recovery
        m_LastSafeTileCenter = GetCurrentTileCenter(tileSize);
    }

    // === Smooth snap to tile center (with smoothstep, frame-rate independent) ===
    if (distanceToCenter > 0.5f)
    {
        // Check if target changed - if so, restart the interpolation
        if (targetCenter != m_SnapTargetPos || m_SnapProgress >= 1.0f)
        {
            m_SnapStartPos = m_Position;
            m_SnapTargetPos = targetCenter;
            m_SnapProgress = 0.0f;
        }

        // Duration of the snap interpolation in seconds
        constexpr float snapDuration = 0.3f;

        // Advance progress linearly (frame-rate independent)
        m_SnapProgress += deltaTime / snapDuration;
        m_SnapProgress = std::min(m_SnapProgress, 1.0f);

        // Apply smoothstep to total progress for ease-in/ease-out: t*t*(3.0 - 2.0*t)
        float t = m_SnapProgress;
        float smoothT = t * t * (3.0f - 2.0f * t);

        // Calculate target position using smoothed progress
        glm::vec2 desiredPos = m_SnapStartPos + (m_SnapTargetPos - m_SnapStartPos) * smoothT;
        glm::vec2 snapMovement = desiredPos - m_Position;

        if (tilemap)
        {
            // Test each axis independently to allow partial snapping
            glm::vec2 finalSnap(0.0f);

            glm::vec2 testX = m_Position + glm::vec2(snapMovement.x, 0.0f);
            if (!CollidesWithTilesStrict(testX, tilemap, 0, 0, false) &&
                !CollidesWithNPC(testX, npcPositions))
                finalSnap.x = snapMovement.x;

            glm::vec2 testY = m_Position + glm::vec2(0.0f, snapMovement.y);
            if (!CollidesWithTilesStrict(testY, tilemap, 0, 0, false) &&
                !CollidesWithNPC(testY, npcPositions))
                finalSnap.y = snapMovement.y;

            m_Position += finalSnap;

            // Final exact snap when progress complete and position is safe
            if (m_SnapProgress >= 1.0f &&
                !CollidesWithTilesStrict(targetCenter, tilemap, 0, 0, false) &&
                !CollidesWithNPC(targetCenter, npcPositions))
            {
                m_Position = targetCenter;
            }
        }
        else
        {
            m_Position = desiredPos;
        }
    }
    else
    {
        // Close enough to center - snap exactly to avoid fractional positions
        if (tilemap &&
            !CollidesWithTilesStrict(targetCenter, tilemap, 0, 0, false) &&
            !CollidesWithNPC(targetCenter, npcPositions))
        {
            m_Position = targetCenter;
        }
        m_SnapProgress = 1.0f;
    }

    Stop();
}

glm::vec2 PlayerCharacter::GetCurrentTileCenter(float tileSize) const
{
    constexpr float EPS = 0.001f; // Small epsilon to handle edge cases

    // Calculate tile indices
    int tileX = static_cast<int>(std::floor(m_Position.x / tileSize));
    int tileY = static_cast<int>(std::floor((m_Position.y - tileSize * 0.5f - EPS) / tileSize));

    // Return bottom-center position at tile center
    return glm::vec2(
        tileX * tileSize + tileSize * 0.5f, // Horizontal center of tile
        tileY * tileSize + tileSize         // Bottom of tile
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

glm::vec2 PlayerCharacter::GetSpriteCoords(int frame, Direction dir, AnimationType anim, bool requiresYFlip)
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
