#pragma once

/**
 * @struct NpcIdle
 * @brief Per-NPC idle / standing-still / look-around timing state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The idle-behavior state carved out of NonPlayerCharacter's loose members. At
 * each waypoint the NPC may pause, look around, and occasionally enter a longer
 * standing-still state on a small random schedule. The FSM that drives these
 * fields is the NPC AI logic (a future NpcAiSystem).
 *
 * Plain data struct: flat aggregate (no ctors/bases/virtuals), usable directly
 * as an ECS component (packed storage).
 */
struct NpcIdle
{
    float waitTimer{0.0f};        ///< Countdown before resuming patrol after reaching a waypoint.
    bool isStopped{false};        ///< Soft-collision halt while overlapping the player.
    bool standingStill{false};    ///< In the random standing-still idle state.
    float lookAroundTimer{0.0f};  ///< Countdown until the next look-around step.
    float randomStandStillCheckTimer{0.0f};  ///< Interval (~5-10 s) between stand-still rolls.
    float randomStandStillTimer{0.0f};       ///< Remaining time (~2-5 s) in the stand-still state.
};
