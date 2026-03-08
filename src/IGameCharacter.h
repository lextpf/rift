#pragma once

#include <glm/glm.hpp>

/**
 * @enum CharacterDirection
 * @brief Cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entity
 *
 * Unified direction enum shared by PlayerCharacter and NonPlayerCharacter.
 * Values map directly to sprite sheet row offsets for animation lookup,
 * though each derived class may apply its own row-mapping table on top.
 *
 * @par Sprite Sheet Row Mapping
 * | Direction | Value | Player Row | NPC Row |
 * |-----------|-------|------------|---------|
 * | DOWN      |     0 | 0          | 2       |
 * | UP        |     1 | 1          | 3       |
 * | LEFT      |     2 | 2          | 1       |
 * | RIGHT     |     3 | 3          | 0       |
 *
 * @par Type Aliases
 * `Direction` and `NPCDirection` are provided as backward-compatible
 * aliases so existing PlayerCharacter and NonPlayerCharacter code
 * compiles without modification.
 *
 * @see GameCharacter, PlayerCharacter, NonPlayerCharacter
 */
enum class CharacterDirection
{
    DOWN = 0,  ///< Facing down (towards camera, +Y direction)
    UP = 1,    ///< Facing up (away from camera, -Y direction)
    LEFT = 2,  ///< Facing left (-X direction)
    RIGHT = 3  ///< Facing right (+X direction)
};

using Direction = CharacterDirection;     ///< Alias for PlayerCharacter code
using NPCDirection = CharacterDirection;  ///< Alias for NonPlayerCharacter code

/**
 * @class IGameCharacter
 * @brief Abstract interface for game character state and behavior.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entity
 *
 * IGameCharacter defines the public API contract that all game characters
 * must implement. This abstraction documents the shared interface between
 * PlayerCharacter and NonPlayerCharacter, following the same interface
 * pattern used by IRenderer.
 *
 * @par Design
 * Game stores `PlayerCharacter m_Player` and `vector<NonPlayerCharacter>`
 * by value, never through an IGameCharacter pointer. The interface exists
 * for API documentation and consistency with codebase conventions; no
 * virtual dispatch is needed at runtime.
 *
 * @see GameCharacter, PlayerCharacter, NonPlayerCharacter, CharacterDirection
 */
class IGameCharacter
{
public:
    virtual ~IGameCharacter() = default;

    /// @name Position & Direction
    /// @{
    virtual glm::vec2 GetPosition() const = 0;
    virtual void SetPosition(glm::vec2 pos) = 0;
    virtual CharacterDirection GetDirection() const = 0;
    virtual void SetDirection(CharacterDirection dir) = 0;
    /// @}

    /// @name Elevation
    /// @{
    virtual float GetElevationOffset() const = 0;
    virtual float GetTargetElevation() const = 0;
    virtual void SetElevationOffset(float offset) = 0;
    virtual void UpdateElevation(float deltaTime) = 0;
    /// @}

    /// @name Movement
    /// @{
    virtual float GetSpeed() const = 0;
    virtual void SetSpeed(float speed) = 0;
    /// @}

    /// @name Animation
    /// @{
    virtual int GetCurrentFrame() const = 0;
    virtual void SetCurrentFrame(int frame) = 0;
    virtual float GetAnimationTime() const = 0;
    virtual void SetAnimationTime(float time) = 0;
    virtual int GetWalkSequenceIndex() const = 0;
    virtual void SetWalkSequenceIndex(int index) = 0;
    virtual void AdvanceWalkAnimation() = 0;
    virtual void ResetAnimation() = 0;
    /// @}
};
