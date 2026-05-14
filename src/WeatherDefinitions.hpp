#pragma once

#include "EnumTraits.hpp"

#include <glm/glm.hpp>
#include <utility>

/**
 * @enum WeatherState
 * @brief Weather conditions affecting lighting, particles, and sky rendering.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Each state drives a `WeatherDefinition` (see GetWeatherDefinition) that
 * specifies ambient tint, particle spawn config, and sky modifications.
 */
enum class WeatherState
{
    // Baseline
    Clear = 0,
    Overcast,

    // Precipitation
    LightRain,
    HeavyRain,
    Thunderstorm,
    Snow,
    Blizzard,

    // Atmosphere
    Fog,
    Mist,
    HeatHaze,
    Sandstorm,

    // Floral / seasonal
    FallingLeaves,
    CherryBlossoms,
    PollenStorm,

    // Special / night
    AuroraNight,
    MeteorShower,
    FireflySwarm,
    AshFall,
    EmberStorm
};

/// Compile-time reflection for WeatherState.
template <>
struct EnumTraits<WeatherState> : EnumTraitsBase<WeatherState, EnumTraits<WeatherState>>
{
    static constexpr size_t Count = 19;
    static constexpr std::string_view Names[] = {"Clear",
                                                 "Overcast",
                                                 "LightRain",
                                                 "HeavyRain",
                                                 "Thunderstorm",
                                                 "Snow",
                                                 "Blizzard",
                                                 "Fog",
                                                 "Mist",
                                                 "HeatHaze",
                                                 "Sandstorm",
                                                 "FallingLeaves",
                                                 "CherryBlossoms",
                                                 "PollenStorm",
                                                 "AuroraNight",
                                                 "MeteorShower",
                                                 "FireflySwarm",
                                                 "AshFall",
                                                 "EmberStorm"};

    static_assert(std::to_underlying(WeatherState::EmberStorm) == Count - 1,
                  "Update EnumTraits<WeatherState> when adding new WeatherState values");
};

/**
 * @enum WeatherParticleType
 * @brief Identifies which particle effect a weather state spawns.
 *
 * Decoupled from ParticleSystem's internal `ParticleType` enum so that
 * WeatherDefinitions.h can stay renderer-free and unit-testable.
 * ParticleSystem translates this to the concrete ParticleType at spawn time.
 */
enum class WeatherParticleType
{
    None = 0,  ///< No additional particles.
    Rain,      ///< Maps to ParticleType::Rain.
    Snow,      ///< Maps to ParticleType::Snow.
    Fog,       ///< Maps to ParticleType::Fog.
    Leaf,      ///< Maps to ParticleType::DriftingLeaf.
    Blossom,   ///< Maps to ParticleType::CherryBlossom.
    Pollen,    ///< Maps to ParticleType::Pollen.
    Ash,       ///< Maps to ParticleType::Ash.
    Ember,     ///< Maps to ParticleType::Ember.
    Sand,      ///< Maps to ParticleType::Sand.
    Firefly    ///< Maps to ParticleType::Firefly.
};

/**
 * @enum LightSchedule
 * @brief When a WorldLight emits.
 *
 * Drives `ComputeLightIntensity(schedule, hour)` to produce a smooth
 * 0-1 envelope around dusk/dawn.
 */
enum class LightSchedule
{
    AlwaysOn = 0,  ///< Full intensity 24h.
    NightOnly,     ///< Full 22:00-04:00, fades 04:00-06:00 and 20:00-22:00.
    DuskToDawn     ///< Full 20:00-04:00, fades 04:00-07:00 and 18:00-20:00.
};

template <>
struct EnumTraits<LightSchedule> : EnumTraitsBase<LightSchedule, EnumTraits<LightSchedule>>
{
    static constexpr size_t Count = 3;
    static constexpr std::string_view Names[] = {"AlwaysOn", "NightOnly", "DuskToDawn"};

    static_assert(std::to_underlying(LightSchedule::DuskToDawn) == Count - 1,
                  "Update EnumTraits<LightSchedule> when adding new schedule values");
};

/**
 * @struct WorldLight
 * @brief A point light source anchored to a world position.
 *
 * Owned by `Tilemap` (serialized in the map JSON). Rendered as an additive
 * soft-circle sprite in `Game::Render`, with intensity driven by
 * `ComputeLightIntensity(schedule, time.GetTimeOfDay()) * nightFactor`.
 */
