// Tests for KarisBloomWeight and KarisBloomChromaWeight - the soft threshold
// weighting functions used by the bloom bright-pass shader. Same math runs in
// shader and CPU, so these tests verify the math is continuous and threshold-
// correct independently of GL state. KarisBloomWeight gates on luma (legacy);
// KarisBloomChromaWeight gates on HSV saturation (current shader path).

#include "PostFXParams.h"

#include <gtest/gtest.h>

namespace
{
constexpr float kTolerance = 1e-5f;

TEST(KarisBloomWeight, ZeroAtAndBelowThreshold)
{
    // At and below threshold the weight is exactly zero - bloom contributes
    // nothing for sub-threshold pixels.
    EXPECT_NEAR(KarisBloomWeight(0.0f, 0.85f), 0.0f, kTolerance);
    EXPECT_NEAR(KarisBloomWeight(0.50f, 0.85f), 0.0f, kTolerance);
    EXPECT_NEAR(KarisBloomWeight(0.85f, 0.85f), 0.0f, kTolerance);
}

TEST(KarisBloomWeight, RampsSmoothlyAboveThreshold)
{
    // Above the threshold, weight starts at 0 and increases monotonically.
    float prev = 0.0f;
    for (float lum = 0.851f; lum < 5.0f; lum += 0.01f)
    {
        float w = KarisBloomWeight(lum, 0.85f);
        EXPECT_GE(w, prev) << "Weight non-monotonic at lum=" << lum;
        EXPECT_GE(w, 0.0f);
        EXPECT_LE(w, 1.0f);
        prev = w;
    }
}

TEST(KarisBloomWeight, ContinuousAtThreshold)
{
    // The weight must be continuous at the threshold - no kink that would
    // make bloom pop on/off as scene luminance crosses the boundary.
    float justBelow = KarisBloomWeight(0.85f - 1e-4f, 0.85f);
    float justAbove = KarisBloomWeight(0.85f + 1e-4f, 0.85f);
    EXPECT_NEAR(justBelow, 0.0f, 1e-3f);
    EXPECT_NEAR(justAbove, 0.0f, 1e-3f);
}

TEST(KarisBloomWeight, AsymptoticToOne)
{
    // As lum -> infinity, over/(1+over) -> 1. Verify large luminance approaches 1.
    EXPECT_GT(KarisBloomWeight(100.0f, 0.85f), 0.99f);
    EXPECT_LT(KarisBloomWeight(100.0f, 0.85f), 1.0f);
}

TEST(KarisBloomWeight, NeverNaNOrInf)
{
    // Robustness under degenerate inputs.
    EXPECT_FALSE(std::isnan(KarisBloomWeight(0.0f, 0.0f)));
    EXPECT_FALSE(std::isnan(KarisBloomWeight(0.0f, 1.0f)));
    EXPECT_FALSE(std::isinf(KarisBloomWeight(1e6f, 0.85f)));
}

// =============================================================================
// KarisBloomChromaWeight - same Karis shape, but gated on HSV saturation.
// This is the active feeder for the current bloom path; the shader pairs this
// weight with HsvSaturation to admit only colored pixels into the mip chain.
// =============================================================================

TEST(KarisBloomChromaWeight, ZeroAtAndBelowThreshold)
{
    EXPECT_NEAR(KarisBloomChromaWeight(0.0f, 0.30f), 0.0f, kTolerance);
    EXPECT_NEAR(KarisBloomChromaWeight(0.20f, 0.30f), 0.0f, kTolerance);
    EXPECT_NEAR(KarisBloomChromaWeight(0.30f, 0.30f), 0.0f, kTolerance);
}

TEST(KarisBloomChromaWeight, MatchesExpectedRampValues)
{
    // Spec-anchor numbers: confirm the soft filter sits where the design
    // expects it to. These are the values the spec calls out as "visible
    // bleed but not overwhelming" at threshold 0.30.
    EXPECT_NEAR(KarisBloomChromaWeight(0.50f, 0.30f), 0.20f / 1.20f, 1e-4f);
    EXPECT_NEAR(KarisBloomChromaWeight(0.80f, 0.30f), 0.50f / 1.50f, 1e-4f);
    EXPECT_NEAR(KarisBloomChromaWeight(1.00f, 0.30f), 0.70f / 1.70f, 1e-4f);
}

TEST(KarisBloomChromaWeight, RampsMonotonically)
{
    // Saturation is bounded in [0, 1] under HSV definition (max-min)/max - the
    // ramp must be non-decreasing across that range.
    float prev = 0.0f;
    for (float s = 0.301f; s <= 1.0f; s += 0.01f)
    {
        float w = KarisBloomChromaWeight(s, 0.30f);
        EXPECT_GE(w, prev) << "Weight non-monotonic at sat=" << s;
        EXPECT_GE(w, 0.0f);
        EXPECT_LT(w, 1.0f);
        prev = w;
    }
}

TEST(KarisBloomChromaWeight, ContinuousAtThreshold)
{
    float justBelow = KarisBloomChromaWeight(0.30f - 1e-4f, 0.30f);
    float justAbove = KarisBloomChromaWeight(0.30f + 1e-4f, 0.30f);
    EXPECT_NEAR(justBelow, 0.0f, 1e-3f);
    EXPECT_NEAR(justAbove, 0.0f, 1e-3f);
}

TEST(KarisBloomChromaWeight, NeverNaNOrInf)
{
    EXPECT_FALSE(std::isnan(KarisBloomChromaWeight(0.0f, 0.0f)));
    EXPECT_FALSE(std::isnan(KarisBloomChromaWeight(0.0f, 1.0f)));
    EXPECT_FALSE(std::isinf(KarisBloomChromaWeight(100.0f, 0.30f)));
}

}  // namespace
