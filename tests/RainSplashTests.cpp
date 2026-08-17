#include <gtest/gtest.h>

#include "../src/ParticleSystem.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// Rain impacts spawn a dedicated RainSplash particle (a life-mapped 4-frame
// water-splash sprite), replacing the old procedural Sparkles droplet burst.
// These tests drive ParticleSystem::Update directly - no renderer, no atlas -
// exercising the same spawn/update path the game uses.

namespace
{
int CountOfType(const ParticleSystem& ps, ParticleType type)
{
    int count = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == type)
        {
            ++count;
        }
    }
    return count;
}

// Editor Rain zone positioned inside the default camera rect so the
// visibility check passes and its drops fall into view.
void PlaceRainZone(ParticleSystem& ps, std::vector<ParticleZone>& zones)
{
    zones.emplace_back(glm::vec2(100.0f, 100.0f), glm::vec2(64.0f, 64.0f), ParticleType::Rain);
    ps.SetZones(&zones);
    ps.SetTimeOfDay(2.0f);  // Deep night: global ambient spawning is gated off.
}

// Editor Snow zone, same placement, for the snow-impact (SnowSplash) path.
void PlaceSnowZone(ParticleSystem& ps, std::vector<ParticleZone>& zones)
{
    zones.emplace_back(glm::vec2(100.0f, 100.0f), glm::vec2(64.0f, 64.0f), ParticleType::Snow);
    ps.SetZones(&zones);
    ps.SetTimeOfDay(2.0f);
}

constexpr glm::vec2 kCameraPos{0.0f, 0.0f};
constexpr glm::vec2 kViewSize{640.0f, 480.0f};
}  // namespace

// Rain drops crossing the ground band must produce RainSplash particles.
// The ~30% spawn throttle is randomized (RNG seeded from random_device), so
// we watch across many impacts rather than a single frame: over ~20 simulated
// seconds of continuous rain the probability of never seeing a live splash is
// vanishingly small.
TEST(RainSplash, EditorRainZoneProducesSplashes)
{
    ParticleSystem ps;
    std::vector<ParticleZone> zones;
    PlaceRainZone(ps, zones);

    bool sawSplash = false;
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.1f, kCameraPos, kViewSize);
        if (CountOfType(ps, ParticleType::RainSplash) > 0)
        {
            sawSplash = true;
        }
    }
    EXPECT_TRUE(sawSplash);
}

// Regression: rain impacts must no longer emit Sparkles. With only a Rain zone
// active and ambient spawning gated off, any Sparkles in the pool at any frame
// would betray the old droplet-burst splash path.
TEST(RainSplash, RainImpactsNoLongerEmitSparkles)
{
    ParticleSystem ps;
    std::vector<ParticleZone> zones;
    PlaceRainZone(ps, zones);

    int maxSparkles = 0;
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.1f, kCameraPos, kViewSize);
        maxSparkles = std::max(maxSparkles, CountOfType(ps, ParticleType::Sparkles));
    }
    EXPECT_EQ(maxSparkles, 0);
}

// SpawnOne(RainSplash) (console / hand-placed path) appends exactly one valid,
// finite, non-additive splash with a positive lifetime.
TEST(RainSplash, SpawnOneAppendsFiniteSplash)
{
    ParticleSystem ps;
    ps.SpawnOne(ParticleType::RainSplash, glm::vec2(320.0f, 240.0f));

    ASSERT_EQ(CountOfType(ps, ParticleType::RainSplash), 1);
    const Particle& p = ps.GetParticles().front();
    EXPECT_TRUE(std::isfinite(p.position.x) && std::isfinite(p.position.y));
    EXPECT_GT(p.size, 0.0f);
    EXPECT_GT(p.lifetime, 0.0f);
    EXPECT_FALSE(p.additive);
}

// A spawned splash keeps a finite, in-range alpha across its short life and is
// eventually recycled (life-mapped one-shot, not a persistent particle).
TEST(RainSplash, SplashFadesAndRecycles)
{
    ParticleSystem ps;
    ps.SpawnOne(ParticleType::RainSplash, glm::vec2(320.0f, 240.0f));
    ASSERT_EQ(CountOfType(ps, ParticleType::RainSplash), 1);

    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.05f, kCameraPos, kViewSize);
        for (const auto& p : ps.GetParticles())
        {
            if (p.type == ParticleType::RainSplash)
            {
                EXPECT_TRUE(std::isfinite(p.color.a));
                EXPECT_GE(p.color.a, 0.0f);
                EXPECT_LE(p.color.a, 1.0f);
            }
        }
    }
    // ~3 simulated seconds >> the 0.30-0.40s splash lifetime, so it is gone.
    EXPECT_EQ(CountOfType(ps, ParticleType::RainSplash), 0);
}

