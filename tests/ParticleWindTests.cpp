#include <gtest/gtest.h>

#include "../src/ParticleSystem.hpp"
#include "../src/WeatherDefinitions.hpp"

#include <glm/glm.hpp>

/// @file ParticleWindTests.cpp
/// @brief Phase-2 particle coverage: cap accounting, wind plumbing, dual-stream
/// transition spawning. All headless (no GL context).
namespace
{
int CountType(const ParticleSystem& ps, ParticleType type)
{
    int n = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == type && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            ++n;
        }
    }
    return n;
}
}  // namespace

// The per-type cap still binds exactly after the count hoist: Fog weather is
// cap-bound at 2500 at this viewport (same setup as FogOpacityTests) and must
// neither exceed the cap nor stall below it.
TEST(ParticleWind, HoistedCapStillBindsExactly)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Fog), 1.0f);

    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(CountType(ps, ParticleType::Fog),
                  GetWeatherDefinition(WeatherState::Fog).maxWeatherParticles);
    }
    // Cap-bound at steady state: within 5% below the cap (population cycles as
    // puffs die and respawn), never above.
    EXPECT_GE(CountType(ps, ParticleType::Fog),
              static_cast<int>(GetWeatherDefinition(WeatherState::Fog).maxWeatherParticles * 0.95));
}

namespace
{
/// Spawn a burst of weather snow at the given wind and return the velocity
/// ranges of the freshly spawned flakes.
struct VelocityRange
{
    float minX{1e9f}, maxX{-1e9f}, minY{1e9f}, maxY{-1e9f};
};

VelocityRange SpawnSnowBurst(float windStrength, glm::vec2 windDir)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind(windDir, windStrength);
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Blizzard), 1.0f);
    ps.Update(0.5f, {0.0f, 0.0f}, {640.0f, 480.0f});

    VelocityRange r;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type != ParticleType::Snow || p.zoneIndex != ParticleSystem::WEATHER_ZONE_INDEX)
        {
            continue;
        }
        r.minX = std::min(r.minX, p.velocity.x);
        r.maxX = std::max(r.maxX, p.velocity.x);
        r.minY = std::min(r.minY, p.velocity.y);
        r.maxY = std::max(r.maxY, p.velocity.y);
    }
    return r;
}
}  // namespace

// At full strength the boost reproduces today's Blizzard exactly: the Snow
// type's base spawn velocity (x in +/-6, y in 12..22) scaled by 7.0 / 3.5,
// with x sign-coherent along the wind instead of random.
TEST(ParticleWind, SnowBoostMatchesBlizzardAtFullStrength)
{
    VelocityRange r = SpawnSnowBurst(1.0f, {-1.0f, 0.0f});
    EXPECT_LT(r.maxX, 0.0f) << "wind blows -X: all flakes must drift left";
    EXPECT_GE(r.minX, -6.0f * 7.0f - 1e-3f);
    EXPECT_GE(r.minY, 12.0f * 3.5f - 1e-3f);
    EXPECT_LE(r.maxY, 22.0f * 3.5f + 1e-3f);
}

// The old >= 0.7 cliff is gone: strengths just below/above it produce nearly
// identical velocity ranges (continuous smoothstep, not a 7x step).
TEST(ParticleWind, SnowBoostHasNoCliff)
{
    VelocityRange below = SpawnSnowBurst(0.69f, {-1.0f, 0.0f});
    VelocityRange above = SpawnSnowBurst(0.71f, {-1.0f, 0.0f});
    // At the cliff the old code multiplied by 7x across this boundary. The
    // smoothstep's slope here allows only a few percent difference.
    EXPECT_LT(std::abs(above.minX - below.minX), std::abs(below.minX) * 0.15f);
    EXPECT_LT(std::abs(above.maxY - below.maxY), below.maxY * 0.15f);
}

