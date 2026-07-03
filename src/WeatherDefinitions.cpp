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
    // Blizzard: heaviest snow with gusty wind. SpawnWeatherParticle ramps the snow's
    // wind boost with gusted strength (smoothstep 0.3-0.9, sign-coherent so flakes
    // share one drift) up to full at windIntensity 1.0, and stretches per-flake
    // lifetime for a persistent population. Secondary Fog is a 50/50 mist/blob mix.
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
        .secondaryBaseSpawnRate = 27.0f,  // thinned blizzard mist (was 45)
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
        .baseSpawnRate = 110.0f,      // thinned so fog reads as haze, not a wall (was 180)
        .maxWeatherParticles = 2500,  // lower ceiling holds the thin-out when zoomed out (was 5000)
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
        // 0.5 = the calm anchor reproducing the pre-wind drift speed; raise for gustier, lower for
        // stiller.
        .windIntensity = 0.5f,
    },

    // Floral / seasonal.
    // FallingLeaves: sparse world-anchored flurry (rate matches PollenStorm). Particles
    // spawn at the left/right buffer edges and drift inward (side-edge case in
    // SpawnWeatherParticle); when the player moves, nearby leaves get a rapid radial
    // push away (particles.js-style cursor avoidance, gated on player motion).
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.95f, 0.85f},
        .particleType = WeatherParticleType::Leaf,
        .baseSpawnRate = 60.0f,
        .maxWeatherParticles = 2000,
        // 0.5 = the calm anchor reproducing the pre-wind drift speed; raise for gustier, lower for
        // stiller.
        .windIntensity = 0.5f,
    },
    // CherryBlossoms: dense pink flurry. The Blossom behavior's per-spawn tier system
    // mixes sizes/hues so density isn't uniform, and an alpha pulse in Update makes
    // petals breathe. Pink tint (R at white point, G/B suppressed); a pink-tinted Fog
    // secondary adds a sakura wash behind the petals (tint in SpawnWeatherParticle).
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.80f, 0.92f},
        .particleType = WeatherParticleType::Blossom,
        .baseSpawnRate = 230.0f,
        .maxWeatherParticles = 10000,
        .windIntensity = 0.35f,
        .secondaryParticleType = WeatherParticleType::Fog,
        .secondaryBaseSpawnRate = 9.0f,  // lighter sakura wash (was 15)
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
    // AuroraNight: sparse Wisp-class motes drift alongside the sky ribbons; Wisp's slow
    // spiral + color variety reads as aurora dust, not fireflies. The post-spawn block
    // in SpawnWeatherParticle restricts the palette to cool aurora hues (emerald/cyan/
    // violet/magenta), and the rate is kept low so the ribbons stay the hero element.
    WeatherDefinition{
        .ambientTintMultiplier = {0.85f, 0.92f, 1.08f},
        .particleType = WeatherParticleType::Wisp,
        .baseSpawnRate = 35.0f,
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
    // GodRays: light shafts through soft haze. Each Sunshine particle picks a rainbow
    // palette tier from its phase (WEATHER_ZONE_INDEX branch in Sunshine::Update) for a
    // red-to-violet spread of beams; the Fog secondary supplies the mist that justifies
    // the prismatic look. Sunshine::Update's baseAlpha curve keeps beams visible at night.
    WeatherDefinition{
        .ambientTintMultiplier = {1.00f, 0.98f, 1.00f},
        .particleType = WeatherParticleType::Sunshine,
        .baseSpawnRate = 6.0f,
        .maxWeatherParticles = 200,
        .windIntensity = 0.0f,
        .secondaryParticleType = WeatherParticleType::Fog,
        .secondaryBaseSpawnRate = 15.0f,  // thinner mist behind the beams (was 25)
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
