// Regression guard for multi-tile upright structures in the 3D world path.
// No GL/Vulkan context is created here (see the rift_tests constraint in
// CMakeLists.txt) - MockRenderer records the submitted quads instead.
//
// Multi-tile artwork broke apart in two separate ways, one per axis, and both
// are pinned here.
//
// VERTICALLY, a building is authored as a run of tiles that paint one above the
// other in the flat top-down view and read as one tall image. The first 3D
// implementation stood each tile on its own grid row, putting the run at
// different DEPTHS instead of stacking it, and lifted along world vertical
// rather than along the billboard's leaning up axis - so the wall both marched
// away from the camera and split open at every seam.
//
// HORIZONTALLY, each upright quad turns toward the camera about its OWN centre,
// so neighbours standing side by side pivot about different axes and their
// shared edge separates as (1 - cos yaw). A two-tile log visibly broke in half
// as soon as the camera moved. Both axes are fixed by giving the whole run a
// single shared pivot and placing each tile as a slice along the shared axes.
#include "../src/CameraRig.hpp"
#include "../src/MathConstants.hpp"
#include "../src/SceneMath.hpp"
#include "../src/Tilemap.hpp"
#include "MockRenderer.hpp"

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
constexpr float kTol = 1e-3f;
constexpr int kTileSize = 16;

constexpr float Degrees(float d)
{
    return d * rift::PiF / 180.0f;
}

// Layer indices are 0-based for the Set/GetLayer* API. Layer 0 is a background
// layer, so its tiles are flat unless their stance stands them up - which is
// exactly what we want to exercise: the stance, not the layer.
constexpr size_t kLayer = 0;

Tilemap MakeMap(int w = 40, int h = 40)
{
    Tilemap tm;
    tm.SetTilemapSize(w, h, false);
    return tm;
}

// A camera looking at the given world point, angled enough that a mistake in the
// lift axis separates the quads visibly rather than hiding at pitch 90.
cameraRig::RigParams MakeRig(glm::vec2 target)
{
    cameraRig::RigParams rig;
    rig.target = target;
    rig.visibleWorldSize = {320.0f, 180.0f};
    rig.sceneRadius = 2048.0f;
    cameraRig::ApplyPreset(rig, cameraRig::Preset::DS);
    return rig;
}

// Paint a horizontal run of Structure tiles - a log, a wall, a fence rail: the
// shape that splits apart when each tile pivots about itself.
void PaintUprightRow(Tilemap& tm, int leftX, int y, int columnCount)
{
    for (int i = 0; i < columnCount; ++i)
    {
        tm.SetLayerTile(leftX + i, y, kLayer, /*tileID=*/1);
        tm.SetLayerStance(leftX + i, y, kLayer, TileStance::Structure);
    }
}

// Quads sorted west-to-east along the run's own right axis.
std::vector<MockRenderer::Quad3D> SortedByRunOrder(std::vector<MockRenderer::Quad3D> quads)
{
    std::sort(quads.begin(),
              quads.end(),
              [](const MockRenderer::Quad3D& a, const MockRenderer::Quad3D& b)
              {
                  return a.corners[sceneMath::QUAD_BOTTOM_LEFT].x <
                         b.corners[sceneMath::QUAD_BOTTOM_LEFT].x;
              });
    return quads;
}

// Paint a vertical run of Structure tiles, bottom row last.
void PaintUprightColumn(Tilemap& tm, int x, int topRow, int rowCount)
{
    for (int i = 0; i < rowCount; ++i)
    {
        const int y = topRow + i;
        tm.SetLayerTile(x, y, kLayer, /*tileID=*/1);
        tm.SetLayerStance(x, y, kLayer, TileStance::Structure);
    }
}

// Paint a vertical run of one stance - a fence running north-south. Upright like
// the above, but NOT a stacking structure: separate props on their own rows.
void PaintStanceColumn(Tilemap& tm, int x, int topRow, int rowCount, TileStance stance)
{
    for (int i = 0; i < rowCount; ++i)
    {
        const int y = topRow + i;
        tm.SetLayerTile(x, y, kLayer, /*tileID=*/1);
        tm.SetLayerStance(x, y, kLayer, stance);
    }
}

// Quads recorded for one map column, ordered bottom-of-screen first is not
// guaranteed, so sort by height instead.
std::vector<MockRenderer::Quad3D> SortedByHeight(std::vector<MockRenderer::Quad3D> quads)
{
    std::sort(quads.begin(),
              quads.end(),
              [](const MockRenderer::Quad3D& a, const MockRenderer::Quad3D& b)
              {
                  return a.corners[sceneMath::QUAD_BOTTOM_LEFT].y <
                         b.corners[sceneMath::QUAD_BOTTOM_LEFT].y;
              });
    return quads;
}

// Quads sorted north-to-south along scene Z, for a run that advances down a
// single map column (increasing row) rather than along a row.
std::vector<MockRenderer::Quad3D> SortedByDepth(std::vector<MockRenderer::Quad3D> quads)
{
    std::sort(quads.begin(),
              quads.end(),
              [](const MockRenderer::Quad3D& a, const MockRenderer::Quad3D& b)
              {
                  return a.corners[sceneMath::QUAD_BOTTOM_LEFT].z <
                         b.corners[sceneMath::QUAD_BOTTOM_LEFT].z;
              });
    return quads;
}
}  // namespace

TEST(World3DStackingTest, UprightRunStacksIntoOneWall)
{
    Tilemap tm = MakeMap();
    // A 3-tall structure at column 20, occupying rows 10, 11, 12.
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/3);

    MockRenderer renderer;
    const cameraRig::RigParams rig = MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize});
    tm.RenderWorld3D(renderer, rig);

    ASSERT_EQ(renderer.quads3D.size(), 3u);
    const auto quads = SortedByHeight(renderer.quads3D);

    // The run must form ONE continuous wall three tiles long. Measuring the
    // total extent from the base quad's foot to the top quad's head is what
    // distinguishes a wall from the original bug's column-marching-north: a
    // per-row anchored run spans three rows of DEPTH, so its end-to-end length
    // is larger than 3 tiles and does not lie along the billboard up axis.
    const glm::vec3 foot = quads.front().corners[sceneMath::QUAD_BOTTOM_LEFT];
    const glm::vec3 head = quads.back().corners[sceneMath::QUAD_TOP_LEFT];
    EXPECT_NEAR(glm::distance(foot, head), 3.0f * kTileSize, kTol);

    // ...and that extent runs along the quads' own up axis, not off at an angle.
    const glm::vec3 alongWall = glm::normalize(head - foot);
    const glm::vec3 quadUp = glm::normalize(quads.front().corners[sceneMath::QUAD_TOP_LEFT] -
                                            quads.front().corners[sceneMath::QUAD_BOTTOM_LEFT]);
    EXPECT_NEAR(glm::dot(alongWall, quadUp), 1.0f, kTol);

    // Every tile of the run shares the same column.
    for (size_t i = 1; i < quads.size(); ++i)
    {
        EXPECT_NEAR(quads[i].corners[sceneMath::QUAD_BOTTOM_LEFT].x, foot.x, kTol)
            << "quad " << i << " drifted in X";
    }
}

