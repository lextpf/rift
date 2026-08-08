#include "CharacterKinematics.hpp"

#include "CharacterConstants.hpp"
#include "SurfaceSystem.hpp"
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

// Every production movement path uses SurfaceSystem::ResolveMove plus CommitSupport instead,
// and nothing outside ElevationZTests calls this. Its axis test is deliberately looser than the
// live one, because it accepts a diagonal step onto a ramp, so tightening it here would silently
// change what those tests pin.
void UpdatePlane(Elevation& elev, int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy)
{
    if (tileAxis == ElevationAxis::None)
    {
        // Ground / non-elevated tile: always engage so an entity stepping
        // off an elevated region snaps back to ground regardless of drop
        // height. The step gate only protects elevated entries.
        elev.plane = destTileElev;
        elev.surface = destTileElev == 0 ? SupportSurface::Ground : SupportSurface::Elevation;
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
    elev.surface = SupportSurface::Elevation;
    SetElevationTarget(elev, static_cast<float>(destTileElev));
}

SupportState GetSupport(const Elevation& elev)
{
    return {elev.surface, elev.plane};
}

SurfaceTransition ResolveSupport(const Elevation& elev,
                                 glm::vec2 before,
                                 glm::vec2 after,
                                 const Tilemap& tilemap)
{
    return SurfaceSystem::ResolveMove(GetSupport(elev), before, after, tilemap);
}

void CommitSupport(Elevation& elev, SupportState support)
{
    elev.surface = support.surface;
    elev.plane = support.height;
    SetElevationTarget(elev, static_cast<float>(support.height));
}

void DerivePlane(Elevation& elev, glm::vec2 before, glm::vec2 after, const Tilemap& tilemap)
{
    const SurfaceTransition transition = ResolveSupport(elev, before, after, tilemap);
    if (transition.connected)
    {
        CommitSupport(elev, transition.support);
    }
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