// Calm wind leaves the base spawn velocities untouched (multiplier 1.0 below
// the smoothstep's 0.3 lower edge).
TEST(ParticleWind, SnowCalmWindIsUnboosted)
{
    VelocityRange r = SpawnSnowBurst(0.2f, {-1.0f, 0.0f});
    EXPECT_GE(r.minX, -6.0f - 1e-3f);
    EXPECT_LE(r.maxX, 6.0f + 1e-3f);
    EXPECT_LE(r.maxY, 22.0f + 1e-3f);
}

// Wind direction sign drives the horizontal drift: +X wind -> all flakes +X.
TEST(ParticleWind, SnowFollowsWindSign)
{
    VelocityRange r = SpawnSnowBurst(1.0f, {1.0f, 0.0f});
    EXPECT_GT(r.minX, 0.0f);
}

namespace
{
/// Displacement magnitude of a single zone-spawned particle of the given type
/// after stepping `seconds` at the given wind strength.
float DriftDistanceX(ParticleType type, float windStrength, float seconds)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, windStrength);
    // Exactly ONE particle: the pool mutates order on removal, so tracking
    // "the first particle of the type" is only identity-stable with a single
    // live particle in the zone.
    ps.SetMaxParticlesPerZone(1);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    std::vector<ParticleZone> zones;
    zones.emplace_back(cameraPos, viewSize, type);
    ps.SetZones(&zones);

    // Zone spawn rates are a few particles/sec: step until the single
    // particle appears (bounded) so the helper never returns vacuously flaky.
    float startX = 0.0f;
    bool found = false;
    for (int i = 0; i < 100 && !found; ++i)  // up to 5 s of spawn attempts
    {
        ps.Update(0.05f, cameraPos, viewSize);
        for (const auto& p : ps.GetParticles())
        {
            if (p.type == type)
            {
                startX = p.position.x;
                found = true;
                break;
            }
        }
    }
    if (!found)
    {
        return -1.0f;  // caller asserts >= 0 as the vacuity guard
    }
    for (int i = 0; i < static_cast<int>(seconds / 0.05f); ++i)
    {
        ps.Update(0.05f, cameraPos, viewSize);
    }
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == type)
        {
            return std::abs(p.position.x - startX);
        }
    }
    return -1.0f;
}
}  // namespace

// Leaf and pollen drift scale with wind strength; the default 0.5 strength
// reproduces today's numbers (leaf 18 px/s, pollen 8 px/s along the wind).
TEST(ParticleWind, LeafAndPollenDriftScaleWithStrength)
{
    const float leafCalm = DriftDistanceX(ParticleType::DriftingLeaf, 0.5f, 1.0f);
    const float leafGust = DriftDistanceX(ParticleType::DriftingLeaf, 1.0f, 1.0f);
    ASSERT_GE(leafCalm, 0.0f) << "test vacuous: no leaf spawned";
    ASSERT_GE(leafGust, 0.0f);
    // 18 px/s baseline (with a little slack for the sine sway on Y only - X is pure wind).
    EXPECT_NEAR(leafCalm, 18.0f, 2.5f);
    EXPECT_GT(leafGust, leafCalm * 1.25f) << "gust must visibly push leaves";

    const float pollenCalm = DriftDistanceX(ParticleType::Pollen, 0.5f, 1.0f);
    const float pollenGust = DriftDistanceX(ParticleType::Pollen, 1.0f, 1.0f);
    ASSERT_GE(pollenCalm, 0.0f) << "test vacuous: no pollen spawned";
    EXPECT_NEAR(pollenCalm, 8.0f, 1.5f);
    EXPECT_GT(pollenGust, pollenCalm * 1.25f);
}

