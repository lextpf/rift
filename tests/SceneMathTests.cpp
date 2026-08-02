// Pure-math guard for the world-pixel <-> scene-space lift. No GL/Vulkan context
// is created here (see the rift_tests constraint in CMakeLists.txt).
#include "../src/SceneMath.hpp"

#include <gtest/gtest.h>

namespace
{
constexpr float kTol = 1e-4f;
}  // namespace

TEST(SceneMathTest, ToSceneRelabelsAxes)
{
    // World Y (south) becomes scene Z; the height argument becomes scene Y.
    const glm::vec3 s = sceneMath::ToScene({120.0f, 340.0f}, 8.0f);
    EXPECT_NEAR(s.x, 120.0f, kTol);
    EXPECT_NEAR(s.y, 8.0f, kTol);
    EXPECT_NEAR(s.z, 340.0f, kTol);
}

TEST(SceneMathTest, ToWorldRoundTripsAndDropsHeight)
{
    const glm::vec2 world{-17.5f, 993.25f};
    const glm::vec2 back = sceneMath::ToWorld(sceneMath::ToScene(world, 64.0f));
    EXPECT_NEAR(back.x, world.x, kTol);
    EXPECT_NEAR(back.y, world.y, kTol);
}

TEST(SceneMathTest, GroundQuadCornersFollowTheDocumentedOrder)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({16.0f, 32.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, corners);

    // Top-left is the map-north-west corner; the quad reads the same way round
    // as the 2D path drew it.
    EXPECT_NEAR(corners[sceneMath::QUAD_TOP_LEFT].x, 16.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_TOP_LEFT].z, 32.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_TOP_RIGHT].x, 32.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_TOP_RIGHT].z, 32.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_BOTTOM_RIGHT].x, 32.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_BOTTOM_RIGHT].z, 48.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_BOTTOM_LEFT].x, 16.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_BOTTOM_LEFT].z, 48.0f, kTol);
}

TEST(SceneMathTest, GroundQuadIsFlatAtTheGivenHeight)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({0.0f, 0.0f}, {16.0f, 16.0f}, 12.0f, 0.0f, corners);
    for (const glm::vec3& c : corners)
    {
        EXPECT_NEAR(c.y, 12.0f, kTol);
    }
}

TEST(SceneMathTest, GroundQuadRotationMatchesTheFlatPipelineFormula)
{
    // Per-tile rotation must reproduce IRenderer::RotateCorners exactly, or
    // rotated tiles change orientation the moment world3d is switched on. That
    // routine rotates screen-space corners in a Y-DOWN frame:
    //     p' = (p.x cos - p.y sin,  p.x sin + p.y cos)
    // On the ground plane screen-x is world-x and screen-y is world-y (scene z),
    // so the same numbers must come out here.
    constexpr float kRotation = 37.0f;
    const glm::vec2 topLeft{0.0f, 0.0f};
    const glm::vec2 size{16.0f, 16.0f};

    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad(topLeft, size, 0.0f, kRotation, corners);

    // Reference: the flat path's own maths, applied to the same corner offsets.
    const float rad = kRotation * 3.14159265f / 180.0f;
    const float cosR = std::cos(rad);
    const float sinR = std::sin(rad);
    const glm::vec2 offsets[sceneMath::QUAD_CORNER_COUNT] = {
        {-8.0f, -8.0f}, {8.0f, -8.0f}, {8.0f, 8.0f}, {-8.0f, 8.0f}};
    const glm::vec2 centre = topLeft + size * 0.5f;

    for (int i = 0; i < sceneMath::QUAD_CORNER_COUNT; ++i)
    {
        const glm::vec2 expected(offsets[i].x * cosR - offsets[i].y * sinR + centre.x,
                                 offsets[i].x * sinR + offsets[i].y * cosR + centre.y);
        EXPECT_NEAR(corners[i].x, expected.x, kTol) << "corner " << i;
        EXPECT_NEAR(corners[i].z, expected.y, kTol) << "corner " << i;
    }
}

