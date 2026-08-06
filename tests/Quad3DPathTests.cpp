// The golden test for the 3D world draw path. No GL/Vulkan context is created
// here (see the rift_tests constraint in CMakeLists.txt).
//
// The overhaul's core safety claim is that the new pipeline is a strict superset
// of the old one: under the Classic preset a tile submitted as a world-space quad
// must rasterize to exactly the pixels the flat screen-space path drew it at.
// This file proves that end to end - build the geometry the way Tilemap will,
// project it the way the renderer will, and compare against the pre-overhaul
// formula `(worldPos - cameraTopLeft) * screen / visibleWorld`.
#include "../src/Billboard.hpp"
#include "../src/CameraRig.hpp"
#include "../src/MathConstants.hpp"
#include "../src/RenderModes.hpp"
#include "../src/SceneMath.hpp"
#include "MockRenderer.hpp"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
constexpr float kTol = 1e-3f;

constexpr float Degrees(float d)
{
    return d * rift::PiF / 180.0f;
}

cameraRig::RigParams ClassicParams()
{
    cameraRig::RigParams params;
    params.target = {1000.0f, 500.0f};
    params.visibleWorldSize = {320.0f, 180.0f};
    params.sceneRadius = 4096.0f;
    cameraRig::ApplyPreset(params, cameraRig::Preset::Classic);
    return params;
}

// What the flat pipeline would have produced for a world point.
glm::vec2 LegacyScreenPos(const cameraRig::RigParams& params, glm::vec2 world, glm::vec2 screen)
{
    const glm::vec2 topLeft = params.target - params.visibleWorldSize * 0.5f;
    return (world - topLeft) * (screen / params.visibleWorldSize);
}
}  // namespace

TEST(Quad3DPathTest, GroundTileLandsOnTheLegacyScreenRect)
{
    const cameraRig::RigParams params = ClassicParams();
    const glm::mat4 viewProj = cameraRig::BuildViewProjection(params);
    const glm::vec2 screen{1520.0f, 855.0f};

    // A 16x16 tile near the middle of the view, addressed exactly as Tilemap
    // addresses tiles: (tileX * tileWidth, tileY * tileHeight).
    const glm::vec2 tileTopLeft{1008.0f, 496.0f};
    const glm::vec2 tileSize{16.0f, 16.0f};

    glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad(tileTopLeft, tileSize, 0.0f, 0.0f, corners);

    const glm::vec2 expected[sceneMath::QUAD_CORNER_COUNT] = {
        LegacyScreenPos(params, tileTopLeft, screen),
        LegacyScreenPos(params, {tileTopLeft.x + tileSize.x, tileTopLeft.y}, screen),
        LegacyScreenPos(params, tileTopLeft + tileSize, screen),
        LegacyScreenPos(params, {tileTopLeft.x, tileTopLeft.y + tileSize.y}, screen),
    };

    for (int i = 0; i < sceneMath::QUAD_CORNER_COUNT; ++i)
    {
        const std::optional<glm::vec2> pixel =
            cameraRig::WorldToScreen(corners[i], viewProj, screen);
        ASSERT_TRUE(pixel.has_value()) << "corner " << i;
        EXPECT_NEAR(pixel->x, expected[i].x, kTol) << "corner " << i;
        EXPECT_NEAR(pixel->y, expected[i].y, kTol) << "corner " << i;
    }
}

TEST(Quad3DPathTest, ClassicPresetKeepsTilesAxisAlignedAndUnscaled)
{
    // Under the flat pipeline a tile was always an upright screen rectangle of a
    // fixed pixel size. The Classic preset must preserve that exactly - no
    // keystoning, no size drift with position on screen.
    const cameraRig::RigParams params = ClassicParams();
    const glm::mat4 viewProj = cameraRig::BuildViewProjection(params);
    const glm::vec2 screen{1520.0f, 855.0f};
    const glm::vec2 tileSize{16.0f, 16.0f};
    const glm::vec2 scale = screen / params.visibleWorldSize;

    // Sample tiles spread across the view, including the far corners.
    const glm::vec2 origins[] = {
        params.target - params.visibleWorldSize * 0.5f,
        params.target,
        params.target + params.visibleWorldSize * 0.5f - tileSize,
    };

    for (const glm::vec2& origin : origins)
    {
        glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
        sceneMath::MakeGroundQuad(origin, tileSize, 0.0f, 0.0f, corners);

        glm::vec2 pixels[sceneMath::QUAD_CORNER_COUNT];
        for (int i = 0; i < sceneMath::QUAD_CORNER_COUNT; ++i)
        {
            const std::optional<glm::vec2> p =
                cameraRig::WorldToScreen(corners[i], viewProj, screen);
            ASSERT_TRUE(p.has_value());
            pixels[i] = *p;
        }

        // Top edge horizontal, left edge vertical: still an axis-aligned rect.
        EXPECT_NEAR(pixels[sceneMath::QUAD_TOP_LEFT].y, pixels[sceneMath::QUAD_TOP_RIGHT].y, kTol);
        EXPECT_NEAR(
            pixels[sceneMath::QUAD_TOP_LEFT].x, pixels[sceneMath::QUAD_BOTTOM_LEFT].x, kTol);

        // And exactly one tile wide/tall in screen pixels, wherever it sits.
        EXPECT_NEAR(pixels[sceneMath::QUAD_TOP_RIGHT].x - pixels[sceneMath::QUAD_TOP_LEFT].x,
                    tileSize.x * scale.x,
                    kTol);
        EXPECT_NEAR(pixels[sceneMath::QUAD_BOTTOM_LEFT].y - pixels[sceneMath::QUAD_TOP_LEFT].y,
                    tileSize.y * scale.y,
                    kTol);
    }
}