TEST(World3DStackingTest, AdjacentTilesInARunShareAnEdgeExactly)
{
    // The seam test: quad i's TOP edge must be quad i+1's BOTTOM edge. This is
    // what fails if the lift runs along world vertical instead of the leaning
    // billboard up axis - the wall splits open at every tile boundary.
    Tilemap tm = MakeMap();
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/4);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 4u);
    const auto quads = SortedByHeight(renderer.quads3D);

    for (size_t i = 0; i + 1 < quads.size(); ++i)
    {
        const glm::vec3 lowerTopLeft = quads[i].corners[sceneMath::QUAD_TOP_LEFT];
        const glm::vec3 lowerTopRight = quads[i].corners[sceneMath::QUAD_TOP_RIGHT];
        const glm::vec3 upperBottomLeft = quads[i + 1].corners[sceneMath::QUAD_BOTTOM_LEFT];
        const glm::vec3 upperBottomRight = quads[i + 1].corners[sceneMath::QUAD_BOTTOM_RIGHT];

        EXPECT_NEAR(glm::distance(lowerTopLeft, upperBottomLeft), 0.0f, kTol)
            << "gap at seam " << i << " (left)";
        EXPECT_NEAR(glm::distance(lowerTopRight, upperBottomRight), 0.0f, kTol)
            << "gap at seam " << i << " (right)";
    }
}

TEST(World3DStackingTest, RunIsOrderedBottomRowLowest)
{
    // The southern-most map row is the base and must end up lowest in the scene;
    // getting this backwards would render the building upside down.
    Tilemap tm = MakeMap();
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/3);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));
    ASSERT_EQ(renderer.quads3D.size(), 3u);

    // Submission order follows the row scan (north to south), so the FIRST quad
    // is the top of the building and must be the highest.
    const float firstHeight = renderer.quads3D.front().corners[sceneMath::QUAD_BOTTOM_LEFT].y;
    const float lastHeight = renderer.quads3D.back().corners[sceneMath::QUAD_BOTTOM_LEFT].y;
    EXPECT_GT(firstHeight, lastHeight);

    // The base tile stands on the ground plane.
    EXPECT_NEAR(lastHeight, 0.0f, kTol);
}

TEST(World3DStackingTest, LoneUprightTileStandsOnItsOwnCell)
{
    // A fence post or single bush is a run of length 1 and must be unaffected by
    // the base-row search.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Structure);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    const glm::vec3 foot = renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT];
    EXPECT_NEAR(foot.y, 0.0f, kTol);
    // Stands on its own row's south edge.
    EXPECT_NEAR(foot.z, static_cast<float>((12 + 1) * kTileSize), kTol);
}

TEST(World3DStackingTest, SeparateRunsDoNotMerge)
{
    // Two structures with a gap between them keep independent bases; if the
    // downward scan ran through empty tiles they would fuse into one tall wall.
    Tilemap tm = MakeMap();
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/8, /*rowCount=*/2);   // rows 8-9
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/12, /*rowCount=*/2);  // rows 12-13

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 11 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 4u);

    // Exactly two quads sit on the ground - one base per run.
    int onGround = 0;
    for (const auto& quad : renderer.quads3D)
    {
        if (std::abs(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].y) < kTol)
        {
            ++onGround;
        }
    }
    EXPECT_EQ(onGround, 2);
}

TEST(World3DStackingTest, SideBySideTilesShareAnEdgeWhenTheCameraIsYawed)
{
    // The reported bug: a 2-tile-wide log on the ground splits in half as soon
    // as the camera turns. Each upright quad pivots toward the camera about its
    // OWN centre, so neighbours rotate about different axes and their shared
    // edge separates - the gap opening as (1 - cos yaw). Only a shared pivot
    // keeps them welded.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/2);

    cameraRig::RigParams rig = MakeRig({21 * kTileSize, 12 * kTileSize});
    rig.yawRadians = Degrees(40.0f);  // the angle that used to break it

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, rig);

    ASSERT_EQ(renderer.quads3D.size(), 2u);
    const auto quads = SortedByRunOrder(renderer.quads3D);

    // West tile's east edge is exactly the east tile's west edge, top and bottom.
    EXPECT_NEAR(glm::distance(quads[0].corners[sceneMath::QUAD_TOP_RIGHT],
                              quads[1].corners[sceneMath::QUAD_TOP_LEFT]),
                0.0f,
                kTol);
    EXPECT_NEAR(glm::distance(quads[0].corners[sceneMath::QUAD_BOTTOM_RIGHT],
                              quads[1].corners[sceneMath::QUAD_BOTTOM_LEFT]),
                0.0f,
                kTol);
}

TEST(World3DStackingTest, WideRunStaysWeldedAtEveryYaw)
{
    // Sweep a full turn: a run must never open a seam at any angle, not just the
    // one that happened to be tested.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/4);

    for (int deg = -180; deg < 180; deg += 15)
    {
        cameraRig::RigParams rig = MakeRig({22 * kTileSize, 12 * kTileSize});
        rig.yawRadians = Degrees(static_cast<float>(deg));

        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        ASSERT_EQ(renderer.quads3D.size(), 4u) << "yaw " << deg;

        const auto quads = SortedByRunOrder(renderer.quads3D);
        for (size_t i = 0; i + 1 < quads.size(); ++i)
        {
            EXPECT_NEAR(glm::distance(quads[i].corners[sceneMath::QUAD_BOTTOM_RIGHT],
                                      quads[i + 1].corners[sceneMath::QUAD_BOTTOM_LEFT]),
                        0.0f,
                        kTol)
                << "seam " << i << " split at yaw " << deg;
        }
    }
}

