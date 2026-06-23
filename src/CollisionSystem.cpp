// CollisionSystem - Player vs. Tilemap / NPC collision pipeline (free functions).
//
// @author Claude (https://github.com/claude)
// The tile-overlap test is gated by a short cascade of permissive checks
// before any "hard" collision is reported. Reading the helpers top-down without
// this map can be misleading - each phase reasons about a different geometric
// situation and has its own tolerance budget.
//
//                   +--------------------------------------+
//   moveDx,moveDy   | CollidesWithTilesStrict              |
//   diagonalInput   |   for each overlapping tile:         |
//   bottomCenterPos |     1) ShouldSkipDiagonalTile        |--+--> "no
//                   |        cardinal grazing past corner  |  |     collision"
//                   |     2) ShouldTolerateWallPenetration |  |
//                   |        sliding along a wall face     |  |
//                   |     3) ShouldAllowCornerCut          |  |
//                   |        through an exposed convex     |  |
//                   |        corner with escape route      |  |
//                   |     4) else: report collision        |--+--> "blocked"
//                   +--------------------------------------+
//
// When a strict collision is reported, TrySlideMovement consults
// GetCornerSlideDirection to project the desired motion onto the nearest
// open corridor and binary-searches for the largest safe step. The
// PlayerMovementState slide hysteresis fields persist between frames to stop
// direction flips when the player is jittering near a corner's tie-breaker.
//
// The threshold constants below are calibrated for the project's 16 px
// tile and ~16 px hitbox; changing the tile/hitbox geometry requires
// re-tuning them rather than scaling proportionally.

#include "CollisionSystem.hpp"

#include "CharacterConstants.hpp"
#include "CollisionGeometry.hpp"
#include "PlayerMovementState.hpp"
#include "Tilemap.hpp"
#include "TileMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <optional>

