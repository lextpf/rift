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
#include "../src/RenderDrawable.hpp"
#include "../src/Tilemap.hpp"

#include <algorithm>

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
                                                            CharacterKinematics::GetSupport(elev));
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
                                                            CharacterKinematics::GetSupport(elev));
    EXPECT_TRUE(blocked);
}

TEST(CollisionZSkip, GroundWallBlocksGroundButNotElevation)
{
    // Ground and elevation are separate collision spaces even at one X/Y cell.
    Tilemap tm = MakeTilemap();
    tm.SetTileCollision(5, 5, true);

    Hitbox hitbox;
    EXPECT_TRUE(CollisionSystem::CollidesWithTilesStrict(
        hitbox, FeetAtTile(5, 5), &tm, 0, 0, false, {SupportSurface::Ground, 0}));
    EXPECT_FALSE(CollisionSystem::CollidesWithTilesStrict(
        hitbox, FeetAtTile(5, 5), &tm, 0, 0, false, {SupportSurface::Elevation, 10}));
}

TEST(CollisionZSkip, DifferentElevationHeightDoesNotCloseCornerEscape)
{
    Tilemap tm = MakeTilemap();
    tm.SetElevation(6, 4, 10);
    tm.SetTileCollision(6, 4, true);
    tm.SetCornerCutBlocked(6, 4, Tilemap::CORNER_BL, true);

    // A shallow diagonal overlap with the 10px railing is a real collision on
    // the deck, but must be invisible while the player is still on the 6px
    // ramp. Collapsing both heights to "Elevation" closes this escape route.
    const glm::vec2 cornerOverlap(90.0f, 92.0f);
    Hitbox hitbox;
    EXPECT_FALSE(CollisionSystem::CollidesWithTilesStrict(
        hitbox, cornerOverlap, &tm, 1, 1, true, {SupportSurface::Elevation, 6}));
    EXPECT_TRUE(CollisionSystem::CollidesWithTilesStrict(
        hitbox, cornerOverlap, &tm, 1, 1, true, {SupportSurface::Elevation, 10}));
}

// --- Support graph ---------------------------------------------------------

TEST(SurfaceTransition, RampEntryCommitsElevation)
{
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 6);
    tm.SetElevation(6, 5, 10);

    const SurfaceTransition transition =
        CharacterKinematics::ResolveSupport(Elevation{}, FeetAtTile(4, 5), FeetAtTile(5, 5), tm);

    ASSERT_TRUE(transition.connected);
    EXPECT_EQ(transition.support.surface, SupportSurface::Elevation);
    EXPECT_EQ(transition.support.height, 6);
}

TEST(SurfaceTransition, PerpendicularBridgeCrossingStaysOnImplicitGround)
{
    Tilemap tm = MakeTilemap();
    for (int x = 4; x <= 8; ++x)
    {
        tm.SetElevation(x, 5, 10);
    }

    Elevation elevation;
    const SurfaceTransition enter =
        CharacterKinematics::ResolveSupport(elevation, FeetAtTile(6, 4), FeetAtTile(6, 5), tm);
    ASSERT_TRUE(enter.connected);
    ASSERT_EQ(enter.support.surface, SupportSurface::Ground);

    CharacterKinematics::CommitSupport(elevation, enter.support);
    const SurfaceTransition leave =
        CharacterKinematics::ResolveSupport(elevation, FeetAtTile(6, 5), FeetAtTile(6, 6), tm);
    ASSERT_TRUE(leave.connected);
    EXPECT_EQ(leave.support.surface, SupportSurface::Ground);
}

TEST(SurfaceTransition, GroundBelowRampCannotJoinDeckFromInsideFootprint)
{
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 6);
    tm.SetElevation(6, 5, 10);

    const SurfaceTransition transition =
        CharacterKinematics::ResolveSupport(Elevation{}, FeetAtTile(5, 5), FeetAtTile(6, 5), tm);

    ASSERT_TRUE(transition.connected);
    EXPECT_EQ(transition.support.surface, SupportSurface::Ground);
    EXPECT_EQ(transition.support.height, 0);
}

TEST(SurfaceTransition, DiagonalRampCornerDoesNotEngage)
{
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 6);
    tm.SetElevation(6, 5, 10);

    const SurfaceTransition transition =
        CharacterKinematics::ResolveSupport(Elevation{}, FeetAtTile(4, 4), FeetAtTile(5, 5), tm);

    ASSERT_TRUE(transition.connected);
    EXPECT_EQ(transition.support.surface, SupportSurface::Ground);
}