TEST(Quad3DPathTest, PerspectivePresetActuallyConverges)
{
    // The counterpart to the test above: under the DS preset a tile further from
    // the camera must project SMALLER. If this ever passes trivially, the
    // projection has silently fallen back to orthographic.
    cameraRig::RigParams params = ClassicParams();
    cameraRig::ApplyPreset(params, cameraRig::Preset::DS);

    const glm::mat4 viewProj = cameraRig::BuildViewProjection(params);
    const glm::vec2 screen{1520.0f, 855.0f};
    const glm::vec2 tileSize{16.0f, 16.0f};

    auto screenWidthOfTileAt = [&](glm::vec2 origin)
    {
        glm::vec3 corners[sceneMath::QUAD_CORNER_COUNT];
        sceneMath::MakeGroundQuad(origin, tileSize, 0.0f, 0.0f, corners);
        const glm::vec2 tl =
            *cameraRig::WorldToScreen(corners[sceneMath::QUAD_TOP_LEFT], viewProj, screen);
        const glm::vec2 tr =
            *cameraRig::WorldToScreen(corners[sceneMath::QUAD_TOP_RIGHT], viewProj, screen);
        return tr.x - tl.x;
    };

    // Map-north (smaller world Y) is away from the camera at yaw 0.
    const float nearWidth = screenWidthOfTileAt(params.target + glm::vec2(0.0f, 40.0f));
    const float farWidth = screenWidthOfTileAt(params.target - glm::vec2(0.0f, 40.0f));

    EXPECT_GT(nearWidth, farWidth);
}

TEST(Quad3DPathTest, MockRecordsSubmissionsWithPassState)
{
    MockRenderer renderer;
    // Call through the interface, as engine code does: the trailing default
    // arguments are declared on IRenderer, and RIFT_DECLARE_COMMON_RENDERER_METHODS
    // deliberately does not repeat them on the overrides, so a call through the
    // concrete type would have to spell out all ten.
    IRenderer& api = renderer;
    const Texture texture;

    glm::vec3 ground[sceneMath::QUAD_CORNER_COUNT];
    sceneMath::MakeGroundQuad({0.0f, 0.0f}, {16.0f, 16.0f}, 0.0f, 0.0f, ground);

    api.SetViewProjection(cameraRig::BuildViewProjection(ClassicParams()));
    api.DrawQuad3D(texture,
                   ground,
                   {0.0f, 0.0f},
                   {16.0f, 16.0f},
                   glm::vec4(1.0f),
                   renderModes::BlendMode::Alpha,
                   renderModes::DepthMode::TestAndWrite);

    glm::vec3 upright[sceneMath::QUAD_CORNER_COUNT];
    billboard::MakeQuad({8.0f, 0.0f, 16.0f},
                        {16.0f, 32.0f},
                        0.0f,
                        Degrees(51.34f),
                        billboard::DefaultDamping(billboard::Role::Character),
                        upright);
    api.DrawQuad3D(texture,
                   upright,
                   {0.0f, 0.0f},
                   {16.0f, 32.0f},
                   glm::vec4(1.0f),
                   renderModes::BlendMode::Additive,
                   renderModes::DepthMode::TestOnly);

    ASSERT_EQ(renderer.quads3D.size(), 2u);
    EXPECT_EQ(renderer.quads3D[0].depth, renderModes::DepthMode::TestAndWrite);
    EXPECT_EQ(renderer.quads3D[0].blend, renderModes::BlendMode::Alpha);
    EXPECT_EQ(renderer.quads3D[1].depth, renderModes::DepthMode::TestOnly);
    EXPECT_EQ(renderer.quads3D[1].blend, renderModes::BlendMode::Additive);

    // The ground quad is flat; the billboard stands up out of the plane.
    EXPECT_NEAR(renderer.quads3D[0].corners[sceneMath::QUAD_TOP_LEFT].y, 0.0f, kTol);
    EXPECT_GT(renderer.quads3D[1].corners[sceneMath::QUAD_TOP_LEFT].y, 0.0f);

    renderer.ClearRecorded();
    EXPECT_TRUE(renderer.quads3D.empty());
}

TEST(Quad3DPathTest, RenderModeNamesRoundTrip)
{
    EXPECT_EQ(EnumTraits<renderModes::DepthMode>::ToString(renderModes::DepthMode::TestAndWrite),
              "TestAndWrite");
    EXPECT_EQ(EnumTraits<renderModes::BlendMode>::FromString("Additive"),
              renderModes::BlendMode::Additive);
    EXPECT_FALSE(EnumTraits<renderModes::DepthMode>::FromString("Always").has_value());
}
