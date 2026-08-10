// Integration tests for momentum-based player movement: drives a real Tilemap +
// a player ECS entity through PlayerSystem::Move (data paths only, no renderer /
// WorldServices). Tile size is 16x16; feet positions are bottom-center anchored.
#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "../src/AnimationState.hpp"
#include "../src/CharacterConstants.hpp"
#include "../src/CharacterKinematics.hpp"
#include "../src/CollisionSystem.hpp"
#include "../src/Elevation.hpp"
#include "../src/EntityStore.hpp"
#include "../src/Hitbox.hpp"
#include "../src/Motor.hpp"
#include "../src/PlayerInputState.hpp"
#include "../src/PlayerModes.hpp"
#include "../src/PlayerSystem.hpp"
#include "../src/Tilemap.hpp"
#include "../src/Transform.hpp"

#include <ecs.hpp>

namespace
{
constexpr float TILE = 16.0f;
constexpr float DT = 1.0f / 60.0f;

float AlignedCenterX(float x)
{
    return std::round((x - TILE * 0.5f) / TILE) * TILE + TILE * 0.5f;
}
float AlignedBottomY(float y)
{
    return std::round(y / TILE) * TILE;
}

class PlayerMovementTest : public ::testing::Test
{
protected:
    ecs::registry world;
    ecs::entity player{};
    Tilemap tilemap;

    void SetUp() override
    {
        tilemap.SetTilemapSize(40, 40, false);
        player = EntityStore::SpawnPlayer(world);
    }

    glm::vec2 Pos() const { return world.get<Transform>(player).position; }
    bool IsMoving() const { return world.get<PlayerInputState>(player).isMoving; }
};
}  // namespace

// Holding right then releasing: the player glides to rest tile-aligned on both axes.
TEST_F(PlayerMovementTest, GlideToRestLandsTileAligned)
{
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 3.0f, 10.0f * TILE + 1.0f));

    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }
    ASSERT_TRUE(IsMoving());

    int frames = 0;
    while (IsMoving() && frames < 2000)
    {
        PlayerSystem::Move(world, player, glm::vec2(0.0f), DT, &tilemap, nullptr);
        ++frames;
    }
    for (int i = 0; i < 60; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(0.0f), DT, &tilemap, nullptr);  // settle
    }

    glm::vec2 pos = Pos();
    EXPECT_NEAR(pos.x, AlignedCenterX(pos.x), 0.6f);
    EXPECT_NEAR(pos.y, AlignedBottomY(pos.y), 0.6f);
}

// Releasing input does not stop the player on the same frame (momentum is felt).
TEST_F(PlayerMovementTest, ReleaseDoesNotStopInstantly)
{
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));
    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }
    glm::vec2 before = Pos();
    PlayerSystem::Move(world, player, glm::vec2(0.0f), DT, &tilemap, nullptr);  // one idle frame
    glm::vec2 after = Pos();
    EXPECT_GT(glm::length(after - before), 0.0f);  // still gliding
    EXPECT_TRUE(IsMoving());
}

// A solid wall to the right stops X movement and does not accumulate velocity.
TEST_F(PlayerMovementTest, WallStopsMovementCleanly)
{
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));
    // Three-tile column so the middle tile is a flat-wall face (no slide-around escape).
    tilemap.SetTileCollision(11, 9, true);
    tilemap.SetTileCollision(11, 10, true);
    tilemap.SetTileCollision(11, 11, true);

    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }
    // Player must not have passed into the solid tile column.
    EXPECT_LT(Pos().x, 11.0f * TILE);
    // Velocity into the wall must not have built up.
    EXPECT_LT(std::abs(world.get<Motor>(player).velocity.x), 5.0f);
}

// Animation stays "moving" through the glide after input is released (velocity-driven),
// unlike the old binary input-driven gate.
TEST_F(PlayerMovementTest, AnimationFollowsVelocityNotInput)
{
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));
    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }
    ASSERT_TRUE(IsMoving());

    PlayerSystem::Move(world, player, glm::vec2(0.0f), DT, &tilemap, nullptr);  // released, gliding
    EXPECT_TRUE(IsMoving());                                                    // still in motion

    int frames = 0;
    while (IsMoving() && frames < 2000)
    {
        PlayerSystem::Move(world, player, glm::vec2(0.0f), DT, &tilemap, nullptr);
        ++frames;
    }
    EXPECT_FALSE(IsMoving());  // eventually idles when velocity reaches zero
}