TEST(SurfaceTransition, DeckEdgeWithoutRampIsDisconnected)
{
    Tilemap tm = MakeTilemap();
    for (int x = 4; x <= 8; ++x)
    {
        tm.SetElevation(x, 5, 10);
    }

    Elevation elevation;
    CharacterKinematics::CommitSupport(elevation, {SupportSurface::Elevation, 10});
    const SurfaceTransition transition =
        CharacterKinematics::ResolveSupport(elevation, FeetAtTile(6, 5), FeetAtTile(6, 4), tm);

    EXPECT_FALSE(transition.connected);
    EXPECT_EQ(transition.support.surface, SupportSurface::Elevation);
    EXPECT_EQ(transition.support.height, 10);
}

TEST(SurfaceCollision, ProbeUsesCandidateSurfaceBeforeAdmittingMove)
{
    Tilemap tm = MakeTilemap();
    tm.SetElevation(5, 5, 6);
    tm.SetElevation(6, 5, 10);
    tm.SetTileCollision(6, 5, true);

    const SupportState current{SupportSurface::Elevation, 6};
    Hitbox hitbox;
    const CollisionSystem::MovementProbeResult probe = CollisionSystem::ProbeMovement(
        hitbox, FeetAtTile(5, 5), FeetAtTile(6, 5), current, &tm, nullptr, 1, 0, false);

    EXPECT_TRUE(probe.transition.connected);
    EXPECT_EQ(probe.transition.support.height, 10);
    EXPECT_TRUE(probe.tileBlocked);
    EXPECT_TRUE(probe.IsBlocked());
    EXPECT_EQ(current.height, 6);
}

TEST(SurfaceCollision, ElevatedCollisionDoesNotBlockUnderpassProbe)
{
    Tilemap tm = MakeTilemap();
    for (int x = 4; x <= 8; ++x)
    {
        tm.SetElevation(x, 5, 10);
    }
    tm.SetTileCollision(6, 5, true);

    Hitbox hitbox;
    const CollisionSystem::MovementProbeResult probe = CollisionSystem::ProbeMovement(
        hitbox, FeetAtTile(6, 4), FeetAtTile(6, 5), {}, &tm, nullptr, 0, 1, false);

    EXPECT_FALSE(probe.IsBlocked());
    EXPECT_EQ(probe.transition.support.surface, SupportSurface::Ground);
}

// --- Elevated render ownership --------------------------------------------

TEST(ElevationRegions, ConnectedFootprintIsLocalAndGroundBesideItHasNoRegion)
{
    Tilemap tm = MakeTilemap(5, 5);
    tm.SetElevation(1, 1, 6);
    tm.SetElevation(2, 1, 10);
    tm.SetElevation(4, 4, 10);

    const int bridgeRegion = tm.GetElevationRegionId(1, 1);
    ASSERT_GE(bridgeRegion, 0);
    EXPECT_EQ(tm.GetElevationRegionId(2, 1), bridgeRegion);
    EXPECT_NE(tm.GetElevationRegionId(4, 4), bridgeRegion);
    EXPECT_EQ(tm.GetElevationRegionId(1, 2), -1);
    EXPECT_EQ(tm.GetElevationRegionIdAtWorldPos(FeetAtTile(1, 2).x, FeetAtTile(1, 2).y), -1);
}

