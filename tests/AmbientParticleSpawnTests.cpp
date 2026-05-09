#include <gtest/gtest.h>

#include "../src/AmbienceConfig.h"
#include "../src/ParticleSystem.h"

#include <glm/glm.hpp>

#include <cmath>

class AmbientParticleSpawnTest : public ::testing::Test
{
protected:
    ParticleSystem ps;
    glm::vec2 cameraPos{0.0f, 0.0f};
    glm::vec2 viewSize{640.0f, 480.0f};

    int CountAmbient() const
    {
        int count = 0;
        for (const auto& p : ps.GetParticles())
        {
            switch (p.type)
            {
                case ParticleType::DriftingLeaf:
                case ParticleType::DustMote:
                case ParticleType::Pollen:
                    ++count;
                    break;
                default:
                    break;
            }
        }
        return count;
    }

    int CountOfType(ParticleType type) const
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
};

TEST_F(AmbientParticleSpawnTest, RespectsTotalCapAtPeakBias)
{
    ps.SetTimeOfDay(12.0f);  // Peak leaf and dust bias.
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.5f, cameraPos, viewSize);
        EXPECT_LE(CountAmbient(), ambience::AMBIENT_PARTICLE_TOTAL_CAP);
    }
}

TEST_F(AmbientParticleSpawnTest, NoAmbientSpawnAtDeepNight)
{
    ps.SetTimeOfDay(2.0f);  // All three biases are zero at this hour.
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.5f, cameraPos, viewSize);
    }
    EXPECT_EQ(CountAmbient(), 0);
}

TEST_F(AmbientParticleSpawnTest, PollenAbsentAtMidday)
{
    // Midday: leaves and dust spawn, but pollen's golden-hour bias is zero.
    ps.SetTimeOfDay(12.0f);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_EQ(CountOfType(ParticleType::Pollen), 0);
    EXPECT_GT(CountOfType(ParticleType::DustMote), 0);
}

TEST_F(AmbientParticleSpawnTest, PollenSpawnsAtDawnGoldenHour)
{
    ps.SetTimeOfDay(6.5f);  // Peak pollen bias at dawn golden hour.
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountOfType(ParticleType::Pollen), 0);
}

TEST_F(AmbientParticleSpawnTest, PollenSpawnsAtDuskGoldenHour)
{
    ps.SetTimeOfDay(19.0f);  // Peak pollen bias at dusk golden hour.
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountOfType(ParticleType::Pollen), 0);
}

TEST_F(AmbientParticleSpawnTest, AmbientParticlesDieAndRecycle)
{
    ps.SetTimeOfDay(12.0f);
    // Saturate, then advance simulated time well past max lifetime (15s leaves).
    // Pool size should remain bounded across the recycle.
    for (int i = 0; i < 200; ++i)
    {
        ps.Update(0.5f, cameraPos, viewSize);
    }
    int count = static_cast<int>(ps.GetParticles().size());
    // Zones not set, so all particles come through the global ambient cap.
    EXPECT_LE(count, ambience::AMBIENT_PARTICLE_TOTAL_CAP);
}

// --- Editor zone tests: zones placed for ambient types must produce particles
// independent of the global ambient spawner and time-of-day biasing. ---

class AmbientParticleZoneTest : public AmbientParticleSpawnTest
{
protected:
    std::vector<ParticleZone> zones;

    void PlaceZone(ParticleType type)
    {
        // Position inside the camera rect so the visibility check passes.
        ParticleZone z(glm::vec2(100.0f, 100.0f), glm::vec2(64.0f, 64.0f), type);
        zones.push_back(z);
        ps.SetZones(&zones);
    }
};

TEST_F(AmbientParticleZoneTest, DriftingLeafZoneSpawnsLeaves)
{
    ps.SetTimeOfDay(2.0f);  // Deep night: global ambient gating zero.
    PlaceZone(ParticleType::DriftingLeaf);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountOfType(ParticleType::DriftingLeaf), 0);
}

TEST_F(AmbientParticleZoneTest, DustMoteZoneSpawnsMotes)
{
    ps.SetTimeOfDay(2.0f);
    PlaceZone(ParticleType::DustMote);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountOfType(ParticleType::DustMote), 0);
}

