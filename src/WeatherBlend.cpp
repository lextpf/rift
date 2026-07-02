#include "WeatherBlend.hpp"

#include "AmbienceConfig.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

float BlendSmoothstep(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool WeatherSpawnsFogType(const WeatherDefinition& def)
{
    return def.particleType == WeatherParticleType::Fog ||
           def.secondaryParticleType == WeatherParticleType::Fog;
}

int WeatherCapForType(const WeatherDefinition& def, WeatherParticleType type)
{
    if (type == WeatherParticleType::None)
    {
        return 0;
    }
    if (def.particleType == type)
    {
        return def.maxWeatherParticles;
    }
    if (def.secondaryParticleType == type)
    {
        return def.secondaryMaxWeatherParticles;
    }
    return 0;
}

namespace
{
float Mix(float a, float b, float t)
{
    return a + (b - a) * t;
}

glm::vec3 Mix(const glm::vec3& a, const glm::vec3& b, float t)
{
    return a + (b - a) * t;
}

bool SpawnsType(const WeatherDefinition& def, WeatherParticleType type)
{
    return type != WeatherParticleType::None &&
           (def.particleType == type || def.secondaryParticleType == type);
}

// Blended cap for the destination slot. When both endpoints spawn the type, hold
// min(a-cap, b-cap) with 0-as-infinite semantics (no mixing); otherwise mix-and-round.
// For the min case the a-side cap must come from otherEndpointCap (WeatherCapForType),
// not capA, which can be the wrong slot's cap for a shared type (Blizzard: Snow/Fog).
int BlendCap(int capA, int capB, float t, bool bothSpawnType, int otherEndpointCap)
{
    if (bothSpawnType)
    {
        // min with 0-as-infinite semantics.
        if (otherEndpointCap == 0)
        {
            return capB;
        }
        if (capB == 0)
        {
            return otherEndpointCap;
        }
        return std::min(otherEndpointCap, capB);
    }
    return static_cast<int>(
        std::lround(Mix(static_cast<float>(capA), static_cast<float>(capB), t)));
}

// Rate for a destination spawn slot: mix when the outgoing endpoint spawns
// the same type in the same slot, ramp from zero otherwise.
float BlendSlotRate(
    WeatherParticleType typeA, float rateA, WeatherParticleType typeB, float rateB, float t)
{
    if (typeA == typeB)
    {
        return Mix(rateA, rateB, t);
    }
    return rateB * t;
}

float BlendLightningInterval(float intervalA, float intervalB, float t, float freqEps)
{
    float fa = (intervalA > 0.0f) ? 1.0f / intervalA : 0.0f;
    float fb = (intervalB > 0.0f) ? 1.0f / intervalB : 0.0f;
    float f = Mix(fa, fb, t);
    if (f < freqEps)
    {
        return 0.0f;
    }
    return 1.0f / f;
}
}  // namespace

WeatherDefinition BlendWeatherDefinitions(const WeatherDefinition& a,
                                          const WeatherDefinition& b,
                                          float t)
{
    if (t <= 0.0f)
    {
        return a;
    }
    if (t >= 1.0f)
    {
        return b;
    }

    WeatherDefinition out = b;  // destination copies: sentinels, bools, type enums

    out.ambientTintMultiplier = Mix(a.ambientTintMultiplier, b.ambientTintMultiplier, t);
    out.particleSizeScale = Mix(a.particleSizeScale, b.particleSizeScale, t);
    out.meteorRateMultiplier = Mix(a.meteorRateMultiplier, b.meteorRateMultiplier, t);
    out.windIntensity = Mix(a.windIntensity, b.windIntensity, t);
    out.fogAlphaMultiplier = Mix(a.fogAlphaMultiplier, b.fogAlphaMultiplier, t);
    out.hazeAmplitude = Mix(a.hazeAmplitude, b.hazeAmplitude, t);
    out.lightningIntervalSeconds = BlendLightningInterval(a.lightningIntervalSeconds,
                                                          b.lightningIntervalSeconds,
                                                          t,
                                                          ambience::WEATHER_LIGHTNING_FREQ_EPS);

    // Primary spawn slot (type is b's; rate ramps or mixes; cap floors when shared).
    out.baseSpawnRate =
        BlendSlotRate(a.particleType, a.baseSpawnRate, b.particleType, b.baseSpawnRate, t);
    out.maxWeatherParticles = BlendCap(a.maxWeatherParticles,
                                       b.maxWeatherParticles,
                                       t,
                                       SpawnsType(a, b.particleType),
                                       WeatherCapForType(a, b.particleType));

    // Secondary spawn slot, same rules.
    out.secondaryBaseSpawnRate = BlendSlotRate(a.secondaryParticleType,
                                               a.secondaryBaseSpawnRate,
                                               b.secondaryParticleType,
                                               b.secondaryBaseSpawnRate,
                                               t);
    out.secondaryMaxWeatherParticles = BlendCap(a.secondaryMaxWeatherParticles,
                                                b.secondaryMaxWeatherParticles,
                                                t,
                                                SpawnsType(a, b.secondaryParticleType),
                                                WeatherCapForType(a, b.secondaryParticleType));

    return out;
}

uint64_t SplitMix64(uint64_t x)
{
    // Standard SplitMix64 finalizer (Steele/Lea/Flood). Deterministic across
    // platforms; used for gust phases now and forecast rolls in phase 3.
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

glm::vec3 GustPhases(uint64_t seed)
{
    // Three independent phases in [0, 2*pi) from consecutive mixer outputs.
    const float twoPi = glm::two_pi<float>();
    auto phase = [twoPi](uint64_t bits)
    { return static_cast<float>(bits % 100000ULL) / 100000.0f * twoPi; };
    return {phase(SplitMix64(seed)), phase(SplitMix64(seed + 1)), phase(SplitMix64(seed + 2))};
}

float GustWindStrength(float base, double clockSeconds, const glm::vec3& phases)
{
    const float t = static_cast<float>(clockSeconds);
    const float twoPi = glm::two_pi<float>();
    float envelope =
        0.6f * std::sin(twoPi * t / ambience::WEATHER_GUST_PERIOD_PRIMARY_S + phases.x) +
        0.4f * std::sin(twoPi * t / ambience::WEATHER_GUST_PERIOD_SECONDARY_S + phases.y);
    return std::max(0.0f, base * (1.0f + ambience::WEATHER_GUST_AMP * envelope));
}

glm::vec2 GustWindDirection(double clockSeconds, const glm::vec3& phases)
{
    const float t = static_cast<float>(clockSeconds);
    const float twoPi = glm::two_pi<float>();
    const float wander = glm::radians(ambience::WEATHER_WIND_WANDER_DEG) *
                         std::sin(twoPi * t / ambience::WEATHER_WIND_WANDER_PERIOD_S + phases.z);
    const glm::vec2 base = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);
    const float c = std::cos(wander);
    const float s = std::sin(wander);
    return glm::normalize(glm::vec2(base.x * c - base.y * s, base.x * s + base.y * c));
}

namespace
{
// Weighted natural front pool. Clear dominates; harsher weathers are rare.
// GodRays/EmberStorm/AshFall/HeatHaze stay console-only (maintainer decision
// 2026-07-02); the three night states arrive as events, not fronts.
struct FrontWeight
{
    WeatherState state;
    int weight;
};
constexpr std::array<FrontWeight, 10> kFrontPool = {{
    {WeatherState::Clear, 40},
    {WeatherState::LightRain, 10},
    {WeatherState::Fog, 8},
    {WeatherState::FallingLeaves, 8},
    {WeatherState::CherryBlossoms, 6},
    {WeatherState::PollenStorm, 6},
    {WeatherState::HeavyRain, 5},
    {WeatherState::Sandstorm, 4},
    {WeatherState::Blizzard, 4},
    {WeatherState::Thunderstorm, 3},
}};
constexpr int kFrontPoolTotalWeight = 94;  // keep in sync with the table

// Boundary of front k: nominal k*L, jittered by {-1, 0, +1} from the hash so
// front lengths vary 2..6 days without gaps or overlaps (|jitter| <= 1 < L/2).
int64_t FrontBoundary(uint64_t seed, int64_t k)
{
    const uint64_t h = SplitMix64(seed ^ 0x9E37ULL ^ (static_cast<uint64_t>(k) * 0x9E3779B9ULL));
    const int jitter = static_cast<int>(h % 3ULL) - 1;
    return k * ambience::WEATHER_FRONT_LENGTH_DAYS + jitter;
}

// 0..1 float from a hash, for probability rolls.
float HashUnitFloat(uint64_t h)
{
    return static_cast<float>(h % 100000ULL) / 100000.0f;
}
}  // namespace

int64_t ForecastFrontIndex(uint64_t seed, int64_t dayIndex)
{
    // Nominal guess, then local search: boundaries move at most 1 day from
    // k*L, so the containing front is within one step of the guess.
    int64_t k = dayIndex / ambience::WEATHER_FRONT_LENGTH_DAYS;
    // Handle negative days (C++ division truncates toward zero).
    if (dayIndex < 0 && dayIndex % ambience::WEATHER_FRONT_LENGTH_DAYS != 0)
    {
        --k;
    }
    while (FrontBoundary(seed, k) > dayIndex)
    {
        --k;
    }
    while (FrontBoundary(seed, k + 1) <= dayIndex)
    {
        ++k;
    }
    return k;
}

ForecastEntry ForecastForDay(uint64_t seed, int64_t dayIndex)
{
    ForecastEntry entry;

    const int64_t front = ForecastFrontIndex(seed, dayIndex);
    if (front == ForecastFrontIndex(seed, 0))
    {
        // Boot front: the world starts Clear, exactly like today.
        entry.front = WeatherState::Clear;
    }
    else
    {
        const uint64_t roll =
            SplitMix64(seed ^ 0xF407ULL ^ (static_cast<uint64_t>(front) * 0x9E3779B9ULL));
        int pick = static_cast<int>(roll % static_cast<uint64_t>(kFrontPoolTotalWeight));
        for (const FrontWeight& fw : kFrontPool)
        {
            pick -= fw.weight;
            if (pick < 0)
            {
                entry.front = fw.state;
                break;
            }
        }
    }

    // Night event roll: independent of the front, per-day, moon-weighted.
    const uint64_t eventRoll =
        SplitMix64(seed ^ 0xE7E47ULL ^ (static_cast<uint64_t>(dayIndex) * 0x85EBCA6BULL));
    if (HashUnitFloat(eventRoll) < ambience::WEATHER_EVENT_NIGHT_CHANCE)
    {
        entry.hasNightEvent = true;
        // Moon phase 0 = new, 4 = full (TimeManager: dayCount % 8). Aurora
        // and meteors love bright full-ish nights in this world's fiction
        // (they are sky spectacles); fireflies love dark new-moon nights.
        const int phase = static_cast<int>(((dayIndex % 8) + 8) % 8);
        const int distFromFull = std::abs(phase - 4);    // 0 (full) .. 4 (new)
        const int auroraW = 2 + (4 - distFromFull) * 2;  // 2..10, peak at full
        const int meteorW = 1 + (4 - distFromFull) * 2;  // 1..9, peak at full
        const int fireflyW = 2 + distFromFull * 2;       // 2..10, peak at new
        const uint64_t which = SplitMix64(eventRoll);
        int pick = static_cast<int>(which % static_cast<uint64_t>(auroraW + meteorW + fireflyW));
        if (pick < auroraW)
        {
            entry.nightEvent = WeatherState::AuroraNight;
        }
        else if (pick < auroraW + meteorW)
        {
            entry.nightEvent = WeatherState::MeteorShower;
        }
        else
        {
            entry.nightEvent = WeatherState::FireflySwarm;
        }
    }

    return entry;
}