TEST(DepthSortedTiles, ElevatedObjectTileLeavesGroundLayersBelow)
{
    Tilemap tm = MakeTilemap(4, 4);
    tm.SetElevation(1, 1, 10);
    tm.SetLayerTile(1, 1, 0, 7);
    tm.SetLayerTile(1, 1, 1, 8);
    tm.SetLayerTile(1, 1, 2, 9);

    EXPECT_FALSE(tm.IsDepthSortedTile(1, 1, 0));
    EXPECT_FALSE(tm.IsDepthSortedTile(1, 1, 1));
    EXPECT_TRUE(tm.IsDepthSortedTile(1, 1, 2));

    const auto& tiles = tm.GetVisibleDepthSortedTiles(glm::vec2(0.0f), glm::vec2(4.0f * TILE));
    ASSERT_EQ(tiles.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(tiles.front().layer, 2);
    EXPECT_FLOAT_EQ(tiles.front().anchorY, 2.0f * TILE);
    EXPECT_FLOAT_EQ(tiles.front().supportHeight, 10.0f);
    EXPECT_EQ(tiles.front().supportSurface, SupportSurface::Elevation);
}

TEST(DepthSortedTiles, StructureArtworkInheritsElevationOutsideWalkableFootprint)
{
    Tilemap tm = MakeTilemap(4, 4);
    tm.SetElevation(1, 1, 10);

    constexpr size_t layer = 2;
    constexpr int oneBasedLayer = 3;
    constexpr int structureId = 42;
    tm.SetLayerTile(1, 1, layer, 9);
    tm.SetLayerYSortPlus(1, 1, layer, true);
    tm.SetTileStructureId(1, 1, oneBasedLayer, structureId);

    // Diagonal artwork has no elevation of its own and is not in the same
    // vertical Y-sort stack. Explicit structure membership is what associates
    // it with the elevated surface.
    tm.SetLayerTile(0, 0, layer, 10);
    tm.SetLayerYSortPlus(0, 0, layer, true);
    tm.SetTileStructureId(0, 0, oneBasedLayer, structureId);

    const auto& tiles = tm.GetVisibleDepthSortedTiles(glm::vec2(0.0f), glm::vec2(4.0f * TILE));
    const auto overhang = std::find_if(
        tiles.begin(), tiles.end(), [](const auto& tile) { return tile.x == 0 && tile.y == 0; });

    ASSERT_NE(overhang, tiles.end());
    EXPECT_EQ(overhang->supportSurface, SupportSurface::Elevation);
    EXPECT_FLOAT_EQ(overhang->supportHeight, 10.0f);
    EXPECT_EQ(overhang->surfaceRegionId, tm.GetElevationRegionId(1, 1));
}

TEST(DepthSortedTiles, ConnectedYSortArtworkInheritsRegionWithoutStructureId)
{
    Tilemap tm = MakeTilemap(4, 4);
    tm.SetElevation(1, 1, 10);

    constexpr size_t layer = 2;
    tm.SetLayerTile(1, 1, layer, 9);
    tm.SetLayerYSortPlus(1, 1, layer, true);
    tm.SetLayerTile(0, 1, layer, 10);
    tm.SetLayerYSortPlus(0, 1, layer, true);

    const auto& tiles = tm.GetVisibleDepthSortedTiles(glm::vec2(0.0f), glm::vec2(4.0f * TILE));
    const auto overhang = std::find_if(
        tiles.begin(), tiles.end(), [](const auto& tile) { return tile.x == 0 && tile.y == 1; });

    ASSERT_NE(overhang, tiles.end());
    EXPECT_EQ(overhang->surfaceRegionId, tm.GetElevationRegionId(1, 1));
    EXPECT_TRUE(overhang->authoredYSort);
}

TEST(DepthSortedTiles, ForegroundRampStackRetainsExplicitYSortPhase)
{
    Tilemap tm = MakeTilemap(4, 4);
    tm.SetElevation(1, 1, 10);
    tm.SetElevation(1, 2, 10);

    // Mirrors the real bridge ramp: a foreground Y-sort column extends one
    // ground cell below its elevated footprint.
    constexpr size_t foregroundLayer = 5;
    for (int y = 1; y <= 3; ++y)
    {
        tm.SetLayerTile(1, y, foregroundLayer, 9 + y);
        tm.SetLayerYSortPlus(1, y, foregroundLayer, true);
    }

    const auto& tiles = tm.GetVisibleDepthSortedTiles(glm::vec2(0.0f), glm::vec2(4.0f * TILE));
    const auto bottom = std::find_if(
        tiles.begin(), tiles.end(), [](const auto& tile) { return tile.x == 1 && tile.y == 3; });

    ASSERT_NE(bottom, tiles.end());
    EXPECT_FALSE(bottom->isBackground);
    EXPECT_TRUE(bottom->authoredYSort);
    EXPECT_EQ(bottom->surfaceRegionId, tm.GetElevationRegionId(1, 1));
    EXPECT_FLOAT_EQ(bottom->anchorY, 4.0f * TILE);
    EXPECT_EQ(TileDrawablePhase(*bottom), DrawablePhase::YSorted);
}
