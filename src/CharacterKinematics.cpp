#include "CharacterKinematics.hpp"

#include "CharacterConstants.hpp"
#include "Tilemap.hpp"

#include <cstdlib>

namespace CharacterKinematics
{
void SetElevationTarget(Elevation& elev, float offset)
{
    if (offset != elev.target)
    {
        elev.start = elev.offset;
        elev.target = offset;
        elev.progress = 0.0f;
    }
}

void UpdatePlane(Elevation& elev, int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy)
{
    if (tileAxis == ElevationAxis::None)
    {
        // Ground / non-elevated tile: always engage so an entity stepping
        // off an elevated region snaps back to ground regardless of drop
        // height. The step gate only protects elevated entries.
        elev.plane = destTileElev;
        SetElevationTarget(elev, static_cast<float>(destTileElev));
        return;
    }

    bool movementMatchesAxis = (tileAxis == ElevationAxis::X && moveDx != 0) ||
                               (tileAxis == ElevationAxis::Y && moveDy != 0);
    if (!movementMatchesAxis)
    {
        // Perpendicular crossing - entity passes underneath/over without
        // engaging the elevation; logical plane stays where it was.
        return;
    }

    int delta = destTileElev - elev.plane;
    if (std::abs(delta) > CharacterConstants::MAX_STEP_HEIGHT)
    {
        // No ramp connecting the two planes - reject the direct jump.
        return;
    }

    elev.plane = destTileElev;
    SetElevationTarget(elev, static_cast<float>(destTileElev));
}

void DerivePlane(Elevation& elev, glm::vec2 before, glm::vec2 after, const Tilemap& tilemap)
{
    glm::vec2 movement = after - before;
    int dx = movement.x > 0.01f ? 1 : (movement.x < -0.01f ? -1 : 0);
    int dy = movement.y > 0.01f ? 1 : (movement.y < -0.01f ? -1 : 0);

    int tileX = 0;
    int tileY = 0;
    tilemap.WorldToTileCoord(after.x, after.y, tileX, tileY);
    int destElev = tilemap.GetElevation(tileX, tileY);
    ElevationAxis destAxis = tilemap.GetElevationAxisAt(tileX, tileY);
    UpdatePlane(elev, destElev, destAxis, dx, dy);
}

void UpdateElevation(Elevation& elev, float deltaTime)
{
    if (elev.progress < 1.0f)
    {
        constexpr float transitionDuration = 0.15f;
        elev.progress += deltaTime / transitionDuration;

        if (elev.progress >= 1.0f)
        {
            elev.progress = 1.0f;
            elev.offset = elev.target;
        }
        else
        {
            float t = elev.progress;
            float smoothT = t * t * (3.0f - 2.0f * t);
            elev.offset = elev.start + (elev.target - elev.start) * smoothT;
        }
    }
}

void AdvanceWalkAnimation(AnimationState& anim)
{
    anim.walkSequenceIndex =
        (anim.walkSequenceIndex + 1) % CharacterConstants::WALK_SEQUENCE_LENGTH;
    anim.currentFrame = CharacterConstants::WALK_SEQUENCE[anim.walkSequenceIndex];
}

void ResetAnimation(AnimationState& anim)
{
    anim.currentFrame = 0;
    anim.walkSequenceIndex = 0;
    anim.animationTime = 0.0f;
}
}  // namespace CharacterKinematics