TEST(World3DStackingTest, WideRunKeepsItsTotalWidth)
{
    // Welding the seams must not do it by overlapping the slices: the run's
    // end-to-end width is still exactly four tiles.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/4);

    cameraRig::RigParams rig = MakeRig({22 * kTileSize, 12 * kTileSize});
    rig.yawRadians = Degrees(55.0f);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, rig);
    ASSERT_EQ(renderer.quads3D.size(), 4u);

    const auto quads = SortedByRunOrder(renderer.quads3D);
    EXPECT_NEAR(glm::distance(quads.front().corners[sceneMath::QUAD_BOTTOM_LEFT],
                              quads.back().corners[sceneMath::QUAD_BOTTOM_RIGHT]),
                4.0f * kTileSize,
                kTol);
}

TEST(World3DStackingTest, LoneUprightTileIsUnaffectedByTheRunPivot)
{
    // A single tile is a run of one, so it must still stand on its own centre
    // rather than being displaced by the run-centre arithmetic.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Structure);

    cameraRig::RigParams rig = MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize});
    rig.yawRadians = Degrees(70.0f);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, rig);
    ASSERT_EQ(renderer.quads3D.size(), 1u);

    // Bottom edge midpoint is the tile's own foot, at any yaw.
    const glm::vec3 mid = (renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT] +
                           renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_RIGHT]) *
                          0.5f;
    EXPECT_NEAR(mid.x, 20 * kTileSize + kTileSize * 0.5f, kTol);
    EXPECT_NEAR(mid.z, static_cast<float>((12 + 1) * kTileSize), kTol);
}

TEST(World3DStackingTest, WideStructureStaysAnchoredAsTheCameraOrbits)
{
    // The reported bug: buildings and bridge borders drifted off their world
    // positions while orbiting, swinging as they tried to face the camera. A
    // rotating rigid body sweeps everything that is offset from its pivot, so a
    // structure with real width cannot billboard - it must stay grid-locked.
    //
    // The assertion is total invariance: a wide structure's geometry may not
    // depend on the camera's yaw AT ALL.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/4);

    cameraRig::RigParams reference = MakeRig({22 * kTileSize, 12 * kTileSize});
    reference.yawRadians = 0.0f;

    MockRenderer referenceRenderer;
    tm.RenderWorld3D(referenceRenderer, reference);
    ASSERT_EQ(referenceRenderer.quads3D.size(), 4u);
    const auto expected = SortedByRunOrder(referenceRenderer.quads3D);

    for (int deg = -180; deg < 180; deg += 20)
    {
        cameraRig::RigParams rig = reference;
        rig.yawRadians = Degrees(static_cast<float>(deg));

        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        ASSERT_EQ(renderer.quads3D.size(), 4u) << "yaw " << deg;
        const auto actual = SortedByRunOrder(renderer.quads3D);

        for (size_t q = 0; q < actual.size(); ++q)
        {
            for (int c = 0; c < sceneMath::QUAD_CORNER_COUNT; ++c)
            {
                EXPECT_NEAR(glm::distance(actual[q].corners[c], expected[q].corners[c]), 0.0f, kTol)
                    << "tile " << q << " corner " << c << " moved at yaw " << deg;
            }
        }
    }
}

TEST(World3DStackingTest, LoneTileStillTurnsTowardTheCameraButHoldsItsAnchor)
{
    // The counterpart: one-tile artwork - lanterns, stones, tree trunks - is
    // effectively a pole, so it SHOULD keep turning to face the camera. It spins
    // about itself, so its footprint never moves. If this test ever passes by
    // the geometry being frozen, the pivot behaviour has been lost entirely.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Structure);

    const glm::vec2 anchor{20 * kTileSize + kTileSize * 0.5f, (12 + 1) * kTileSize};

    auto footMidpointAtYaw = [&](float degrees)
    {
        cameraRig::RigParams rig = MakeRig(anchor);
        rig.yawRadians = Degrees(degrees);
        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        EXPECT_EQ(renderer.quads3D.size(), 1u);
        return renderer.quads3D;
    };

    const auto straight = footMidpointAtYaw(0.0f);
    const auto turned = footMidpointAtYaw(70.0f);
    ASSERT_EQ(straight.size(), 1u);
    ASSERT_EQ(turned.size(), 1u);

    // It DID turn - the corners are not the same.
    EXPECT_GT(glm::distance(turned[0].corners[sceneMath::QUAD_BOTTOM_LEFT],
                            straight[0].corners[sceneMath::QUAD_BOTTOM_LEFT]),
              0.1f);

    // ...but the anchor it spins about did not move.
    for (const auto& quads : {straight, turned})
    {
        const glm::vec3 mid = (quads[0].corners[sceneMath::QUAD_BOTTOM_LEFT] +
                               quads[0].corners[sceneMath::QUAD_BOTTOM_RIGHT]) *
                              0.5f;
        EXPECT_NEAR(mid.x, anchor.x, kTol);
        EXPECT_NEAR(mid.z, anchor.y, kTol);
    }
}

TEST(World3DStackingTest, TallSingleColumnStillCountsAsAPole)
{
    // Width is what decides, not tile count: a 1-wide, 3-tall tower is still a
    // pole and may pivot. Keyed off the run's COLUMN span, so height must not
    // accidentally lock it to the grid.
    Tilemap tm = MakeMap();
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/3);

    const cameraRig::RigParams straight =
        MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize});
    cameraRig::RigParams turned = straight;
    turned.yawRadians = Degrees(70.0f);

    MockRenderer a;
    MockRenderer b;
    tm.RenderWorld3D(a, straight);
    tm.RenderWorld3D(b, turned);
    ASSERT_EQ(a.quads3D.size(), 3u);
    ASSERT_EQ(b.quads3D.size(), 3u);

    EXPECT_GT(glm::distance(b.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT],
                            a.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT]),
              0.1f);
}

