#pragma once

#include "WeatherDefinitions.hpp"

#include <cstdint>

/**
 * @brief Pure blend math for weather transitions.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Renderer-free free functions consumed by WeatherDirector. Field-by-field
 * blend rules are documented in
 * docs/superpowers/specs/2026-07-02-weather-director-design.md (section 3).
 */

/**
 * @brief Standard smoothstep easing, clamped to [0, 1].
 * @param t Raw progress.
 * @return t*t*(3-2t) after clamping.
 */
float BlendSmoothstep(float t);

/**
 * @brief True when the weather spawns Fog-type particles (primary or secondary).
 *
 * Drives the WeatherDirector fog-hold rule: fog -> no-fog transitions hold the
 * outgoing fogAlphaMultiplier so surviving puffs don't brighten.
 */
bool WeatherSpawnsFogType(const WeatherDefinition& def);

/**
 * @brief Blend two weather definitions at progress t.
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
 *  - Integer caps mix-and-round; for a type BOTH endpoints spawn, the cap is
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
 *
 * Same input -> same output, forever (used for gust phases and, in Phase 3,
 * forecast rolls).
 *
 * @param x Input value to mix.
 * @return Mixed 64-bit value.
 */
uint64_t SplitMix64(uint64_t x);

/**
 * @brief Three gust phase offsets in [0, 2*pi), derived deterministically
 * from a seed (typically hash of the day index).
 *
 * .x/.y drive the two strength sines, .z drives the direction wander.
 *
 * @param seed Deterministic seed.
 * @return Three phase offsets in radians, each in [0, 2*pi).
 */
glm::vec3 GustPhases(uint64_t seed);

/**
 * @brief Gusted wind strength, always non-negative.
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
 *
 * ambience::CLOUD_SHADOW_WIND_DIR rotated by a slow sine wander of
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
 *
 * Returns 0 (uncapped) when the definition doesn't spawn the type. This is
 * the existing .cpp-internal CapForType helper PROMOTED to the public API --
 * Task 6's per-stream shared-type cap floor needs it.
 *
 * @param def Weather definition to inspect.
 * @param type Particle type to look up.
 * @return The cap for that type, or 0 if not spawned by either slot.
 */
int WeatherCapForType(const WeatherDefinition& def, WeatherParticleType type);

/**
 * @brief One day's forecast: the front weather holding that day, plus an
 * optional night event overlaying dusk (20:00) through the next day's dawn
 * (5:00).
 */
struct ForecastEntry
{
    WeatherState front{WeatherState::Clear};       ///< Front weather holding this day.
    bool hasNightEvent{false};                     ///< Whether a night event overlays tonight.
    WeatherState nightEvent{WeatherState::Clear};  ///< Night event, valid only if hasNightEvent.
};

/**
 * @brief Index of the front containing @p dayIndex.
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
 *
 * Deterministic, allocation-free, O(1). Front 0 (containing day 0) is always
 * Clear - preserves boot behavior.
 *
 * @param seed Deterministic world seed.
 * @param dayIndex In-game day index (may be negative).
 * @return The forecast entry for that day.
 */
ForecastEntry ForecastForDay(uint64_t seed, int64_t dayIndex);
