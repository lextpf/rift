#include <gtest/gtest.h>

#include "../src/ParticleSystem.hpp"
#include "../src/WeatherDefinitions.hpp"

#include <glm/glm.hpp>

#include <algorithm>

namespace
{
int CountOfType(const ParticleSystem& ps, ParticleType t)
{
    int n = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type == t)
        {
            ++n;
        }
    }
    return n;
}
}  // namespace

// Base HeavyRain + overlay Aurora must run all three weather particle layers at once:
// Rain (base) + Aurora + Wisp (overlay) - past the 2-slot-per-weather ceiling.
TEST(WeatherOverlayParticles, BaseRainPlusOverlayAuroraRunsThreeLayers)
{
    ParticleSystem ps;
    const glm::vec2 cam{0.0f, 0.0f};
    const glm::vec2 view{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::HeavyRain), 1.0f);
    ps.SetWeatherOverlay(&GetWeatherDefinition(WeatherState::Aurora), 1.0f);
    for (int i = 0; i < 240; ++i)
    {
        ps.Update(1.0f / 60.0f, cam, view);
    }
    EXPECT_GT(CountOfType(ps, ParticleType::Rain), 0);
    EXPECT_GT(CountOfType(ps, ParticleType::Aurora), 0);
    EXPECT_GT(CountOfType(ps, ParticleType::Wisp), 0);
}

TEST(WeatherOverlayParticles, ClearingOverlayStopsOverlaySpawns)
{
    ParticleSystem ps;
    const glm::vec2 cam{0.0f, 0.0f};
    const glm::vec2 view{640.0f, 480.0f};
    ps.SetWeatherState(&GetWeatherDefinition(WeatherState::Clear), 1.0f);
    ps.SetWeatherOverlay(nullptr, 0.0f);  // no overlay
    for (int i = 0; i < 120; ++i)
    {
        ps.Update(1.0f / 60.0f, cam, view);
    }
    EXPECT_EQ(CountOfType(ps, ParticleType::Aurora), 0);
}

TEST(WeatherParticleVisuals, SnowflakesUseRestrainedSizeRange)
{
    ParticleSystem ps;
    for (int i = 0; i < 64; ++i)
    {
        ps.SpawnOne(ParticleType::Snow, glm::vec2(320.0f, 240.0f));
    }

    ASSERT_EQ(CountOfType(ps, ParticleType::Snow), 64);
    for (const auto& p : ps.GetParticles())
    {
        EXPECT_GE(p.size, 3.0f);
        EXPECT_LE(p.size, 5.5f);
    }
}

TEST(WeatherParticleVisuals, AuroraMotesUseSoftAlphaBlendingAtNoon)
{
    ParticleSystem ps;
    const glm::vec2 cam{0.0f, 0.0f};
    const glm::vec2 view{640.0f, 480.0f};
    ps.SetTimeOfDay(12.0f);
    ps.SetNightFactor(0.0f);  // Aurora strength must not depend on natural night.
    for (int i = 0; i < 64; ++i)
    {
        ps.SpawnOne(ParticleType::Aurora, glm::vec2(320.0f, 240.0f));
    }

    float maxObservedAlpha = 0.0f;
    for (int frame = 0; frame < 240; ++frame)
    {
        ps.Update(1.0f / 60.0f, cam, view);
        for (const auto& p : ps.GetParticles())
        {
            if (p.type != ParticleType::Aurora)
            {
                continue;
            }
            EXPECT_FALSE(p.additive);
            EXPECT_LE(p.color.a, 0.341f);
            maxObservedAlpha = std::max(maxObservedAlpha, p.color.a);
        }
    }
    EXPECT_GT(maxObservedAlpha, 0.20f);
}