TEST(World3DStackingTest, WideStructureBaseSitsOnItsAuthoredFootprint)
{
    // Not just invariant - invariant at the RIGHT place. Each tile's foot must
    // land on its own authored column, so the structure covers exactly the tiles
    // it was painted on.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/3);

    cameraRig::RigParams rig = MakeRig({21 * kTileSize, 12 * kTileSize});
    rig.yawRadians = Degrees(50.0f);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, rig);
    ASSERT_EQ(renderer.quads3D.size(), 3u);
    const auto quads = SortedByRunOrder(renderer.quads3D);

    for (size_t i = 0; i < quads.size(); ++i)
    {
        const glm::vec3 mid = (quads[i].corners[sceneMath::QUAD_BOTTOM_LEFT] +
                               quads[i].corners[sceneMath::QUAD_BOTTOM_RIGHT]) *
                              0.5f;
        const float expectedX =
            static_cast<float>(20 + static_cast<int>(i)) * kTileSize + kTileSize * 0.5f;
        EXPECT_NEAR(mid.x, expectedX, kTol) << "tile " << i;
        EXPECT_NEAR(mid.z, static_cast<float>((12 + 1) * kTileSize), kTol) << "tile " << i;
    }
}

TEST(World3DStackingTest, GroundDrawsWithoutDepthSoCoplanarTilesCannotFight)
{
    // Every layer of ground art sits at height 0, so stacked tiles are exactly
    // coplanar. That was survivable while tiles were axis-aligned - identical
    // geometry gives bit-identical depth and GL_LEQUAL picks the later draw,
    // which IS the layer order. Rotation broke it: a rotated quad overhangs its
    // cell, and those overlaps are coplanar surfaces with different geometry,
    // so their depths differ by float noise and the winner shimmers as the
    // camera moves.
    //
    // Ground therefore submits with depth disabled outright. Nothing can be
    // behind a flat floor viewed from above, so it never needed depth at all.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerRotation(20, 12, kLayer, 37.0f);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    EXPECT_EQ(renderer.quads3D[0].depth, renderModes::DepthMode::None);
}

TEST(World3DStackingTest, UprightArtworkStillUsesDepth)
{
    // The counterpart: upright artwork and actors DO need depth against each
    // other, so only the flat floor opts out.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Structure);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    EXPECT_EQ(renderer.quads3D[0].depth, renderModes::DepthMode::TestAndWrite);
}

TEST(World3DStackingTest, AllGroundIsSubmittedBeforeAnyUprightArtwork)
{
    // Ground has no depth, so its only ordering is submission order - it must
    // therefore all land before the upright pass, or a wall would be painted
    // over by the floor beside it. Also keeps the batch whole: DepthMode is
    // pipeline state, so interleaving would flush between alternating tiles.
    Tilemap tm = MakeMap();
    for (int x = 18; x <= 22; ++x)
    {
        tm.SetLayerTile(x, 12, kLayer, 1);  // ground
        tm.SetLayerTile(x, 11, kLayer, 1);  // more ground
        // Some upright, interleaved with flat, so the partition cannot be an
        // accident of authoring order.
        tm.SetLayerStance(
            x, 11, kLayer, (x % 2 == 0) ? TileStance::Structure : TileStance::Flat);
    }

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize, 12 * kTileSize}));
    ASSERT_GT(renderer.quads3D.size(), 4u);

    bool seenUpright = false;
    for (const auto& quad : renderer.quads3D)
    {
        if (quad.depth == renderModes::DepthMode::TestAndWrite)
        {
            seenUpright = true;
        }
        else
        {
            EXPECT_FALSE(seenUpright) << "ground submitted after upright artwork";
        }
    }
    EXPECT_TRUE(seenUpright) << "test painted no upright tiles";
}

TEST(World3DStackingTest, YSortFlagsAloneLeaveATileFlat)
{
    // Regression guard for the rule this stance enum replaced. IsUpright used to
    // be `noProjection || ySortPlus || ySortMinus`, which stood every flat decal
    // an author had tagged for sorting on its edge - 387 cells of the shipped map.
    //
    // The flat pipeline settles the argument: there the two y-sort flags change
    // draw ORDER only and never reach a geometry branch. They keep that meaning
    // and no longer touch geometry, so a tile carrying nothing but a y-sort flag
    // is ground artwork in the depth-free pass.
    for (const bool useMinus : {false, true})
    {
        Tilemap tm = MakeMap();
        tm.SetLayerTile(20, 12, kLayer, 1);
        if (useMinus)
        {
            tm.SetLayerYSortMinus(20, 12, kLayer, true);
        }
        else
        {
            tm.SetLayerYSortPlus(20, 12, kLayer, true);
        }

        MockRenderer renderer;
        tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

        ASSERT_EQ(renderer.quads3D.size(), 1u) << "ySortMinus=" << useMinus;
        EXPECT_EQ(renderer.quads3D[0].depth, renderModes::DepthMode::None)
            << "ySortMinus=" << useMinus;

        // Lying on the floor: every corner is on the ground plane.
        for (const glm::vec3& corner : renderer.quads3D[0].corners)
        {
            EXPECT_NEAR(corner.y, 0.0f, kTol) << "ySortMinus=" << useMinus;
        }
    }
}

TEST(World3DStackingTest, AWallColumnRecedesAlongTheGroundInsteadOfStacking)
{
    // The reported bug: a fence running north-south was read as a building and
    // climbed into a tower. A vertical run of tiles is ambiguous - one tall
    // image, or a line of separate panels receding into the distance - and only
    // TileStance::Structure says "stacks". Three fence panels are three panels.
    Tilemap tm = MakeMap();
    PaintStanceColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/3, TileStance::Wall);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 11 * kTileSize}));
    ASSERT_EQ(renderer.quads3D.size(), 3u);

    // Every panel stands on the ground...
    for (const auto& quad : renderer.quads3D)
    {
        EXPECT_NEAR(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].y, 0.0f, kTol)
            << "a fence panel was lifted into the air";
        // ...and genuinely stands: its top edge is above its base. Checking the
        // scene-Y rise rather than the edge length is deliberate - a flat ground
        // quad has the same one-tile edge length, so length alone would pass
        // whether the panel stood up or lay down.
        EXPECT_GT(quad.corners[sceneMath::QUAD_TOP_LEFT].y,
                  quad.corners[sceneMath::QUAD_BOTTOM_LEFT].y)
            << "a fence panel is lying on the ground";
        // ...exactly one tile tall. (Measured along the quad's own edge: it
        // leans, so its extent in scene Y alone is shorter than a tile.)
        EXPECT_NEAR(glm::distance(quad.corners[sceneMath::QUAD_TOP_LEFT],
                                  quad.corners[sceneMath::QUAD_BOTTOM_LEFT]),
                    static_cast<float>(kTileSize),
                    kTol);
    }

    // ...and each on its OWN row, so the fence recedes north rather than
    // collapsing onto one depth the way a stacked wall does.
    std::vector<float> depths;
    depths.reserve(renderer.quads3D.size());
    for (const auto& quad : renderer.quads3D)
    {
        depths.push_back(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].z);
    }
    std::sort(depths.begin(), depths.end());
    for (size_t i = 0; i < depths.size(); ++i)
    {
        EXPECT_NEAR(depths[i], static_cast<float>((10 + static_cast<int>(i) + 1) * kTileSize), kTol)
            << "post " << i << " is not on its own row";
    }
}

