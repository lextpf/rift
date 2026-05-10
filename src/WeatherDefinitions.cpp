#include "WeatherDefinitions.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// One entry per WeatherState, indexed by std::to_underlying(state).
// Order MUST match the WeatherState enum.
const std::array<WeatherDefinition, 19> kWeatherTable = {{
    // --- Baseline ----------------------------------------------------------
    // Clear
    WeatherDefinition{},
    // Overcast
    WeatherDefinition{
        .ambientTintMultiplier = {0.70f, 0.70f, 0.75f},
        .skyColorOverride = {0.50f, 0.50f, 0.55f},
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
    },

    // --- Precipitation -----------------------------------------------------
    // LightRain
    WeatherDefinition{
        .ambientTintMultiplier = {0.80f, 0.82f, 0.90f},
        .particleType = WeatherParticleType::Rain,
        .baseSpawnRate = 40.0f,
        .maxWeatherParticles = 200,
        .windIntensity = 0.4f,
    },
    // HeavyRain
    WeatherDefinition{
        .ambientTintMultiplier = {0.65f, 0.68f, 0.80f},
        .skyColorOverride = {0.42f, 0.45f, 0.55f},
        .particleType = WeatherParticleType::Rain,
        .baseSpawnRate = 120.0f,
        .maxWeatherParticles = 500,
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
        .baseSpawnRate = 200.0f,
        .maxWeatherParticles = 800,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .lightningIntervalSeconds = 8.0f,
        .windIntensity = 1.0f,
    },
    // Snow
    WeatherDefinition{
        .ambientTintMultiplier = {0.90f, 0.92f, 0.98f},
        .particleType = WeatherParticleType::Snow,
        .baseSpawnRate = 25.0f,
        .maxWeatherParticles = 300,
        .windIntensity = 0.3f,
    },
    // Blizzard: heavier and faster than regular Snow. The Snow particle's
    // base velocity is multiplied per-particle in SpawnWeatherParticles when
    // windIntensity >= 0.7 so the snow visibly hammers down rather than just
    // increasing in count.
    WeatherDefinition{
        .ambientTintMultiplier = {0.80f, 0.83f, 0.95f},
        .skyColorOverride = {0.78f, 0.80f, 0.85f},
        .particleType = WeatherParticleType::Snow,
        .baseSpawnRate = 150.0f,
        .maxWeatherParticles = 700,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 1.0f,
    },

    // --- Atmosphere --------------------------------------------------------
    // Fog: dense, large slow-drifting blobs that read as a thick fog wall.
    WeatherDefinition{
        .ambientTintMultiplier = {0.75f, 0.77f, 0.80f},
        .particleType = WeatherParticleType::Fog,
        .baseSpawnRate = 8.0f,
        .maxWeatherParticles = 100,
        .particleSizeScale = 1.8f,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 0.2f,
    },
    // Mist: lighter cousin - half the density and smaller blobs.
    WeatherDefinition{
        .ambientTintMultiplier = {0.88f, 0.90f, 0.93f},
        .particleType = WeatherParticleType::Fog,
        .baseSpawnRate = 4.0f,
        .maxWeatherParticles = 50,
        .particleSizeScale = 1.2f,
        .windIntensity = 0.2f,
    },
    // HeatHaze (hazeAmplitude is reserved; tint applies today)
    WeatherDefinition{
        .ambientTintMultiplier = {1.05f, 1.02f, 0.95f},
        .windIntensity = 0.1f,
        .hazeAmplitude = 2.0f,
    },
    // Sandstorm
    WeatherDefinition{
        .ambientTintMultiplier = {0.75f, 0.65f, 0.50f},
        .skyColorOverride = {0.70f, 0.55f, 0.40f},
        .particleType = WeatherParticleType::Sand,
        .baseSpawnRate = 80.0f,
        .maxWeatherParticles = 400,
        .starVisibilityOverride = 0.0f,
        .showCelestialBodies = false,
        .windIntensity = 1.0f,
    },

    // --- Floral / seasonal -------------------------------------------------
    // FallingLeaves
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.95f, 0.85f},
        .particleType = WeatherParticleType::Leaf,
        .baseSpawnRate = 8.0f,
        .maxWeatherParticles = 60,
        .windIntensity = 0.4f,
    },
    // CherryBlossoms: dense flurry. Per-spawn tier system in the Blossom
    // behavior gives mixed sizes/hues so density doesn't read as uniform.
    // Pinker tint (R bumped past 1.0, G/B suppressed) reads as a slight pink
    // wash over the world sprites.
    WeatherDefinition{
        .ambientTintMultiplier = {1.10f, 0.85f, 0.95f},
        .particleType = WeatherParticleType::Blossom,
        .baseSpawnRate = 60.0f,
        .maxWeatherParticles = 450,
        .windIntensity = 0.35f,
    },
    // PollenStorm
    WeatherDefinition{
        .ambientTintMultiplier = {1.05f, 1.00f, 0.85f},
        .particleType = WeatherParticleType::Pollen,
        .baseSpawnRate = 30.0f,
        .maxWeatherParticles = 200,
        .windIntensity = 0.5f,
    },

    // --- Special / night ---------------------------------------------------
    // AuroraNight
    WeatherDefinition{
        .ambientTintMultiplier = {0.85f, 0.90f, 1.05f},
        .starVisibilityOverride = 0.85f,
        .showAurora = true,
    },
    // MeteorShower: more frequent, larger, brighter streaks. Rate bump to 12
    // collapses the spawn interval (~4s base / 12 = ~0.3s); the per-star size
    // and brightness boost lives in SkyRenderer::SpawnShootingStar so this
    // weather actually reads as an event instead of "did I see a star?".
    WeatherDefinition{
        .ambientTintMultiplier = {0.95f, 0.95f, 1.00f},
        .starVisibilityOverride = 1.0f,
        .meteorRateMultiplier = 12.0f,
    },
    // FireflySwarm: denser and brighter than ambient zone fireflies. Per-particle
    // size matches the ambient zone default (scale 1.0) - the swarm reads as
    // "many fireflies" via baseSpawnRate / maxWeatherParticles, not chunkier sprites.
    WeatherDefinition{
        .ambientTintMultiplier = {0.90f, 1.00f, 0.85f},
        .particleType = WeatherParticleType::Firefly,
        .baseSpawnRate = 70.0f,
        .maxWeatherParticles = 450,
        .particleSizeScale = 1.0f,
    },
    // AshFall
    WeatherDefinition{
        .ambientTintMultiplier = {0.70f, 0.65f, 0.60f},
        .skyColorOverride = {0.55f, 0.50f, 0.48f},
        .particleType = WeatherParticleType::Ash,
        .baseSpawnRate = 30.0f,
        .maxWeatherParticles = 200,
        .starVisibilityOverride = 0.2f,
        .windIntensity = 0.3f,
    },
    // EmberStorm
    WeatherDefinition{
        .ambientTintMultiplier = {0.85f, 0.60f, 0.45f},
        .skyColorOverride = {0.55f, 0.30f, 0.20f},
        .particleType = WeatherParticleType::Ember,
        .baseSpawnRate = 50.0f,
        .maxWeatherParticles = 250,
        .starVisibilityOverride = 0.4f,
        .windIntensity = 0.7f,
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
