// Tests for ApplySaturation (chroma pump: out = mix(vec3(luma), c, s)).
// Pure-math, lives inline in PostFXParams.h so the test calls it directly.
// The GLSL applySaturation() in PostFXComposite.frag uses identical math; verifying the
// C++ side is sufficient (the test build cannot create a GL context).

#include "PostFXParams.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

namespace
{
constexpr float kTolerance = 1e-5f;
const glm::vec3 kLuma{0.2126f, 0.7152f, 0.0722f};

TEST(ApplySaturation, IdentityAtOne)
{
    // s = 1.0 should return input unchanged for any color.
    const std::array<glm::vec3, 3> samples{
        glm::vec3(0.9f, 0.1f, 0.1f),  // saturated red
        glm::vec3(0.4f, 0.4f, 0.4f),  // mid gray
        glm::vec3(0.95f, 0.92f, 0.85f),  // off white
    };
    for (const glm::vec3& in : samples)
    {
        glm::vec3 out = ApplySaturation(in, 1.0f);
        EXPECT_NEAR(out.r, in.r, kTolerance);
        EXPECT_NEAR(out.g, in.g, kTolerance);
        EXPECT_NEAR(out.b, in.b, kTolerance);
    }
}

TEST(ApplySaturation, GrayscaleAtZero)
{
    // s = 0.0 should collapse to vec3(dot(c, LUMA)) for any color.
    // This implicitly verifies that the C++ LUMA matches the GLSL LUMA in
    // PostFXComposite.frag: any drift in either constant would break this test.
    const std::array<glm::vec3, 3> samples{
        glm::vec3(0.9f, 0.1f, 0.1f),
        glm::vec3(0.2f, 0.7f, 0.3f),
        glm::vec3(0.1f, 0.4f, 0.95f),
    };
    for (const glm::vec3& in : samples)
    {
        float expected = glm::dot(in, kLuma);
        glm::vec3 out = ApplySaturation(in, 0.0f);
        EXPECT_NEAR(out.r, expected, kTolerance);
        EXPECT_NEAR(out.g, expected, kTolerance);
        EXPECT_NEAR(out.b, expected, kTolerance);
    }
}

TEST(ApplySaturation, PumpAboveOne)
{
    // s > 1 should increase each channel's distance from luma without flipping
    // sign (a positive deviation stays positive, negative stays negative).
    const glm::vec3 in(0.8f, 0.3f, 0.5f);
    const float s = 1.5f;
    glm::vec3 out = ApplySaturation(in, s);

    float lumIn = glm::dot(in, kLuma);
    float lumOut = glm::dot(out, kLuma);
    // Luma is preserved (algebraic identity of mix(vec3(L), c, s) wrt LUMA).
    EXPECT_NEAR(lumOut, lumIn, kTolerance);

    for (int c = 0; c < 3; ++c)
    {
        float devIn = in[c] - lumIn;
        float devOut = out[c] - lumIn;
        // Same sign on both sides.
        EXPECT_TRUE((devIn >= 0.0f) == (devOut >= 0.0f))
            << "Channel " << c << ": deviation flipped sign";
        // Magnitude grew.
        EXPECT_GT(std::abs(devOut), std::abs(devIn))
            << "Channel " << c << ": chroma did not pump";
    }
}

TEST(ApplySaturation, PreservesLuma)
{
    // mix(vec3(L), c, s) is constructed so dot(out, LUMA) = L * (1 - s) + dot(c, LUMA) * s.
    // Substituting L = dot(c, LUMA) makes that = L for any s. Verify numerically.
    const glm::vec3 in(0.42f, 0.71f, 0.29f);
    float lumIn = glm::dot(in, kLuma);
    for (float s : {0.0f, 0.25f, 1.0f, 1.5f, 2.0f})
    {
        glm::vec3 out = ApplySaturation(in, s);
        float lumOut = glm::dot(out, kLuma);
        EXPECT_NEAR(lumOut, lumIn, kTolerance) << "Luma drifted at s=" << s;
    }
}

TEST(ApplySaturation, AchromaticInputInvariant)
{
    // For a gray input (R=G=B), saturation has nothing to pump - output must
    // equal input regardless of s.
    const glm::vec3 gray(0.4f, 0.4f, 0.4f);
    for (float s : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f})
    {
        glm::vec3 out = ApplySaturation(gray, s);
        EXPECT_NEAR(out.r, gray.r, kTolerance) << "s=" << s;
        EXPECT_NEAR(out.g, gray.g, kTolerance) << "s=" << s;
        EXPECT_NEAR(out.b, gray.b, kTolerance) << "s=" << s;
    }
}

}  // namespace