struct WorldLight
{
    glm::vec2 position{0.0f};             ///< World pixel coords (center of pool).
    glm::vec3 color{1.0f, 0.85f, 0.55f};  ///< RGB tint (default: warm lantern).
    float radius{64.0f};                  ///< Soft-circle radius in world pixels.
    LightSchedule schedule{LightSchedule::NightOnly};
};

/**
 * @struct WeatherDefinition
 * @brief Per-weather configuration for ambient, particles, and sky FX.
 *
 * Looked up by `GetWeatherDefinition(state)` - a static table indexed
 * by `std::to_underlying(WeatherState)`. Pure data, no side effects.
 */
struct WeatherDefinition
{
    /// Ambient tint multiplier composed on top of the TimeManager tint.
    glm::vec3 ambientTintMultiplier{1.0f, 1.0f, 1.0f};

    /// If any component is < 0, no override (use TimeManager::GetSkyColor).
    glm::vec3 skyColorOverride{-1.0f, -1.0f, -1.0f};

    /// Primary weather particle. None disables weather-driven spawning.
    WeatherParticleType particleType{WeatherParticleType::None};

    /// Particles spawned per second across the visible viewport (before intensity).
    float baseSpawnRate{0.0f};

    /// Hard cap on simultaneously live weather particles.
    int maxWeatherParticles{0};

    /// Multiplier on the type's default sprite size.
    float particleSizeScale{1.0f};

    /// If >= 0, overrides `TimeManager::GetStarVisibility` (clamped 0-1).
    float starVisibilityOverride{-1.0f};

    /// Hide sun/moon body sprites entirely (rays still draw if sun is up).
    bool showCelestialBodies{true};

    /// If > 0, lightning flashes this often (seconds, with +/-30% jitter).
    float lightningIntervalSeconds{0.0f};

    /// Render aurora bands in the upper sky.
    bool showAurora{false};

    /// Spawn rate multiplier on shooting stars (1.0 = default cadence).
    float meteorRateMultiplier{1.0f};

    /// Wind intensity 0-1 - affects horizontal drift of leaf/pollen/ash/sand.
    float windIntensity{0.5f};

    /// Optional secondary weather particle that spawns alongside @ref particleType.
    /// Default `None` means single-type spawning (current behavior). Used by
    /// Blizzard to layer Fog particles on top of Snow without losing either.
    WeatherParticleType secondaryParticleType{WeatherParticleType::None};

    /// Spawn rate (particles/sec) for the secondary particle. Ignored when
    /// @ref secondaryParticleType is `None`.
    float secondaryBaseSpawnRate{0.0f};

    /// Hard cap on simultaneously live secondary particles. 0 = unlimited
    /// (still subject to global ParticleSystem cap).
    int secondaryMaxWeatherParticles{0};

    /// Multiplier on the Fog particle's render-time base alpha (default 1.0
    /// = unchanged). Lets fog-bearing weathers soften the fog wall without
    /// changing density. Read by `ParticleBehavior<ParticleType::Fog>::Update`.
    float fogAlphaMultiplier{1.0f};

    /// Reserved for a future heat-haze post-FX pass; currently unused.
    /// The tint multiplier still applies.
    float hazeAmplitude{0.0f};
};

/**
 * @brief Look up the static definition for a weather state.
 * @param state Any WeatherState value.
 * @return Reference to a static WeatherDefinition (lifetime: program).
 */
const WeatherDefinition& GetWeatherDefinition(WeatherState state);

/**
 * @brief Compute light envelope (0-1) for a schedule at the given hour.
 *
 * Uses a smoothstep ramp at the dawn and dusk boundaries:
 *  - AlwaysOn   -> 1.0
 *  - NightOnly  -> 1.0 in [22, 4) (wraps midnight), smoothstep ramp
 *                 [20, 22] up and [4, 6] down, 0.0 elsewhere.
 *  - DuskToDawn -> 1.0 in [20, 4) (wraps midnight), smoothstep ramp
 *                 [18, 20] up and [4, 7] down, 0.0 elsewhere.
 *
 * @param schedule Light schedule.
 * @param hourOfDay Time in hours [0, 24).
 * @return Intensity in [0, 1].
 */
float ComputeLightIntensity(LightSchedule schedule, float hourOfDay);