// Sand streak speed scales with strength around the same 0.5 anchor; sand
// keeps its own +X axis this phase (spawn-edge bias assumes rightward).
TEST(ParticleWind, SandSpeedScalesWithStrength)
{
    ParticleSystem psCalm;
    psCalm.SetTimeOfDay(12.0f);
    psCalm.SetNightFactor(0.0f);
    psCalm.SetWind({-1.0f, 0.0f}, 0.5f);
    psCalm.SetWeatherState(&GetWeatherDefinition(WeatherState::Sandstorm), 1.0f);
    psCalm.Update(0.5f, {0.0f, 0.0f}, {640.0f, 480.0f});

    ParticleSystem psGust;
    psGust.SetTimeOfDay(12.0f);
    psGust.SetNightFactor(0.0f);
    psGust.SetWind({-1.0f, 0.0f}, 1.0f);
    psGust.SetWeatherState(&GetWeatherDefinition(WeatherState::Sandstorm), 1.0f);
    psGust.Update(0.5f, {0.0f, 0.0f}, {640.0f, 480.0f});

    auto maxSandVx = [](const ParticleSystem& ps)
    {
        float m = -1.0f;
        for (const auto& p : ps.GetParticles())
        {
            if (p.type == ParticleType::Sand)
            {
                m = std::max(m, p.velocity.x);
            }
        }
        return m;
    };
    const float calm = maxSandVx(psCalm);
    const float gust = maxSandVx(psGust);
    ASSERT_GT(calm, 0.0f) << "test vacuous: no sand spawned";
    EXPECT_LE(calm, 200.0f + 1e-3f);  // today's spawn range at the 0.5 anchor
    EXPECT_GT(gust, calm * 1.2f);
    EXPECT_GT(maxSandVx(psGust), 0.0f) << "sand keeps +X axis";
}

// Mid-transition, BOTH endpoint particle types are present and the mix shifts
// with the weight: early = mostly outgoing (rain), late = mostly incoming
// (snow). This is the cross-fade that phase 1 could not do.
TEST(ParticleWind, TransitionCrossFadesTypes)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 0.5f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    const WeatherDefinition& storm = GetWeatherDefinition(WeatherState::Thunderstorm);
    const WeatherDefinition& blizzard = GetWeatherDefinition(WeatherState::Blizzard);

    // Steady-state rain first.
    ps.SetWeatherState(&storm, 1.0f);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    ASSERT_GT(CountType(ps, ParticleType::Rain), 0) << "test vacuous: no rain";

    // Early transition (w = 0.15): rain still dominant, snow appearing.
    ps.SetWeatherState(&blizzard, 1.0f);  // effective def reports destination
    ps.SetWeatherTransition(&storm, &blizzard, 0.15f);
    for (int i = 0; i < 30; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    const int rainEarly = CountType(ps, ParticleType::Rain);
    const int snowEarly = CountType(ps, ParticleType::Snow);
    EXPECT_GT(rainEarly, 0) << "outgoing stream stopped spawning too early";
    EXPECT_GT(snowEarly, 0) << "incoming stream not spawning";

    // Late transition (w = 0.9): snow dominant.
    ps.SetWeatherTransition(&storm, &blizzard, 0.9f);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountType(ps, ParticleType::Snow), CountType(ps, ParticleType::Rain));

    // Transition ends: back to two streams, snow only (rain decays naturally).
    ps.SetWeatherTransition(nullptr, nullptr, 0.0f);
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_EQ(CountType(ps, ParticleType::Rain), 0) << "rain never fully decayed";
    EXPECT_GT(CountType(ps, ParticleType::Snow), 0);
}

