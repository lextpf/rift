// Tests for the z-axis elevation system: auto-derived elevation axis,
// CharacterKinematics plane update with axis-engagement + step-gate, and the
// z-aware skip in CollisionSystem. Pure data paths - no GL/Vulkan
// context is created (per CMakeLists.txt:352-355).

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "../src/CharacterKinematics.hpp"
#include "../src/CollisionSystem.hpp"
#include "../src/Elevation.hpp"
#include "../src/ElevationAxis.hpp"
#include "../src/Hitbox.hpp"
#include "../src/Tilemap.hpp"

namespace
{
constexpr float TILE = 16.0f;

// Build a feet (bottom-center) position for the player at a given tile.
glm::vec2 FeetAtTile(int tx, int ty)
{
    return glm::vec2(tx * TILE + TILE * 0.5f, ty * TILE + TILE);
}

// Small helper: stand up an empty tilemap we can paint elevation onto.
Tilemap MakeTilemap(int w = 20, int h = 20)
{
    Tilemap tm;
    tm.SetTilemapSize(w, h, false);
    return tm;
}
}  // namespace

// --- Tilemap::GetElevationAxisAt -------------------------------------------

TEST(ElevationAxisDerive, DeckCenterReturnsX)
{
    // Horizontal bridge: deck spans X with elevation 10, ground (0) to N/S.
    //   . . . . .   <- N row (elev 0)
    //   . D D D .   <- deck row (elev 10)
    //   . . . . .   <- S row (elev 0)
    Tilemap tm = MakeTilemap();
    tm.SetElevation(4, 5, 10);
    tm.SetElevation(5, 5, 10);
    tm.SetElevation(6, 5, 10);
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::X);
}

TEST(ElevationAxisDerive, VerticalDeckReturnsY)
{
    // Vertical bridge: deck spans Y at elev 10.
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 4, 10);
    tm.SetElevation(5, 5, 10);
    tm.SetElevation(5, 6, 10);
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::Y);
}

TEST(ElevationAxisDerive, RampTipResolvesByGradient)
{
    // Ramp at (5,5) with elev 6, ground (0) to W, deck (10) to E, ground N/S.
    // Gradient |dE_x|=10 > |dE_y|=0 -> X.
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 6);
    tm.SetElevation(6, 5, 10);  // deck east
    // (4,5) stays at 0 (ground west)
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::X);
}

TEST(ElevationAxisDerive, MultiRowRampGradientWinsOverContinuity)
{
    // Three-row-tall horizontal ramp column: ground W, deck E, more ramp N/S
    // sharing this cell's elevation. A continuity-first rule would mistakenly
    // pick Y because eN == eS == z; the gradient |dE_x|=10 > |dE_y|=0 forces
    // the correct X axis so pure A/D engages the climb.
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 4, 6);   // ramp above
    tm.SetElevation(5, 5, 6);   // ramp center (under test)
    tm.SetElevation(5, 6, 6);   // ramp below
    tm.SetElevation(6, 4, 10);  // deck east, top
    tm.SetElevation(6, 5, 10);  // deck east, middle
    tm.SetElevation(6, 6, 10);  // deck east, bottom
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::X);
}

TEST(ElevationAxisDerive, MultiRowDeckCenterReturnsXViaExtentScan)
{
    // 3-row-tall, 5-column-wide horizontal deck at elev 10. Deck-center has
    // all four neighbors at elev 10 so both gradients are zero. The bounded
    // extent scan walks +/-X (long bridge axis) further than +/-Y (3 rows tall),
    // so the axis must resolve to X.
    Tilemap tm = MakeTilemap();
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            tm.SetElevation(5 + dx, 5 + dy, 10);
        }
    }
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::X);
}

TEST(ElevationAxisDerive, MultiColumnDeckCenterReturnsYViaExtentScan)
{
    // Same idea, rotated: 5-row-tall, 3-column-wide vertical deck.
    Tilemap tm = MakeTilemap();
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            tm.SetElevation(5 + dx, 5 + dy, 10);
        }
    }
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::Y);
}

TEST(ElevationAxisDerive, GroundReturnsNone)
{
    // Elevation 0 always returns None so the axis-engagement rule allows
    // entities to fall back to ground from any direction.
    Tilemap tm = MakeTilemap();
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::None);
}

TEST(ElevationAxisDerive, OutOfBoundsTreatedAsGround)
{
    // GetElevation returns 0 for OOB; axis derivation should match.
    Tilemap tm = MakeTilemap();
    EXPECT_EQ(tm.GetElevationAxisAt(-1, -1), ElevationAxis::None);
    EXPECT_EQ(tm.GetElevationAxisAt(9999, 9999), ElevationAxis::None);
}

TEST(ElevationAxisDerive, IsolatedPlatformDefaultsX)
{
    // Single elevated cell with all neighbors at 0: dx == dy == 0, falls
    // through to the documented X default. This is the truly-ambiguous
    // "square platform" case from the spec.
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 10);
    EXPECT_EQ(tm.GetElevationAxisAt(5, 5), ElevationAxis::X);
}

// --- GameCharacter::UpdatePlane -------------------------------------------

