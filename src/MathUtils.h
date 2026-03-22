#pragma once

#include <algorithm>
#include <cmath>

namespace rift
{

/// Compute exponential-decay smoothing alpha for use with lerp.
///
/// Produces frame-rate independent motion: unlike a fixed lerp factor
/// (e.g., 10% per frame), the result scales with delta time so the
/// visual speed is consistent regardless of frame rate.
///
/// @param dt  Delta time this frame (seconds).
/// @param st  Settle time: roughly how long to reach the target (seconds).
/// @param e   Epsilon: how close to target counts as "arrived" (default 1%).
/// @return    Alpha in [0,1] for use with: current = lerp(current, target, alpha).
inline float ExpApproachAlpha(float dt, float st, float e = 0.01f)
{
    dt = std::max(0.0f, dt);
    st = std::max(1e-5f, st);
    return std::clamp(1.0f - std::pow(e, dt / st), 0.0f, 1.0f);
}

}  // namespace rift