// Per-stream definition: the outgoing stream spawns with ITS OWN size scale,
// not the destination's. AuroraNight scales wisps to 0.85; Clear's scale is
// 1.0 - outgoing wisps mid-transition must still come out scaled.
TEST(ParticleWind, OutgoingStreamUsesOwnSizeScale)
{
    ParticleSystem psTransition;
    psTransition.SetTimeOfDay(0.0f);  // night: wisps are a night weather
    psTransition.SetNightFactor(1.0f);
    psTransition.SetWind({-1.0f, 0.0f}, 0.5f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    const WeatherDefinition& aurora = GetWeatherDefinition(WeatherState::AuroraNight);
    const WeatherDefinition& clear = GetWeatherDefinition(WeatherState::Clear);
    ASSERT_NE(aurora.particleSizeScale, 1.0f)
        << "test premise: AuroraNight must have a non-default size scale";

    // Reference: wisp sizes under plain AuroraNight.
    ParticleSystem psRef;
    psRef.SetTimeOfDay(0.0f);
    psRef.SetNightFactor(1.0f);
    psRef.SetWeatherState(&aurora, 1.0f);
    psRef.Update(1.0f, cameraPos, viewSize);
    float refMax = 0.0f;
    for (const auto& p : psRef.GetParticles())
    {
        if (p.type == ParticleType::Wisp)
        {
            refMax = std::max(refMax, p.size);
        }
    }
    ASSERT_GT(refMax, 0.0f) << "test vacuous: no reference wisps";

    // Transitioning AuroraNight -> Clear at small w: outgoing wisps must not
    // exceed the reference max (a destination-def bug would spawn at 1.0/0.85
    // = ~18% larger).
    psTransition.SetWeatherState(&clear, 1.0f);
    psTransition.SetWeatherTransition(&aurora, &clear, 0.1f);
    psTransition.Update(1.0f, cameraPos, viewSize);
    float maxSize = 0.0f;
    for (const auto& p : psTransition.GetParticles())
    {
        if (p.type == ParticleType::Wisp && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            maxSize = std::max(maxSize, p.size);
        }
    }
    ASSERT_GT(maxSize, 0.0f) << "test vacuous: outgoing stream spawned no wisps";
    // 5% headroom over sample-max noise; a destination-def bug lands at ~1.176x.
    EXPECT_LE(maxSize, refMax * 1.05f) << "outgoing stream used the wrong def's size scale";
}

// Shared-type cap floor under dual streams: Fog -> Blizzard mid-transition
// must keep the SHARED fog population at the smaller endpoint's cap (2500) -
// the incoming stream's own 10000 cap must not add headroom.
TEST(ParticleWind, DualStreamSharedTypeCapFloors)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 0.5f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    const WeatherDefinition& fog = GetWeatherDefinition(WeatherState::Fog);
    const WeatherDefinition& blizzard = GetWeatherDefinition(WeatherState::Blizzard);

    ps.SetWeatherState(&fog, 1.0f);
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
    }
    ASSERT_GT(CountType(ps, ParticleType::Fog), 2000) << "test vacuous: fog not cap-bound";

    ps.SetWeatherState(&blizzard, 1.0f);
    ps.SetWeatherTransition(&fog, &blizzard, 0.5f);
    for (int i = 0; i < 100; ++i)
    {
        ps.Update(0.2f, cameraPos, viewSize);
        EXPECT_LE(CountType(ps, ParticleType::Fog), 2500) << "shared cap breached at frame " << i;
    }
}

namespace
{
/// Live weather particles of `type` whose position lies in the bottom half of
/// the viewport anchored at `cameraPos`.
int CountTypeInBottomHalf(const ParticleSystem& ps,
                          ParticleType type,
                          glm::vec2 cameraPos,
                          glm::vec2 viewSize)
{
    int n = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == type && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX &&
            p.position.y > cameraPos.y + viewSize.y * 0.5f &&
            p.position.y < cameraPos.y + viewSize.y)
        {
            ++n;
        }
    }
    return n;
}
}  // namespace

// A camera sprinting downward must not outrun falling weather: the bottom
// half of the moving viewport stays populated and the splash band tracks the
// camera instead of freezing at the spawn-time position (maintainer-reported
// bug: Blizzard snow landed mid-screen under S+Shift).
TEST(ParticleWind, FallingWeatherSurvivesDownwardCameraSprint)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 1.0f);  // full Blizzard boost: fall 42-77 px/s

    glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Blizzard), 1.0f);

    // Steady state at rest.
    for (int i = 0; i < 100; ++i)
    {
        ps.Update(0.05f, cameraPos, viewSize);
    }
    ASSERT_GT(CountTypeInBottomHalf(ps, ParticleType::Snow, cameraPos, viewSize), 0)
        << "test vacuous: no snow at rest";

    // Sprint downward at 150 px/s (faster than boosted snowfall) for 5 s.
    for (int i = 0; i < 100; ++i)
    {
        cameraPos.y += 150.0f * 0.05f;
        ps.Update(0.05f, cameraPos, viewSize);
    }

    // Coverage: the revealed bottom half is populated.
    EXPECT_GT(CountTypeInBottomHalf(ps, ParticleType::Snow, cameraPos, viewSize), 20)
        << "bottom half starved: weather outrun by the camera";

    // Band tracking: no live flake's ground line lags above the current band
    // floor (the stale-band bug put them mid-screen).
    const float bandFloor = cameraPos.y + viewSize.y * 0.10f - 1.0f;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Snow && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX &&
            p.bakedGroundY > 0.0f)
        {
            EXPECT_GE(p.bakedGroundY, bandFloor) << "splash band lagged the camera";
        }
    }
}

