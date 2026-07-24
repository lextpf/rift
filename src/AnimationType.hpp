#pragma once

/**
 * @enum AnimationType
 * @brief Animation state machine states.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * Decides whether the walk cycle advances, and - together with the PlayerModes flags - which
 * sprite sheet the player draws from. PlayerRender::ResolveRenderSheet performs the sheet
 * lookup and tests PlayerModes::isBicycling first, so this enum does not pick the sheet alone.
 *
 * The enum carries no timing of its own. Frame cadence comes from actual velocity in
 * PlayerMovementSystem::UpdateAnimation.
 */
enum class AnimationType
{
    IDLE = 0,  ///< Standing still; the walk cycle resets to frame 0 instead of advancing.
    WALK = 1,  ///< Walk cycle advancing over the sequence [1,0,2,0], drawn from the walking sheet.
    /**
     * @brief Same sequence and timing as WALK; covers running and bicycling alike.
     *
     * There is no separate bicycle state. PlayerRender::ResolveRenderSheet tests
     * PlayerModes::isBicycling before this enum, so RUN draws the bicycle sheet while
     * bicycling and the running sheet otherwise.
     */
    RUN = 2
};
