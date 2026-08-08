#pragma once

#include "SupportSurface.hpp"

/**
 * @struct Elevation
 * @brief Smooth elevation transition state plus the logical collision plane.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Entities
 *
 * The transition logic is the free functions in CharacterKinematics.hpp.
 *
 * @par Three roles, three readers
 * The fields split by consumer, and confusing them is the common bug here.
 * - @ref offset is cosmetic. Only the render paths read it: PlayerRender, NpcRender, and the
 *   render-list cull in Game. It lags @ref plane by one smoothstep transition, so mid-climb it
 *   holds a fractional pixel value. Nothing gates on it.
 * - @ref plane is logical: the committed support height in whole pixels, matching
 *   Tilemap::GetElevation. Collision gates on it through CharacterKinematics::GetSupport.
 * - @ref surface is topology: ground, or authored elevation. It is what separates a character
 *   under a bridge from one standing on its deck at the same X and Y.
 *
 * @ref plane and @ref surface form one @ref SupportState and mean nothing apart. Write them only
 * through CharacterKinematics::CommitSupport, which sets both and starts the matching visual
 * transition. Assigning either on its own desynchronizes the character from the world topology.
 * @ref SurfaceSystem::ResolveMove is the rule that produces the support in the first place.
 *
 * @see CharacterKinematics, SurfaceSystem, SupportState, Transform
 */
struct Elevation
{
    float offset{0.0f};    ///< Smoothed visual Y shift in pixels; render-only, lags `plane`.
    float target{0.0f};    ///< Visual offset the transition is interpolating toward.
    float start{0.0f};     ///< Visual offset when the current transition began.
    float progress{1.0f};  ///< Interpolation progress; 0 = start, 1 = settled.
    int plane{0};          ///< Committed support height in pixels; 0 = ground. Collision reads it.
    SupportSurface surface{
        SupportSurface::Ground};  ///< Committed topology; pairs with `plane` as a SupportState.
};