TEST_F(AmbientParticleZoneTest, PollenZoneSpawnsOutsideGoldenHour)
{
    // Midday: global pollen bias is zero, but a placed zone must still spawn.
    ps.SetTimeOfDay(12.0f);
    PlaceZone(ParticleType::Pollen);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.1f, cameraPos, viewSize);
    }
    EXPECT_GT(CountOfType(ParticleType::Pollen), 0);
}

TEST_F(AmbientParticleZoneTest, ZoneSpawnedParticlesAreInsideZoneBounds)
{
    ps.SetTimeOfDay(2.0f);
    PlaceZone(ParticleType::DriftingLeaf);
    // Single update with dt slightly above the spawn interval (1/2.5 = 0.4s)
    // so exactly one spawn fires. Spawning happens after the per-particle
    // Update pass, so the just-spawned particle has not yet been moved.
    ps.Update(0.5f, cameraPos, viewSize);
    ASSERT_GT(CountOfType(ParticleType::DriftingLeaf), 0);
    const auto& z = zones.front();
    for (const auto& p : ps.GetParticles())
    {
        if (p.type != ParticleType::DriftingLeaf)
        {
            continue;
        }
        EXPECT_GE(p.position.x, z.position.x);
        EXPECT_LE(p.position.x, z.position.x + z.size.x);
        EXPECT_GE(p.position.y, z.position.y);
        EXPECT_LE(p.position.y, z.position.y + z.size.y);
    }
}

// --- Color palette constraints --------------------------------------------
// DustMote represents floating dust caught in light - it should look
// neutral. Pollen carries the chromatic ambient palette and must not
// re-introduce the white/grey range.

TEST_F(AmbientParticleZoneTest, DustMoteColorsAreNeutralGreyOnly)
{
    ps.SetTimeOfDay(2.0f);
    PlaceZone(ParticleType::DustMote);
    for (int i = 0; i < 60; ++i)
    {
        ps.Update(0.5f, cameraPos, viewSize);
    }
    int sampled = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type != ParticleType::DustMote)
        {
            continue;
        }
        ++sampled;
        // Pure grey: R == G == B (within float tolerance from accumulated math).
        EXPECT_NEAR(p.color.r, p.color.g, 1e-4f)
            << "DustMote has non-neutral RGB: (" << p.color.r << ", " << p.color.g << ", "
            << p.color.b << ")";
        EXPECT_NEAR(p.color.g, p.color.b, 1e-4f)
            << "DustMote has non-neutral RGB: (" << p.color.r << ", " << p.color.g << ", "
            << p.color.b << ")";
    }
    EXPECT_GT(sampled, 0);
}

TEST_F(AmbientParticleZoneTest, PollenIsNeverWhitish)
{
    ps.SetTimeOfDay(12.0f);
    PlaceZone(ParticleType::Pollen);
    for (int i = 0; i < 80; ++i)
    {
        ps.Update(0.5f, cameraPos, viewSize);
    }
    int sampled = 0;
    for (const auto& p : ps.GetParticles())
    {
        if (p.type != ParticleType::Pollen)
        {
            continue;
        }
        ++sampled;
        // The previous "White (dandelion)" species set R/G/B all >= 0.95 with
        // tight grouping. Forbid any pollen falling in that envelope.
        const bool whitish = p.color.r >= 0.9f && p.color.g >= 0.9f && p.color.b >= 0.9f &&
                             std::abs(p.color.r - p.color.g) <= 0.10f &&
                             std::abs(p.color.g - p.color.b) <= 0.10f;
        EXPECT_FALSE(whitish) << "Pollen rendered nearly white: (" << p.color.r << ", "
                              << p.color.g << ", " << p.color.b << ")";
    }
    EXPECT_GT(sampled, 0);
}

TEST(ParticleType, EnumLayoutInvariant)
{
    // Original cozy types stay at their stable indices so saved zones in
    // existing maps deserialize the right kind. Weather-driven types live
    // strictly after Pollen.
    EXPECT_EQ(static_cast<int>(ParticleType::Pollen), 10);
    EXPECT_EQ(static_cast<int>(ParticleType::CherryBlossom), 11);
    EXPECT_EQ(static_cast<int>(ParticleType::Ash), 12);
    EXPECT_EQ(static_cast<int>(ParticleType::Ember), 13);
    EXPECT_EQ(static_cast<int>(ParticleType::Sand), 14);
    EXPECT_EQ(EnumTraits<ParticleType>::Count, 15u);
}
