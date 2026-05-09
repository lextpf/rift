#include "GameCharacter.h"

#include <cstdlib>

GameCharacter::GameCharacter()
    : m_Position(0.0f, 0.0f),
      m_ElevationOffset(0.0f),
      m_TargetElevation(0.0f),
      m_ElevationStart(0.0f),
      m_ElevationProgress(1.0f),
      m_Direction(CharacterDirection::DOWN),
      m_CurrentFrame(0),
      m_AnimationTime(0.0f),
      m_WalkSequenceIndex(0),
      m_Speed(100.0f)
{
}

void GameCharacter::SetElevationOffset(float offset)
{
    if (offset != m_TargetElevation)
    {
        m_ElevationStart = m_ElevationOffset;
        m_TargetElevation = offset;
        m_ElevationProgress = 0.0f;
    }
}

void GameCharacter::UpdatePlane(int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy)
{
    if (tileAxis == ElevationAxis::None)
    {
        // Ground / non-elevated tile: always engage so an entity stepping
        // off an elevated region snaps back to ground regardless of drop
        // height. The step gate only protects elevated entries.
        m_Plane = destTileElev;
        SetElevationOffset(static_cast<float>(destTileElev));
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

    int delta = destTileElev - m_Plane;
    if (std::abs(delta) > MAX_STEP_HEIGHT)
    {
        // No ramp connecting the two planes - reject the direct jump.
        return;
    }

    m_Plane = destTileElev;
    SetElevationOffset(static_cast<float>(destTileElev));
}

void GameCharacter::UpdateElevation(float deltaTime)
{
    if (m_ElevationProgress < 1.0f)
    {
        constexpr float transitionDuration = 0.15f;
        m_ElevationProgress += deltaTime / transitionDuration;

        if (m_ElevationProgress >= 1.0f)
        {
            m_ElevationProgress = 1.0f;
            m_ElevationOffset = m_TargetElevation;
        }
        else
        {
            float t = m_ElevationProgress;
            float smoothT = t * t * (3.0f - 2.0f * t);
            m_ElevationOffset = m_ElevationStart + (m_TargetElevation - m_ElevationStart) * smoothT;
        }
    }
}

void GameCharacter::AdvanceWalkAnimation()
{
    m_WalkSequenceIndex = (m_WalkSequenceIndex + 1) % WALK_SEQUENCE_LENGTH;
    m_CurrentFrame = WALK_SEQUENCE[m_WalkSequenceIndex];
}

void GameCharacter::ResetAnimation()
{
    m_CurrentFrame = 0;
    m_WalkSequenceIndex = 0;
    m_AnimationTime = 0.0f;
}
