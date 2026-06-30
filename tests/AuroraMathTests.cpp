// Renderer-free tests for the pure aurora math helpers.
#include <gtest/gtest.h>

#include <cmath>

#include "AuroraMath.hpp"

TEST(AuroraMathTests, AuroraColorWrapsAtIntegerPhase)
{
    glm::vec3 a = AuroraMath::AuroraColor(0.0f);
    glm::vec3 b = AuroraMath::AuroraColor(1.0f);
    EXPECT_NEAR(a.r, b.r, 1e-5f);
    EXPECT_NEAR(a.g, b.g, 1e-5f);
    EXPECT_NEAR(a.b, b.b, 1e-5f);
}

TEST(AuroraMathTests, AuroraColorChannelsInRange)
{
    for (int k = 0; k < 80; ++k)
    {
        glm::vec3 c = AuroraMath::AuroraColor(k * 0.0125f);
        EXPECT_GE(std::min(c.r, std::min(c.g, c.b)), 0.0f);
        EXPECT_LE(std::max(c.r, std::max(c.g, c.b)), 1.0f);
    }
}

TEST(AuroraMathTests, TangentAngleFlatIsZero)
{
    EXPECT_NEAR(AuroraMath::TangentAngleDeg({0.0f, 0.0f}, {10.0f, 0.0f}), 0.0f, 1e-4f);
}

TEST(AuroraMathTests, TangentAngleSlopeSign)
{
    // Screen space: +Y is down, so a segment going down-right has a positive angle.
    EXPECT_GT(AuroraMath::TangentAngleDeg({0.0f, 0.0f}, {10.0f, 10.0f}), 0.0f);
    EXPECT_LT(AuroraMath::TangentAngleDeg({0.0f, 0.0f}, {10.0f, -10.0f}), 0.0f);
    EXPECT_NEAR(AuroraMath::TangentAngleDeg({0.0f, 0.0f}, {10.0f, 10.0f}), 45.0f, 1e-3f);
}

TEST(AuroraMathTests, SweepBoostPeaksAtCenterAndDecays)
{
    // At t=0, seed=0 the hot-spot center sits at segNorm 0.
    float atCenter = AuroraMath::SweepBoost(0.0f, 0.0f, 0.15f, 0.20f, 0.0f);
    float farAway = AuroraMath::SweepBoost(0.5f, 0.0f, 0.15f, 0.20f, 0.0f);
    EXPECT_NEAR(atCenter, 1.0f, 1e-4f);
    EXPECT_LT(farAway, atCenter);
    EXPECT_GT(farAway, 0.0f);
    EXPECT_LE(farAway, 1.0f);
}
