// Regression guard for the GLSL perspective math. The vertex shader runs the
// transform in single-precision float; the canonical CPU math (used for anchor
// calculations via ProjectPoint) runs in double. This test pins the precision
// delta by comparing a CPU mirror of the shader (PerspectiveTransformFloat) to
// the canonical double implementation at representative camera positions.

#include "../src/PerspectiveTransform.hpp"
#include "../src/PerspectiveTransformFloat.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{

constexpr float kTol = 1e-2f;  // Pixels. ~ULP at the edge of the globe.

struct Scenario
{
    const char* name;
    float viewWidth;
    float viewHeight;
    float horizonY;
    float horizonScale;
    float sphereRadius;
    bool hasGlobe;
    bool hasVanishing;
};

constexpr Scenario kScenarios[] = {
    {"VanishingOnly", 1280, 800, 200, 0.3f, 2000, false, true},
    {"GlobeOnly", 1280, 800, 0, 1.0f, 2000, true, false},
    {"Fisheye", 1280, 800, 200, 0.3f, 2000, true, true},
    {"NarrowHorizonScale", 1280, 800, 200, 0.05f, 2000, false, true},
    {"WideSphere", 1280, 800, 200, 0.3f, 5000, true, false},
};

double TransformDouble(double& x, double& y, const Scenario& s)
{
    perspectiveTransform::Params p{};
    p.centerX = s.viewWidth * 0.5;
    p.centerY = s.viewHeight * 0.5;
    p.horizonY = s.horizonY;
    p.screenHeight = s.viewHeight;
    p.horizonScale = s.horizonScale;
    const double R = s.sphereRadius;
    p.sphereRadiusX = R * perspectiveTransform::kGlobeRadiusXScale;
    p.sphereRadiusY = R * perspectiveTransform::kGlobeRadiusYScale;
    perspectiveTransform::GetTransformFn(s.hasGlobe, s.hasVanishing)(x, y, p);
    return 0;
}

}  // namespace

TEST(PerspectiveTransformFloatTests, MatchesDoubleAtCenter)
{
    for (const auto& s : kScenarios)
    {
        double dx = s.viewWidth * 0.5;
        double dy = s.viewHeight * 0.5;
        TransformDouble(dx, dy, s);

        float fx = s.viewWidth * 0.5f;
        float fy = s.viewHeight * 0.5f;
        const auto p = perspectiveTransformFloat::MakeParamsF(
            s.viewWidth, s.viewHeight, s.horizonY, s.horizonScale, s.sphereRadius);
        perspectiveTransformFloat::TransformPointFloat(fx, fy, s.hasGlobe, s.hasVanishing, p);

        EXPECT_NEAR(static_cast<float>(dx), fx, kTol) << s.name;
        EXPECT_NEAR(static_cast<float>(dy), fy, kTol) << s.name;
    }
}

TEST(PerspectiveTransformFloatTests, MatchesDoubleAtHorizon)
{
    for (const auto& s : kScenarios)
    {
        // Pick a Y near the horizon (worst case for vanishing-point scaling).
        const float testY = s.horizonY + 1.0f;
        double dx = s.viewWidth * 0.25;
        double dy = testY;
        TransformDouble(dx, dy, s);

        float fx = s.viewWidth * 0.25f;
        float fy = testY;
        const auto p = perspectiveTransformFloat::MakeParamsF(
            s.viewWidth, s.viewHeight, s.horizonY, s.horizonScale, s.sphereRadius);
        perspectiveTransformFloat::TransformPointFloat(fx, fy, s.hasGlobe, s.hasVanishing, p);

        EXPECT_NEAR(static_cast<float>(dx), fx, kTol) << s.name;
        EXPECT_NEAR(static_cast<float>(dy), fy, kTol) << s.name;
    }
}

TEST(PerspectiveTransformFloatTests, MatchesDoubleAtGlobeEdge)
{
    // Place the test point well inside the globe but off-center so curvature
    // contributes meaningfully. (dNorm ~ 0.5 in normalized-radius space.)
    for (const auto& s : kScenarios)
    {
        if (!s.hasGlobe)
        {
            continue;
        }
        const float testX = s.viewWidth * 0.85f;
        const float testY = s.viewHeight * 0.5f;
        double dx = testX;
        double dy = testY;
        TransformDouble(dx, dy, s);

        float fx = testX;
        float fy = testY;
        const auto p = perspectiveTransformFloat::MakeParamsF(
            s.viewWidth, s.viewHeight, s.horizonY, s.horizonScale, s.sphereRadius);
        perspectiveTransformFloat::TransformPointFloat(fx, fy, s.hasGlobe, s.hasVanishing, p);

        EXPECT_NEAR(static_cast<float>(dx), fx, kTol) << s.name;
        EXPECT_NEAR(static_cast<float>(dy), fy, kTol) << s.name;
    }
}

TEST(PerspectiveTransformFloatTests, DisabledIsIdentity)
{
    // Neither globe nor vanishing - transform is a no-op.
    const auto p = perspectiveTransformFloat::MakeParamsF(1280, 800, 200, 0.3f, 2000);
    float fx = 123.45f;
    float fy = 678.90f;
    perspectiveTransformFloat::TransformPointFloat(fx, fy, false, false, p);
    EXPECT_FLOAT_EQ(fx, 123.45f);
    EXPECT_FLOAT_EQ(fy, 678.90f);
}

TEST(PerspectiveTransformFloatTests, GlobeNearCenterPasses)
{
    // dNorm <= 0.001 short-circuit: tiny distance from center should leave
    // coordinates untouched (the shader's "if (dNorm > 0.001)" guard).
    const auto p = perspectiveTransformFloat::MakeParamsF(1280, 800, 0, 1.0f, 2000);
    float fx = p.centerX + 0.0001f;
    float fy = p.centerY + 0.0001f;
    perspectiveTransformFloat::TransformPointFloat(fx, fy, true, false, p);
    EXPECT_NEAR(fx, p.centerX + 0.0001f, 1e-5f);
    EXPECT_NEAR(fy, p.centerY + 0.0001f, 1e-5f);
}
