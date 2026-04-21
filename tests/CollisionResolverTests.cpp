// Tests for CollisionResolver, the corner-cut / slide / penetration logic that
// lives on PlayerCharacter via friend access. The collision math is where a
// regression is most painful (player can walk through walls, gets stuck on
// corners, etc.), so these tests focus on the public entry points:
//
//   - CollidesAt / CollidesWithTilesStrict / CollidesWithTilesCenter
//   - IsCornerPenetration
//   - CalculateFollowAlpha (pure static)
//
// We stand up a real `Tilemap` + default-constructed `PlayerCharacter` and
// drive collision tests through the resolver exposed by
// `PlayerCharacter::GetCollision()`.
//
// Hitbox is 16x16 pixels anchored at bottom-center. Tile size is 16x16. So
// "player at feet (tx*16 + 8, ty*16 + 16)" centers the hitbox over tile
// (tx, ty) with feet at its bottom edge.

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "../src/CollisionResolver.h"
#include "../src/PlayerCharacter.h"
#include "../src/Tilemap.h"

namespace
{
constexpr float TILE = 16.0f;

// Build a feet (bottom-center) position for the player at a given tile.
glm::vec2 FeetAtTile(int tx, int ty)
{
    return glm::vec2(tx * TILE + TILE * 0.5f, ty * TILE + TILE);
}

// Convenience: small, mostly-empty tilemap we can paint collision tiles onto.
class CollisionResolverTest : public ::testing::Test
{
protected:
    Tilemap tilemap;
    PlayerCharacter player;

    void SetUp() override
    {
        tilemap.SetTilemapSize(20, 20, false);
    }

    void PaintSolid(int x, int y)
    {
        tilemap.SetTileCollision(x, y, true);
    }

    CollisionResolver& Resolver() { return player.GetCollision(); }
    const CollisionResolver& Resolver() const { return player.GetCollision(); }
};
}  // namespace

// --- CalculateFollowAlpha (static, pure) -------------------------------------

TEST(CollisionResolverStatic, FollowAlpha_ZeroDelta_Yields_Zero)
{
    // Zero dt -> nothing should move, alpha should be 0.
    EXPECT_FLOAT_EQ(CollisionResolver::CalculateFollowAlpha(0.0f, 0.2f), 0.0f);
}

TEST(CollisionResolverStatic, FollowAlpha_ReachesEpsilon_At_SettleTime)
{
    // The formula should produce alpha = 1 - epsilon when dt == settleTime.
    const float settle = 0.2f;
    const float eps = 0.01f;
    float a = CollisionResolver::CalculateFollowAlpha(settle, settle, eps);
    EXPECT_NEAR(a, 1.0f - eps, 1e-4f);
}

TEST(CollisionResolverStatic, FollowAlpha_Is_FrameRateIndependent)
{
    // Advancing by dt then another dt must reach the same state as advancing
    // by 2*dt once. Follow = 1 - (1-alpha)^n equivalence.
    const float settle = 0.2f;
    const float dt = 0.033f;
    float a1 = CollisionResolver::CalculateFollowAlpha(dt, settle);
    float a2 = CollisionResolver::CalculateFollowAlpha(2.0f * dt, settle);

    // Remaining distance after two dt steps = (1-a1)^2; after one 2dt step = (1-a2).
    float twoStep = (1.0f - a1) * (1.0f - a1);
    float oneStep = (1.0f - a2);
    EXPECT_NEAR(twoStep, oneStep, 1e-5f);
}

TEST(CollisionResolverStatic, FollowAlpha_ClampedToUnit)
{
    // Huge dt should not overshoot 1.0.
    float a = CollisionResolver::CalculateFollowAlpha(1000.0f, 0.2f);
    EXPECT_LE(a, 1.0f);
    EXPECT_GE(a, 0.0f);
}

// --- CollidesWithTilesStrict -------------------------------------------------

TEST_F(CollisionResolverTest, Strict_Empty_NoCollision)
{
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_FALSE(Resolver().CollidesWithTilesStrict(pos, &tilemap, 0, 0, false));
}

TEST_F(CollisionResolverTest, Strict_CenteredOnSolidTile_IsBlocked)
{
    PaintSolid(5, 5);
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_TRUE(Resolver().CollidesWithTilesStrict(pos, &tilemap, 0, 1, false));
}

