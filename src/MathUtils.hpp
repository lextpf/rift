#pragma once

#include <algorithm>
#include <cmath>

/**
 * @brief Frame-rate independent smoothing helpers.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Stateless free functions over plain values - no global, engine, or GPU state.
 */

namespace rift
{

/**
 * @brief Compute exponential-decay smoothing alpha for use with lerp.
 *
 * Produces frame-rate independent motion: unlike a fixed lerp factor (e.g., 10% per
 * frame), the result scales with delta time, so the visual speed is consistent
 * regardless of frame rate:
 * @f[
 *   \alpha = 1 - \varepsilon^{\,\Delta t / t_{settle}}
 * @f]
 * where @f$ \varepsilon @f$ is the @p e parameter (not Euler's number). Inputs are
 * sanitised rather than asserted: negative @p dt clamps to 0, and @p st is floored at
 * 1e-5 so the exponent stays finite.
 *
 * @param dt  Delta time this frame (seconds). Negative values clamp to 0.
 * @param st  Settle time: how long until only @p e of the distance remains
 *            (seconds). Values below 1e-5 are raised to 1e-5.
 * @param e   Epsilon: the fraction of the distance still left after @p st seconds
 *            (default 0.01, i.e. 99% covered).
 * @return    Alpha in [0,1] for use with: current = lerp(current, target, alpha).
 */
inline float ExpApproachAlpha(float dt, float st, float e = 0.01f)
{
    dt = std::max(0.0f, dt);
    st = std::max(1e-5f, st);
    return std::clamp(1.0f - std::pow(e, dt / st), 0.0f, 1.0f);
}

}  // namespace rift
