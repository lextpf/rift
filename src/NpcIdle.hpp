#pragma once

/**
 * @struct NpcIdle
 * @brief Per-NPC idle, standing-still and look-around timing state.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * At each waypoint an NPC may pause, look around, and occasionally enter a longer standing-still
 * state on a small random schedule. NpcAiSystem::Update owns every timer field here.
 * @ref isStopped is different: NpcAiSystem::ApplyPlayerOverlapStop rewrites it every frame, and
 * the dialogue path and the console `npc.freeze` command also set it - see the field's own note.
 *
 * @see NpcAiSystem
 */
struct NpcIdle
{
    float waitTimer{0.0f};  ///< Countdown before resuming patrol after reaching a waypoint.
    /**
     * @brief Halt flag.
     *
     * Two writers share this field with different intents, and they do not compose.
     * NpcAiSystem::ApplyPlayerOverlapStop assigns it every frame from same-support player overlap,
     * while the dialogue path and the console `npc.freeze` command set it as a sticky hold. The
     * per-frame writer therefore clears any hold on the next frame unless the player happens to be
     * overlapping. The dialogue freeze survives only because NpcAiSystem::UpdateAll skips the
     * speaker by @c Identity::instanceId rather than by this flag.
     */
    bool isStopped{false};
    bool standingStill{false};               ///< True while in the random standing-still state.
    float lookAroundTimer{0.0f};             ///< Countdown until the next look-around step.
    float randomStandStillCheckTimer{0.0f};  ///< Seconds between stand-still rolls, about 5 to 10.
    float randomStandStillTimer{0.0f};  ///< Seconds left in the stand-still state, about 2 to 5.
};
