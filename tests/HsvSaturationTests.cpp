// Tests for HsvSaturation - the V-relative saturation formula used by the
// chroma-bloom bright-pass to gate which pixels enter the mip chain. Same
// math runs in shader and CPU, so these tests verify it's monotonic, well-
// defined on degenerate inputs, and brightness-independent.

#include "PostFXParams.h"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

namespace
{
constexpr float kTolerance = 1e-5f;

TEST(HsvSaturation, PureRedFullySaturated)
{
    EXPECT_NEAR(HsvSaturation(glm::vec3(1.0f, 0.0f, 0.0f)), 1.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.0f, 1.0f, 0.0f)), 1.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.0f, 0.0f, 1.0f)), 1.0f, kTolerance);
}

TEST(HsvSaturation, WhiteIsAchromatic)
{
    // White and any equal-channel grey return 0 - exactly the desired
    // bright-pass behavior: white surfaces don't bleed.
    EXPECT_NEAR(HsvSaturation(glm::vec3(1.0f, 1.0f, 1.0f)), 0.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.5f, 0.5f, 0.5f)), 0.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.25f, 0.25f, 0.25f)), 0.0f, kTolerance);
}

TEST(HsvSaturation, DimRedReadsFullySaturated)
{
    // V-relative saturation is brightness-independent: a dim red lantern
    // returns 1.0 the same as a bright one. This is the property that
    // separates "neon glow" from "highlight glow."
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.3f, 0.0f, 0.0f)), 1.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.05f, 0.0f, 0.0f)), 1.0f, kTolerance);
}

TEST(HsvSaturation, NearBlackReturnsZeroViaEpsilon)
{
    // Near-black pixels are read as achromatic (epsilon guards against
    // dividing by very small max values that would amplify float noise into
    // bogus saturation).
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.0f, 0.0f, 0.0f)), 0.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(1e-5f, 1e-5f, 1e-5f)), 0.0f, kTolerance);
    EXPECT_NEAR(HsvSaturation(glm::vec3(1e-5f, 0.0f, 0.0f)), 0.0f, kTolerance);
}

TEST(HsvSaturation, MidPaletteValuesAreCorrect)
{
    // A pixel that's half-saturated (e.g., dim red mixed with grey) returns
    // a fractional value: (max - min) / max.
    //   (1.0, 0.5, 0.5) -> (1.0 - 0.5) / 1.0 = 0.5
    EXPECT_NEAR(HsvSaturation(glm::vec3(1.0f, 0.5f, 0.5f)), 0.5f, kTolerance);
    //   (0.8, 0.4, 0.2) -> (0.8 - 0.2) / 0.8 = 0.75
    EXPECT_NEAR(HsvSaturation(glm::vec3(0.8f, 0.4f, 0.2f)), 0.75f, kTolerance);
}

TEST(HsvSaturation, HdrInputIsWellDefined)
{
    // The scene FBO is RGB16F so post-bloom values can exceed 1.0. The
    // formula is still well-defined and bounded.
    //   (2.0, 0.5, 0.5) -> (2.0 - 0.5) / 2.0 = 0.75
    EXPECT_NEAR(HsvSaturation(glm::vec3(2.0f, 0.5f, 0.5f)), 0.75f, kTolerance);
    //   (3.0, 3.0, 3.0) -> 0 (still achromatic)
    EXPECT_NEAR(HsvSaturation(glm::vec3(3.0f, 3.0f, 3.0f)), 0.0f, kTolerance);
}

TEST(HsvSaturation, NeverNaNOrInf)
{
    EXPECT_FALSE(std::isnan(HsvSaturation(glm::vec3(0.0f))));
    EXPECT_FALSE(std::isnan(HsvSaturation(glm::vec3(1e-9f))));
    EXPECT_FALSE(std::isinf(HsvSaturation(glm::vec3(1e6f, 0.0f, 0.0f))));
}

}  // namespace