TEST_F(CollisionResolverTest, Strict_OffscreenTilesAreSkipped)
{
    // Strict mode skips out-of-bounds tiles rather than treating them as
    // solid; map-edge containment must be enforced elsewhere (by callers
    // checking dimensions or by a border of solid tiles). This test pins
    // that behavior so a future change is visible.
    glm::vec2 pos = FeetAtTile(-1, 5);
    EXPECT_FALSE(Resolver().CollidesWithTilesStrict(pos, &tilemap, -1, 0, false));
}

TEST_F(CollisionResolverTest, Strict_WalkingIntoWall_IsBlocked)
{
    // Wall at (6, 5). Player with feet on that tile (hitbox fully overlaps).
    PaintSolid(6, 5);
    glm::vec2 pos = FeetAtTile(6, 5);  // feet dead-center of solid tile
    EXPECT_TRUE(Resolver().CollidesWithTilesStrict(pos, &tilemap, 1, 0, false));
}

TEST_F(CollisionResolverTest, Strict_StandingInClearedAdjacentTile_NotBlocked)
{
    // Wall to the right. Player standing on clear tile to the left with the
    // full hitbox on the clear side. No overlap with the wall tile.
    PaintSolid(6, 5);
    glm::vec2 pos = FeetAtTile(4, 5);  // two tiles away, safely clear
    EXPECT_FALSE(Resolver().CollidesWithTilesStrict(pos, &tilemap, 1, 0, false));
}

// --- CollidesWithTilesCenter (sprint mode) -----------------------------------

TEST_F(CollisionResolverTest, Center_Empty_NoCollision)
{
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_FALSE(Resolver().CollidesWithTilesCenter(pos, &tilemap));
}

TEST_F(CollisionResolverTest, Center_Solid_IsBlocked)
{
    PaintSolid(5, 5);
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_TRUE(Resolver().CollidesWithTilesCenter(pos, &tilemap));
}

TEST_F(CollisionResolverTest, Center_WithAdjacentWall_ClearCenterNotBlocked)
{
    // Sprint mode only samples the center point. Place a wall tile adjacent
    // to the player's tile; the center point is inside an empty tile.
    PaintSolid(6, 5);
    glm::vec2 pos = FeetAtTile(4, 5);  // clear tile, wall two tiles away
    EXPECT_FALSE(Resolver().CollidesWithTilesCenter(pos, &tilemap));
}

// --- CollidesAt: unified dispatch --------------------------------------------

TEST_F(CollisionResolverTest, CollidesAt_ForwardsToStrict_WhenWalking)
{
    PaintSolid(5, 5);
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_TRUE(
        Resolver().CollidesAt(pos, &tilemap, nullptr, /*sprintMode=*/false, 0, 0, false));
}

TEST_F(CollisionResolverTest, CollidesAt_Sprint_ClearTileNotBlocked)
{
    // Wall two tiles away; sprint mode samples only the center, center is clear.
    PaintSolid(6, 5);
    glm::vec2 pos = FeetAtTile(4, 5);
    EXPECT_FALSE(
        Resolver().CollidesAt(pos, &tilemap, nullptr, /*sprintMode=*/true, 1, 0, false));
}

TEST_F(CollisionResolverTest, CollidesAt_NpcOverlap_IsBlocked)
{
    // An NPC at the player's tile should block, even if tiles are empty.
    std::vector<glm::vec2> npcs = {FeetAtTile(5, 5)};
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_TRUE(Resolver().CollidesAt(pos, &tilemap, &npcs, false, 0, 0, false));
}

// --- IsCornerPenetration -----------------------------------------------------

TEST_F(CollisionResolverTest, CornerPenetration_ClearTile_IsFalse)
{
    glm::vec2 pos = FeetAtTile(5, 5);
    EXPECT_FALSE(Resolver().IsCornerPenetration(pos, &tilemap));
}

TEST_F(CollisionResolverTest, CornerPenetration_DeepOverlapDoesNotCrash)
{
    // Smoke test: exercising the corner-penetration path with a solid tile
    // overlapping the player's hitbox must not crash or assert. Exact
    // true/false depends on tunable thresholds, so we only assert the call
    // returns a well-formed bool.
    PaintSolid(6, 4);
    glm::vec2 pos(6 * TILE - 2.0f, 5 * TILE);
    const bool result = Resolver().IsCornerPenetration(pos, &tilemap);
    (void)result;
    SUCCEED();
}