TEST(World3DStackingTest, APropBelowABuildingIsNotItsGroundFloor)
{
    // The base-row search walks only Structure artwork. A bush or fence post
    // painted directly south of a building must not become that building's
    // foundation - which would lift the whole structure by one tile and leave
    // the prop welded to a wall it has nothing to do with.
    Tilemap tm = MakeMap();
    PaintUprightColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/2);  // building, rows 10-11
    tm.SetLayerTile(20, 12, kLayer, 1);                               // prop, row 12
    tm.SetLayerStance(20, 12, kLayer, TileStance::Prop);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 11 * kTileSize}));
    ASSERT_EQ(renderer.quads3D.size(), 3u);

    // Two feet on the ground: the building's base and the prop. If the prop had
    // been absorbed as the base row there would be only one.
    int onGround = 0;
    for (const auto& quad : renderer.quads3D)
    {
        if (std::abs(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].y) < kTol)
        {
            ++onGround;
        }
    }
    EXPECT_EQ(onGround, 2);

    // The building stands on row 11, the prop on row 12 - each on its own south
    // edge, one tile apart in depth.
    std::vector<float> groundDepths;
    groundDepths.reserve(renderer.quads3D.size());
    for (const auto& quad : renderer.quads3D)
    {
        if (std::abs(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].y) < kTol)
        {
            groundDepths.push_back(quad.corners[sceneMath::QUAD_BOTTOM_LEFT].z);
        }
    }
    ASSERT_EQ(groundDepths.size(), 2u);
    std::sort(groundDepths.begin(), groundDepths.end());
    EXPECT_NEAR(groundDepths[0], static_cast<float>((11 + 1) * kTileSize), kTol);
    EXPECT_NEAR(groundDepths[1], static_cast<float>((12 + 1) * kTileSize), kTol);
}

TEST(World3DStackingTest, APropDoesNotJoinAnAdjacentStructuresRun)
{
    // The horizontal counterpart. A Structure run welds its tiles into one rigid
    // body and a body wider than a tile is grid-locked, so a lone post touching
    // the east end of a wall must not be frozen along with it. A Prop does not
    // scan for a run at all, which makes this structural rather than incidental.
    Tilemap tm = MakeMap();
    PaintUprightRow(tm, /*leftX=*/20, /*y=*/12, /*columnCount=*/2);  // building, columns 20-21
    tm.SetLayerTile(22, 12, kLayer, 1);                              // post, column 22
    tm.SetLayerStance(22, 12, kLayer, TileStance::Prop);

    const glm::vec2 target{21 * kTileSize, 12 * kTileSize};

    auto quadsAtYaw = [&](float degrees)
    {
        cameraRig::RigParams rig = MakeRig(target);
        rig.yawRadians = Degrees(degrees);
        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        return SortedByRunOrder(renderer.quads3D);
    };

    const auto straight = quadsAtYaw(0.0f);
    const auto turned = quadsAtYaw(60.0f);
    ASSERT_EQ(straight.size(), 3u);
    ASSERT_EQ(turned.size(), 3u);

    // The two wall tiles are grid-locked: unchanged by the orbit.
    for (size_t i = 0; i < 2; ++i)
    {
        for (int c = 0; c < sceneMath::QUAD_CORNER_COUNT; ++c)
        {
            EXPECT_NEAR(glm::distance(turned[i].corners[c], straight[i].corners[c]), 0.0f, kTol)
                << "wall tile " << i << " corner " << c << " moved";
        }
    }

    // The post is a pole of its own and still turns...
    EXPECT_GT(glm::distance(turned[2].corners[sceneMath::QUAD_BOTTOM_LEFT],
                            straight[2].corners[sceneMath::QUAD_BOTTOM_LEFT]),
              0.1f)
        << "the post was absorbed into the wall's run and frozen with it";

    // ...about its own authored anchor.
    const glm::vec3 mid = (turned[2].corners[sceneMath::QUAD_BOTTOM_LEFT] +
                           turned[2].corners[sceneMath::QUAD_BOTTOM_RIGHT]) *
                          0.5f;
    EXPECT_NEAR(mid.x, 22 * kTileSize + kTileSize * 0.5f, kTol);
    EXPECT_NEAR(mid.z, static_cast<float>((12 + 1) * kTileSize), kTol);
}

TEST(World3DStackingTest, NorthSouthWallRunIsGridLocked)
{
    // THE FENCE REGRESSION. A fence running north-south is a run of panels at
    // successive DEPTHS, so the horizontal run scan - which only ever looked
    // along X - measured each one as width 1 and gave it the pivoting
    // orientation. Every panel then twisted about its own centre, and because a
    // turned card's edges sweep +/-(tileW/2)*sin(yaw) in Z, the run opened up
    // like venetian blinds.
    //
    // TileStance::Wall says "surface" outright, so no scan and no neighbour
    // decides anything: the panels stay parallel at every camera angle.
    Tilemap tm = MakeMap();
    PaintStanceColumn(tm, /*x=*/20, /*topRow=*/10, /*rowCount=*/3, TileStance::Wall);

    const glm::vec2 target{20 * kTileSize + kTileSize * 0.5f, 11 * kTileSize};

    auto quadsAtYaw = [&](float degrees)
    {
        cameraRig::RigParams rig = MakeRig(target);
        rig.yawRadians = Degrees(degrees);
        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        return SortedByHeight(renderer.quads3D);
    };

    const auto straight = quadsAtYaw(0.0f);
    ASSERT_EQ(straight.size(), 3u);

    for (const float degrees : {-120.0f, -45.0f, 30.0f, 60.0f, 150.0f})
    {
        const auto turned = quadsAtYaw(degrees);
        ASSERT_EQ(turned.size(), 3u) << "yaw " << degrees;
        for (size_t i = 0; i < turned.size(); ++i)
        {
            for (int c = 0; c < sceneMath::QUAD_CORNER_COUNT; ++c)
            {
                EXPECT_NEAR(glm::distance(turned[i].corners[c], straight[i].corners[c]), 0.0f, kTol)
                    << "panel " << i << " corner " << c << " swung at yaw " << degrees;
            }
        }
    }
}

