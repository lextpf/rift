#pragma once

#include "WeatherDefinitions.hpp"

#include <cstdint>

/**
 * @brief Pure blend math for weather transitions.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Renderer-free free functions consumed by WeatherDirector.
 */

/**
 * @brief Standard smoothstep easing, clamped to [0, 1].
 * @ingroup Effects
 * @param t Raw progress.
 * @return t*t*(3-2t) after clamping.
 */
float BlendSmoothstep(float t);

/**
 * @brief True when the weather spawns Fog-type particles (primary or secondary).
 * @ingroup Effects
 *
 * Drives the WeatherDirector fog-hold rule: fog -> no-fog transitions hold the
 * outgoing fogAlphaMultiplier so surviving puffs don't brighten.
 */
bool WeatherSpawnsFogType(const WeatherDefinition& def);

/**
 * @brief Blend two weather definitions at progress t.
 * @ingroup Effects
 *
 * Contract: t <= 0 returns @p a verbatim and t >= 1 returns @p b verbatim
 * (every field, sentinels included). Interior t rules:
 *  - Plain floats mix linearly (tint, rates, size, wind, haze, meteor, fog
 *    alpha). Caller applies easing to t; this function is a straight combine.
 *  - lightningIntervalSeconds blends in frequency space (1/interval) so a
 *    ramp-in never sweeps through tiny strobing intervals.
 *  - Spawn slots whose particle type differs between endpoints ramp the
 *    incoming rate from zero (rate = b.rate * t) instead of mixing across
 *    unrelated types.
 *  - Integer caps mix-and-round; for a type both endpoints spawn, the cap is
 *    min of the two endpoints' caps for that type (no mixing - caps are
 *    safety ceilings; 0 = uncapped counts as infinite).
 *  - Sentinel overrides (sky color, star visibility), bools, and particle
 *    type enums are copied from @p b; TimeManager resolves sentinels per
 *    endpoint and WeatherDirector publishes fade scalars for the bools.
 *
 * @param a Outgoing endpoint.
 * @param b Incoming endpoint.
 * @param t Blend progress (typically already smoothstepped).
 * @return Blended definition (by value).
 */
WeatherDefinition BlendWeatherDefinitions(const WeatherDefinition& a,
                                          const WeatherDefinition& b,
                                          float t);

/**
 * @brief Deterministic 64-bit mixer (SplitMix64).
 * @ingroup Effects
 *
 * Same input -> same output, forever. Used for gust phases, front boundaries,
 * and forecast rolls.
 *
 * @param x Input value to mix.
 * @return Mixed 64-bit value.
 */
uint64_t SplitMix64(uint64_t x);

/**
 * @brief Three gust phase offsets in [0, 2*pi), derived deterministically
 * from a seed (typically hash of the day index).
 * @ingroup Effects
 *
 * .x/.y drive the two strength sines, .z drives the direction wander.
 *
 * @param seed Deterministic seed.
 * @return Three phase offsets in radians, each in [0, 2*pi).
 */
glm::vec3 GustPhases(uint64_t seed);

/**
 * @brief Gusted wind strength, always non-negative.
 * @ingroup Effects
 *
 * strength = base * (1 + GUST_AMP * (0.6*sin(2*pi*t/T1 + p1) +
 * 0.4*sin(2*pi*t/T2 + p2))), clamped at zero.
 *
 * @param base Base wind strength (weather's steady-state value).
 * @param clockSeconds Real-time clock, seconds.
 * @param phases Gust phase offsets from GustPhases (.x/.y used).
 * @return Gusted strength, never negative.
 */
float GustWindStrength(float base, double clockSeconds, const glm::vec3& phases);

/**
 * @brief Gust wind direction.
 * @ingroup Effects
 *
 * ambience::WEATHER_WIND_BASE_DIR rotated by a slow sine wander of
 * +/- WEATHER_WIND_WANDER_DEG.
 *
 * @param clockSeconds Real-time clock, seconds.
 * @param phases Gust phase offsets from GustPhases (.z used).
 * @return Normalized wind direction.
 */
