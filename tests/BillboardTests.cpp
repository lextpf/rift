// Pure-math guard for damped billboard orientation. No GL/Vulkan context is
// created here (see the rift_tests constraint in CMakeLists.txt).
//
// The calibration figures are measured from Pokemon DS Map Studio's shipped
// tilesets, which bake this lean into static geometry; they are what the default
// damping factors were chosen to reproduce.
#include "../src/Billboard.hpp"
#include "../src/CameraRig.hpp"
#include "../src/MathConstants.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{
constexpr float kTol = 1e-4f;
constexpr float kLooseTol = 2e-3f;

constexpr float Degrees(float d)
{
    return d * rift::PiF / 180.0f;
}

constexpr billboard::Damping kFull{1.0f, 1.0f};
}  // namespace

TEST(BillboardTest, FullFollowUpMatchesTheCameraUpVector)
{
    // A quad that follows the camera completely has, by definition, the camera's
    // own up vector - it lies in the image plane. This single identity pins both
    // the yaw and the lean formulas at once.
    for (const float yawDeg : {0.0f, 37.0f, 90.0f, 180.0f, -125.0f})
    {
        for (const float pitchDeg : {10.0f, 51.34f, 89.0f, 90.0f})
        {
            const float yaw = Degrees(yawDeg);
            const float pitch = Degrees(pitchDeg);
            const glm::vec3 up = billboard::Orient(yaw, pitch, kFull).up;
            const glm::vec3 expected = cameraRig::UpVector(yaw, pitch);
            EXPECT_NEAR(up.x, expected.x, kTol) << yawDeg << " " << pitchDeg;
            EXPECT_NEAR(up.y, expected.y, kTol) << yawDeg << " " << pitchDeg;
            EXPECT_NEAR(up.z, expected.z, kTol) << yawDeg << " " << pitchDeg;
        }
    }
}

TEST(BillboardTest, FullFollowUpIsPerpendicularToTheViewRay)
{
    const float yaw = Degrees(63.0f);
    const float pitch = Degrees(41.0f);
    const glm::vec3 up = billboard::Orient(yaw, pitch, kFull).up;
    EXPECT_NEAR(glm::dot(up, cameraRig::EyeDirection(yaw, pitch)), 0.0f, kTol);
}

TEST(BillboardTest, ZeroLeanStandsBoltUpright)
{
    const billboard::Orientation o =
        billboard::Orient(Degrees(80.0f), Degrees(45.0f), {1.0f, 0.0f});
    EXPECT_NEAR(o.up.x, 0.0f, kTol);
    EXPECT_NEAR(o.up.y, 1.0f, kTol);
    EXPECT_NEAR(o.up.z, 0.0f, kTol);
}

TEST(BillboardTest, FullLeanFromOverheadLiesFlatPointingNorth)
{
    // The top edge tips AWAY from the camera. Looking straight down, a fully
    // facing quad is flat on the ground with its top toward map-north (-Z), not
    // standing up. Getting this sign backwards is the easy mistake here.
    const billboard::Orientation o = billboard::Orient(0.0f, Degrees(90.0f), kFull);
    EXPECT_NEAR(o.up.x, 0.0f, kTol);
    EXPECT_NEAR(o.up.y, 0.0f, kTol);
    EXPECT_NEAR(o.up.z, -1.0f, kTol);
}

TEST(BillboardTest, OrientationAxesStayOrthonormal)
{
    const billboard::Orientation o =
        billboard::Orient(Degrees(-48.0f), Degrees(29.0f), {0.5f, 0.8f});
    EXPECT_NEAR(glm::length(o.right), 1.0f, kTol);
    EXPECT_NEAR(glm::length(o.up), 1.0f, kTol);
    EXPECT_NEAR(glm::dot(o.right, o.up), 0.0f, kTol);
}

TEST(BillboardTest, DampingScalesBothAngles)
{
    const billboard::Orientation o =
        billboard::Orient(Degrees(90.0f), Degrees(60.0f), {0.5f, 0.25f});
    EXPECT_NEAR(o.yawRadians, Degrees(45.0f), kTol);
    EXPECT_NEAR(o.leanRadians, Degrees(15.0f), kTol);
}

TEST(BillboardTest, ApparentHeightIsFullWhenLeanMatchesPitch)
{
    const float pitch = cameraRig::DS_PITCH_RADIANS;
    EXPECT_NEAR(billboard::ApparentHeightScale(pitch, pitch), 1.0f, kTol);
}

