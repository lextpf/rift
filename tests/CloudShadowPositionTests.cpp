// Tests for SkyRenderer::ComputeCloudShadowPosition (pure-math static method).
//
// Per CLAUDE.md, tests cannot create a GL/Vulkan context. The position helper
// is inlined in SkyRenderer.h so we can verify drift behaviour without
// instantiating SkyRenderer or hitting GL.

#include "AmbienceConfig.h"
#include "SkyRenderer.h"

#include <gtest/gtest.h>

namespace
{
constexpr float kTolerance = 1e-3f;

TEST(CloudShadowPosition, DriftsAlongWindOverTime)
{
    glm::vec2 origin(0.0f, 0.0f);
    glm::vec2 wind = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);

    glm::vec2 a = SkyRenderer::ComputeCloudShadowPosition(0, 0.0f, origin);
    glm::vec2 b = SkyRenderer::ComputeCloudShadowPosition(0, 1.0f, origin);

    glm::vec2 delta = b - a;
    // The dot product with wind should equal the drift speed times time
    // (modulo wrap-around, which doesn't apply at small t).
    float projected = delta.x * wind.x + delta.y * wind.y;
    EXPECT_NEAR(projected, ambience::CLOUD_SHADOW_DRIFT_SPEED, kTolerance);
}

TEST(CloudShadowPosition, IsPeriodic)
{
    // After drifting one full loop period (2 * 480 = 960 world pixels), the
    // shadow should be back at its t=0 location.
    constexpr float kCellSize = 480.0f;
    constexpr float kLoopWorld = 2.0f * kCellSize;
    const float loopTime = kLoopWorld / ambience::CLOUD_SHADOW_DRIFT_SPEED;

    glm::vec2 origin(0.0f, 0.0f);
    glm::vec2 a = SkyRenderer::ComputeCloudShadowPosition(0, 0.0f, origin);
    glm::vec2 b = SkyRenderer::ComputeCloudShadowPosition(0, loopTime, origin);
    EXPECT_NEAR(a.x, b.x, kTolerance);
    EXPECT_NEAR(a.y, b.y, kTolerance);
}

TEST(CloudShadowPosition, DifferentSlotsAtDifferentLocations)
{
    glm::vec2 origin(0.0f, 0.0f);
    glm::vec2 a = SkyRenderer::ComputeCloudShadowPosition(0, 0.0f, origin);
    glm::vec2 b = SkyRenderer::ComputeCloudShadowPosition(1, 0.0f, origin);
    glm::vec2 c = SkyRenderer::ComputeCloudShadowPosition(2, 0.0f, origin);

    // Slots 0/1/2 must occupy distinct world cells so a 4-shadow render covers
    // multiple corners of the camera rect.
    EXPECT_FALSE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(b == c);
}

TEST(CloudShadowPosition, RemainsNearOriginAsCameraMoves)
{
    // The position is anchored to the camera tile so visible shadows continue
    // to surround the camera even after the camera has travelled far.
    constexpr float kCellSize = 480.0f;
    constexpr float kLoopWorld = 2.0f * kCellSize;

    // Two camera positions in different "cloud tiles" should produce shadows
    // whose distance from the camera remains bounded (i.e. they don't drift
    // ever further behind the player).
    glm::vec2 originA(100.0f, 100.0f);
    glm::vec2 originB(originA.x + kLoopWorld * 5.0f, originA.y);

    glm::vec2 shadowA = SkyRenderer::ComputeCloudShadowPosition(0, 12.5f, originA);
    glm::vec2 shadowB = SkyRenderer::ComputeCloudShadowPosition(0, 12.5f, originB);

    glm::vec2 deltaA = shadowA - originA;
    glm::vec2 deltaB = shadowB - originB;

    // The shadow's offset from camera origin must be bounded by the loop period
    // - if it grew with camera distance, the bound would fail.
    EXPECT_LE(std::abs(deltaA.x - deltaB.x), kLoopWorld + kTolerance);
}

}  // namespace
