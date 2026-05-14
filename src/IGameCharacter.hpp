#pragma once

#include "ElevationAxis.h"

#include <glm/glm.hpp>

/**
 * @enum CharacterDirection
 * @brief Cardinal direction a character is facing.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
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
 * @ingroup Entities
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

    /// @brief Get the character's world position (bottom-center of sprite).
    virtual glm::vec2 GetPosition() const = 0;
    /// @brief Set the character's world position.
    virtual void SetPosition(glm::vec2 pos) = 0;
    /// @brief Get the direction the character is facing.
    virtual CharacterDirection GetDirection() const = 0;
    /// @brief Set the direction the character faces.
    virtual void SetDirection(CharacterDirection dir) = 0;
    /// @}

    /// @name Elevation
    /// @{

    /// @brief Get the current visual elevation offset in pixels.
    virtual float GetElevationOffset() const = 0;
    /// @brief Get the target elevation the character is transitioning to.
    virtual float GetTargetElevation() const = 0;
    /// @brief Set the visual elevation offset directly.
    virtual void SetElevationOffset(float offset) = 0;
    /// @brief Smoothly interpolate elevation toward target over time.
    virtual void UpdateElevation(float deltaTime) = 0;

    /// @brief Get the character's logical elevation plane in pixels.
    ///
    /// The plane is the discrete elevation the character is occupying
    /// (e.g. 0 for ground, 10 for a bridge deck). Used by collision gating.
    /// Differs from GetElevationOffset(), which is the smoothed visual offset
    /// that interpolates toward the plane.
    virtual int GetPlane() const = 0;

    /// @brief Update the logical plane based on a destination tile.
    ///
    /// Applies the axis-engagement rule: the plane only tracks the
    /// destination tile's elevation when the tile's elevation axis
    /// matches the movement direction (or the tile has no axis), and
    /// the elevation delta is within MAX_STEP_HEIGHT. Movement
    /// perpendicular to the axis leaves the plane unchanged so the
    /// entity walks under (or over) the tile.
    ///
    /// @param destTileElev Elevation of the tile being entered (pixels).
    /// @param tileAxis     Auto-derived axis of the destination tile.
    /// @param moveDx       Movement X direction sign (-1, 0, +1).
    /// @param moveDy       Movement Y direction sign (-1, 0, +1).
    virtual void UpdatePlane(int destTileElev, ElevationAxis tileAxis, int moveDx, int moveDy) = 0;
    /// @}

    /// @name Movement
    /// @{

    /// @brief Get the character's movement speed in pixels per second.
    virtual float GetSpeed() const = 0;
    /// @brief Set the character's movement speed in pixels per second.
    virtual void SetSpeed(float speed) = 0;
    /// @}

    /// @name Animation
    /// @{

    /// @brief Get the current animation frame index.
    virtual int GetCurrentFrame() const = 0;
    /// @brief Set the current animation frame index.
    virtual void SetCurrentFrame(int frame) = 0;
    /// @brief Get the animation timer value in seconds.
    virtual float GetAnimationTime() const = 0;
    /// @brief Set the animation timer value in seconds.
    virtual void SetAnimationTime(float time) = 0;
    /// @brief Get the current position in the walk sequence [1,0,2,0].
    virtual int GetWalkSequenceIndex() const = 0;
    /// @brief Set the current position in the walk sequence.
    virtual void SetWalkSequenceIndex(int index) = 0;
    /// @brief Advance to the next frame in the walk cycle.
    virtual void AdvanceWalkAnimation() = 0;
    /// @brief Reset animation to the idle frame.
    virtual void ResetAnimation() = 0;
    /// @}
};
