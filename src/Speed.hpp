#pragma once

/**
 * @struct Speed
 * @brief Base movement speed in pixels per second, shared by all characters.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The per-character base speed only. The movement logic applies mode multipliers for running and
 * bicycling, and the developer-console multiplier, on top of this value.
 *
 * @warning Nothing observes the 100 initializer in play, because every spawn path overwrites it.
 * EntityStore::SpawnPlayer writes @ref CharacterConstants::PLAYER_BASE_SPEED, which is 50, and
 * EntityStore::SpawnNpc writes @ref CharacterConstants::NPC_BASE_SPEED, which is 25. Those are
 * the real speeds to reason about. 50 is also the 1.0x cadence reference that the player's
 * animation timing scales against, so treat 100 as an inert default rather than a tuning knob.
 *
 * @see CharacterConstants, MotionSystem
 */
struct Speed
{
    float value{100.0f};  ///< Base movement speed in px/s; overwritten at spawn.
};
