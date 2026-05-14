// Tests for ComputeGradingParams (pure-math helper used by Game::Render to
// drive the post-FX LGG grading uniforms). Verifies the lift/gamma/gain triplet
// is identity at midday, warm at golden hour, cool at night, and bounded.

#include "AmbienceConfig.hpp"
#include "PostFXParams.hpp"

#include <gtest/gtest.h>

namespace
{
constexpr float kTolerance = 1e-4f;

TEST(GradingParams, IdentityAtMidday)
{
    // Midday with no night factor must produce the LGG identity: no grading.
    auto p = ComputeGradingParams(12.0f, 0.0f);
    EXPECT_NEAR(p.lift.r, 0.0f, kTolerance);
    EXPECT_NEAR(p.lift.g, 0.0f, kTolerance);
    EXPECT_NEAR(p.lift.b, 0.0f, kTolerance);
    EXPECT_NEAR(p.gamma.r, 1.0f, kTolerance);
    EXPECT_NEAR(p.gamma.g, 1.0f, kTolerance);
    EXPECT_NEAR(p.gamma.b, 1.0f, kTolerance);
    EXPECT_NEAR(p.gain.r, 1.0f, kTolerance);
    EXPECT_NEAR(p.gain.g, 1.0f, kTolerance);
    EXPECT_NEAR(p.gain.b, 1.0f, kTolerance);
}

TEST(GradingParams, WarmGainAtGoldenHour)
{
    // Dawn and dusk should boost red and reduce blue in the gain (highlights).
    auto dawn = ComputeGradingParams(6.0f, 0.0f);
    EXPECT_GT(dawn.gain.r, 1.0f) << "Dawn highlights should be warmer (red boost)";
    EXPECT_LT(dawn.gain.b, 1.0f) << "Dawn highlights should be warmer (blue suppress)";

    auto dusk = ComputeGradingParams(19.0f, 0.0f);
    EXPECT_GT(dusk.gain.r, 1.0f) << "Dusk highlights should be warmer";
    EXPECT_LT(dusk.gain.b, 1.0f) << "Dusk highlights should be warmer";
}

TEST(GradingParams, CoolLiftAtNight)
{
    // Deep night (nightFactor=1) shifts shadows toward navy: lift.b > lift.r.
    auto night = ComputeGradingParams(1.0f, 1.0f);
    EXPECT_GT(night.lift.b, night.lift.r) << "Night shadows should lean blue";
    EXPECT_LT(night.lift.r, 0.0f) << "Night shadows should crush red";
    // Highlights at night should also lean cool: gain.b > gain.r.
    EXPECT_GT(night.gain.b, night.gain.r);
}

TEST(GradingParams, DawnDuskAreDistinctlyDifferent)
{
    // Dawn shadows are slightly cool (-r, +b); dusk shadows are slightly purple
    // (+r, -g, +b). The two should NOT be identical - that's the point of LGG
    // over a scalar tint: each time of day has its own character.
    auto dawn = ComputeGradingParams(6.0f, 0.0f);
    auto dusk = ComputeGradingParams(19.0f, 0.0f);
    bool liftDiffers = std::abs(dawn.lift.r - dusk.lift.r) > kTolerance ||
                       std::abs(dawn.lift.g - dusk.lift.g) > kTolerance ||
                       std::abs(dawn.lift.b - dusk.lift.b) > kTolerance;
    EXPECT_TRUE(liftDiffers) << "Dawn vs dusk shadow tint should differ";
}

TEST(GradingParams, BoundedByAmplitude)
{
    // No component should drift outside (1 +/- 2 * amplitude) for gain or
    // (0 +/- 2 * amplitude) for lift, even at simultaneous golden hour + night.
    constexpr float kBound = ambience::GRADING_TINT_AMPLITUDE * 2.0f;
    for (float t = 0.0f; t < 24.0f; t += 0.25f)
    {
        for (float n = 0.0f; n <= 1.0f; n += 0.1f)
        {
            auto p = ComputeGradingParams(t, n);
            EXPECT_LE(std::abs(p.lift.r), kBound + kTolerance) << "t=" << t << " n=" << n;
            EXPECT_LE(std::abs(p.lift.g), kBound + kTolerance) << "t=" << t << " n=" << n;
            EXPECT_LE(std::abs(p.lift.b), kBound + kTolerance) << "t=" << t << " n=" << n;
            EXPECT_LE(std::abs(p.gain.r - 1.0f), kBound + kTolerance) << "t=" << t << " n=" << n;
            EXPECT_LE(std::abs(p.gain.g - 1.0f), kBound + kTolerance) << "t=" << t << " n=" << n;
            EXPECT_LE(std::abs(p.gain.b - 1.0f), kBound + kTolerance) << "t=" << t << " n=" << n;
        }
    }
}

TEST(GradingParams, ContinuousAcrossDayBoundary)
{
    // At the dawn-on / dusk-off threshold the params should be nearly neutral
    // (warmth ramps from 0 outside the window) so transitions don't pop.
    auto preDawn = ComputeGradingParams(4.99f, 0.0f);
    auto atFive = ComputeGradingParams(5.0f, 0.0f);
    EXPECT_NEAR(preDawn.gain.r, atFive.gain.r, 0.01f);
    EXPECT_NEAR(preDawn.lift.b, atFive.lift.b, 0.01f);
}

}  // namespace