TEST(World3DStackingTest, AdjacentPropsBothStillTurn)
{
    // The false positive bought out by authoring surface-vs-pole instead of
    // deriving it. Under the old rule a horizontal run longer than one tile was
    // grid-locked, so two unrelated bushes placed side by side both silently
    // stopped facing the camera. A Prop is a pole whatever it is standing next to.
    Tilemap tm = MakeMap();
    for (int x = 20; x <= 21; ++x)
    {
        tm.SetLayerTile(x, 12, kLayer, 1);
        tm.SetLayerStance(x, 12, kLayer, TileStance::Prop);
    }

    const glm::vec2 target{21 * kTileSize, 12 * kTileSize};

    auto quadsAtYaw = [&](float degrees)
    {
        cameraRig::RigParams rig = MakeRig(target);
        rig.yawRadians = Degrees(degrees);
        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);
        return SortedByRunOrder(renderer.quads3D);
    };

    const auto straight = quadsAtYaw(0.0f);
    const auto turned = quadsAtYaw(60.0f);
    ASSERT_EQ(straight.size(), 2u);
    ASSERT_EQ(turned.size(), 2u);

    for (size_t i = 0; i < turned.size(); ++i)
    {
        // Each turned...
        EXPECT_GT(glm::distance(turned[i].corners[sceneMath::QUAD_BOTTOM_LEFT],
                                straight[i].corners[sceneMath::QUAD_BOTTOM_LEFT]),
                  0.1f)
            << "prop " << i << " was frozen by its neighbour";

        // ...about its own authored cell, so neither wandered off its footprint.
        const glm::vec3 mid = (turned[i].corners[sceneMath::QUAD_BOTTOM_LEFT] +
                               turned[i].corners[sceneMath::QUAD_BOTTOM_RIGHT]) *
                              0.5f;
        EXPECT_NEAR(mid.x, (20 + static_cast<int>(i)) * kTileSize + kTileSize * 0.5f, kTol);
        EXPECT_NEAR(mid.z, static_cast<float>((12 + 1) * kTileSize), kTol);
    }
}

TEST(World3DStackingTest, AdjacentWallsShareAnEdgeExactlyAtEveryYaw)
{
    // A Wall needs no shared-pivot machinery: at yawFollow 0 its right axis is
    // exactly world +X, so placing it at its own cell gives the same corners a
    // run pivot would - and two Walls side by side derive their common edge from
    // the identical grid expression. This is the same argument that lets ground
    // quads drop the flat path's seamFix fudge.
    Tilemap tm = MakeMap();
    for (int x = 20; x <= 21; ++x)
    {
        tm.SetLayerTile(x, 12, kLayer, 1);
        tm.SetLayerStance(x, 12, kLayer, TileStance::Wall);
    }

    for (const float degrees : {0.0f, 35.0f, 90.0f, -75.0f})
    {
        cameraRig::RigParams rig = MakeRig({21 * kTileSize, 12 * kTileSize});
        rig.yawRadians = Degrees(degrees);
        MockRenderer renderer;
        tm.RenderWorld3D(renderer, rig);

        const auto quads = SortedByRunOrder(renderer.quads3D);
        ASSERT_EQ(quads.size(), 2u) << "yaw " << degrees;
        EXPECT_NEAR(glm::distance(quads[0].corners[sceneMath::QUAD_TOP_RIGHT],
                                  quads[1].corners[sceneMath::QUAD_TOP_LEFT]),
                    0.0f,
                    kTol)
            << "top seam opened at yaw " << degrees;
        EXPECT_NEAR(glm::distance(quads[0].corners[sceneMath::QUAD_BOTTOM_RIGHT],
                                  quads[1].corners[sceneMath::QUAD_BOTTOM_LEFT]),
                    0.0f,
                    kTol)
            << "bottom seam opened at yaw " << degrees;
    }
}

TEST(World3DStackingTest, ElevationWithoutARoleDoesNotDisplaceTiles)
{
    // Elevation alone still changes nothing. The per-cell value drives collision,
    // the support graph and walkability; only an authored ElevationRole makes a
    // LAYER rise to it. This is what keeps an unmarked map rendering exactly as it
    // did before the field existed.
    Tilemap plain = MakeMap();
    plain.SetLayerTile(20, 12, kLayer, 1);

    Tilemap elevated = MakeMap();
    elevated.SetLayerTile(20, 12, kLayer, 1);
    elevated.SetElevation(20, 12, 5);

    const cameraRig::RigParams rig = MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize});

    MockRenderer plainRenderer;
    plain.RenderWorld3D(plainRenderer, rig);
    MockRenderer elevatedRenderer;
    elevated.RenderWorld3D(elevatedRenderer, rig);

    ASSERT_EQ(plainRenderer.quads3D.size(), 1u);
    ASSERT_EQ(elevatedRenderer.quads3D.size(), 1u);
    for (int c = 0; c < sceneMath::QUAD_CORNER_COUNT; ++c)
    {
        EXPECT_EQ(plainRenderer.quads3D[0].corners[c], elevatedRenderer.quads3D[0].corners[c])
            << "corner " << c << " moved without a role";
    }
}

