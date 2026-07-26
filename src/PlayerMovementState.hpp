#pragma once

#include <glm/glm.hpp>

/**
 * @struct PlayerMovementState
 * @brief Per-player slide, axis and idle-snap hysteresis state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Groups the frame-to-frame collision state that @ref CollisionSystem reads and writes while
 * resolving player movement: wall-slide direction commit, axis preference, last input sign, and
 * the stuck-recovery anchor.
 *
 * Every field is hysteresis. Each exists so that a decision made on one frame is not immediately
 * reversed on the next, which is what stops the player flickering between two equally valid
 * resolutions at a corner.
 *
 * EntityStore::SpawnPlayer attaches this component.
 *
 * @see PlayerMovementSystem, CollisionSystem
 */
struct PlayerMovementState
{
    glm::vec2 slideDir{0.0f};  ///< Last chosen wall-slide direction, held to damp jitter.
    /**
     * @brief Seconds left on the slide-direction commit, about 120 ms when a direction is chosen.
     *
     * While it runs, CollisionSystem refuses to clear @ref slideDir, so a player wedged in a
     * tie-breaker keeps sliding one way instead of alternating every frame.
     */
    float slideTimer{0.0f};
    int axisPref{0};        ///< Axis preference: -1 prefers Y, +1 prefers X, 0 is no preference.
    float axisTimer{0.0f};  ///< Seconds remaining before the axis preference may change.
    glm::vec2 snapStart{0.0f};   ///< Unused: nothing in the tree reads or writes it.
    glm::vec2 snapTarget{0.0f};  ///< Unused: nothing in the tree reads or writes it.
    float snapProgress{1.0f};    ///< Unused: nothing in the tree reads or writes it.
    /**
     * @brief Feet position at the center of the last tile the player occupied while not embedded
     *        in a solid tile.
     *
     * Write-only as things stand. EntityStore::SpawnPlayer seeds it, and both
     * PlayerMovementSystem::Step and CollisionSystem::HandleStuckRecovery keep it current, but
     * nothing reads it. Recovery recomputes a target from scratch through
     * CollisionSystem::FindClosestSafeTileCenter instead.
     *
     * The seed is the raw spawn position, not a tile center, so the tile-center invariant only
     * holds from the first PlayerMovementSystem::Step onward.
     */
    glm::vec2 lastSafeTileCenter{0.0f};
    int lastInputX{0};  ///< Sign of the last non-zero horizontal input: -1 left, +1 right.
    int lastInputY{0};  ///< Sign of the last non-zero vertical input: -1 up, +1 down.
};