TEST(UpdatePlane, PerpendicularToAxisLeavesPlane)
{
    // Player at plane 0 walking N->S into a horizontal-bridge deck cell.
    // Movement is on Y, tile axis is X -> perpendicular -> no engagement.
    Elevation elev;
    EXPECT_EQ(elev.plane, 0);

    CharacterKinematics::UpdatePlane(elev,
                                     /*destTileElev=*/10,
                                     ElevationAxis::X,
                                     /*moveDx=*/0,
                                     /*moveDy=*/1);
    EXPECT_EQ(elev.plane, 0);
}

TEST(UpdatePlane, AxisMatchWithinStepGateEngages)
{
    // Player at plane 6 (mid-ramp) walking +X onto deck (10). Delta = 4
    // which is within MAX_STEP_HEIGHT (8) -> engages.
    Elevation elev;
    CharacterKinematics::UpdatePlane(elev, 6, ElevationAxis::X, 1, 0);  // bring plane up to 6
    EXPECT_EQ(elev.plane, 6);

    CharacterKinematics::UpdatePlane(elev, 10, ElevationAxis::X, 1, 0);
    EXPECT_EQ(elev.plane, 10);
}

TEST(UpdatePlane, DirectGroundToDeckJumpRejected)
{
    // No ramp authored: stepping from plane 0 directly onto a deck (elev 10)
    // exceeds MAX_STEP_HEIGHT (8). Plane must stay 0 (player walks under).
    Elevation elev;
    EXPECT_EQ(elev.plane, 0);

    CharacterKinematics::UpdatePlane(elev, 10, ElevationAxis::X, 1, 0);
    EXPECT_EQ(elev.plane, 0);
}

TEST(UpdatePlane, GroundTileAlwaysEngages)
{
    // Player at plane 10 (on deck) steps onto axis=None ground. The step
    // gate must not block this - falling-off-bridge needs to work even
    // when the drop exceeds MAX_STEP_HEIGHT.
    Elevation elev;
    // Bring plane up: small ramp (4) then deck (10).
    CharacterKinematics::UpdatePlane(elev, 4, ElevationAxis::X, 1, 0);
    CharacterKinematics::UpdatePlane(elev, 10, ElevationAxis::X, 1, 0);
    EXPECT_EQ(elev.plane, 10);

    CharacterKinematics::UpdatePlane(elev, 0, ElevationAxis::None, 0, 1);
    EXPECT_EQ(elev.plane, 0);
}

TEST(UpdatePlane, ZeroMovementOnAxisTileDoesNotEngage)
{
    // No movement (dx=dy=0) onto an X-axis tile: matchesAxis is false
    // because moveDx != 0 is required. Plane stays.
    Elevation elev;
    EXPECT_EQ(elev.plane, 0);

    CharacterKinematics::UpdatePlane(elev, 10, ElevationAxis::X, 0, 0);
    EXPECT_EQ(elev.plane, 0);
}

// --- CollisionResolver z-aware skip ---------------------------------------

TEST(CollisionZSkip, TileAbovePlayerPlaneDoesNotBlock)
{
    // Bridge railing at (5,5) with collision and elevation 10. Player at
    // ground (plane 0) standing on the same tile -> resolver should skip
    // the railing entirely and report no collision.
    Tilemap tm = MakeTilemap();
    tm.SetTileCollision(5, 5, true);
    tm.SetElevation(5, 5, 10);

    Elevation elev;
    Hitbox hitbox;
    EXPECT_EQ(elev.plane, 0);

    bool blocked = CollisionSystem::CollidesWithTilesStrict(hitbox,
                                                            FeetAtTile(5, 5),
                                                            &tm,
                                                            /*moveDx=*/0,
                                                            /*moveDy=*/0,
                                                            /*diagonalInput=*/false,
                                                            elev.plane);
    EXPECT_FALSE(blocked);
}

TEST(CollisionZSkip, TileAtPlayerPlaneStillBlocks)
{
    // Same setup but the player is on the deck (plane 10). Now the railing
    // is at-or-below the player's plane and must block.
    Tilemap tm = MakeTilemap();
    tm.SetTileCollision(5, 5, true);
    tm.SetElevation(5, 5, 10);

    Elevation elev;
    Hitbox hitbox;
    // Climb to plane 10 via a small ramp step that respects the gate.
    CharacterKinematics::UpdatePlane(elev, 4, ElevationAxis::X, 1, 0);
    CharacterKinematics::UpdatePlane(elev, 10, ElevationAxis::X, 1, 0);
    ASSERT_EQ(elev.plane, 10);

    bool blocked = CollisionSystem::CollidesWithTilesStrict(hitbox,
                                                            FeetAtTile(5, 5),
                                                            &tm,
                                                            /*moveDx=*/0,
                                                            /*moveDy=*/0,
                                                            /*diagonalInput=*/false,
                                                            elev.plane);
    EXPECT_TRUE(blocked);
}

TEST(CollisionZSkip, GroundWallBlocksAtAnyPlane)
{
    // A ground-level wall (elev 0) blocks the player regardless of plane.
    // This guards against accidentally over-skipping non-elevated tiles.
    Tilemap tm = MakeTilemap();
    tm.SetTileCollision(5, 5, true);
    // elev defaults to 0

    Elevation elev;
    Hitbox hitbox;
    // Plane stays 0 by default - wall blocks at same plane.
    bool blocked =
        CollisionSystem::CollidesWithTilesStrict(hitbox, FeetAtTile(5, 5), &tm, 0, 0, false, elev.plane);
    EXPECT_TRUE(blocked);
}