TEST(World3DStackingTest, RaisedTilesSitAtTheirCellElevation)
{
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetElevation(20, 12, 6);
    tm.SetLayerElevationRole(20, 12, kLayer, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    for (const glm::vec3& corner : renderer.quads3D[0].corners)
    {
        EXPECT_NEAR(corner.y, 6.0f, kTol);
    }
}

TEST(World3DStackingTest, LayersAtOneCellRiseIndependently)
{
    // THE case the per-layer decision exists for: water painted on layer 0 stays
    // on the ground while the bridge deck on layer 2 lifts. Lifting per CELL
    // instead would carry the water up with the bridge.
    Tilemap tm = MakeMap();
    tm.SetElevation(20, 12, 6);
    tm.SetLayerTile(20, 12, 0, 1);
    tm.SetLayerTile(20, 12, 2, 1);
    tm.SetLayerElevationRole(20, 12, 2, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 2u);
    const auto quads = SortedByHeight(renderer.quads3D);
    EXPECT_NEAR(quads.front().corners[sceneMath::QUAD_BOTTOM_LEFT].y, 0.0f, kTol)
        << "the unmarked layer was lifted with the cell";
    EXPECT_NEAR(quads.back().corners[sceneMath::QUAD_BOTTOM_LEFT].y, 6.0f, kTol);
}

TEST(World3DStackingTest, ARampRunMeetsTheDeckWithNoSeam)
{
    // The shipped bridge reproduced cell for cell along X: bare ground, ramps at 2
    // and 4, deck at 6. Each cell's high edge must equal the next cell's low edge,
    // and the run must land on the deck exactly rather than at an average.
    Tilemap tm = MakeMap();
    for (int x = 20; x <= 24; ++x)
    {
        tm.SetLayerTile(x, 12, kLayer, 1);
    }
    tm.SetElevation(21, 12, 2);
    tm.SetElevation(22, 12, 4);
    tm.SetElevation(23, 12, 6);
    tm.SetElevation(24, 12, 6);
    tm.SetLayerElevationRole(21, 12, kLayer, ElevationRole::Ramp);
    tm.SetLayerElevationRole(22, 12, kLayer, ElevationRole::Ramp);
    tm.SetLayerElevationRole(23, 12, kLayer, ElevationRole::Raised);
    tm.SetLayerElevationRole(24, 12, kLayer, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({22 * kTileSize, 12 * kTileSize}));

    const std::vector<MockRenderer::Quad3D> quads = SortedByRunOrder(renderer.quads3D);
    ASSERT_EQ(quads.size(), 5u);
    // If this count is lower, the frustum cull dropped an end of the run rather
    // than the geometry being wrong. Widen MakeRig's visibleWorldSize or recentre
    // the rig. Do NOT weaken the assertion to match.

    const auto westY = [](const MockRenderer::Quad3D& q)
    { return q.corners[sceneMath::QUAD_TOP_LEFT].y; };
    const auto eastY = [](const MockRenderer::Quad3D& q)
    { return q.corners[sceneMath::QUAD_TOP_RIGHT].y; };

    EXPECT_NEAR(westY(quads[0]), 0.0f, kTol);  // x=20, bare ground
    EXPECT_NEAR(eastY(quads[0]), 0.0f, kTol);
    EXPECT_NEAR(westY(quads[1]), 0.0f, kTol);  // x=21, ramp 0 -> 3
    EXPECT_NEAR(eastY(quads[1]), 3.0f, kTol);
    EXPECT_NEAR(westY(quads[2]), 3.0f, kTol);  // x=22, ramp 3 -> 6
    EXPECT_NEAR(eastY(quads[2]), 6.0f, kTol);
    EXPECT_NEAR(westY(quads[3]), 6.0f, kTol);  // x=23, deck, level
    EXPECT_NEAR(eastY(quads[3]), 6.0f, kTol);
    EXPECT_NEAR(westY(quads[4]), 6.0f, kTol);  // x=24, deck, level
    EXPECT_NEAR(eastY(quads[4]), 6.0f, kTol);

    for (size_t i = 0; i + 1 < quads.size(); ++i)
    {
        EXPECT_NEAR(eastY(quads[i]), westY(quads[i + 1]), kTol) << "seam " << i << " opened";
    }
}

TEST(World3DStackingTest, ARampReadsNeighboursOnItsOwnLayerOnly)
{
    // A ramp's edge heights come from what the SAME layer does next door. Reading
    // the raw cell elevation instead would let a layer that is not participating
    // pull the ramp up - the per-layer split would leak.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetLayerTile(21, 12, kLayer, 1);
    tm.SetElevation(20, 12, 6);  // neighbour cell IS elevated...
    tm.SetElevation(21, 12, 4);
    // ...but on THIS layer it is Ground, so it must contribute 0.
    tm.SetLayerElevationRole(21, 12, kLayer, ElevationRole::Ramp);

    // A real destination to climb to on the EAST side, so a regression where
    // every ramp resolves flat at 0 cannot pass by coincidence.
    tm.SetLayerTile(22, 12, kLayer, 1);
    tm.SetElevation(22, 12, 6);
    tm.SetLayerElevationRole(22, 12, kLayer, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({21 * kTileSize, 12 * kTileSize}));

    const std::vector<MockRenderer::Quad3D> quads = SortedByRunOrder(renderer.quads3D);
    ASSERT_EQ(quads.size(), 3u);
    // The ramp's west edge must be 0, not 6: the elevated-but-Ground neighbour at
    // x=20 contributes nothing.
    EXPECT_NEAR(quads[1].corners[sceneMath::QUAD_TOP_LEFT].y, 0.0f, kTol)
        << "the ramp read the neighbour's cell elevation instead of its layer role";
    // ...but it DOES climb when the neighbour actually participates.
    EXPECT_NEAR(quads[1].corners[sceneMath::QUAD_TOP_RIGHT].y, 6.0f, kTol)
        << "the ramp failed to climb toward a participating neighbour";
}

TEST(World3DStackingTest, ARampRunMeetsTheDeckWithNoSeamGoingNorthSouth)
{
    // The Y-axis counterpart of ARampRunMeetsTheDeckWithNoSeam. A ramp can also
    // climb north-south down a single column: GetElevationAxisAt must read this
    // as ElevationAxis::Y (the elevation gradient runs along Y, not X), which
    // sends ResolveSurfaceSlope down the alongZ branch - stepping neighbours in
    // Y instead of X - and that path is otherwise only covered indirectly via
    // ApplySlopeHeights, not at the Tilemap level.
    Tilemap tm = MakeMap();
    for (int y = 20; y <= 23; ++y)
    {
        tm.SetLayerTile(20, y, kLayer, 1);
    }
    tm.SetElevation(20, 21, 2);
    tm.SetElevation(20, 22, 4);
    tm.SetElevation(20, 23, 6);
    tm.SetLayerElevationRole(20, 21, kLayer, ElevationRole::Ramp);
    tm.SetLayerElevationRole(20, 22, kLayer, ElevationRole::Ramp);
    tm.SetLayerElevationRole(20, 23, kLayer, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 22 * kTileSize}));

    // Sorted by depth (scene Z), not by run order (scene X): this run advances
    // south, not east, so every tile shares the same X.
    const std::vector<MockRenderer::Quad3D> quads = SortedByDepth(renderer.quads3D);
    ASSERT_EQ(quads.size(), 4u);

    // North is the low-coordinate edge here (TOP_LEFT/TOP_RIGHT share it, since
    // ApplySlopeHeights samples along Z when alongZ is true), south is the
    // high-coordinate edge (BOTTOM_LEFT/BOTTOM_RIGHT).
    const auto northY = [](const MockRenderer::Quad3D& q)
    { return q.corners[sceneMath::QUAD_TOP_LEFT].y; };
    const auto southY = [](const MockRenderer::Quad3D& q)
    { return q.corners[sceneMath::QUAD_BOTTOM_LEFT].y; };

    EXPECT_NEAR(northY(quads[0]), 0.0f, kTol);  // y=20, bare ground
    EXPECT_NEAR(southY(quads[0]), 0.0f, kTol);
    EXPECT_NEAR(northY(quads[1]), 0.0f, kTol);  // y=21, ramp 0 -> 3
    EXPECT_NEAR(southY(quads[1]), 3.0f, kTol);
    EXPECT_NEAR(northY(quads[2]), 3.0f, kTol);  // y=22, ramp 3 -> 6
    EXPECT_NEAR(southY(quads[2]), 6.0f, kTol);
    EXPECT_NEAR(northY(quads[3]), 6.0f, kTol);  // y=23, deck, level
    EXPECT_NEAR(southY(quads[3]), 6.0f, kTol);

    for (size_t i = 0; i + 1 < quads.size(); ++i)
    {
        EXPECT_NEAR(southY(quads[i]), northY(quads[i + 1]), kTol) << "seam " << i << " opened";
    }
}