TEST_F(PlayerMovementTest, InputCaptureStopClearsLatchedWalkAnimation)
{
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));
    for (int i = 0; i < 30; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
        PlayerSystem::Update(world, player, DT);
    }

    ASSERT_TRUE(IsMoving());
    ASSERT_GT(glm::length(world.get<Motor>(player).velocity), 0.0f);
    ASSERT_NE(world.get<PlayerModes>(player).animationType, AnimationType::IDLE);

    // Console input capture skips subsequent movement steps, while the cosmetic
    // update continues. Its explicit stop must clear both sources of walk animation.
    PlayerSystem::Stop(world, player);
    for (int i = 0; i < 60; ++i)
    {
        PlayerSystem::Update(world, player, DT);
    }

    const Motor& motor = world.get<Motor>(player);
    const AnimationState& animation = world.get<AnimationState>(player);
    EXPECT_FALSE(IsMoving());
    EXPECT_FLOAT_EQ(motor.velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(motor.velocity.y, 0.0f);
    EXPECT_EQ(world.get<PlayerModes>(player).animationType, AnimationType::IDLE);
    EXPECT_EQ(animation.currentFrame, 0);
    EXPECT_EQ(animation.walkSequenceIndex, 0);
}

// Regression: rounding a collision-box corner while holding a direction must not
// stutter to a crawl ("jelly"). A perpendicular corner slide redirects the blocked
// forward velocity into a productive move, so the motor must KEEP that velocity
// instead of zeroing it as if pinned against a flat wall. If it is zeroed, the next
// frames ramp the forward speed up from zero, the player drifts slowly back into the
// corner, slides again, and re-zeros - the climb runs at roughly a third of full speed.
TEST_F(PlayerMovementTest, CornerSlideRoundsAtFullSpeedNotJelly)
{
    // One solid tile with clear space above it: holding RIGHT, the player slides up and
    // around the tile. Approach it from a couple tiles to the left (natural ramp + contact)
    // so the player's resting feet land just shy of the wall - the geometry where, with the
    // bug, an up-slide breaks contact and the player must drift slowly back in to re-engage.
    tilemap.SetTileCollision(15, 11, true);
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(13.0f * TILE + 8.0f, 11.0f * TILE + 16.0f));

    constexpr int FRAMES = 55;
    const float startX = Pos().x;
    for (int i = 0; i < FRAMES; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }
    const float forwardProgress = Pos().x - startX;

    // Straight-line full speed over the window would be ~45 px; rounding the tile costs some
    // of that to the upward slide. Measured: ~38 px with the fix, ~29 px with the jelly bug
    // (forward velocity zeroed each slide). 33 px sits cleanly between the two.
    EXPECT_GT(forwardProgress, 33.0f);
}

TEST_F(PlayerMovementTest, LaneSnapCorrectionDoesNotBecomeDiagonalInput)
{
    // The player is moving RIGHT while a gentle lane correction nudges DOWN.
    // The correction grazes the blocked tile's bottom-left corner. Cardinal
    // collision intentionally permits that shallow diagonal-tile overlap; a
    // second probe that reclassifies the correction as diagonal input rejects
    // the exact same movement and leaves the player stuck in place.
    tilemap.SetTileCollision(6, 4, true);
    tilemap.SetCornerCutBlocked(6, 4, Tilemap::CORNER_BL, true);
    PlayerSystem::SetPositionRaw(world, player, glm::vec2(89.0f, 92.0f));

    const glm::vec2 before = Pos();
    PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);

    EXPECT_GT(Pos().x, before.x);
    EXPECT_GT(Pos().y, before.y);
}

