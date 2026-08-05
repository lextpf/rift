// Pure-math guard for the orbit camera's input mapping: mouse-drag rotation and
// camera-relative sprite row selection. No GL/Vulkan context is created here
// (see the rift_tests constraint in CMakeLists.txt).
//
// Note what is NOT here: movement is world-absolute. WASD is never rotated into
// the camera's frame - W is always map-north however far the camera has orbited
// - so that movement stays predictable against the tile grid the collision and
// navigation maps are built on. Only the sprite ROW is camera-relative.
//
// The load-bearing property throughout is that yaw 0 is an EXACT identity, so
// none of this perturbs the flat pipeline while the two coexist.
#include "../src/CameraFacing.hpp"
#include "../src/CameraRig.hpp"
#include "../src/MathConstants.hpp"

#include <gtest/gtest.h>

namespace
{
constexpr float kTol = 1e-5f;

constexpr float Degrees(float d)
{
    return d * rift::PiF / 180.0f;
}

constexpr glm::vec2 kViewport{1000.0f, 500.0f};
}  // namespace

// --- Mouse drag -----------------------------------------------------------

TEST(CameraOrbitInputTest, DraggingRightSwingsTheCameraWest)
{
    // "Grab the world": dragging right must make the ground appear to follow the
    // cursor, which means the camera orbits the other way.
    cameraRig::OrbitAngles a;
    a.yawRadians = 0.0f;
    a.pitchRadians = Degrees(45.0f);

    const cameraRig::OrbitAngles moved = cameraRig::ApplyOrbitDrag(a, {100.0f, 0.0f}, kViewport);
    EXPECT_LT(moved.yawRadians, 0.0f);
}

TEST(CameraOrbitInputTest, AFullSweepRotatesTheDocumentedAmount)
{
    cameraRig::OrbitAngles a;
    a.yawRadians = 0.0f;
    a.pitchRadians = Degrees(45.0f);

    // Dragging the full window width is one full DRAG_SWEEP_RADIANS.
    const cameraRig::OrbitAngles moved =
        cameraRig::ApplyOrbitDrag(a, {kViewport.x, 0.0f}, kViewport);
    EXPECT_NEAR(moved.yawRadians, cameraRig::WrapYaw(-cameraRig::DRAG_SWEEP_RADIANS), 1e-4f);
}

TEST(CameraOrbitInputTest, DraggingDownRaisesTheCameraTowardTopDown)
{
    cameraRig::OrbitAngles a;
    a.yawRadians = 0.0f;
    a.pitchRadians = Degrees(45.0f);

    const cameraRig::OrbitAngles down = cameraRig::ApplyOrbitDrag(a, {0.0f, 50.0f}, kViewport);
    EXPECT_GT(down.pitchRadians, a.pitchRadians);

    const cameraRig::OrbitAngles up = cameraRig::ApplyOrbitDrag(a, {0.0f, -50.0f}, kViewport);
    EXPECT_LT(up.pitchRadians, a.pitchRadians);
}

TEST(CameraOrbitInputTest, DragCannotPushThePitchOutOfRange)
{
    // Dragging hard must not put the camera under the map or past the zenith,
    // no matter how far the cursor travels.
    cameraRig::OrbitAngles a;
    a.pitchRadians = Degrees(45.0f);

    const cameraRig::OrbitAngles farDown =
        cameraRig::ApplyOrbitDrag(a, {0.0f, 100000.0f}, kViewport);
    EXPECT_NEAR(farDown.pitchRadians, cameraRig::MAX_PITCH_RADIANS, kTol);

    const cameraRig::OrbitAngles farUp =
        cameraRig::ApplyOrbitDrag(a, {0.0f, -100000.0f}, kViewport);
    EXPECT_NEAR(farUp.pitchRadians, cameraRig::MIN_PITCH_RADIANS, kTol);
}

TEST(CameraOrbitInputTest, RepeatedDraggingKeepsYawBounded)
{
    // Spinning the camera for a long time must not drift the stored angle off
    // toward large magnitudes and lose float precision.
    cameraRig::OrbitAngles a;
    for (int i = 0; i < 500; ++i)
    {
        a = cameraRig::ApplyOrbitDrag(a, {kViewport.x, 0.0f}, kViewport);
    }
    EXPECT_LE(std::abs(a.yawRadians), rift::PiF + kTol);
}

TEST(CameraOrbitInputTest, DragIsResolutionIndependent)
{
    // The same fraction of the window gives the same rotation at any size.
    const cameraRig::OrbitAngles a;
    const cameraRig::OrbitAngles small =
        cameraRig::ApplyOrbitDrag(a, {50.0f, 0.0f}, {500.0f, 250.0f});
    const cameraRig::OrbitAngles large =
        cameraRig::ApplyOrbitDrag(a, {200.0f, 0.0f}, {2000.0f, 1000.0f});
    EXPECT_NEAR(small.yawRadians, large.yawRadians, kTol);
}

// --- Camera-relative sprite row -------------------------------------------

TEST(CameraFacingTest, IdentityAtYawZero)
{
    // Again the flat-pipeline guarantee.
    for (const CharacterDirection dir : {CharacterDirection::UP,
                                         CharacterDirection::DOWN,
                                         CharacterDirection::LEFT,
                                         CharacterDirection::RIGHT})
    {
        EXPECT_EQ(cameraFacing::ScreenFacing(dir, 0.0f), dir);
    }
}

TEST(CameraFacingTest, OrbitingBehindACharacterShowsTheirBack)
{
    // Camera swung half way round: a character facing world-south is now facing
    // AWAY from the viewer, so the back (UP) row is correct.
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::DOWN, rift::PiF),
              CharacterDirection::UP);
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::UP, rift::PiF),
              CharacterDirection::DOWN);
}

TEST(CameraFacingTest, QuarterTurnMapsToProfileRows)
{
    // Camera moved to the east: a character walking world-south now crosses the
    // screen rather than coming toward the viewer.
    const float yaw = Degrees(90.0f);
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::DOWN, yaw), CharacterDirection::LEFT);
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::UP, yaw), CharacterDirection::RIGHT);
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::RIGHT, yaw), CharacterDirection::DOWN);
    EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::LEFT, yaw), CharacterDirection::UP);
}

TEST(CameraFacingTest, SmallYawDoesNotChangeTheRow)
{
    // The sprite must not flicker between rows while the camera drifts a little;
    // a row only changes once the yaw passes the 45 degree bisector.
    for (const float yawDeg : {-40.0f, -10.0f, 0.0f, 10.0f, 40.0f})
    {
        EXPECT_EQ(cameraFacing::ScreenFacing(CharacterDirection::DOWN, Degrees(yawDeg)),
                  CharacterDirection::DOWN)
            << yawDeg;
    }
}

TEST(CameraFacingTest, EveryYawYieldsSomeRowAndStaysAFullTurnConsistent)
{
    // Sweeping a full turn must cycle each facing through all four rows exactly
    // once - no gaps, no direction that never appears.
    int seen[4] = {0, 0, 0, 0};
    for (int deg = -180; deg < 180; ++deg)
    {
        const CharacterDirection row =
            cameraFacing::ScreenFacing(CharacterDirection::DOWN, Degrees(static_cast<float>(deg)));
        ++seen[static_cast<int>(row)];
    }
    for (const int count : seen)
    {
        EXPECT_GT(count, 0);
    }
}
