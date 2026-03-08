#include "GameCharacter.h"

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