namespace
{
// Maximum slide distance for corner-cutting (pixels).
constexpr float MAX_SLIDE_DISTANCE = 16.0f;

/// @brief Transient per-tile overlap evaluation context, built once per
/// overlapping tile inside CollidesWithTilesStrict() and passed to the tolerance
/// helpers so they decide without redundant recalculation. Implementation detail
/// (never a component); all positions use the bottom-center (feet) convention.
struct TileOverlapContext
{
    glm::vec2 bottomCenterPos;  ///< Player feet position being tested
    glm::vec2 hitboxCenter;     ///< Center of the player hitbox AABB
    float hitboxArea;           ///< Total hitbox area in pixels squared
    float overlapW;             ///< Horizontal overlap between hitbox and tile (pixels)
    float overlapH;             ///< Vertical overlap between hitbox and tile (pixels)
    float overlapRatio;         ///< Overlap area as a fraction of hitbox area (0-1)
    int tx, ty;                 ///< Tile grid coordinates of the overlapping tile
    float tileMinX, tileMaxX;   ///< Tile AABB horizontal bounds (world pixels)
    float tileMinY, tileMaxY;   ///< Tile AABB vertical bounds (world pixels)
    int playerTileX;            ///< Tile column the player's feet center occupies
    int playerTileY;            ///< Tile row the player's feet center occupies
    int moveDx, moveDy;         ///< Movement direction signs (-1, 0, or +1)
    bool diagonalInput;         ///< True if two directional keys are held simultaneously
    float tileW, tileH;         ///< Tile dimensions in pixels (typically 16x16)
};

// Check if a diagonal corner tile should be ignored during cardinal movement.
bool ShouldSkipDiagonalTile(const TileOverlapContext& ctx)
{
    bool cardinalMove = ((ctx.moveDx != 0) ^ (ctx.moveDy != 0));  // exactly one axis non-zero
    if (cardinalMove && !ctx.diagonalInput)
    {
        int dxT = ctx.tx - ctx.playerTileX;
        int dyT = ctx.ty - ctx.playerTileY;

        // Only diagonally-adjacent tiles
        if (std::abs(dxT) == 1 && std::abs(dyT) == 1)
        {
            // How deep we penetrated into the diagonal tile along the forward axis
            float forwardPenetration = (ctx.moveDy != 0) ? ctx.overlapH : ctx.overlapW;

            // Ignore diagonal tiles until the player is at least this many pixels
            // into them. This prevents a diagonal tile from blocking movement when
            // the player is only grazing its far edge during cardinal movement.
            constexpr float DIAGONAL_CORNER_ACTIVATION_PX = 4.0f;

            if (forwardPenetration < DIAGONAL_CORNER_ACTIVATION_PX)
                return true;
        }
    }
    return false;
}

// Check if shallow wall penetration should be tolerated (corridor sliding).
bool ShouldTolerateWallPenetration(const TileOverlapContext& ctx)
{
    const bool hasMotion = (ctx.moveDx != 0) || (ctx.moveDy != 0);
    if (hasMotion && !ctx.diagonalInput && ctx.overlapW > 0.0f && ctx.overlapH > 0.0f)
    {
        float tileCenterX = (ctx.tileMinX + ctx.tileMaxX) * 0.5f;
        float tileCenterY = (ctx.tileMinY + ctx.tileMaxY) * 0.5f;

        bool tileAbove = tileCenterY < ctx.hitboxCenter.y;
        bool tileBelow = tileCenterY > ctx.hitboxCenter.y;
        bool tileLeft = tileCenterX < ctx.hitboxCenter.x;
        bool tileRight = tileCenterX > ctx.hitboxCenter.x;

        // Determine which axis is the penetration axis
        bool penetrationIsY = (ctx.overlapH <= ctx.overlapW);
        float penetrationPx = penetrationIsY ? ctx.overlapH : ctx.overlapW;

        // Allow up to 5px of overlap when the player is sliding parallel
        // to a wall face (not pushing into it). This prevents getting stuck
        // when the hitbox is slightly embedded after a corner cut.
        constexpr float PASSIVE_PENETRATION_PX = 5.0f;

        bool movingInto = false;
        if (penetrationIsY)
        {
            // Y+ is down in our world
            if (tileAbove)
                movingInto = (ctx.moveDy < 0);  // moving up into top wall
            else if (tileBelow)
                movingInto = (ctx.moveDy > 0);  // moving down into bottom wall
            // moveDy == 0 is OK (sliding sideways while scraping)
        }
        else
        {
            if (tileLeft)
                movingInto = (ctx.moveDx < 0);  // moving left into left wall
            else if (tileRight)
                movingInto = (ctx.moveDx > 0);  // moving right into right wall
            // moveDx == 0 is OK (sliding vertically while scraping)
        }

        // Require at least 4px of contact along the wall face before
        // suppressing collision. Near corners, faceOverlap shrinks - we
        // must NOT suppress there or the player could clip through.
        constexpr float FACE_CONTACT_MIN_PX = 4.0f;
        float faceOverlap = penetrationIsY ? ctx.overlapW : ctx.overlapH;

        // Only allow passive tolerance when we're clearly alongside a wall face.
        // Near corners, faceOverlap gets small -> do NOT suppress collision there.
        if (!movingInto && penetrationPx <= PASSIVE_PENETRATION_PX &&
            faceOverlap >= FACE_CONTACT_MIN_PX)
            return true;
    }
    return false;
}

// Evaluate corner cutting and side-wall tolerance for a tile overlap.
bool ShouldAllowCornerCut(const Hitbox& hitbox,
                          const TileOverlapContext& ctx,
                          const Tilemap* tilemap,
                          bool& forceCollision)
{
    forceCollision = false;
    const float HALF_W = hitbox.halfWidth;
    const float BOX_H = hitbox.height;
    constexpr float EPS = CharacterConstants::COLLISION_EPS;
    // Maximum hitbox-area overlap with a corner tile before we stop allowing
    // corner cutting. 20% lets the player clip through exposed convex corners
    // smoothly but still blocks if they push too far into the tile.
    constexpr float CORNER_OVERLAP_THRESHOLD = 0.20f;

    // Small overlaps with side walls are tolerated when moving along a corridor.
    // Without this, the player would snag on walls when slightly misaligned
    // after a corner cut.
    constexpr float SIDE_WALL_TOLERANCE = 0.15f;

    auto inBounds = [&](int x, int y)
    { return x >= 0 && y >= 0 && x < tilemap->GetMapWidth() && y < tilemap->GetMapHeight(); };

    auto tileBlocked = [&](int x, int y)
    { return !inBounds(x, y) || tilemap->GetTileCollision(x, y); };

    // Corner cutting: when the player clips a true corner with <20% overlap, allow
    // the overlap if open space exists perpendicular to motion. Stops "stuck on
    // corner" without letting the player phase through walls.

    // Check adjacent tiles to identify exposed corners
    bool emptyAbove = !tileBlocked(ctx.tx, ctx.ty - 1);
    bool emptyBelow = !tileBlocked(ctx.tx, ctx.ty + 1);
    bool emptyLeft = !tileBlocked(ctx.tx - 1, ctx.ty);
    bool emptyRight = !tileBlocked(ctx.tx + 1, ctx.ty);

    // Check if corner cutting is blocked for each corner
    bool tlBlocked = tilemap->IsCornerCutBlocked(ctx.tx, ctx.ty, Tilemap::CORNER_TL);
    bool trBlocked = tilemap->IsCornerCutBlocked(ctx.tx, ctx.ty, Tilemap::CORNER_TR);
    bool blBlocked = tilemap->IsCornerCutBlocked(ctx.tx, ctx.ty, Tilemap::CORNER_BL);
    bool brBlocked = tilemap->IsCornerCutBlocked(ctx.tx, ctx.ty, Tilemap::CORNER_BR);

    bool isTopLeftCorner = emptyAbove && emptyLeft && !tlBlocked;
    bool isTopRightCorner = emptyAbove && emptyRight && !trBlocked;
    bool isBottomLeftCorner = emptyBelow && emptyLeft && !blBlocked;
    bool isBottomRightCorner = emptyBelow && emptyRight && !brBlocked;

    bool isTrueCorner =
        isTopLeftCorner || isTopRightCorner || isBottomLeftCorner || isBottomRightCorner;

    // When moving horizontally, tolerate small overlaps with tiles above/below
    // When moving vertically, tolerate small overlaps with tiles left/right
    // This prevents getting stuck in narrow corridors after corner cutting
    if (!isTrueCorner && ctx.overlapRatio <= SIDE_WALL_TOLERANCE && ctx.overlapRatio > 0.01f)
    {
        float tileCenterX = (ctx.tileMinX + ctx.tileMaxX) * 0.5f;
        float tileCenterY = (ctx.tileMinY + ctx.tileMaxY) * 0.5f;

        bool tileIsAboveOrBelow =
            std::abs(ctx.hitboxCenter.y - tileCenterY) > std::abs(ctx.hitboxCenter.x - tileCenterX);
        bool tileIsLeftOrRight = !tileIsAboveOrBelow;

        // Moving horizontally and tile is above/below = side wall, tolerate
        if (ctx.moveDx != 0 && ctx.moveDy == 0 && tileIsAboveOrBelow)
            return true;

        // Moving vertically and tile is left/right = side wall, tolerate
        if (ctx.moveDy != 0 && ctx.moveDx == 0 && tileIsLeftOrRight)
            return true;
    }

    if (isTrueCorner)
    {
        float tileCenterX = (ctx.tileMinX + ctx.tileMaxX) * 0.5f;
        float tileCenterY = (ctx.tileMinY + ctx.tileMaxY) * 0.5f;

        // Deadzone around the tile center prevents sub-pixel jitter from
        // flipping the player between quadrants each frame, which would cause
        // flickering collision results. Must be wide enough to absorb floating-
        // point noise but narrow enough to still distinguish genuine sides.
        constexpr float CORNER_QUAD_EPS = 4.0f;

        auto sideSign = [](float v, float eps) -> int
        {
            if (v > eps)
                return 1;
            if (v < -eps)
                return -1;
            return 0;  // near center
        };

        float dx = ctx.hitboxCenter.x - tileCenterX;
        float dy = ctx.hitboxCenter.y - tileCenterY;

        int sx = sideSign(dx, CORNER_QUAD_EPS);
        int sy = sideSign(dy, CORNER_QUAD_EPS);

        // Tie-break when we're near the center using movement direction (approach
        // direction). If we're moving right, we are approaching the blocking tile from the
        // left, etc.
        if (sx == 0)
        {
            if (ctx.moveDx > 0)
                sx = -1;
            else if (ctx.moveDx < 0)
                sx = 1;
        }
        if (sy == 0)
        {
            // Y+ is down in our world
            if (ctx.moveDy > 0)
                sy = -1;  // moving down -> approaching from above
            else if (ctx.moveDy < 0)
                sy = 1;  // moving up -> approaching from below
        }

        bool playerLeftOfTile = (sx < 0);
        bool playerRightOfTile = (sx > 0);
        bool playerAboveTile = (sy < 0);
        bool playerBelowTile = (sy > 0);

        // If both movement axes are pushing directly into blocked faces (solid rectangle
        // corner), do not allow corner cutting - force a collision so we slide instead of
        // clipping through.
        bool movingIntoClosedCorner =
            ctx.diagonalInput &&
            ((ctx.moveDx > 0 && !emptyRight) || (ctx.moveDx < 0 && !emptyLeft)) &&
            ((ctx.moveDy > 0 && !emptyBelow) || (ctx.moveDy < 0 && !emptyAbove));
        if (movingIntoClosedCorner)
        {
            forceCollision = true;
            return false;
        }

        bool canCutThisCorner = false;

        // Check if the escape route in the perpendicular direction is clear
        // by looking at adjacent tiles to the PLAYER, not to the collision tile

        auto hasEscapeRoute = [&](int escapeX, int escapeY) -> bool
        {
            // Check if moving in the escape direction leads to open space
            glm::vec2 escapePos = ctx.bottomCenterPos +
                                  glm::vec2(escapeX * ctx.tileW * 0.5f, escapeY * ctx.tileH * 0.5f);

            const CollisionGeometry::Aabb escBox =
                CollisionGeometry::MakeFeetAabb(escapePos, HALF_W, BOX_H, EPS);
            const float escMinX = escBox.minX;
            const float escMaxX = escBox.maxX;
            const float escMaxY = escBox.maxY;
            const float escMinY = escBox.minY;

            int escTileX0 = TileMath::TileIndex(escMinX, ctx.tileW);
            int escTileX1 = TileMath::TileIndex(escMaxX, ctx.tileW);
            int escTileY0 = TileMath::TileIndex(escMinY, ctx.tileH);
            int escTileY1 = TileMath::TileIndex(escMaxY, ctx.tileH);

            for (int ety = escTileY0; ety <= escTileY1; ++ety)
            {
                for (int etx = escTileX0; etx <= escTileX1; ++etx)
                {
                    if (tileBlocked(etx, ety))
                    {
                        // Check overlap at escape position
                        float etMinX = etx * ctx.tileW, etMaxX = (etx + 1) * ctx.tileW;
                        float etMinY = ety * ctx.tileH, etMaxY = (ety + 1) * ctx.tileH;

                        float eOverlapW =
                            std::max(0.0f, std::min(escMaxX, etMaxX) - std::max(escMinX, etMinX));
                        float eOverlapH =
                            std::max(0.0f, std::min(escMaxY, etMaxY) - std::max(escMinY, etMinY));

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

        if (ctx.diagonalInput && !canCutThisCorner)
        {
            if (isTopLeftCorner && playerAboveTile && playerLeftOfTile && ctx.moveDx > 0 &&
                ctx.moveDy > 0)
            {
                if (hasEscapeRoute(0, -1) || hasEscapeRoute(-1, 0))
                    canCutThisCorner = true;
            }
            if (isTopRightCorner && playerAboveTile && playerRightOfTile && ctx.moveDx < 0 &&
                ctx.moveDy > 0)
            {
                if (hasEscapeRoute(0, -1) || hasEscapeRoute(1, 0))
                    canCutThisCorner = true;
            }
            if (isBottomLeftCorner && playerBelowTile && playerLeftOfTile && ctx.moveDx > 0 &&
                ctx.moveDy < 0)
            {
                if (hasEscapeRoute(0, 1) || hasEscapeRoute(-1, 0))
                    canCutThisCorner = true;
            }
            if (isBottomRightCorner && playerBelowTile && playerRightOfTile && ctx.moveDx < 0 &&
                ctx.moveDy < 0)
            {
                if (hasEscapeRoute(0, 1) || hasEscapeRoute(1, 0))
                    canCutThisCorner = true;
            }
        }

        if (canCutThisCorner)
        {
            // For cardinal movement, use perpendicular penetration in pixels
            // rather than overlap-area ratio. Area-based thresholds are too strict
            // when the player is aligned along one axis (tall, thin overlap sliver).
            // Pixel-based check lets us allow up to 4px of corner grazing.
            bool cardinalMove = ((ctx.moveDx != 0) ^ (ctx.moveDy != 0)) && !ctx.diagonalInput;
            if (cardinalMove)
            {
                float perpPenPx = (ctx.moveDx != 0)
                                      ? ctx.overlapH
                                      : ctx.overlapW;  // moving horizontal -> perp is Y overlap
                constexpr float CORNER_PERP_PX = 4.0f;
                if (perpPenPx <= CORNER_PERP_PX)
                    return true;  // allow corner cut
            }

            // Fallback for diagonal/etc.
            if (ctx.overlapRatio <= CORNER_OVERLAP_THRESHOLD)
                return true;  // allow corner cut
        }
    }

    return false;
}
}  // namespace

namespace CollisionSystem
{
float CalculateFollowAlpha(float deltaTime, float settleTime, float epsilon)
{
    deltaTime = std::max(0.0f, deltaTime);
    settleTime = std::max(1e-5f, settleTime);  // Prevent division by zero

    // Exponential decay formula: after settleTime seconds, the remaining
    // distance will be `epsilon` fraction of the original. This produces
    // frame-rate-independent smoothing - the same visual result regardless
    // of whether Update runs at 30 or 144 fps.
    float alpha = 1.0f - std::pow(epsilon, deltaTime / settleTime);
    return std::clamp(alpha, 0.0f, 1.0f);
}

bool CollidesWithNPC(const Hitbox& hitbox,
                     const glm::vec2& bottomCenterPos,
                     const std::vector<glm::vec2>* npcPositions)
{
    if (!npcPositions || npcPositions->empty())
        return false;

    // Player vs each NPC: same feet-anchored 16x16 box, epsilon-shrunk to avoid
    // edge-on-edge false positives. See CollisionGeometry::FeetBoxesOverlap.
    for (const glm::vec2& npcPos : *npcPositions)
    {
        if (CollisionGeometry::FeetBoxesOverlap(bottomCenterPos,
                                                npcPos,
                                                hitbox.halfWidth,
                                                hitbox.height,
                                                CharacterConstants::COLLISION_EPS))
        {
            return true;
        }
    }
    return false;
}

bool CollidesWithTilesStrict(const Hitbox& hitbox,
                             const glm::vec2& bottomCenterPos,
                             const Tilemap* tilemap,
                             int moveDx,
                             int moveDy,
                             bool diagonalInput,
                             int plane)
{
    if (!tilemap)
        return false;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());
    const float HALF_W = hitbox.halfWidth;
    const float BOX_H = hitbox.height;
    constexpr float EPS = CharacterConstants::COLLISION_EPS;

    // Calculate player AABB bounds (feet-anchored, eps-shrunk) via the shared helper.
    const CollisionGeometry::Aabb box =
        CollisionGeometry::MakeFeetAabb(bottomCenterPos, HALF_W, BOX_H, EPS);
    const float minX = box.minX;
    const float maxX = box.maxX;
    const float maxY = box.maxY;
    const float minY = box.minY;

    glm::vec2 hitboxCenter(bottomCenterPos.x, bottomCenterPos.y - BOX_H * 0.5f);
    // Calculate tile range that overlaps hitbox
    int tileX0 = TileMath::TileIndex(minX, TILE_W);
    int tileX1 = TileMath::TileIndex(maxX, TILE_W);
    int tileY0 = TileMath::TileIndex(minY, TILE_H);
    int tileY1 = TileMath::TileIndex(maxY, TILE_H);

    int playerTileX = TileMath::TileIndex(bottomCenterPos.x, TILE_W);
    int playerTileY = TileMath::AnchorTileRow(bottomCenterPos.y, TILE_H, EPS);

    auto inBounds = [&](int x, int y)
    { return x >= 0 && y >= 0 && x < tilemap->GetMapWidth() && y < tilemap->GetMapHeight(); };

    float hitboxArea = (maxX - minX) * (maxY - minY);

    for (int ty = tileY0; ty <= tileY1; ++ty)
    {
        for (int tx = tileX0; tx <= tileX1; ++tx)
        {
            if (!inBounds(tx, ty) || !tilemap->GetTileCollision(tx, ty))
                continue;

            // Z-aware skip: a collision tile only blocks the character when
            // its elevation is at-or-below the character's logical plane.
            // Tiles above the plane (e.g. a bridge railing while the player
            // is at ground level) are non-blocking - the player walks under.
            if (tilemap->GetElevation(tx, ty) > plane)
                continue;

            float tileMinX = tx * TILE_W, tileMaxX = (tx + 1) * TILE_W;
            float tileMinY = ty * TILE_H, tileMaxY = (ty + 1) * TILE_H;
            float overlapW = std::max(0.0f, std::min(maxX, tileMaxX) - std::max(minX, tileMinX));
            float overlapH = std::max(0.0f, std::min(maxY, tileMaxY) - std::max(minY, tileMinY));
            float overlapRatio = (overlapW * overlapH) / hitboxArea;

            TileOverlapContext ctx{bottomCenterPos,
                                   hitboxCenter,
                                   hitboxArea,
                                   overlapW,
                                   overlapH,
                                   overlapRatio,
                                   tx,
                                   ty,
                                   tileMinX,
                                   tileMaxX,
                                   tileMinY,
                                   tileMaxY,
                                   playerTileX,
                                   playerTileY,
                                   moveDx,
                                   moveDy,
                                   diagonalInput,
                                   TILE_W,
                                   TILE_H};

            if (ShouldSkipDiagonalTile(ctx))
                continue;
            if (ShouldTolerateWallPenetration(ctx))
                continue;

            bool forceCollision = false;
            if (ShouldAllowCornerCut(hitbox, ctx, tilemap, forceCollision))
                continue;
            if (forceCollision)
                return true;

            if (overlapRatio > 0.01f)
                return true;
        }
    }
    return false;
}

bool CollidesAt(const Hitbox& hitbox,
                const glm::vec2& bottomCenterPos,
                const Tilemap* tilemap,
                const std::vector<glm::vec2>* npcPositions,
                int moveDx,
                int moveDy,
                bool diagonalInput,
                int plane)
{
    return CollidesWithTilesStrict(
               hitbox, bottomCenterPos, tilemap, moveDx, moveDy, diagonalInput, plane) ||
           CollidesWithNPC(hitbox, bottomCenterPos, npcPositions);
}

// @author Codex (https://github.com/codex)
// Corner here means a blocked tile with a perpendicular opening
// (open above/below for horizontal, or left/right for vertical).
// Mid-wall tiles in a long flat wall are explicitly rejected so
// the player doesn't get pulled sideways along a straight surface.
// When both perpendicular directions are open, the choice
// is made in this order:
//   1. geometric necessity (only one side has open space)  ->  forced
//   2. player offset from the wall-tile center (>= 4 px)   ->  off-center bias
//   3. last-frame slide direction / last input axis        ->  hysteresis
//   4. counter-clockwise relative to forward               ->  deterministic
// The resulting direction is committed for ~120 ms via movement.slideTimer to
// stop frame-to-frame oscillation when the player is wedged in a tie-breaker.
glm::vec2 GetCornerSlideDirection(const Hitbox& hitbox,
                                  glm::vec2 playerPos,
                                  PlayerMovementState& movement,
                                  int plane,
                                  const glm::vec2& testPos,
                                  const Tilemap* tilemap,
                                  int /*moveDirX*/,
                                  int /*moveDirY*/)
{
    if (!tilemap)
        return glm::vec2(0.0f);

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());

    auto signi = [](float v) -> int { return (v > 0.001f) ? 1 : (v < -0.001f) ? -1 : 0; };

    glm::vec2 step = testPos - playerPos;
    bool horizontalPrimary = std::abs(step.x) >= std::abs(step.y);

    // Use a fixed 1-pixel forward probe distance for corner detection.
    // This makes detection frame-rate independent - we're just checking IF a path exists,
    // not how far we can move this frame. The actual movement distance is handled separately.
    float forwardSign =
        horizontalPrimary ? (step.x >= 0 ? 1.0f : -1.0f) : (step.y >= 0 ? 1.0f : -1.0f);
    glm::vec2 forward =
        horizontalPrimary ? glm::vec2(forwardSign, 0.0f) : glm::vec2(0.0f, forwardSign);

    auto tileBlocked = [&](int tx, int ty) -> bool
    {
        if (tx < 0 || ty < 0 || tx >= tilemap->GetMapWidth() || ty >= tilemap->GetMapHeight())
            return true;
        return tilemap->GetTileCollision(tx, ty);
    };

    // === Detect corner type based on the CLOSEST ACTUAL CORNER to player's center ===
    // For multi-tile walls, only the END tiles are corners - middle tiles have no perpendicular
    // opening We need to find the nearest tile that actually has a corner (perpendicular opening)
    bool cornerEmptyAbove = false;
    bool cornerEmptyBelow = false;
    bool cornerEmptyLeft = false;
    bool cornerEmptyRight = false;
    {
        float hitboxCenterX = testPos.x;
        float hitboxCenterY = testPos.y - hitbox.height * 0.5f;

        int forwardTileX, forwardTileY;
        int bestTileX = 0, bestTileY = 0;
        float bestCornerDist = std::numeric_limits<float>::max();
        bool foundAnyCorner = false;
        bool foundAnyBlocked = false;

        if (horizontalPrimary)
        {
            // Moving horizontally - find the closest CORNER tile in the forward column
            forwardTileX = (step.x < 0) ? TileMath::TileIndex(testPos.x - hitbox.halfWidth, TILE_W)
                                        : TileMath::TileIndex(testPos.x + hitbox.halfWidth, TILE_W);

            int hitboxTopTileY = TileMath::TileIndex(testPos.y - hitbox.height, TILE_H);
            int hitboxBottomTileY = TileMath::TileIndex(testPos.y - 0.01f, TILE_H);

            for (int ty = hitboxTopTileY; ty <= hitboxBottomTileY; ++ty)
            {
                if (!tileBlocked(forwardTileX, ty))
                    continue;
                foundAnyBlocked = true;

                // Check if this tile has a perpendicular opening (is a corner)
                bool hasOpenAbove = !tileBlocked(forwardTileX, ty - 1);
                bool hasOpenBelow = !tileBlocked(forwardTileX, ty + 1);
                if (!hasOpenAbove && !hasOpenBelow)
                    continue;  // This is a middle wall tile, not a corner

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
            forwardTileY = (step.y < 0) ? TileMath::TileIndex(testPos.y - hitbox.height, TILE_H)
                                        : TileMath::TileIndex(testPos.y, TILE_H);

            int hitboxLeftTileX = TileMath::TileIndex(testPos.x - hitbox.halfWidth, TILE_W);
            int hitboxRightTileX =
                TileMath::TileIndex(testPos.x + hitbox.halfWidth - 0.01f, TILE_W);

            for (int tx = hitboxLeftTileX; tx <= hitboxRightTileX; ++tx)
            {
                if (!tileBlocked(tx, forwardTileY))
                    continue;
                foundAnyBlocked = true;

                // Check if this tile has a perpendicular opening (is a corner)
                bool hasOpenLeft = !tileBlocked(tx - 1, forwardTileY);
                bool hasOpenRight = !tileBlocked(tx + 1, forwardTileY);
                if (!hasOpenLeft && !hasOpenRight)
                    continue;  // This is a middle wall tile, not a corner

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
            if (movement.slideTimer <= 0.0f)
                movement.slideDir = glm::vec2(0.0f);
            return glm::vec2(0.0f);
        }

        // Don't slide if the closest corner is too far away
        // This prevents pulling toward distant corners when facing the middle of a long wall
        float maxCornerDist = horizontalPrimary ? (TILE_H * 0.75f) : (TILE_W * 0.75f);
        if (bestCornerDist > maxCornerDist)
        {
            if (movement.slideTimer <= 0.0f)
                movement.slideDir = glm::vec2(0.0f);
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
    auto hardTileBlocked = [&](const glm::vec2& p, int dx, int dy) -> bool
    { return CollidesWithTilesStrict(hitbox, p, tilemap, dx, dy, /*diagonalInput*/ false, plane); };

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

    auto evalDir = [&](const glm::vec2& dir, float maxProbe) -> Eval
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
            if (hardTileBlocked(playerPos + offset, sdx, sdy))
                continue;

            e.canSlideOnly = true;

            // slide + forward must be safe
            if (!hardTileBlocked(playerPos + offset + forward, fdx, fdy))
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
    bool bothDirectionsOpen = horizontalPrimary ? (cornerEmptyAbove && cornerEmptyBelow)
                                                : (cornerEmptyLeft && cornerEmptyRight);

    // Calculate player's offset from wall center to use as tiebreaker
    float playerOffset = 0.0f;  // Negative = toward dNeg, Positive = toward dPos
    {
        float hitboxCenterY = testPos.y - hitbox.height * 0.5f;
        int wallTileX, wallTileY;

        if (horizontalPrimary)
        {
            wallTileX = (step.x < 0) ? TileMath::TileIndex(testPos.x - hitbox.halfWidth, TILE_W)
                                     : TileMath::TileIndex(testPos.x + hitbox.halfWidth, TILE_W);
            wallTileY = TileMath::TileIndex(hitboxCenterY, TILE_H);
            float wallCenterY = (wallTileY + 0.5f) * TILE_H;
            playerOffset = hitboxCenterY - wallCenterY;  // Negative = above center (toward dNeg/up)
        }
        else
        {
            wallTileY = (step.y < 0) ? TileMath::TileIndex(testPos.y - hitbox.height, TILE_H)
                                     : TileMath::TileIndex(testPos.y, TILE_H);
            wallTileX = TileMath::TileIndex(testPos.x, TILE_W);
            float wallCenterX = (wallTileX + 0.5f) * TILE_W;
            playerOffset = testPos.x - wallCenterX;  // Negative = left of center (toward dNeg/left)
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

            // Both directions open - use player offset as tiebreaker only if significantly
            // off-center Threshold of 4.0 pixels ensures minor hitbox misalignment doesn't pull
            // toward corners
            if (playerOffset < -4.0f)
                return {dNeg, dPos};  // Player above center -> prefer up
            if (playerOffset > 4.0f)
                return {dPos, dNeg};  // Player below center -> prefer down

            // Nearly centered - use hysteresis/last input
            if (movement.slideDir.y < 0.0f)
                return {dNeg, dPos};
            if (movement.slideDir.y > 0.0f)
                return {dPos, dNeg};
            if (movement.lastInputY < 0)
                return {dNeg, dPos};
            if (movement.lastInputY > 0)
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

            // Both directions open - use player offset as tiebreaker only if significantly
            // off-center Threshold of 4.0 pixels ensures minor hitbox misalignment doesn't pull
            // toward corners
            if (playerOffset < -4.0f)
                return {dNeg, dPos};  // Player left of center -> prefer left
            if (playerOffset > 4.0f)
                return {dPos, dNeg};  // Player right of center -> prefer right

            // Nearly centered - use hysteresis/last input
            if (movement.slideDir.x < 0.0f)
                return {dNeg, dPos};
            if (movement.slideDir.x > 0.0f)
                return {dPos, dNeg};
            if (movement.lastInputX < 0)
                return {dNeg, dPos};
            if (movement.lastInputX > 0)
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

    auto pick = [&](const Eval& e1, const Eval& e2) -> glm::vec2
    {
        // If only one direction was geometrically valid, only consider e1
        if (!bothDirectionsOpen)
        {
            if (e1.canForward)
                return e1.dir;
            if (e1.canSlideOnly)
                return e1.dir;
            return glm::vec2(0.0f);  // Preferred direction failed -> stop
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
        float hitboxCenterY = testPos.y - hitbox.height * 0.5f;
        int blockTileX, blockTileY;

        if (horizontalPrimary)
        {
            // Moving horizontally - blocking tile is in the forward column
            blockTileX = (step.x < 0) ? TileMath::TileIndex(testPos.x - hitbox.halfWidth, TILE_W)
                                      : TileMath::TileIndex(testPos.x + hitbox.halfWidth, TILE_W);
            // Find the tile we're actually sliding around (closest to hitbox center)
            int hitboxTopTileY = TileMath::TileIndex(testPos.y - hitbox.height, TILE_H);
            int hitboxBottomTileY = TileMath::TileIndex(testPos.y - 0.01f, TILE_H);
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
            blockTileY = (step.y < 0) ? TileMath::TileIndex(testPos.y - hitbox.height, TILE_H)
                                      : TileMath::TileIndex(testPos.y, TILE_H);
            int hitboxLeftTileX = TileMath::TileIndex(testPos.x - hitbox.halfWidth, TILE_W);
            int hitboxRightTileX =
                TileMath::TileIndex(testPos.x + hitbox.halfWidth - 0.01f, TILE_W);
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
        if (glm::length(movement.slideDir) < 0.001f ||
            glm::dot(chosen, movement.slideDir) < 0.5f)  // Direction changed significantly
        {
            movement.slideTimer = 0.12f;  // Commit to this direction for 120ms
        }
        movement.slideDir = chosen;
    }

    return chosen;
}

glm::vec2 FindClosestSafeTileCenter(const Hitbox& hitbox,
                                    glm::vec2 playerPos,
                                    int plane,
                                    const Tilemap* tilemap,
                                    const std::vector<glm::vec2>* npcPositions)
{
    if (!tilemap)
        return playerPos;

    const float TILE_W = static_cast<float>(tilemap->GetTileWidth());
    const float TILE_H = static_cast<float>(tilemap->GetTileHeight());

    int baseTileX = TileMath::TileIndex(playerPos.x, TILE_W);
    int baseTileY = TileMath::AnchorTileRow(playerPos.y, TILE_H);

    float bestDist2 = std::numeric_limits<float>::infinity();
    glm::vec2 bestCenter = playerPos;

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            int tx = baseTileX + dx;
            int ty = baseTileY + dy;

            if (tx < 0 || ty < 0 || tx >= tilemap->GetMapWidth() || ty >= tilemap->GetMapHeight())
                continue;

            glm::vec2 bottomCenterPos(tx * TILE_W + TILE_W * 0.5f, ty * TILE_H + TILE_H);

            if (!CollidesWithTilesStrict(hitbox, bottomCenterPos, tilemap, 0, 0, false, plane) &&
                !CollidesWithNPC(hitbox, bottomCenterPos, npcPositions))
            {
                float dist2 = glm::dot(bottomCenterPos - playerPos, bottomCenterPos - playerPos);
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

glm::vec2 TrySlideMovement(const Hitbox& hitbox,
                           glm::vec2 playerPos,
                           PlayerMovementState& movement,
                           int plane,
                           glm::vec2 desiredMovement,
                           glm::vec2 normalizedDir,
                           float deltaTime,
                           float currentSpeed,
                           const Tilemap* tilemap,
                           const std::vector<glm::vec2>* npcPositions,
                           int moveDx,
                           int moveDy,
                           bool diagonalInput)
{
    const float maxSlide = currentSpeed * deltaTime;

    // Test if desired movement is already valid
    glm::vec2 testPos = playerPos + desiredMovement;

    if (!CollidesAt(hitbox, testPos, tilemap, npcPositions, moveDx, moveDy, diagonalInput, plane) &&
        !CollidesWithNPC(hitbox, testPos, npcPositions))
    {
        // Only reset hysteresis if commit timer expired (prevents jitter at corners)
        if (movement.slideTimer <= 0.0f)
            movement.slideDir = glm::vec2(0.0f);
        return desiredMovement;  // No collision - use original movement
    }

    // NPC collision: don't slide, just stop
    if (CollidesWithNPC(hitbox, testPos, npcPositions))
    {
        movement.slideDir = glm::vec2(0.0f);
        movement.slideTimer = 0.0f;
        return glm::vec2(0.0f);
    }

    // Tile collision: find slide direction away from obstacle
    glm::vec2 slideDir = GetCornerSlideDirection(
        hitbox, playerPos, movement, plane, testPos, tilemap, moveDx, moveDy);

    if (glm::length(slideDir) < 0.001f)
    {
        // Only reset hysteresis if commit timer expired
        if (movement.slideTimer <= 0.0f)
            movement.slideDir = glm::vec2(0.0f);
        return glm::vec2(0.0f);  // No valid slide direction
    }

    auto attemptDir = [&](glm::vec2 dir) -> glm::vec2
    {
        bool horizontalPrimary = std::abs(desiredMovement.x) > std::abs(desiredMovement.y);

        // Use fixed 1-pixel forward probe for detection (frame-rate independent)
        // The actual movement uses desiredMovement which is frame-rate dependent
        glm::vec2 forwardProbe = horizontalPrimary
                                     ? glm::vec2(desiredMovement.x >= 0 ? 1.0f : -1.0f, 0.0f)
                                     : glm::vec2(0.0f, desiredMovement.y >= 0 ? 1.0f : -1.0f);

        glm::vec2 forwardMove = horizontalPrimary ? glm::vec2(desiredMovement.x, 0.0f)
                                                  : glm::vec2(0.0f, desiredMovement.y);

        for (int slideAmountInt = 1; slideAmountInt <= 16; ++slideAmountInt)
        {
            float slideAmount = static_cast<float>(slideAmountInt);
            glm::vec2 slideOffset = horizontalPrimary ? glm::vec2(0.0f, dir.y * slideAmount)
                                                      : glm::vec2(dir.x * slideAmount, 0.0f);

            // Use fixed 1-pixel probe for DETECTION of valid corner path
            glm::vec2 testSlideForward = playerPos + slideOffset + forwardProbe;

            if (!CollidesAt(hitbox,
                            testSlideForward,
                            tilemap,
                            npcPositions,
                            moveDx,
                            moveDy,
                            diagonalInput,
                            plane))
            {
                float clampedSlide = std::min(slideAmount, maxSlide);
                glm::vec2 clampedOffset = horizontalPrimary ? glm::vec2(0.0f, dir.y * clampedSlide)
                                                            : glm::vec2(dir.x * clampedSlide, 0.0f);

                // must be safe to apply the perpendicular step
                if (CollidesAt(hitbox,
                               playerPos + clampedOffset,
                               tilemap,
                               npcPositions,
                               (int)dir.x,
                               (int)dir.y,
                               diagonalInput,
                               plane))
                    continue;

                // Limit perpendicular shove so it doesn't exceed 75% of forward distance (prevents
                // violent kicks)
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
                    glm::vec2 tryPos = playerPos + clampedOffset + forwardMove * mid;
                    if (!CollidesAt(hitbox,
                                    tryPos,
                                    tilemap,
                                    npcPositions,
                                    moveDx,
                                    moveDy,
                                    diagonalInput,
                                    plane))
                        lo = mid;
                    else
                        hi = mid;
                }

                glm::vec2 slideResult = clampedOffset + forwardMove * lo;

                // Blend toward the original desired movement to smooth the direction change,
                // but only keep it if still collision-free.
                constexpr float SLIDE_BLEND = 0.35f;
                glm::vec2 blended = glm::mix(desiredMovement, slideResult, SLIDE_BLEND);
                if (!CollidesAt(hitbox,
                                playerPos + blended,
                                tilemap,
                                npcPositions,
                                moveDx,
                                moveDy,
                                diagonalInput,
                                plane))
                    return blended;

                return slideResult;
            }

            glm::vec2 testSlideOnly = playerPos + slideOffset;
            if (!CollidesAt(hitbox,
                            testSlideOnly,
                            tilemap,
                            npcPositions,
                            (int)dir.x,
                            (int)dir.y,
                            diagonalInput,
                            plane))
            {
                float clampedSlide = std::min(slideAmount, maxSlide);
                return horizontalPrimary ? glm::vec2(0.0f, dir.y * clampedSlide)
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

glm::vec2 ApplyLaneSnapping(const Hitbox& hitbox,
                            glm::vec2 playerPos,
                            glm::vec2 tileCenter,
                            int plane,
                            glm::vec2 desiredMovement,
                            glm::vec2 normalizedDir,
                            float deltaTime,
                            const Tilemap* tilemap,
                            const std::vector<glm::vec2>* npcPositions,
                            int moveDx,
                            int moveDy)
{
    if (!tilemap)
        return desiredMovement;

    glm::vec2 bottomCenterPos = tileCenter;
    glm::vec2 offsetToCenter = bottomCenterPos - playerPos;

    constexpr float laneSettleTime = 0.3f;
    float alpha = CalculateFollowAlpha(deltaTime, laneSettleTime);

    bool movingHorizontal = std::abs(normalizedDir.x) > std::abs(normalizedDir.y);

    // Keep correction small per frame so it can ratchet into tight gaps.
    auto clampCorr = [](float c) { return std::clamp(c, -1.2f, 1.2f); };

    if (movingHorizontal)
    {
        if (std::abs(desiredMovement.y) > 0.01f)
            return desiredMovement;

        float correction = clampCorr(offsetToCenter.y * alpha);
        if (std::abs(correction) < 0.001f)
            return desiredMovement;

        glm::vec2 testPos = playerPos + glm::vec2(desiredMovement.x, correction);

        // moving horizontally: moveDx matters, moveDy = 0
        if (!CollidesAt(hitbox, testPos, tilemap, npcPositions, moveDx, 0, false, plane))
        {
            desiredMovement.y += correction;
        }
        else
        {
            // try perpendicular-only with correct direction
            int corrDy = (correction > 0.0f) ? 1 : -1;
            glm::vec2 testPerpOnly = playerPos + glm::vec2(0.0f, correction);

            if (!CollidesAt(hitbox, testPerpOnly, tilemap, npcPositions, 0, corrDy, false, plane))
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

        glm::vec2 testPos = playerPos + glm::vec2(correction, desiredMovement.y);

        // moving vertically: moveDy matters, moveDx = 0
        if (!CollidesAt(hitbox, testPos, tilemap, npcPositions, 0, moveDy, false, plane))
        {
            desiredMovement.x += correction;
        }
        else
        {
            int corrDx = (correction > 0.0f) ? 1 : -1;
            glm::vec2 testPerpOnly = playerPos + glm::vec2(correction, 0.0f);

            if (!CollidesAt(hitbox, testPerpOnly, tilemap, npcPositions, corrDx, 0, false, plane))
                desiredMovement.x += correction;
        }
    }

    return desiredMovement;
}

std::optional<glm::vec2> HandleStuckRecovery(const Hitbox& hitbox,
                                             glm::vec2 playerPos,
                                             PlayerMovementState& movement,
                                             int plane,
                                             glm::vec2 currentTileCenter,
                                             const Tilemap* tilemap,
                                             const std::vector<glm::vec2>* npcPositions)
{
    if (!tilemap)
    {
        return std::nullopt;
    }

    if (CollidesWithTilesStrict(hitbox, playerPos, tilemap, 0, 0, false, plane))
    {
        // Embedded in a solid tile: report the teleport-out target. The caller
        // applies it via PlayerSystem::SetPositionRaw so the position-snap +
        // motor-reset stay in one place.
        return FindClosestSafeTileCenter(hitbox, playerPos, plane, tilemap, npcPositions);
    }

    movement.lastSafeTileCenter = currentTileCenter;
    return std::nullopt;
}
}  // namespace CollisionSystem