// A faster WALK (speed multiplier) advances the walk cycle in fewer frames than a
// normal walk. This isolates *continuous* speed-linked cadence: both players are in the
// WALK state, which the old binary cadence treats identically regardless of speed.
TEST_F(PlayerMovementTest, FasterSpeedAnimatesQuicker)
{
    auto framesForAdvances = [](float speedMult, int advances)
    {
        ecs::registry w;
        ecs::entity p = EntityStore::SpawnPlayer(w);
        PlayerSystem::SetPositionRaw(w, p, glm::vec2(5.0f * TILE + 8.0f, 5.0f * TILE + 16.0f));
        w.get<PlayerModes>(p).speedMultiplier = speedMult;
        // Warm up to steady-state speed so cadence (not the ramp) dominates.
        for (int i = 0; i < 180; ++i)
        {
            PlayerSystem::Move(w, p, glm::vec2(1.0f, 0.0f), DT, nullptr, nullptr);
            PlayerSystem::Update(w, p, DT);
        }
        int seen = 0;
        int frames = 0;
        int last = w.get<AnimationState>(p).walkSequenceIndex;
        while (seen < advances && frames < 5000)
        {
            PlayerSystem::Move(w, p, glm::vec2(1.0f, 0.0f), DT, nullptr, nullptr);
            PlayerSystem::Update(w, p, DT);
            if (w.get<AnimationState>(p).walkSequenceIndex != last)
            {
                last = w.get<AnimationState>(p).walkSequenceIndex;
                ++seen;
            }
            ++frames;
        }
        return frames;
    };

    int normalFrames = framesForAdvances(1.0f, 8);  // ~50 px/s, WALK
    int fastFrames = framesForAdvances(2.0f, 8);    // ~100 px/s, WALK
    EXPECT_LT(fastFrames, normalFrames);
}

TEST_F(PlayerMovementTest, PerpendicularBridgeCrossingWalksUnder)
{
    for (int x = 8; x <= 14; ++x)
    {
        tilemap.SetElevation(x, 10, 10);
    }
    tilemap.SetTileCollision(10, 10, true);
    PlayerSystem::SetPositionRaw(world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE));

    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(0.0f, 1.0f), DT, &tilemap, nullptr);
    }

    EXPECT_GT(Pos().y, 11.0f * TILE);
    EXPECT_EQ(world.get<Elevation>(player).surface, SupportSurface::Ground);
    EXPECT_EQ(world.get<Elevation>(player).plane, 0);
}

TEST_F(PlayerMovementTest, RampEntryPromotesPlayerOntoDeck)
{
    for (int y = 9; y <= 11; ++y)
    {
        tilemap.SetElevation(11, y, 6);
        for (int x = 12; x <= 18; ++x)
        {
            tilemap.SetElevation(x, y, 10);
        }
    }
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));

    for (int i = 0; i < 120; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }

    EXPECT_GT(Pos().x, 12.0f * TILE);
    EXPECT_EQ(world.get<Elevation>(player).surface, SupportSurface::Elevation);
    EXPECT_EQ(world.get<Elevation>(player).plane, 10);
}

TEST_F(PlayerMovementTest, DeckCollisionBlocksBeforeSupportCommit)
{
    for (int y = 8; y <= 12; ++y)
    {
        tilemap.SetElevation(11, y, 6);
        for (int x = 12; x <= 18; ++x)
        {
            tilemap.SetElevation(x, y, 10);
        }
        tilemap.SetTileCollision(12, y, true);
    }
    PlayerSystem::SetPositionRaw(
        world, player, glm::vec2(10.0f * TILE + 8.0f, 10.0f * TILE + 16.0f));

    for (int i = 0; i < 180; ++i)
    {
        PlayerSystem::Move(world, player, glm::vec2(1.0f, 0.0f), DT, &tilemap, nullptr);
    }

    const Elevation& elevation = world.get<Elevation>(player);
    EXPECT_LT(Pos().x, 12.0f * TILE);
    EXPECT_EQ(elevation.surface, SupportSurface::Elevation);
    EXPECT_EQ(elevation.plane, 6);
    EXPECT_FALSE(
        CollisionSystem::CollidesWithTilesStrict(world.get<Hitbox>(player),
                                                 Pos(),
                                                 &tilemap,
                                                 0,
                                                 0,
                                                 false,
                                                 CharacterKinematics::GetSupport(elevation)));
}
