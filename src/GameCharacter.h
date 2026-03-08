#pragma once

#include "IGameCharacter.h"

class IRenderer;

/**
 * @class GameCharacter
 * @brief Shared implementation of IGameCharacter for character state and behavior.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entity
 *
 * GameCharacter implements IGameCharacter and factors out the fields and
 * methods common to PlayerCharacter and NonPlayerCharacter: world position,
 * elevation offsets, cardinal direction, walk-cycle animation, and movement
 * speed.
 *
 * @par Design
 * Game stores `PlayerCharacter m_Player` and `vector<NonPlayerCharacter>`
 * by value, never through a GameCharacter pointer. The base class exists
 * purely for code sharing; no virtual dispatch is needed at runtime.
 *
 * @par Position (Bottom-Center)
 * Position is the **bottom-center** of the sprite (where the feet touch
 * the ground). Both derived classes share this convention:
 * @code
 *     +--------+
 *     |        |
 *     | Sprite |  32x32 pixels
 *     |        |
 *     +---oo---+
 *         ^^
 *      m_Position
 * @endcode
 *
 * @par Elevation System
 * Tiles can define a pixel offset that shifts characters vertically to
 * simulate stairs, ramps, and ledges. When a character steps onto a tile
 * with a different elevation, the offset smoothly transitions using a
 * cubic Hermite (smoothstep) interpolation over a fixed duration:
 * @f[
 * t = \text{clamp}\!\bigl(\tfrac{progress}{duration},\;0,\;1\bigr)
 * \qquad
 * offset = \text{start} + (target - start) \times (3t^2 - 2t^3)
 * @f]
 *
 * @par Walk Animation
 * All characters share the same four-frame walk cycle:
 * @code
 * Frame index:  1 -> 0 -> 2 -> 0  (WALK_SEQUENCE)
 *               L    N    R    N
 * @endcode
 * The cycle advances each time the animation timer expires. Derived
 * classes set different timer thresholds (e.g., faster when running).
 *
 * @par Shared Constants
 * | Constant             | Value | Meaning                              |
 * |----------------------|-------|--------------------------------------|
 * | SPRITE_WIDTH/HEIGHT  | 32    | Sprite sheet cell size in pixels     |
 * | COLLISION_EPS        | 0.05  | Floating-point margin for AABB tests |
 * | WALK_SEQUENCE_LENGTH | 4     | Number of frames in the walk cycle   |
 *
 * @see IGameCharacter, PlayerCharacter, NonPlayerCharacter, CharacterDirection
 */
class GameCharacter : public IGameCharacter
{
public:
    GameCharacter();
    ~GameCharacter() override = default;

    /// @name Position & Direction
    /// @{
    glm::vec2 GetPosition() const override { return m_Position; }
    void SetPosition(glm::vec2 pos) override { m_Position = pos; }
    CharacterDirection GetDirection() const override { return m_Direction; }
    void SetDirection(CharacterDirection dir) override { m_Direction = dir; }
    /// @}

    /// @name Elevation
    /// @{
    float GetElevationOffset() const override { return m_ElevationOffset; }
    float GetTargetElevation() const override { return m_TargetElevation; }

    /**
     * @brief Set target elevation offset for stairs/ramps.
     *
     * The visual elevation will smoothly interpolate toward this target
     * using smoothstep over a fixed duration (~0.15 s).
     *
     * @param offset Target Y offset in pixels (positive = rendered higher).
     */
    void SetElevationOffset(float offset) override;

    /**
     * @brief Advance the smooth elevation transition by one frame.
     *
     * Progresses the smoothstep interpolation from the current elevation
     * toward the target. Call exactly once per frame from the derived
     * class's Update().
     *
     * @param deltaTime Frame time in seconds.
     */
    void UpdateElevation(float deltaTime) override;
    /// @}

    /// @name Movement
    /// @{
    float GetSpeed() const override { return m_Speed; }
    void SetSpeed(float speed) override { m_Speed = speed; }
    /// @}

    /// @name Animation
    /// @{
    int GetCurrentFrame() const override { return m_CurrentFrame; }
    void SetCurrentFrame(int frame) override { m_CurrentFrame = frame; }
    float GetAnimationTime() const override { return m_AnimationTime; }
    void SetAnimationTime(float time) override { m_AnimationTime = time; }
    int GetWalkSequenceIndex() const override { return m_WalkSequenceIndex; }
    void SetWalkSequenceIndex(int index) override { m_WalkSequenceIndex = index; }

    /**
     * @brief Advance walk animation to the next frame in the cycle.
     *
     * Steps through the `[1, 0, 2, 0]` walk sequence, wrapping at the
     * end. Call when the per-frame animation timer has expired.
     */
    void AdvanceWalkAnimation() override;

    /**
     * @brief Snap animation back to the idle pose (frame 0, index 0).
     */
    void ResetAnimation() override;
    /// @}

    /// @name Shared Constants
    /// @{
    static constexpr int SPRITE_WIDTH = 32;                ///< Sprite sheet cell width in pixels
    static constexpr int SPRITE_HEIGHT = 32;               ///< Sprite sheet cell height in pixels
    static constexpr float COLLISION_EPS = 0.05f;          ///< AABB floating-point tolerance
    static constexpr int WALK_SEQUENCE[4] = {1, 0, 2, 0};  ///< Walk cycle frame indices
    static constexpr int WALK_SEQUENCE_LENGTH = 4;         ///< Length of WALK_SEQUENCE
    /// @}

protected:
    /// @name Position State
    /// @{
    glm::vec2 m_Position{0.0f, 0.0f};  ///< World position (bottom-center of sprite)
    /// @}

    /// @name Elevation State
    /// @{
    float m_ElevationOffset{0.0f};    ///< Current visual Y offset in pixels
    float m_TargetElevation{0.0f};    ///< Target elevation to interpolate toward
    float m_ElevationStart{0.0f};     ///< Elevation at start of current transition
    float m_ElevationProgress{1.0f};  ///< Interpolation progress (0 = start, 1 = done)
    /// @}

    /// @name Direction & Animation State
    /// @{
    CharacterDirection m_Direction{CharacterDirection::DOWN};  ///< Current facing direction
    int m_CurrentFrame{0};        ///< Active sprite sheet frame (column index)
    float m_AnimationTime{0.0f};  ///< Accumulator for animation timing
    int m_WalkSequenceIndex{0};   ///< Current index into WALK_SEQUENCE
    /// @}

    /// @name Movement State
    /// @{
    float m_Speed{100.0f};  ///< Movement speed in pixels per second
    /// @}
};