TEST(World3DStackingTest, TileRotationReachesTheQuad)
{
    // Per-tile rotation was dropped entirely by the first 3D implementation, so
    // every rotated tile silently rendered unrotated.
    Tilemap plain = MakeMap();
    plain.SetLayerTile(20, 12, kLayer, 1);

    Tilemap turned = MakeMap();
    turned.SetLayerTile(20, 12, kLayer, 1);
    turned.SetLayerRotation(20, 12, kLayer, 90.0f);

    const cameraRig::RigParams rig = MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize});

    MockRenderer plainRenderer;
    MockRenderer turnedRenderer;
    plain.RenderWorld3D(plainRenderer, rig);
    turned.RenderWorld3D(turnedRenderer, rig);

    ASSERT_EQ(plainRenderer.quads3D.size(), 1u);
    ASSERT_EQ(turnedRenderer.quads3D.size(), 1u);

    // A quarter turn maps each corner onto its neighbour's old position.
    for (int i = 0; i < sceneMath::QUAD_CORNER_COUNT; ++i)
    {
        const int next = (i + 1) % sceneMath::QUAD_CORNER_COUNT;
        EXPECT_NEAR(glm::distance(turnedRenderer.quads3D[0].corners[i],
                                  plainRenderer.quads3D[0].corners[next]),
                    0.0f,
                    kTol)
            << "corner " << i;
    }
}

TEST(World3DStackingTest, GroundTilesStayFlatAndInPlace)
{
    // The flat path must be untouched by the upright handling: an unflagged
    // background tile is still a ground quad on its own cell.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    for (const glm::vec3& corner : renderer.quads3D[0].corners)
    {
        EXPECT_NEAR(corner.y, 0.0f, kTol) << "ground tile is not flat";
    }
    EXPECT_NEAR(renderer.quads3D[0].corners[sceneMath::QUAD_TOP_LEFT].z,
                static_cast<float>(12 * kTileSize),
                kTol);
}

TEST(World3DStackingTest, UprightFeetRiseToTheDeck)
{
    // The "borders on each side" half: a railing marked Raised on a deck cell must
    // stand ON the deck, not on the ground under it.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetElevation(20, 12, 6);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Wall);
    tm.SetLayerElevationRole(20, 12, kLayer, ElevationRole::Raised);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    EXPECT_NEAR(renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT].y, 6.0f, kTol);

    // Still exactly one tile tall, measured along its own leaning edge - the lift
    // must move the quad, not stretch it.
    EXPECT_NEAR(glm::distance(renderer.quads3D[0].corners[sceneMath::QUAD_TOP_LEFT],
                              renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT]),
                static_cast<float>(kTileSize),
                kTol);
}

TEST(World3DStackingTest, AnUnmarkedUprightTileStillStandsOnZero)
{
    // The regression net: elevation without a role must not lift artwork, upright
    // or otherwise.
    Tilemap tm = MakeMap();
    tm.SetLayerTile(20, 12, kLayer, 1);
    tm.SetElevation(20, 12, 6);
    tm.SetLayerStance(20, 12, kLayer, TileStance::Wall);

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({20 * kTileSize + kTileSize * 0.5f, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 1u);
    EXPECT_NEAR(renderer.quads3D[0].corners[sceneMath::QUAD_BOTTOM_LEFT].y, 0.0f, kTol);
}

TEST(World3DStackingTest, ARaisedStructureRunStaysRigid)
{
    // A multi-tile body shares ONE foot height so it cannot tilt, even where its
    // base row spans cells of differing elevation. Per-slice heights would shear
    // the body apart at its seams. The shared height itself pins the centre-column
    // choice specifically: footColumn = (20+21)/2 = 20 (integer division), whose
    // elevation is 6 - not the 10 at column 21, and not their average.
    Tilemap tm = MakeMap();
    for (int x = 20; x <= 21; ++x)
    {
        tm.SetLayerTile(x, 12, kLayer, 1);
        tm.SetLayerStance(x, 12, kLayer, TileStance::Structure);
        tm.SetLayerElevationRole(x, 12, kLayer, ElevationRole::Raised);
    }
    tm.SetElevation(20, 12, 6);
    tm.SetElevation(21, 12, 10);  // deliberately uneven

    MockRenderer renderer;
    tm.RenderWorld3D(renderer, MakeRig({21 * kTileSize, 12 * kTileSize}));

    ASSERT_EQ(renderer.quads3D.size(), 2u);
    const auto quads = SortedByRunOrder(renderer.quads3D);
    EXPECT_NEAR(quads[0].corners[sceneMath::QUAD_BOTTOM_LEFT].y,
                quads[1].corners[sceneMath::QUAD_BOTTOM_LEFT].y,
                kTol)
        << "the structure tilted instead of staying rigid";
    EXPECT_NEAR(quads[0].corners[sceneMath::QUAD_BOTTOM_LEFT].y, 6.0f, kTol)
        << "the shared foot did not land on the run's centre-column elevation";
}