// Same guarantee for rain (Thunderstorm), the other bakedGroundY weather.
TEST(ParticleWind, RainSurvivesDownwardCameraSprint)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 0.5f);

    glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Thunderstorm), 1.0f);

    for (int i = 0; i < 100; ++i)
    {
        ps.Update(0.05f, cameraPos, viewSize);
    }
    ASSERT_GT(CountTypeInBottomHalf(ps, ParticleType::Rain, cameraPos, viewSize), 0)
        << "test vacuous: no rain at rest";

    for (int i = 0; i < 100; ++i)
    {
        cameraPos.y += 300.0f * 0.05f;  // rain falls faster; sprint harder
        ps.Update(0.05f, cameraPos, viewSize);
    }
    EXPECT_GT(CountTypeInBottomHalf(ps, ParticleType::Rain, cameraPos, viewSize), 20)
        << "bottom half starved: rain outrun by the camera";

    // Band tracking for rain's own branch (coverage alone is masked by
    // rain's short lifetimes): no live drop's ground line lags above the
    // current band floor.
    const float bandFloor = cameraPos.y + viewSize.y * 0.10f - 1.0f;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Rain && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX &&
            p.bakedGroundY > 0.0f)
        {
            EXPECT_GE(p.bakedGroundY, bandFloor) << "rain splash band lagged the camera";
        }
    }
}

// Static camera keeps today's behavior: flakes die AT the band (never fall
// far past it), so ground impacts still fire and nothing accumulates below
// the viewport.
TEST(ParticleWind, StaticCameraGroundImpactsStillFire)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 1.0f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Blizzard), 1.0f);

    for (int i = 0; i < 400; ++i)
    {
        ps.Update(0.05f, cameraPos, viewSize);
    }
    // UpdateWeatherSpawning runs AFTER the per-particle band-check loop
    // within Update(), so particles pre-warmed on the very last iteration
    // have not yet had their own bakedGroundY check dispatched - in real
    // gameplay that happens on the very next real frame (sub-frame,
    // invisible); here it needs one more zero-dt call to settle before
    // asserting, so this measures steady state rather than a
    // between-iterations artifact. Zero deltaTime advances no positions and
    // spawns nothing new (spawnTimer needs a full interval to fire), so it
    // only dispatches the pending check on particles already present.
    ps.Update(0.0f, cameraPos, viewSize);
    int belowBand = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Snow && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX &&
            p.position.y > cameraPos.y + viewSize.y * 1.05f + 30.0f)
        {
            ++belowBand;
        }
    }
    EXPECT_EQ(belowBand, 0) << "snow fell through the ground band";
}

// Ash pre-ages like the other streaming types: the fall column populates
// immediately at spawn, not one entry-strip at a time.
TEST(ParticleWind, AshPreAgesAcrossTheColumn)
{
    ParticleSystem ps;
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);
    ps.SetWind({-1.0f, 0.0f}, 0.5f);

    const glm::vec2 cameraPos{0.0f, 0.0f};
    const glm::vec2 viewSize{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::AshFall), 1.0f);
    ps.Update(1.0f, cameraPos, viewSize);  // one burst of spawns

    // The spawn strip is the top ~10% of the overspray rect; pre-aged ash
    // must already appear well below it.
    int belowStrip = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == ParticleType::Ash && p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX &&
            p.position.y > cameraPos.y + viewSize.y * 0.3f)
        {
            ++belowStrip;
        }
    }
    EXPECT_GT(belowStrip, 0) << "ash not pre-aged: column fills only at fall speed";
}
