// Pure kinematics tests for MotionSystem over a bare Motor component. No GL, no
// collision, no tilemap. (Formerly PlayerMotorTests, which drove the same logic
// through the retired PlayerMotor wrapper.)
#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "../src/MotionSystem.hpp"
#include "../src/Motor.hpp"

namespace
{
constexpr float DT = 1.0f / 60.0f;
constexpr float TILE = 16.0f;
}  // namespace

// Acceleration: from rest, the first frame moves far less than a full-speed frame.
TEST(MotionSystem, AcceleratesFromRest)
{
    Motor m;
    glm::vec2 pos(100.0f, 100.0f);
    glm::vec2 disp =
        MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 0.0f), 50.0f, TILE, DT);

    float fullSpeedFrame = 50.0f * DT;
    EXPECT_GT(glm::length(disp), 0.0f);
    EXPECT_LT(glm::length(disp), fullSpeedFrame);  // not instantly at full speed
}

// After enough frames of held input, velocity converges to target speed.
TEST(MotionSystem, ConvergesToTargetSpeed)
{
    Motor m;
    glm::vec2 pos(100.0f, 100.0f);
    for (int i = 0; i < 120; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 0.0f), 50.0f, TILE, DT);
    }
    EXPECT_NEAR(m.velocity.x, 50.0f, 0.5f);
    EXPECT_NEAR(m.velocity.y, 0.0f, 0.5f);
}

// Releasing input does not stop instantly: momentum carries a multi-frame glide.
// Tested at a high (bicycle) speed so the glide spans several tiles and the
// "gradual, not instant" property is unambiguous regardless of where in a tile
// the release happens. (At walk speed a release a pixel from a tile center
// legitimately settles in 1-2 frames - that is correct snappy behavior, not a
// regression, so it would make a brittle threshold.)
TEST(MotionSystem, DeceleratesGraduallyOnRelease)
{
    constexpr float BIKE_SPEED = 112.5f;
    Motor m;
    glm::vec2 pos(100.0f, 100.0f);
    for (int i = 0; i < 120; ++i)
    {
        pos +=
            MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 0.0f), BIKE_SPEED, TILE, DT);
    }
    ASSERT_TRUE(MotionSystem::IsMoving(m));

    int frames = 0;
    while (MotionSystem::IsMoving(m) && frames < 600)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), BIKE_SPEED, TILE, DT);
        ++frames;
    }
    EXPECT_GT(frames, 3);  // gradual ramp-down, not instant
    EXPECT_FALSE(MotionSystem::IsMoving(m));
}

// ZeroAxisX drops only the X velocity component.
TEST(MotionSystem, ZeroAxisXClearsXOnly)
{
    Motor m;
    glm::vec2 pos(100.0f, 100.0f);
    for (int i = 0; i < 60; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 1.0f), 80.0f, TILE, DT);
    }
    ASSERT_GT(std::abs(m.velocity.x), 1.0f);
    ASSERT_GT(std::abs(m.velocity.y), 1.0f);
    MotionSystem::ZeroAxisX(m);
    EXPECT_FLOAT_EQ(m.velocity.x, 0.0f);
    EXPECT_GT(std::abs(m.velocity.y), 1.0f);
}

// Reset zeroes velocity.
TEST(MotionSystem, ResetStops)
{
    Motor m;
    glm::vec2 pos(100.0f, 100.0f);
    for (int i = 0; i < 60; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 0.0f), 50.0f, TILE, DT);
    }
    ASSERT_TRUE(MotionSystem::IsMoving(m));
    MotionSystem::Reset(m);
    EXPECT_FALSE(MotionSystem::IsMoving(m));
    EXPECT_FLOAT_EQ(m.velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(m.velocity.y, 0.0f);
}

namespace
{
// Nearest horizontal tile center for an X coordinate.
float AlignedCenterX(float x)
{
    return std::round((x - TILE * 0.5f) / TILE) * TILE + TILE * 0.5f;
}
// Nearest feet-at-tile-bottom line for a Y coordinate (a multiple of TILE).
float AlignedBottomY(float y)
{
    return std::round(y / TILE) * TILE;
}
}  // namespace

// A horizontal glide ends on a tile center (X) and settles the idle axis (Y) onto its line.
TEST(MotionSystemGrid, HorizontalGlideLandsOnGrid)
{
    Motor m;
    glm::vec2 pos(101.0f, 99.0f);  // deliberately off-grid on both axes
    for (int i = 0; i < 90; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 0.0f), 87.5f, TILE, DT);
    }
    int frames = 0;
    while (MotionSystem::IsMoving(m) && frames < 1200)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), 87.5f, TILE, DT);
        ++frames;
    }
    // Run a few more idle frames so the perpendicular axis finishes settling.
    for (int i = 0; i < 60; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), 87.5f, TILE, DT);
    }
    EXPECT_FALSE(MotionSystem::IsMoving(m));
    EXPECT_NEAR(pos.x, AlignedCenterX(pos.x), 0.6f);
    EXPECT_NEAR(pos.y, AlignedBottomY(pos.y), 0.6f);
}

// A diagonal glide lands aligned on BOTH axes.
TEST(MotionSystemGrid, DiagonalGlideLandsOnGrid)
{
    Motor m;
    glm::vec2 pos(53.0f, 67.0f);
    for (int i = 0; i < 90; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(1.0f, 1.0f), 87.5f, TILE, DT);
    }
    int frames = 0;
    while (MotionSystem::IsMoving(m) && frames < 1200)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), 87.5f, TILE, DT);
        ++frames;
    }
    for (int i = 0; i < 60; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), 87.5f, TILE, DT);
    }
    EXPECT_FALSE(MotionSystem::IsMoving(m));
    EXPECT_NEAR(pos.x, AlignedCenterX(pos.x), 0.6f);
    EXPECT_NEAR(pos.y, AlignedBottomY(pos.y), 0.6f);
}

// Starting idle but off-grid, the player gently settles onto the grid with no input.
TEST(MotionSystemGrid, IdleOffGridSettlesOntoGrid)
{
    Motor m;
    glm::vec2 pos(103.0f, 95.0f);
    for (int i = 0; i < 120; ++i)
    {
        pos += MotionSystem::ComputeDisplacement(m, pos, glm::vec2(0.0f), 50.0f, TILE, DT);
    }
    EXPECT_FALSE(MotionSystem::IsMoving(m));
    EXPECT_NEAR(pos.x, AlignedCenterX(pos.x), 0.6f);
    EXPECT_NEAR(pos.y, AlignedBottomY(pos.y), 0.6f);
}