// --- Snow impact (SnowSplash): the snow-landing equivalent of RainSplash. -----

// Snow landings produce SnowSplash particles (dedicated snow-impact sprite),
// replacing the old procedural Sparkles puff.
TEST(SnowSplash, EditorSnowZoneProducesImpacts)
{
    ParticleSystem ps;
    std::vector<ParticleZone> zones;
    PlaceSnowZone(ps, zones);

    bool sawSplash = false;
    for (int i = 0; i < 300; ++i)
    {
        ps.Update(0.1f, kCameraPos, kViewSize);
        if (CountOfType(ps, ParticleType::SnowSplash) > 0)
        {
            sawSplash = true;
        }
    }
    EXPECT_TRUE(sawSplash);
}

// Regression: snow landings must no longer emit Sparkles (the old puff path).
TEST(SnowSplash, SnowImpactsNoLongerEmitSparkles)
{
    ParticleSystem ps;
    std::vector<ParticleZone> zones;
    PlaceSnowZone(ps, zones);

    int maxSparkles = 0;
    for (int i = 0; i < 300; ++i)
    {
        ps.Update(0.1f, kCameraPos, kViewSize);
        maxSparkles = std::max(maxSparkles, CountOfType(ps, ParticleType::Sparkles));
    }
    EXPECT_EQ(maxSparkles, 0);
}

// SpawnOne(SnowSplash) appends one valid, finite, non-additive impact puff.
TEST(SnowSplash, SpawnOneAppendsFiniteImpact)
{
    ParticleSystem ps;
    ps.SpawnOne(ParticleType::SnowSplash, glm::vec2(320.0f, 240.0f));

    ASSERT_EQ(CountOfType(ps, ParticleType::SnowSplash), 1);
    const Particle& p = ps.GetParticles().front();
    EXPECT_TRUE(std::isfinite(p.position.x) && std::isfinite(p.position.y));
    EXPECT_GT(p.size, 0.0f);
    EXPECT_GT(p.lifetime, 0.0f);
    EXPECT_FALSE(p.additive);
}

// Both impact types dim their (white, non-additive) sprite at night so it does
// not glare against the dark scene - the alpha scales down with night factor.
TEST(SplashVisibility, ImpactAlphaIsDimmerAtNight)
{
    auto peakAlpha = [](ParticleType type, float nightFactor)
    {
        ParticleSystem ps;
        ps.SetSceneNightFactor(nightFactor);
        ps.SpawnOne(type, glm::vec2(320.0f, 240.0f));
        ps.Update(0.01f, kCameraPos, kViewSize);  // one tick: Update sets alpha from scene darkness
        float a = 0.0f;
        for (const auto& p : ps.GetParticles())
        {
            if (p.type == type)
            {
                a = std::max(a, p.color.a);
            }
        }
        return a;
    };
    for (const ParticleType t : {ParticleType::RainSplash, ParticleType::SnowSplash})
    {
        const float dayAlpha = peakAlpha(t, 0.0f);
        const float nightAlpha = peakAlpha(t, 1.0f);
        EXPECT_GT(dayAlpha, 0.0f);
        EXPECT_LT(nightAlpha, dayAlpha);
    }
}

// Regression: weather splashes are pushed to m_PendingSpawns and merged AFTER
// the per-frame Update loop, so they render one frame before their first Update.
// The spawn alpha must therefore already match the (scene-night) Update alpha -
// otherwise a splash flashes bright for one frame, glaring at night. At deep
// night no RainSplash should ever exceed the faint steady-state alpha.
TEST(SplashVisibility, NoBrightSpawnFrameFlashAtNight)
{
    ParticleSystem ps;
    std::vector<ParticleZone> zones;
    zones.emplace_back(glm::vec2(100.0f, 100.0f), glm::vec2(64.0f, 64.0f), ParticleType::Rain);
    ps.SetZones(&zones);
    ps.SetTimeOfDay(2.0f);
    ps.SetSceneNightFactor(1.0f);  // deep night: splashes should stay very faint

    float maxSplashAlpha = 0.0f;
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.1f, kCameraPos, kViewSize);
        for (const auto& p : ps.GetParticles())
        {
            if (p.type == ParticleType::RainSplash)
            {
                maxSplashAlpha = std::max(maxSplashAlpha, p.color.a);
            }
        }
    }
    EXPECT_GT(maxSplashAlpha, 0.0f);   // splashes did appear
    EXPECT_LT(maxSplashAlpha, 0.25f);  // ~0.15 steady state; never the old bright ~0.9 spawn flash
}
