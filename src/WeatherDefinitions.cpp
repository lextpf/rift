#include "WeatherDefinitions.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// One entry per WeatherState, indexed by std::to_underlying(state).
// Order MUST match the WeatherState enum.
const std::array<WeatherDefinition, 17> kWeatherTable = {{
    // Baseline.
    // Clear
    WeatherDefinition{},

    // Precipitation.
    // LightRain
    WeatherDefinition{
        .ambientTintMultiplier = {0.80f, 0.82f, 0.90f},
        .particleType = WeatherParticleType::Rain,
        .baseSpawnRate = 200.0f,
        .maxWeatherParticles = 10000,
        .windIntensity = 0.4f,
    },
    // HeavyRain
    WeatherDefinition{
        .ambientTintMultiplier = {0.65f, 0.68f, 0.80f},
        .skyColorOverride = {0.42f, 0.45f, 0.55f},
        .particleType = WeatherParticleType::Rain,
        .baseSpawnRate = 600.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 0.8f,
    },
    // Thunderstorm: less frequent flashes (was 5s -> 8s) so the lightning
    // overlay reads as a punctuating event rather than a constant strobe; the
    // flash alpha itself is tuned in SkyRenderer (see RenderSky).
    WeatherDefinition{
        .ambientTintMultiplier = {0.50f, 0.52f, 0.65f},
        .skyColorOverride = {0.30f, 0.32f, 0.42f},
        .particleType = WeatherParticleType::Rain,
        .baseSpawnRate = 1000.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .lightningIntervalSeconds = 8.0f,
        .windIntensity = 1.0f,
    },
    // Blizzard: heaviest snow with gusty wind. SpawnWeatherParticle randomizes
    // per-particle horizontal wind when windIntensity >= 0.7 so the gusts catch
    // some flakes harder than others, and stretches per-flake lifetime so the
    // population feels persistent without spawning a huge wave each tick.
    // Secondary Fog reads as a mix of small mist and large fog puffs (50/50
    // tier in the post-spawn block).
    WeatherDefinition{
        .ambientTintMultiplier = {0.80f, 0.83f, 0.95f},
        .skyColorOverride = {0.78f, 0.80f, 0.85f},
        .particleType = WeatherParticleType::Snow,
        .baseSpawnRate = 550.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 1.0f,
        .secondaryParticleType = WeatherParticleType::Fog,
        .secondaryBaseSpawnRate = 45.0f,
        .secondaryMaxWeatherParticles = 10000,
        .fogAlphaMultiplier = 0.6f,  // softened so stacked fog stays translucent (was 0.85)
    },

    // Atmosphere.
    // Fog (merged): primary stream tiered post-spawn into 80% small mist-style
    // puffs and 20% large fog blobs. Replaces the former separate Fog and Mist
    // weathers. Soft per-puff alpha keeps the world legible.
    WeatherDefinition{
        .ambientTintMultiplier = {0.80f, 0.83f, 0.87f},
        .particleType = WeatherParticleType::Fog,
        .baseSpawnRate = 180.0f,
        .maxWeatherParticles = 5000,
        .particleSizeScale = 1.0f,
        .windIntensity = 0.2f,
        .fogAlphaMultiplier = 0.65f,
    },
    // HeatHaze (hazeAmplitude is reserved; tint applies today). Tint channels
    // held at/below the white point so the warm cast doesn't over-brighten
    // sprites past full albedo at midday (ambient is an unclamped multiply).
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.99f, 0.95f},
        .windIntensity = 0.1f,
        .hazeAmplitude = 2.0f,
    },
    // Sandstorm
    WeatherDefinition{
        .ambientTintMultiplier = {0.75f, 0.65f, 0.50f},
        .skyColorOverride = {0.70f, 0.55f, 0.40f},
        .particleType = WeatherParticleType::Sand,
        .baseSpawnRate = 400.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 1.0f,
    },

    // Floral / seasonal.
    // FallingLeaves: floats like PollenStorm. World-anchored (no overlay-feel
    // camera drag); particles spawn at the left and right edges of the buffer
    // and drift inward (see the side-edge spawn case in SpawnWeatherParticle).
    // When the player moves, leaves near the camera get a rapid radial push
    // away (particles.js-style cursor avoidance, gated on player motion).
    // Rate matches PollenStorm so both feel like the same family of sparse
    // ambient flurries.
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.95f, 0.85f},
        .particleType = WeatherParticleType::Leaf,
        .baseSpawnRate = 60.0f,
        .maxWeatherParticles = 2000,
        .windIntensity = 0.25f,
    },
    // CherryBlossoms: dense flurry with strong pink wash. Per-spawn tier system
    // in the Blossom behavior gives mixed sizes/hues so density doesn't read as
    // uniform; a deeper alpha pulse in Update makes each petal visibly breathe.
    // Pinker tint (R held at the white point, G/B suppressed) reads as a
    // pronounced pink overlay without over-brightening reds past full albedo.
    // A strongly pink-tinted Fog secondary adds a sakura wash behind the petals
    // (tint applied in SpawnWeatherParticle).
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.80f, 0.92f},
        .particleType = WeatherParticleType::Blossom,
        .baseSpawnRate = 230.0f,
        .maxWeatherParticles = 10000,
        .windIntensity = 0.35f,
        .secondaryParticleType = WeatherParticleType::Fog,
        .secondaryBaseSpawnRate = 15.0f,
        .secondaryMaxWeatherParticles = 10000,
        .fogAlphaMultiplier = 0.5f,
    },
    // PollenStorm: sparse floaty flurry matching FallingLeaves. Particles
    // enter from the left/right edges (see SpawnWeatherParticle edge-bias
    // block) and rapidly scatter away from the player when the player runs
    // through them (Pollen::Update avoidance).
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.98f, 0.85f},
        .particleType = WeatherParticleType::Pollen,
        .baseSpawnRate = 60.0f,
        .maxWeatherParticles = 2000,
        .windIntensity = 0.5f,
    },

    // Special / night.
    // AuroraNight: sparse Wisp-class motes drift alongside the sky ribbons.
    // Wisp's slow spiral motion + diverse color variety reads more as aurora
    // dust than fireflies would. The post-spawn block in SpawnWeatherParticle
    // restricts the palette to cool aurora hues only (emerald/cyan/violet/
    // magenta) so the warm wisp golds/ambers don't slip in. Rate is kept low
    // so the ribbons remain the hero element.
    WeatherDefinition{
        .ambientTintMultiplier = {0.85f, 0.92f, 1.08f},
        .particleType = WeatherParticleType::Wisp,
        .baseSpawnRate = 90.0f,
        .maxWeatherParticles = 600,
        .particleSizeScale = 0.85f,
        .starVisibilityOverride = 0.85f,
        .showAurora = true,
    },
    // MeteorShower: rate bump to 12 collapses the spawn interval (~4s base / 12
    // ~0.3s). Per-star size/brightness boost lives in
    // SkyRenderer::SpawnShootingStar so the weather reads as an event.
    WeatherDefinition{
        .ambientTintMultiplier = {0.95f, 0.95f, 1.00f},
        .starVisibilityOverride = 1.0f,
        .meteorRateMultiplier = 12.0f,
    },
    // FireflySwarm: denser and brighter than ambient zone fireflies. The
    // post-spawn block in SpawnWeatherParticle rewrites the per-particle color
    // based on m_NightFactor so daytime swarms favor cyan/green/yellow tones
    // and nighttime swarms favor red/green/yellow tones.
    WeatherDefinition{
        .ambientTintMultiplier = {0.90f, 1.00f, 0.85f},
        .particleType = WeatherParticleType::Firefly,
        .baseSpawnRate = 600.0f,
        .maxWeatherParticles = 10000,
        .particleSizeScale = 1.0f,
    },
    // AshFall
    WeatherDefinition{
        .ambientTintMultiplier = {0.70f, 0.65f, 0.60f},
        .skyColorOverride = {0.55f, 0.50f, 0.48f},
        .particleType = WeatherParticleType::Ash,
        .baseSpawnRate = 150.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.2f,
        .windIntensity = 0.3f,
    },
    // EmberStorm
    WeatherDefinition{
        .ambientTintMultiplier = {0.85f, 0.60f, 0.45f},
        .skyColorOverride = {0.55f, 0.30f, 0.20f},
        .particleType = WeatherParticleType::Ember,
        .baseSpawnRate = 250.0f,
        .maxWeatherParticles = 10000,
        .starVisibilityOverride = 0.4f,
        .windIntensity = 0.7f,
    },

    // Atmospheric / prismatic.
    // GodRays: shafts of light cut through soft haze. Each Sunshine particle
    // picks a rainbow palette tier from its phase (see the WEATHER_ZONE_INDEX
    // branch in Sunshine::Update) so the sky reads as a spread of red /
    // orange / yellow / green / cyan / blue / violet beams. The Fog secondary
    // supplies the atmospheric mist that justifies the prismatic look; the
    // existing baseAlpha curve in Sunshine::Update keeps the beams visible
    // at night, just dimmer.
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.98f, 1.00f},
        .particleType = WeatherParticleType::Sunshine,
        .baseSpawnRate = 6.0f,
        .maxWeatherParticles = 200,
        .windIntensity = 0.0f,
        .secondaryParticleType = WeatherParticleType::Fog,
        .secondaryBaseSpawnRate = 25.0f,
        .secondaryMaxWeatherParticles = 1500,
        .fogAlphaMultiplier = 0.40f,
    },
}};

float Smoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

const WeatherDefinition& GetWeatherDefinition(WeatherState state)
{
    auto idx = static_cast<size_t>(std::to_underlying(state));
    if (idx >= kWeatherTable.size())
        return kWeatherTable[0];
    return kWeatherTable[idx];
}

float ComputeLightIntensity(LightSchedule schedule, float hourOfDay)
{
    if (schedule == LightSchedule::AlwaysOn)
        return 1.0f;

    // Normalize hour to [0, 24).
    float h = std::fmod(hourOfDay, 24.0f);
    if (h < 0.0f)
        h += 24.0f;

    // Schedule windows. Both wrap midnight; we treat the daytime gap as zero,
    // and ramp at the boundaries.
    float rampOnStart = (schedule == LightSchedule::DuskToDawn) ? 18.0f : 20.0f;
    float rampOnEnd = (schedule == LightSchedule::DuskToDawn) ? 20.0f : 22.0f;
    float rampOffStart = 4.0f;
    float rampOffEnd = (schedule == LightSchedule::DuskToDawn) ? 7.0f : 6.0f;

    if (h >= rampOnStart && h < rampOnEnd)
        return Smoothstep((h - rampOnStart) / (rampOnEnd - rampOnStart));
    if (h >= rampOffStart && h < rampOffEnd)
        return 1.0f - Smoothstep((h - rampOffStart) / (rampOffEnd - rampOffStart));
    if (h >= rampOnEnd || h < rampOffStart)
        return 1.0f;
    return 0.0f;
}
