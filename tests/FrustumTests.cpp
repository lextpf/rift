// Pure-math guard for clip-plane extraction and containment tests. No GL/Vulkan
// context is created here (see the rift_tests constraint in CMakeLists.txt).
#include "../src/CameraRig.hpp"
#include "../src/Frustum.hpp"
#include "../src/MathConstants.hpp"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr float kTol = 1e-3f;

constexpr float Degrees(float d)
{
    return d * rift::PiF / 180.0f;
}

// A simple centred orthographic box: x,y in [-100,100], z in [-100,100].
glm::mat4 MakeBoxViewProjection()
{
    return glm::ortho(-100.0f, 100.0f, -100.0f, 100.0f, -100.0f, 100.0f);
}
}  // namespace

TEST(FrustumTest, PlanesAreNormalized)
{
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    for (const glm::vec4& p : f.planes)
    {
        EXPECT_NEAR(glm::length(glm::vec3(p)), 1.0f, kTol);
    }
}

TEST(FrustumTest, ContainsPointsInsideTheBox)
{
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    EXPECT_TRUE(frustum::ContainsPoint(f, {0.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(frustum::ContainsPoint(f, {99.0f, -99.0f, 50.0f}));
}

TEST(FrustumTest, RejectsPointsOutsideEachFace)
{
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    EXPECT_FALSE(frustum::ContainsPoint(f, {-101.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(frustum::ContainsPoint(f, {101.0f, 0.0f, 0.0f}));
    EXPECT_FALSE(frustum::ContainsPoint(f, {0.0f, -101.0f, 0.0f}));
    EXPECT_FALSE(frustum::ContainsPoint(f, {0.0f, 101.0f, 0.0f}));
}

TEST(FrustumTest, SphereStraddlingAFaceIsKept)
{
    // Culling must be conservative: never reject something partly visible.
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    EXPECT_TRUE(frustum::IntersectsSphere(f, {110.0f, 0.0f, 0.0f}, 20.0f));
    EXPECT_FALSE(frustum::IntersectsSphere(f, {130.0f, 0.0f, 0.0f}, 20.0f));
}

TEST(FrustumTest, AabbStraddlingAFaceIsKept)
{
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    EXPECT_TRUE(frustum::IntersectsAabb(f, {90.0f, -10.0f, -10.0f}, {120.0f, 10.0f, 10.0f}));
    EXPECT_FALSE(frustum::IntersectsAabb(f, {120.0f, -10.0f, -10.0f}, {140.0f, 10.0f, 10.0f}));
}

TEST(FrustumTest, AabbEnclosingTheFrustumIsKept)
{
    const frustum::Frustum f = frustum::FromViewProjection(MakeBoxViewProjection());
    EXPECT_TRUE(frustum::IntersectsAabb(f, {-500.0f, -500.0f, -500.0f}, {500.0f, 500.0f, 500.0f}));
}

TEST(FrustumTest, ClassicCameraKeepsTheFocusAndRejectsFarOffMapTiles)
{
    cameraRig::RigParams params;
    params.target = {1000.0f, 500.0f};
    params.visibleWorldSize = {320.0f, 180.0f};
    params.sceneRadius = 4096.0f;
    cameraRig::ApplyPreset(params, cameraRig::Preset::Classic);

    const frustum::Frustum f = frustum::FromViewProjection(cameraRig::BuildViewProjection(params));

    EXPECT_TRUE(frustum::ContainsPoint(f, {1000.0f, 0.0f, 500.0f}));
    // Well outside the 320x180 visible window on the ground plane.
    EXPECT_FALSE(frustum::ContainsPoint(f, {1000.0f + 400.0f, 0.0f, 500.0f}));
    EXPECT_FALSE(frustum::ContainsPoint(f, {1000.0f, 0.0f, 500.0f + 300.0f}));
}

TEST(FrustumTest, OrbitedCameraSeesWhatTheAxisAlignedRectWouldHaveMissed)
{
    // The whole reason the five inflated-rectangle cull tests had to go: with the
    // camera yawed 45 degrees, ground content diagonally off the old axis-aligned
    // window is squarely on screen.
    cameraRig::RigParams params;
    params.target = {0.0f, 0.0f};
    params.visibleWorldSize = {320.0f, 180.0f};
    params.sceneRadius = 4096.0f;
    cameraRig::ApplyPreset(params, cameraRig::Preset::DS);
    params.yawRadians = Degrees(45.0f);

    const frustum::Frustum f = frustum::FromViewProjection(cameraRig::BuildViewProjection(params));
    EXPECT_TRUE(frustum::ContainsPoint(f, {0.0f, 0.0f, 0.0f}));
}
