// Unit tests for camera look-ahead. CameraController::Update is renderer-free
// (ConfigurePerspective, which touches the renderer, is not called here).
#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "../src/CameraController.hpp"

namespace
{
CameraUpdateParams BaseParams()
{
    CameraUpdateParams p;
    p.deltaTime = 1.0f / 60.0f;
    p.playerFollowTarget = glm::vec2(100.0f, 100.0f);
    p.playerMoving = true;
    p.baseWorldWidth = 400.0f;
    p.baseWorldHeight = 300.0f;
    p.mapPixelWidth = 10000.0f;
    p.mapPixelHeight = 10000.0f;
    return p;
}
}  // namespace

// With rightward velocity, the follow target leads to the right of the raw target.
TEST(CameraLookAheadTests, LeadsInVelocityDirection)
{
    CameraController cam;
    cam.Initialize(glm::vec2(100.0f, 100.0f), 400.0f, 300.0f);
    cam.SetLookAheadDistance(16.0f);

    CameraUpdateParams p = BaseParams();
    p.playerVelocity = glm::vec2(87.5f, 0.0f);
    cam.Update(p);

    EXPECT_GT(cam.GetState().followTarget.x, p.playerFollowTarget.x);
    EXPECT_NEAR(cam.GetState().followTarget.y, p.playerFollowTarget.y, 1e-3f);
}

// Zero look-ahead distance disables the lead entirely.
TEST(CameraLookAheadTests, ZeroDistanceDisablesLead)
{
    CameraController cam;
    cam.Initialize(glm::vec2(100.0f, 100.0f), 400.0f, 300.0f);
    cam.SetLookAheadDistance(0.0f);

    CameraUpdateParams p = BaseParams();
    p.playerVelocity = glm::vec2(87.5f, 0.0f);
    cam.Update(p);

    EXPECT_NEAR(cam.GetState().followTarget.x, p.playerFollowTarget.x, 1e-3f);
}