TEST(SceneMathTest, QuarterTurnRotatesCornersOntoTheirNeighbours)
{
    // A 90 degree turn must land each corner exactly where the next one was -
    // the check that catches a wrong rotation SIGN, which a symmetric angle
    // would hide.
    glm::vec3 unrotated[sceneMath::QUAD_CORNER_COUNT];
    glm::vec3 rotated[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, unrotated);
    sceneMath::MakeGroundQuad({0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f, 90.0f, rotated);

    // Screen-space clockwise (Y-down): top-left moves to where top-right was.
    for (int i = 0; i < sceneMath::QUAD_CORNER_COUNT; ++i)
    {
        const int next = (i + 1) % sceneMath::QUAD_CORNER_COUNT;
        EXPECT_NEAR(glm::distance(rotated[i], unrotated[next]), 0.0f, kTol) << "corner " << i;
    }
}

TEST(SceneMathTest, RotationPreservesSizeAndCentre)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 23.0f, corners);

    EXPECT_NEAR(
        glm::distance(corners[sceneMath::QUAD_TOP_LEFT], corners[sceneMath::QUAD_TOP_RIGHT]),
        16.0f,
        kTol);

    glm::vec3 centre(0.0f);
    for (const glm::vec3& c : corners)
    {
        centre += c;
    }
    centre /= 4.0f;
    EXPECT_NEAR(centre.x, 40.0f, kTol);
    EXPECT_NEAR(centre.z, 56.0f, kTol);
}

TEST(SceneMathTest, AdjacentGroundQuadsShareExactCorners)
{
    // The reason the flat pipeline needed a `seamFix = 0.1f` tile inflation was
    // that it warped each quad independently, so neighbours disagreed on their
    // shared edge. Deriving corners from the tile grid makes them bit-identical,
    // which is what lets the fudge go away.
    glm::vec3 a[sceneMath::QUAD_CORNER_COUNT];
    glm::vec3 b[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, a);
    sceneMath::MakeGroundQuad({16.0f, 16.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, b);

    EXPECT_EQ(a[sceneMath::QUAD_BOTTOM_RIGHT], b[sceneMath::QUAD_TOP_LEFT]);
}

TEST(SceneMathTest, SlopeHeightsAreConstantWhenBothEdgesMatch)
{
    // Ground and Raised both collapse to a level quad, so one code path serves all
    // three roles and the flat case stays bit-identical to a plain ground quad.
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, corners);
    sceneMath::ApplySlopeHeights({32.0f, 48.0f}, {16.0f, 16.0f}, 6.0f, 6.0f, false, corners);

    for (const glm::vec3& corner : corners)
    {
        EXPECT_FLOAT_EQ(corner.y, 6.0f);
    }
}

TEST(SceneMathTest, SlopeAlongXRaisesTheEastEdge)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, corners);
    sceneMath::ApplySlopeHeights({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 6.0f, false, corners);

    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_TOP_LEFT].y, 0.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_BOTTOM_LEFT].y, 0.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_TOP_RIGHT].y, 6.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_BOTTOM_RIGHT].y, 6.0f);
}

TEST(SceneMathTest, SlopeAlongZRaisesTheSouthEdge)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, corners);
    sceneMath::ApplySlopeHeights({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 6.0f, true, corners);

    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_TOP_LEFT].y, 0.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_TOP_RIGHT].y, 0.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_BOTTOM_LEFT].y, 6.0f);
    EXPECT_FLOAT_EQ(corners[sceneMath::QUAD_BOTTOM_RIGHT].y, 6.0f);
}

TEST(SceneMathTest, SlopeIsSampledByPositionSoRotationStillSlopesAlongTheWorldAxis)
{
    // Why heights come from each corner's world position rather than its index: a
    // rotated quad's corners move, but the ground still slopes east-west.
    // Assigning by index would rotate the slope along with the artwork.
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 90.0f, corners);
    sceneMath::ApplySlopeHeights({32.0f, 48.0f}, {16.0f, 16.0f}, 0.0f, 6.0f, false, corners);

    for (const glm::vec3& corner : corners)
    {
        const float t = (corner.x - 32.0f) / 16.0f;
        EXPECT_NEAR(corner.y, 6.0f * t, 1e-4f);
    }
}