TEST(BillboardTest, ApparentHeightSquashesAnUprightQuad)
{
    // A bolt-upright quad under the DS camera keeps only cos(51.34 deg) of its
    // height, which is the foreshortening the baked lean exists to cancel.
    const float pitch = cameraRig::DS_PITCH_RADIANS;
    EXPECT_NEAR(billboard::ApparentHeightScale(0.0f, pitch), 0.6249f, kLooseTol);
}

TEST(BillboardTest, MatchesTheMeasuredHeartGoldTreeLean)
{
    // Pokemon DS Map Studio's HGSS overworld tree quad leans 39.57 deg from
    // vertical under a camera 51.34 deg above the horizon, and so renders at
    // ~98% of full height. Scenery damping is calibrated to land there.
    const float pitch = cameraRig::DS_PITCH_RADIANS;
    const billboard::Damping scenery = billboard::DefaultDamping(billboard::Role::Scenery);
    const float lean = scenery.leanFollow * pitch;

    EXPECT_NEAR(lean, Degrees(41.07f), Degrees(2.0f));
    EXPECT_GT(billboard::ApparentHeightScale(lean, pitch), 0.97f);
}

TEST(BillboardTest, CharacterFollowsYawCompletely)
{
    // Characters must never be seen edge-on, so their yaw is undamped even
    // though their lean is not.
    const billboard::Damping character = billboard::DefaultDamping(billboard::Role::Character);
    EXPECT_NEAR(character.yawFollow, 1.0f, kTol);
    EXPECT_LT(character.leanFollow, 1.0f);
}

TEST(BillboardTest, SceneryUnderFollowsYaw)
{
    // The residual mismatch is the "follows the camera a bit" read.
    const billboard::Damping scenery = billboard::DefaultDamping(billboard::Role::Scenery);
    EXPECT_GT(scenery.yawFollow, 0.0f);
    EXPECT_LT(scenery.yawFollow, 1.0f);
}

TEST(BillboardTest, QuadStandsOnItsFootCenterAtTheAuthoredSize)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    const glm::vec3 foot{100.0f, 0.0f, 200.0f};
    const glm::vec2 size{16.0f, 32.0f};
    billboard::MakeQuad(foot, size, Degrees(35.0f), Degrees(50.0f), kFull, corners);

    // Bottom edge straddles the feet, and both edges are the authored width.
    const glm::vec3 bottomMid =
        (corners[sceneMath::QUAD_BOTTOM_LEFT] + corners[sceneMath::QUAD_BOTTOM_RIGHT]) * 0.5f;
    EXPECT_NEAR(bottomMid.x, foot.x, kTol);
    EXPECT_NEAR(bottomMid.y, foot.y, kTol);
    EXPECT_NEAR(bottomMid.z, foot.z, kTol);

    EXPECT_NEAR(
        glm::length(corners[sceneMath::QUAD_BOTTOM_RIGHT] - corners[sceneMath::QUAD_BOTTOM_LEFT]),
        size.x,
        kTol);
    EXPECT_NEAR(glm::length(corners[sceneMath::QUAD_TOP_RIGHT] - corners[sceneMath::QUAD_TOP_LEFT]),
                size.x,
                kTol);
    EXPECT_NEAR(
        glm::length(corners[sceneMath::QUAD_TOP_LEFT] - corners[sceneMath::QUAD_BOTTOM_LEFT]),
        size.y,
        kTol);
}

TEST(BillboardTest, ZeroLeanQuadIsVerticalInScene)
{
    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    billboard::MakeQuad(
        {0.0f, 0.0f, 0.0f}, {16.0f, 32.0f}, Degrees(20.0f), Degrees(60.0f), {1.0f, 0.0f}, corners);
    EXPECT_NEAR(corners[sceneMath::QUAD_TOP_LEFT].y, 32.0f, kTol);
    EXPECT_NEAR(corners[sceneMath::QUAD_BOTTOM_LEFT].y, 0.0f, kTol);
}

TEST(BillboardTest, RoleNamesRoundTrip)
{
    EXPECT_EQ(EnumTraits<billboard::Role>::ToString(billboard::Role::Scenery), "Scenery");
    EXPECT_EQ(EnumTraits<billboard::Role>::ToString(billboard::Role::Character), "Character");
    EXPECT_EQ(EnumTraits<billboard::Role>::FromString("Character"), billboard::Role::Character);
    EXPECT_FALSE(EnumTraits<billboard::Role>::FromString("Ground").has_value());
}