glm::vec2 GustWindDirection(double clockSeconds, const glm::vec3& phases);

/**
 * @brief Cap for a given weather-particle type across a definition's two
 * spawn slots.
 * @ingroup Effects
 *
 * Returns 0 both when the definition does not spawn the type and when it
 * spawns the type uncapped. BlendCap and ParticleSystem's per-stream
 * shared-type cap floor both treat 0 as infinite, so the two cases are
 * interchangeable there.
 *
 * @param def Weather definition to inspect.
 * @param type Particle type to look up.
 * @return The cap for that type, or 0 if not spawned by either slot.
 */
int WeatherCapForType(const WeatherDefinition& def, WeatherParticleType type);

/**
 * @struct ForecastEntry
 * @brief One day's forecast: the front weather holding that day, plus an
 * optional night event overlaying dusk (20:00) through the next day's dawn
 * (5:00).
 * @ingroup Effects
 */
struct ForecastEntry
{
    WeatherState front{WeatherState::Clear};       ///< Front weather holding this day.
    bool hasNightEvent{false};                     ///< Whether a night event overlays tonight.
    WeatherState nightEvent{WeatherState::Clear};  ///< Night event, valid only if hasNightEvent.
};

/**
 * @brief Index of the front containing @p dayIndex.
 * @ingroup Effects
 *
 * Fronts are runs of ~ambience::WEATHER_FRONT_LENGTH_DAYS days with
 * hash-jittered boundaries; total coverage, no gaps or overlaps.
 * Deterministic in (seed, dayIndex).
 *
 * @param seed Deterministic world seed.
 * @param dayIndex In-game day index (may be negative).
 * @return Front index containing dayIndex; non-decreasing as dayIndex grows,
 * increasing by exactly 1 across a front boundary.
 */
int64_t ForecastFrontIndex(uint64_t seed, int64_t dayIndex);

/**
 * @brief The full forecast for a day.
 * @ingroup Effects
 *
 * Deterministic, allocation-free, O(1). The front that contains day 0 is
 * always Clear - preserves boot behavior. Its index is not necessarily 0:
 * boundary jitter can place day 0 in front -1, so the test is
 * `front == ForecastFrontIndex(seed, 0)`, not `front == 0`.
 *
 * @param seed Deterministic world seed.
 * @param dayIndex In-game day index (may be negative).
 * @return The forecast entry for that day.
 */
ForecastEntry ForecastForDay(uint64_t seed, int64_t dayIndex);

/**
 * @name Overlay merge helpers
 * @brief "Base owns ground, overlay owns sky", scaled by a 0-1 overlay blend.
 * @ingroup Effects
 *
 * Every helper degenerates to the base value at blend/amount 0. Above 0 the
 * guarantee is per-helper: BlendOverlayScalar and BlendOverlayAuroraFade can
 * only raise their channel, while BlendOverlayTint is a multiplicative tint and
 * darkens the base wherever an overlay tint channel is below 1 (FireflySwarm's
 * {0.90, 1.00, 0.85}, for example). Pure; unit-tested in WeatherOverlayTests.
 * @{
 */

/// @brief `lerp(base, max(base, overlay), clamp(blend, 0, 1))` - the overlay
/// can raise the channel but never lower it.
float BlendOverlayScalar(float base, float overlay, float blend);

/// @brief `baseColor * mix(vec3(1), overlayTint, clamp(amount, 0, 1))` - fades
/// the overlay's ambient tint in as a multiplier on the base color.
glm::vec3 BlendOverlayTint(glm::vec3 baseColor, glm::vec3 overlayTint, float amount);

/// @brief `max(baseFade, clamp(blend, 0, 1))` when @p overlayHasAurora, else
/// @p baseFade untouched (the overlay never hides the base weather's aurora).
float BlendOverlayAuroraFade(float baseFade, float blend, bool overlayHasAurora);
/// @}
