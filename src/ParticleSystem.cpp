#include "ParticleSystem.h"

#include "AmbienceConfig.h"
#include "Logger.h"
#include "MathConstants.h"
#include "ProceduralTexture.h"
#include "Tilemap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Particle";
}  // namespace

// ---------------------------------------------------------------------------
// Particle behavior dispatch
// ---------------------------------------------------------------------------

/// Context passed to per-type Update specializations.
struct ParticleUpdateContext
{
    float time;
    float deltaTime;
    float nightFactor;
    const std::vector<ParticleZone>* zones;
    bool hasZones;
};

/// Context passed to per-type Spawn specializations.
struct ParticleSpawnContext
{
    std::mt19937& rng;
    std::uniform_real_distribution<float>& dist;
    std::vector<Particle>& particles;
};

/// Primary template - specialize for each ParticleType enumerator.
template <ParticleType PT>
struct ParticleBehavior
{
    static constexpr float SpawnRate = 5.0f;
    static void Update(Particle& p, const ParticleUpdateContext& ctx);
    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx);
};

// ---------------------------------------------------------------------------
// Firefly
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Firefly>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Gentle random drift
        float driftX = std::sin(ctx.time * 2.0f + p.phase) * 10.0f;
        float driftY = std::cos(ctx.time * 1.5f + p.phase * 1.3f) * 8.0f;
        p.position.x += driftX * ctx.deltaTime;
        p.position.y += driftY * ctx.deltaTime;

        // Slow rotation as they drift
        float rotationSpeed = 20.0f + (p.phase / 6.28f) * 40.0f;  // 20-60 degrees per second
        if (std::fmod(p.phase, 2.0f) < 1.0f)
            rotationSpeed = -rotationSpeed;
        p.rotation += rotationSpeed * ctx.deltaTime;

        // Pulsing glow, alpha oscillates between 0.0 and 0.7
        float pulse = 0.5f + 0.5f * std::sin(ctx.time * 4.0f + p.phase);
        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.3f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.5f);
        p.color.a = pulse * lifeFade * fadeIn * 0.7f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Firefly;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;

        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 5.0f;
        p.velocity.y = (ctx.dist(ctx.rng) - 0.5f) * 5.0f;

        float colorChoice = ctx.dist(ctx.rng);
        if (colorChoice < 0.30f)
        {
            p.color = glm::vec4(1.0f,
                                0.9f + ctx.dist(ctx.rng) * 0.1f,
                                0.3f + ctx.dist(ctx.rng) * 0.2f,
                                0.0f);  // Warm yellow
        }
        else if (colorChoice < 0.45f)
        {
            p.color = glm::vec4(0.4f + ctx.dist(ctx.rng) * 0.2f,
                                1.0f,
                                0.5f + ctx.dist(ctx.rng) * 0.2f,
                                0.0f);  // Green
        }
        else if (colorChoice < 0.60f)
        {
            p.color = glm::vec4(0.4f, 0.8f + ctx.dist(ctx.rng) * 0.15f, 1.0f, 0.0f);  // Cyan-blue
        }
        else if (colorChoice < 0.75f)
        {
            p.color = glm::vec4(1.0f, 0.4f + ctx.dist(ctx.rng) * 0.15f, 0.8f, 0.0f);  // Pink
        }
        else if (colorChoice < 0.90f)
        {
            p.color = glm::vec4(1.0f, 0.4f + ctx.dist(ctx.rng) * 0.15f, 0.2f, 0.0f);  // Red-orange
        }
        else
        {
            p.color = glm::vec4(0.8f + ctx.dist(ctx.rng) * 0.15f, 0.4f, 1.0f, 0.0f);  // Purple
        }

        p.size = 3.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 5.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Rain
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Rain>
{
    static constexpr float SpawnRate = 50.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Fade in smoothly over first 0.15 seconds
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.15f);
        // Target alpha stored in phase
        p.color.a = fadeIn * p.phase;

        // Check if rain has fallen below its zone
        if (ctx.hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(ctx.zones->size()))
        {
            const auto& zone = (*ctx.zones)[p.zoneIndex];

            // Vary ground height per particle using position.x as seed
            float heightVariation =
                std::fmod(std::abs(p.position.x * 7.3f + p.phase * 100.0f), 60.0f);
            float groundY = zone.position.y + zone.size.y + 20.0f + heightVariation;
            if (p.position.y > groundY)
            {
                p.lifetime = 0.0f;
            }
        }
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Rain;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * 10.0f;

        p.velocity.x = 0.0f;
        p.velocity.y = 150.0f + ctx.dist(ctx.rng) * 100.0f;

        float targetAlpha = 0.6f + ctx.dist(ctx.rng) * 0.15f;
        p.color = glm::vec4(0.8f, 0.85f, 1.0f, 0.0f);
        p.phase = targetAlpha;

        p.size = 10.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 2.0f;
        p.maxLifetime = p.lifetime;
        p.rotation = -35.0f - ctx.dist(ctx.rng) * 30.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Snow
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Snow>
{
    static constexpr float SpawnRate = 12.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Snow drifts side to side
        float drift = std::sin(ctx.time * 1.5f + p.phase) * 20.0f;
        p.position.x += drift * ctx.deltaTime;

        // Rotate as it falls
        float rotationSpeed = 30.0f + (p.phase / 6.28f) * 60.0f;  // 30-90 degrees per second
        if (std::fmod(p.phase, 2.0f) < 1.0f)
            rotationSpeed = -rotationSpeed;  // Half rotate clockwise, half counter-clockwise
        p.rotation += rotationSpeed * ctx.deltaTime;

        // Check if snow has fallen below its zone
        if (ctx.hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(ctx.zones->size()))
        {
            const auto& zone = (*ctx.zones)[p.zoneIndex];
            if (p.position.y > zone.position.y + zone.size.y + 50.0f)
            {
                p.lifetime = 0.0f;
            }
        }
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Snow;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * 10.0f;

        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 12.0f;
        p.velocity.y = 12.0f + ctx.dist(ctx.rng) * 10.0f;

        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.6f + ctx.dist(ctx.rng) * 0.15f);

        p.size = 1.5f + ctx.dist(ctx.rng) * 1.5f;
        p.lifetime = 15.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Fog
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Fog>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Fog drifts very slowly
        float driftX = std::sin(ctx.time * 0.15f + p.phase) * 2.5f;
        float driftY = std::cos(ctx.time * 0.1f + p.phase * 0.5f) * 1.0f;

        // Add subtle swirling motion for smoky effect
        float swirl = std::sin(ctx.time * 0.4f + p.phase * 2.0f) * 1.5f;
        p.position.x += (driftX + swirl) * ctx.deltaTime;
        p.position.y += driftY * ctx.deltaTime;

        // Slow pulsing alpha
        float pulse = 0.9f + 0.1f * std::sin(ctx.time * 0.25f + p.phase);

        // Long fade in and fade out for smooth feathered appearance
        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.4f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 4.0f);

        // More visible during day, significantly less at night
        float dayBoost = 1.0f + (1.0f - ctx.nightFactor) * 0.4f;
        float nightReduce = 1.0f - ctx.nightFactor * 0.6f;
        p.color.a = pulse * lifeFade * fadeIn * 0.40f * dayBoost * nightReduce;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Fog;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;

        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 3.0f;
        p.velocity.y = (ctx.dist(ctx.rng) - 0.5f) * 1.5f;

        float grey = 0.88f + ctx.dist(ctx.rng) * 0.12f;
        p.color = glm::vec4(grey, grey, grey, 0.0f);

        p.size = 48.0f + ctx.dist(ctx.rng) * 48.0f;
        p.lifetime = 18.0f + ctx.dist(ctx.rng) * 12.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Sparkles
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Sparkles>
{
    static constexpr float SpawnRate = 18.0f;

    static void Update(Particle& p, const ParticleUpdateContext&)
    {
        // Instant sparkle, bright flash then fade
        float lifeRatio = 1.0f - (p.lifetime / p.maxLifetime);
        float flash = lifeRatio < 0.15f ? 0.75f : 0.0f;
        p.color.a = flash;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Sparkles;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;

        p.velocity.x = 0.0f;
        p.velocity.y = 0.0f;

        float hueChoice = ctx.dist(ctx.rng);
        if (hueChoice < 0.25f)
        {
            p.color = glm::vec4(1.0f,
                                0.9f + ctx.dist(ctx.rng) * 0.1f,
                                0.6f + ctx.dist(ctx.rng) * 0.2f,
                                1.0f);  // Warm gold
        }
        else if (hueChoice < 0.45f)
        {
            p.color = glm::vec4(0.65f + ctx.dist(ctx.rng) * 0.1f, 0.85f, 1.0f, 1.0f);  // Cool blue
        }
        else if (hueChoice < 0.60f)
        {
            p.color = glm::vec4(1.0f, 0.65f + ctx.dist(ctx.rng) * 0.1f, 0.9f, 1.0f);  // Pink
        }
        else if (hueChoice < 0.75f)
        {
            p.color = glm::vec4(0.7f, 1.0f, 0.8f + ctx.dist(ctx.rng) * 0.1f, 1.0f);  // Mint
        }
        else if (hueChoice < 0.90f)
        {
            p.color = glm::vec4(0.75f + ctx.dist(ctx.rng) * 0.1f, 0.7f, 1.0f, 1.0f);  // Lavender
        }
        else
        {
            p.color = glm::vec4(1.0f, 0.75f + ctx.dist(ctx.rng) * 0.1f, 0.55f, 1.0f);  // Peach
        }

        p.size = 2.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 0.5f + ctx.dist(ctx.rng) * 0.5f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Wisp
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Wisp>
{
    static constexpr float SpawnRate = 7.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Magical spiraling movement
        float spiralX = std::sin(ctx.time * 1.5f + p.phase) * 20.0f;
        float spiralY = std::cos(ctx.time * 1.2f + p.phase * 0.7f) * 15.0f;
        float wobble = std::sin(ctx.time * 3.0f + p.phase * 2.0f) * 8.0f;
        p.position.x += (spiralX + wobble) * ctx.deltaTime;
        p.position.y += spiralY * ctx.deltaTime;

        // Gentle rotation
        float rotSpeed = 45.0f + (p.phase / 6.28f) * 30.0f;  // 45-75 deg/sec
        if (std::fmod(p.phase, 2.0f) < 1.0f)
            rotSpeed = -rotSpeed;
        p.rotation += rotSpeed * ctx.deltaTime;

        // Pulsing glow effect
        float twinkle = 0.5f + 0.5f * std::sin(ctx.time * 4.0f + p.phase * 3.0f);
        float shimmer = 0.8f + 0.2f * std::sin(ctx.time * 7.0f + p.phase);
        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.25f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 1.0f);
        p.color.a = twinkle * shimmer * lifeFade * fadeIn * 0.7f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Wisp;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;

        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 8.0f;
        p.velocity.y = (ctx.dist(ctx.rng) - 0.5f) * 6.0f - 5.0f;

        float colorChoice = ctx.dist(ctx.rng);
        if (colorChoice < 0.16f)
        {
            p.color = glm::vec4(0.5f + ctx.dist(ctx.rng) * 0.2f,
                                0.75f + ctx.dist(ctx.rng) * 0.15f,
                                1.0f,
                                0.0f);  // Cyan
        }
        else if (colorChoice < 0.32f)
        {
            p.color = glm::vec4(0.75f + ctx.dist(ctx.rng) * 0.15f,
                                0.5f + ctx.dist(ctx.rng) * 0.15f,
                                1.0f,
                                0.0f);  // Purple
        }
        else if (colorChoice < 0.46f)
        {
            p.color = glm::vec4(0.85f + ctx.dist(ctx.rng) * 0.15f,
                                0.85f + ctx.dist(ctx.rng) * 0.15f,
                                1.0f,
                                0.0f);  // White-blue
        }
        else if (colorChoice < 0.60f)
        {
            p.color = glm::vec4(1.0f,
                                0.5f + ctx.dist(ctx.rng) * 0.2f,
                                0.85f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Magenta
        }
        else if (colorChoice < 0.72f)
        {
            p.color = glm::vec4(0.5f + ctx.dist(ctx.rng) * 0.2f,
                                1.0f,
                                0.6f + ctx.dist(ctx.rng) * 0.2f,
                                0.0f);  // Green
        }
        else if (colorChoice < 0.82f)
        {
            p.color = glm::vec4(1.0f,
                                0.7f + ctx.dist(ctx.rng) * 0.15f,
                                0.4f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Amber
        }
        else if (colorChoice < 0.92f)
        {
            p.color = glm::vec4(1.0f,
                                0.95f + ctx.dist(ctx.rng) * 0.05f,
                                0.5f + ctx.dist(ctx.rng) * 0.2f,
                                0.0f);  // Gold
        }
        else
        {
            p.color = glm::vec4(1.0f,
                                0.4f + ctx.dist(ctx.rng) * 0.2f,
                                0.4f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Crimson
        }

        p.size = 3.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = ctx.dist(ctx.rng) * 360.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Lantern
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Lantern>
{
    static constexpr float SpawnRate = 0.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Stationary glow, only visible at night
        if (ctx.nightFactor < 0.05f)
        {
            p.color.a = 0.0f;
            return;
        }
        float pulse = 0.9f + 0.1f * std::sin(ctx.time * 1.5f + p.phase);
        float flicker = 0.97f + 0.03f * std::sin(ctx.time * 6.0f + p.phase * 2.0f);

        float nightAlpha = ctx.nightFactor * 0.35f;
        p.color.a = pulse * flicker * nightAlpha;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Lantern;
        p.noProjection = zone.noProjection;

        p.position.x = zone.position.x + zone.size.x * 0.5f;
        p.position.y = zone.position.y + zone.size.y * 0.5f;

        p.velocity.x = 0.0f;
        p.velocity.y = 0.0f;

        p.color = glm::vec4(1.0f, 0.85f, 0.6f, 0.5f);

        p.size = std::min(zone.size.x, zone.size.y) * 4.5f;
        p.lifetime = 10.0f + ctx.dist(ctx.rng) * 5.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;

        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Sunshine
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Sunshine>
{
    static constexpr float SpawnRate = 0.8f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Sun & moon rays, yellow during day, blue during night
        float shimmer = 0.95f + 0.05f * std::sin(ctx.time * 1.2f + p.phase);
        float flicker = 0.97f + 0.03f * std::sin(ctx.time * 3.0f + p.phase * 1.5f);

        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.4f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);

        // Interpolate color between golden yellow (day) and pale blue (night)
        float nightBlend = ctx.nightFactor;
        p.color.r = 1.0f * (1.0f - nightBlend) + 0.5f * nightBlend;
        p.color.g = 0.9f * (1.0f - nightBlend) + 0.7f * nightBlend;
        p.color.b = 0.5f * (1.0f - nightBlend) + 1.0f * nightBlend;

        float baseAlpha = 0.16f + (1.0f - ctx.nightFactor) * 0.06f;
        p.color.a = shimmer * flicker * lifeFade * fadeIn * baseAlpha;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        // Helper: Check if a point is covered by a sunshine ray
        auto pointInRay = [](glm::vec2 point, const Particle& ray) -> bool
        {
            float halfWidth = ray.size * 0.5f;
            float halfHeight = ray.size * 2.0f;

            glm::vec2 local = point - ray.position;

            float radians = glm::radians(-ray.rotation);
            float cosR = std::cos(radians);
            float sinR = std::sin(radians);
            glm::vec2 rotated(local.x * cosR - local.y * sinR, local.x * sinR + local.y * cosR);

            return std::abs(rotated.x) <= halfWidth && std::abs(rotated.y) <= halfHeight;
        };

        // Helper: Count how many existing sunshine rays cover a point
        auto countRaysAtPoint = [&](glm::vec2 point) -> int
        {
            int count = 0;
            for (const auto& existing : ctx.particles)
            {
                if (existing.type == ParticleType::Sunshine && pointInRay(point, existing))
                    count++;
            }
            return count;
        };

        // Helper: Check if a candidate ray would create a point with 3+ overlapping rays
        auto wouldOvercrowd = [&](glm::vec2 pos, float rotation, float size) -> bool
        {
            float halfWidth = size * 0.5f;
            float halfHeight = size * 2.0f;
            float radians = glm::radians(rotation);
            float cosR = std::cos(radians);
            float sinR = std::sin(radians);

            const int numSamples = 7;
            for (int i = 0; i < numSamples; i++)
            {
                float t = (i / (float)(numSamples - 1)) - 0.5f;
                float localY = t * halfHeight * 2.0f;

                for (float xOffset : {0.0f, -halfWidth * 0.7f, halfWidth * 0.7f})
                {
                    glm::vec2 sampleWorld(pos.x + xOffset * cosR - localY * sinR,
                                          pos.y + xOffset * sinR + localY * cosR);

                    if (countRaysAtPoint(sampleWorld) >= 2)
                        return true;
                }
            }
            return false;
        };

        // Try to find a valid spawn position (max 3 attempts)
        for (int attempt = 0; attempt < 3; attempt++)
        {
            Particle p;
            p.zoneIndex = zoneIndex;
            p.type = ParticleType::Sunshine;
            p.noProjection = zone.noProjection;
            p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
            p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;

            p.velocity.x = 0.0f;
            p.velocity.y = 0.0f;

            p.color = glm::vec4(1.0f, 0.9f, 0.5f, 0.0f);

            p.size = 40.0f + ctx.dist(ctx.rng) * 24.0f;

            p.lifetime = 5.0f + ctx.dist(ctx.rng) * 4.0f;
            p.maxLifetime = p.lifetime;
            p.phase = ctx.dist(ctx.rng) * 6.28f;

            float baseAngle = (ctx.dist(ctx.rng) < 0.5f) ? -18.0f : 18.0f;
            p.rotation = baseAngle + (ctx.dist(ctx.rng) - 0.5f) * 20.0f;

            p.additive = true;

            if (!wouldOvercrowd(p.position, p.rotation, p.size))
            {
                ctx.particles.push_back(p);
                return;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// DriftingLeaf (ambient cozy: small leaf drifting on prevailing wind)
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::DriftingLeaf>
{
    static constexpr float SpawnRate = 2.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Wind drift along normalised wind direction with gentle Y oscillation.
        const glm::vec2 wind = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);
        p.position += wind * 18.0f * ctx.deltaTime;
        p.position.y += std::sin(ctx.time * 1.4f + p.phase) * 6.0f * ctx.deltaTime;
        p.rotation += 25.0f * ctx.deltaTime;

        // Subtle alpha curve: fade in / fade out over lifetime.
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.8f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.5f);
        p.color.a = fadeIn * lifeFade * ambience::AMBIENT_PARTICLE_ALPHA_CAP;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::DriftingLeaf;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = ctx.dist(ctx.rng) * 360.0f;
        float leafChoice = ctx.dist(ctx.rng);
        if (leafChoice < 0.10f)
        {
            p.color = glm::vec4(0.35f + ctx.dist(ctx.rng) * 0.20f,
                                0.65f + ctx.dist(ctx.rng) * 0.25f,
                                0.20f + ctx.dist(ctx.rng) * 0.20f,
                                0.0f);  // Green (fresh)
        }
        else if (leafChoice < 0.28f)
        {
            p.color = glm::vec4(0.50f + ctx.dist(ctx.rng) * 0.20f,
                                0.25f + ctx.dist(ctx.rng) * 0.20f,
                                0.10f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Brown (dead)
        }
        else if (leafChoice < 0.48f)
        {
            p.color = glm::vec4(0.85f + ctx.dist(ctx.rng) * 0.15f,
                                0.65f + ctx.dist(ctx.rng) * 0.20f,
                                0.20f + ctx.dist(ctx.rng) * 0.20f,
                                0.0f);  // Gold (autumn)
        }
        else if (leafChoice < 0.63f)
        {
            p.color = glm::vec4(0.85f + ctx.dist(ctx.rng) * 0.15f,
                                0.25f + ctx.dist(ctx.rng) * 0.20f,
                                0.15f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Red (maple)
        }
        else if (leafChoice < 0.71f)
        {
            p.color = glm::vec4(0.60f + ctx.dist(ctx.rng) * 0.15f,
                                0.85f + ctx.dist(ctx.rng) * 0.15f,
                                0.30f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Yellow-green
        }
        else if (leafChoice < 0.84f)
        {
            p.color = glm::vec4(0.95f + ctx.dist(ctx.rng) * 0.05f,
                                0.50f + ctx.dist(ctx.rng) * 0.15f,
                                0.15f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Orange (pumpkin)
        }
        else if (leafChoice < 0.93f)
        {
            p.color = glm::vec4(0.55f + ctx.dist(ctx.rng) * 0.20f,
                                0.15f + ctx.dist(ctx.rng) * 0.15f,
                                0.20f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Burgundy (oak)
        }
        else
        {
            p.color = glm::vec4(0.75f + ctx.dist(ctx.rng) * 0.15f,
                                0.45f + ctx.dist(ctx.rng) * 0.15f,
                                0.15f + ctx.dist(ctx.rng) * 0.10f,
                                0.0f);  // Amber (copper)
        }
        p.size = 4.5f + ctx.dist(ctx.rng) * 2.5f;
        p.lifetime = 10.0f + ctx.dist(ctx.rng) * 5.0f;
        p.maxLifetime = p.lifetime;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// DustMote (ambient cozy: tiny golden mote in sunbeams)
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::DustMote>
{
    static constexpr float SpawnRate = 3.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Slow vertical rise/fall + small horizontal jitter.
        p.position.y += std::sin(ctx.time * 0.6f + p.phase) * 4.0f * ctx.deltaTime;
        p.position.x += std::cos(ctx.time * 0.4f + p.phase * 1.3f) * 3.0f * ctx.deltaTime;

        float twinkle = 0.7f + 0.3f * std::sin(ctx.time * 2.5f + p.phase);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.6f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = fadeIn * lifeFade * twinkle * ambience::AMBIENT_PARTICLE_ALPHA_CAP * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::DustMote;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = ctx.dist(ctx.rng) * 360.0f;
        // DustMote is dust caught in light - strictly neutral. R == G == B for
        // every spawn so the palette never drifts into colored territory.
        // Spread across white/light-grey/mid-grey buckets; pure-black is
        // skipped because additive blending would render it invisible.
        const float greyChoice = ctx.dist(ctx.rng);
        float grey;
        if (greyChoice < 0.45f)
        {
            grey = 0.92f + ctx.dist(ctx.rng) * 0.08f;  // Bright white
        }
        else if (greyChoice < 0.80f)
        {
            grey = 0.65f + ctx.dist(ctx.rng) * 0.15f;  // Light grey
        }
        else
        {
            grey = 0.40f + ctx.dist(ctx.rng) * 0.15f;  // Mid grey (dim mote)
        }
        p.color = glm::vec4(grey, grey, grey, 0.0f);
        p.size = 3.0f + ctx.dist(ctx.rng) * 1.5f;
        p.lifetime = 6.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Pollen (ambient cozy: yellow drift during golden hour)
// ---------------------------------------------------------------------------

template <>
struct ParticleBehavior<ParticleType::Pollen>
{
    static constexpr float SpawnRate = 2.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Horizontal drift on wind, very gentle vertical sway.
        const glm::vec2 wind = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);
        p.position += wind * 8.0f * ctx.deltaTime;
        p.position.y += std::sin(ctx.time * 0.9f + p.phase) * 3.0f * ctx.deltaTime;

        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.7f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.2f);
        p.color.a = fadeIn * lifeFade * ambience::AMBIENT_PARTICLE_ALPHA_CAP * 0.9f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Pollen;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = ctx.dist(ctx.rng) * 360.0f;
        // Pollen is the chromatic ambient palette - the white/grey range is
        // owned by DustMote, so dandelion white is intentionally absent here.
        float speciesChoice = ctx.dist(ctx.rng);
        if (speciesChoice < 0.26f)
        {
            p.color = glm::vec4(1.0f,
                                0.95f + ctx.dist(ctx.rng) * 0.05f,
                                0.50f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Yellow
        }
        else if (speciesChoice < 0.48f)
        {
            p.color = glm::vec4(1.0f,
                                0.70f + ctx.dist(ctx.rng) * 0.15f,
                                0.80f + ctx.dist(ctx.rng) * 0.10f,
                                0.0f);  // Pink (cherry blossom)
        }
        else if (speciesChoice < 0.62f)
        {
            p.color = glm::vec4(0.80f + ctx.dist(ctx.rng) * 0.15f,
                                1.0f,
                                0.65f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Pale green
        }
        else if (speciesChoice < 0.74f)
        {
            p.color = glm::vec4(0.85f + ctx.dist(ctx.rng) * 0.10f,
                                0.70f + ctx.dist(ctx.rng) * 0.15f,
                                1.0f,
                                0.0f);  // Lavender
        }
        else if (speciesChoice < 0.86f)
        {
            p.color = glm::vec4(1.0f,
                                0.65f + ctx.dist(ctx.rng) * 0.10f,
                                0.30f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Orange (marigold)
        }
        else if (speciesChoice < 0.94f)
        {
            p.color = glm::vec4(1.0f,
                                0.55f + ctx.dist(ctx.rng) * 0.15f,
                                0.55f + ctx.dist(ctx.rng) * 0.15f,
                                0.0f);  // Coral
        }
        else
        {
            p.color = glm::vec4(1.0f,
                                0.35f + ctx.dist(ctx.rng) * 0.15f,
                                0.85f + ctx.dist(ctx.rng) * 0.10f,
                                0.0f);  // Magenta
        }
        p.size = 3.0f + ctx.dist(ctx.rng) * 1.5f;
        p.lifetime = 7.0f + ctx.dist(ctx.rng) * 5.0f;
        p.maxLifetime = p.lifetime;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// ---------------------------------------------------------------------------
// Dispatch tables - auto-generated from ParticleBehavior specializations
// ---------------------------------------------------------------------------

using UpdateFn = void (*)(Particle&, const ParticleUpdateContext&);
using SpawnFn = void (*)(int, const ParticleZone&, ParticleSpawnContext&);

namespace
{

template <size_t... Is>
constexpr auto MakeSpawnRateTable(std::index_sequence<Is...>)
{
    return std::array<float, sizeof...(Is)>{
        ParticleBehavior<static_cast<ParticleType>(Is)>::SpawnRate...};
}

template <size_t... Is>
auto MakeUpdateTable(std::index_sequence<Is...>)
{
    return std::array<UpdateFn, sizeof...(Is)>{
        &ParticleBehavior<static_cast<ParticleType>(Is)>::Update...};
}

template <size_t... Is>
auto MakeSpawnTable(std::index_sequence<Is...>)
{
    return std::array<SpawnFn, sizeof...(Is)>{
        &ParticleBehavior<static_cast<ParticleType>(Is)>::Spawn...};
}

using Indices = std::make_index_sequence<EnumTraits<ParticleType>::Count>;

constexpr auto kSpawnRates = MakeSpawnRateTable(Indices{});
const auto kUpdateDispatch = MakeUpdateTable(Indices{});
const auto kSpawnDispatch = MakeSpawnTable(Indices{});

}  // namespace

// ---------------------------------------------------------------------------
// ParticleSystem implementation
// ---------------------------------------------------------------------------

ParticleSystem::ParticleSystem()
    : m_Zones(nullptr),    // Particle zones from tilemap
      m_Tilemap(nullptr),  // Reference to tilemap for structure queries
      m_TileWidth(32),     // Tile dimensions for coordinate conversion
      m_TileHeight(32),
      m_MaxParticlesPerZone(25),      // Particle density cap per zone
      m_Time(0.0f),                   // Accumulated time for animation cycles
      m_NightFactor(0.0f),            // Day & night blend (0 = day, 1 = night)
      m_Rng(std::random_device{}()),  // Seeded Mersenne Twister RNG
      m_Dist01(0.0f, 1.0f),           // Uniform distribution for random values
      m_TexturesLoaded(false)         // Lazy-load flag for particle sprites
{
    m_Particles.reserve(500);  // Pre-allocate to reduce reallocations
}

bool ParticleSystem::LoadTextures()
{
    BuildAtlas();
    m_TexturesLoaded = true;
    return true;
}

void ParticleSystem::UploadTextures(IRenderer& renderer)
{
    if (!m_TexturesLoaded)
        return;

    renderer.UploadTexture(m_AtlasTexture);
}

void ParticleSystem::BuildAtlas()
{
    // Particle texture sources: 9 files + 2 procedural, packed into a 512-wide
    // atlas with a simple row layout. Indexed by ParticleType enum value.

    struct TextureSource
    {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };

    constexpr int kAtlasSourceCount = static_cast<int>(EnumTraits<ParticleType>::Count);
    TextureSource sources[kAtlasSourceCount];
    const char* filePaths[6] = {
        "assets/particles/304502d7-426b-4abc-a608-ff01a185df96.png",  // Firefly
        "assets/particles/9509e404-2fce-4fbf-a082-720f85e7244e.png",  // Rain
        "assets/particles/6f9d2bcf-8e79-493f-b468-85040a945d06.png",  // Snow
        "assets/particles/14b6ffec-3289-417b-b99c-82d1ed2a9944.png",  // Fog
        "assets/particles/536fa219-58a1-4220-9171-a8520d126f44.png",  // Sparkles
        "assets/particles/ead11602-6c24-45dc-b657-03d637e2a543.png"   // Wisp
    };

    // Load file-based textures temporarily to get their pixel data.
    // All sources are normalized to RGBA (4 channels) so the atlas copy
    // loop can safely read 4 bytes per pixel regardless of the original format.
    auto loadPng = [](const char* path, TextureSource& src)
    {
        Texture temp;
        if (temp.LoadFromFile(path))
        {
            src.width = temp.GetWidth();
            src.height = temp.GetHeight();
            int channels = temp.GetChannels();
            size_t pixelCount =
                static_cast<size_t>(temp.GetWidth()) * static_cast<size_t>(temp.GetHeight());

            if (channels == 4)
            {
                // Already RGBA -- straight copy.
                size_t dataSize = pixelCount * 4;
                src.pixels.resize(dataSize);
                if (!temp.GetImageData().empty())
                {
                    memcpy(src.pixels.data(), temp.GetImageData().data(), dataSize);
                }
                return;
            }
            if (channels == 3 && !temp.GetImageData().empty())
            {
                // RGB -> RGBA: expand each pixel, setting alpha to 255.
                src.pixels.resize(pixelCount * 4);
                const unsigned char* srcPx = temp.GetImageData().data();
                unsigned char* dst = src.pixels.data();
                for (size_t px = 0; px < pixelCount; ++px)
                {
                    dst[px * 4 + 0] = srcPx[px * 3 + 0];
                    dst[px * 4 + 1] = srcPx[px * 3 + 1];
                    dst[px * 4 + 2] = srcPx[px * 3 + 2];
                    dst[px * 4 + 3] = 255;
                }
                return;
            }
        }
        // Fallback: 16x16 white texture (also covers unsupported channel counts).
        src.width = 16;
        src.height = 16;
        src.pixels.assign(16 * 16 * 4, 255);
    };

    for (int i = 0; i < 6; i++)
    {
        loadPng(filePaths[i], sources[i]);
    }

    // Generate procedural textures
    GenerateLanternPixels(sources[6].pixels, sources[6].width, sources[6].height);
    GenerateSunshinePixels(sources[7].pixels, sources[7].width, sources[7].height);

    // Ambient decorative particles get dedicated atlas slots so each has a
    // distinct visual identity instead of borrowing from Snow / Sparkles.
    const char* ambientFilePaths[3] = {
        "assets/particles/9f7690be-3cc2-4a2c-8941-610dd427ec66.png",  // DriftingLeaf
        "assets/particles/0fe573b0-b024-42aa-93dc-7d17e2758c8e.png",  // DustMote
        "assets/particles/e5c27507-e3bd-4d30-b2ae-add5f2843f80.png"   // Pollen
    };
    for (int i = 0; i < 3; i++)
    {
        loadPng(ambientFilePaths[i], sources[static_cast<int>(ParticleType::DriftingLeaf) + i]);
    }

    // Calculate atlas layout - simple horizontal packing with rows.
    // Pre-scan texture sizes to compute required atlas height so the
    // atlas is always tall enough for all particle textures.
    const int atlasWidth = 512;
    int requiredHeight = 0;
    {
        int scanX = 0;
        int scanRowHeight = 0;
        for (int i = 0; i < kAtlasSourceCount; i++)
        {
            int w = sources[i].width;
            int h = sources[i].height;
            if (scanX + w > atlasWidth)
            {
                scanX = 0;
                requiredHeight += scanRowHeight + 1;
                scanRowHeight = 0;
            }
            scanX += w + 1;
            if (h > scanRowHeight)
            {
                scanRowHeight = h;
            }
        }
        requiredHeight += scanRowHeight;
    }
    const int atlasHeight = std::max(512, requiredHeight);
    std::vector<unsigned char> atlasPixels(atlasWidth * atlasHeight * 4, 0);

    int currentX = 0;
    int currentY = 0;
    int rowHeight = 0;

    for (int i = 0; i < kAtlasSourceCount; i++)
    {
        int w = sources[i].width;
        int h = sources[i].height;

        // Move to next row if needed
        if (currentX + w > atlasWidth)
        {
            currentX = 0;
            currentY += rowHeight + 1;  // 1px padding
            rowHeight = 0;
        }

        // Guard against atlas overflow -- skip textures that don't fit.
        if (currentY + h > atlasHeight)
        {
            Logger::ErrorF(LOG_SUBSYSTEM,
                           "Atlas overflow: texture {} ({}x{}) does not fit at row {} (atlas "
                           "height={})",
                           i,
                           w,
                           h,
                           currentY,
                           atlasHeight);
            // Store degenerate UV region so this type renders as a small corner pixel.
            m_AtlasRegions[i].uvMin = glm::vec2(0.0f);
            m_AtlasRegions[i].uvMax = glm::vec2(1.0f / atlasWidth, 1.0f / atlasHeight);
            continue;
        }

        // Store UV coordinates (normalized)
        m_AtlasRegions[i].uvMin = glm::vec2(static_cast<float>(currentX) / atlasWidth,
                                            static_cast<float>(currentY) / atlasHeight);
        m_AtlasRegions[i].uvMax = glm::vec2(static_cast<float>(currentX + w) / atlasWidth,
                                            static_cast<float>(currentY + h) / atlasHeight);

        // Copy pixels to atlas (source already flipped by stbi for OpenGL)
        for (int y = 0; y < h; y++)
        {
            int srcY = y;
            int dstY = currentY + y;
            if (dstY >= atlasHeight)
                continue;

            for (int x = 0; x < w; x++)
            {
                int dstX = currentX + x;
                if (dstX >= atlasWidth)
                    continue;

                int srcIdx = (srcY * w + x) * 4;
                int dstIdx = (dstY * atlasWidth + dstX) * 4;

                if (srcIdx + 3 < static_cast<int>(sources[i].pixels.size()))
                {
                    atlasPixels[dstIdx + 0] = sources[i].pixels[srcIdx + 0];
                    atlasPixels[dstIdx + 1] = sources[i].pixels[srcIdx + 1];
                    atlasPixels[dstIdx + 2] = sources[i].pixels[srcIdx + 2];
                    atlasPixels[dstIdx + 3] = sources[i].pixels[srcIdx + 3];
                }
            }
        }

        currentX += w + 1;  // 1px padding
        rowHeight = std::max(rowHeight, h);
    }

    // Create the atlas texture
    m_AtlasTexture.LoadFromData(atlasPixels.data(), atlasWidth, atlasHeight, 4, false);

    Logger::InfoF(LOG_SUBSYSTEM, "Atlas built: {}x{}", atlasWidth, atlasHeight);
}

void ParticleSystem::GenerateLanternPixels(std::vector<unsigned char>& pixels,
                                           int& width,
                                           int& height)
{
    width = 256;
    height = 256;
    GeneratePixels(pixels,
                   width,
                   height,
                   [](int x, int y, int w, int) -> Pixel
                   {
                       float center = w / 2.0f;
                       float dx = x - center;
                       float dy = y - center;
                       float dist = std::sqrt(dx * dx + dy * dy) / center;

                       float alpha = std::exp(-dist * dist * 1.2f);
                       float centerReduction = std::exp(-dist * dist * 8.0f) * 0.3f;
                       alpha = alpha * (1.0f - centerReduction);

                       if (dist > 0.6f)
                       {
                           float outerFade = 1.0f - (dist - 0.6f) / 0.4f;
                           outerFade = std::max(0.0f, outerFade);
                           outerFade = std::pow(outerFade, 0.4f);
                           alpha *= outerFade;
                       }

                       return {255,
                               static_cast<uint8_t>(220 + alpha * 35),
                               static_cast<uint8_t>(160 + alpha * 50),
                               static_cast<uint8_t>(alpha * 120)};
                   });
}

void ParticleSystem::GenerateSunshinePixels(std::vector<unsigned char>& pixels,
                                            int& width,
                                            int& height)
{
    width = 48;
    height = 192;
    GeneratePixels(pixels,
                   width,
                   height,
                   [](int x, int y, int w, int h) -> Pixel
                   {
                       float centerX = w / 2.0f;
                       float dx = std::abs(x - centerX) / centerX;
                       float dy = static_cast<float>(y) / static_cast<float>(h);

                       float beamWidth = 0.2f + dy * 0.55f;
                       float horizontalFalloff = 1.0f - std::min(1.0f, dx / beamWidth);
                       horizontalFalloff = std::pow(horizontalFalloff, 1.2f);
                       horizontalFalloff *= std::exp(-dx * dx * 1.5f);

                       float topFeather = std::min(1.0f, dy / 0.30f);
                       topFeather = std::pow(topFeather, 2.0f);
                       float bottomFeather = std::min(1.0f, (1.0f - dy) / 0.30f);
                       bottomFeather = std::pow(bottomFeather, 2.0f);

                       float verticalIntensity = 0.5f + 0.5f * std::sin(dy * rift::PiF);
                       float beamAlpha =
                           horizontalFalloff * verticalIntensity * topFeather * bottomFeather;

                       float groundGlowY = 1.0f - std::abs(dy - 0.78f) / 0.15f;
                       groundGlowY = std::max(0.0f, groundGlowY);
                       float groundGlowX = std::exp(-dx * dx * 1.5f);
                       float groundGlow = groundGlowY * groundGlowX * 0.35f * bottomFeather;

                       float alpha = std::min(1.0f, beamAlpha + groundGlow);

                       return {255, 255, 255, static_cast<uint8_t>(alpha * 140)};
                   });
}

void ParticleSystem::Update(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize)
{
    m_Time += deltaTime;
    const bool hasZones = (m_Zones && !m_Zones->empty());

    // Ensure we have enough spawn timers
    if (hasZones && m_ZoneSpawnTimers.size() < m_Zones->size())
    {
        m_ZoneSpawnTimers.resize(m_Zones->size(), 0.0f);
    }

    const ParticleUpdateContext updateCtx{m_Time, deltaTime, m_NightFactor, m_Zones, hasZones};

    // Update existing particles (mark dead ones, remove in bulk afterward)
    for (auto& p : m_Particles)
    {
        // Decrease lifetime
        p.lifetime -= deltaTime;
        if (p.lifetime <= 0.0f)
        {
            continue;
        }

        // Kill particles whose zone has gone away (e.g., map reloaded, zone
        // deleted). zoneIndex == -1 means "deliberately zoneless" - ambient
        // cozy spawns and the console's particle.spawn - and must be left
        // alone so it lives out its natural lifetime.
        if (p.zoneIndex >= 0 && (!hasZones || p.zoneIndex >= static_cast<int>(m_Zones->size())))
        {
            p.lifetime = 0.0f;
            continue;
        }

        // Update position
        p.position += p.velocity * deltaTime;

        // Dispatch to type-specific update via table (bounds-checked)
        int typeIndex = static_cast<int>(p.type);
        if (typeIndex >= 0 && typeIndex < static_cast<int>(kUpdateDispatch.size()))
        {
            kUpdateDispatch[typeIndex](p, updateCtx);
        }
        else
        {
            p.lifetime = 0.0f;  // Kill particle with invalid type
        }
    }

    // Remove dead and orphaned particles in one pass
    std::erase_if(m_Particles, [](const Particle& p) { return p.lifetime <= 0.0f; });

    if (hasZones)
    {
        // Build per-zone particle counts in a single O(n) pass
        m_ZoneParticleCounts.assign(m_Zones->size(), 0);
        for (const auto& p : m_Particles)
        {
            if (p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(m_ZoneParticleCounts.size()))
            {
                m_ZoneParticleCounts[p.zoneIndex]++;
            }
        }
    }

    // Maintain global ambient population (independent of zones).
    UpdateAmbientSpawning(deltaTime, cameraPos, viewSize);

    if (!hasZones)
    {
        return;
    }

    // Spawn new particles for each zone
    for (size_t i = 0; i < m_Zones->size(); ++i)
    {
        const ParticleZone& zone = (*m_Zones)[i];
        if (!zone.enabled)
        {
            continue;
        }

        // Check if zone is visible in current view
        float margin = 80.0f;  // 5 tiles of margin to spawn offscreen
        bool visible = !(zone.position.x + zone.size.x < cameraPos.x - margin ||
                         zone.position.x > cameraPos.x + viewSize.x + margin ||
                         zone.position.y + zone.size.y < cameraPos.y - margin ||
                         zone.position.y > cameraPos.y + viewSize.y + margin);

        if (!visible)
        {
            continue;
        }

        // Skip spawning lantern glows during daytime to avoid flicker
        if (zone.type == ParticleType::Lantern && m_NightFactor < 0.05f)
        {
            continue;
        }

        size_t zoneParticleCount = m_ZoneParticleCounts[i];

        // Spawn rate from dispatch table (bounds-checked)
        int zoneTypeIndex = static_cast<int>(zone.type);
        if (zoneTypeIndex < 0 || zoneTypeIndex >= static_cast<int>(kSpawnRates.size()))
        {
            continue;
        }
        float spawnRate = kSpawnRates[zoneTypeIndex];

        // Scale spawn rate by zone size
        float areaFactor = (zone.size.x * zone.size.y) / (64.0f * 64.0f);
        spawnRate *= std::max(0.5f, std::min(3.0f, areaFactor));

        m_ZoneSpawnTimers[i] += deltaTime;
        float spawnInterval = 1.0f / spawnRate;

        while (m_ZoneSpawnTimers[i] >= spawnInterval && zoneParticleCount < m_MaxParticlesPerZone)
        {
            m_ZoneSpawnTimers[i] -= spawnInterval;
            SpawnParticleInZone(static_cast<int>(i), zone);
            zoneParticleCount++;
        }
    }
}

void ParticleSystem::SpawnParticleInZone(int zoneIndex, const ParticleZone& zone)
{
    int typeIndex = static_cast<int>(zone.type);
    if (typeIndex < 0 || typeIndex >= static_cast<int>(kSpawnDispatch.size()))
    {
        return;  // Invalid particle type -- skip silently.
    }
    ParticleSpawnContext ctx{m_Rng, m_Dist01, m_Particles};
    kSpawnDispatch[typeIndex](zoneIndex, zone, ctx);
}

namespace
{
/// Returns a smoothstep ramp peaking near `center` (in 0-24h time).
/// Width controls the half-width of the bump.
float TimeOfDayBump(float timeOfDay, float center, float width)
{
    // Wrap-aware shortest distance on a 24h circle.
    float diff = std::abs(timeOfDay - center);
    if (diff > 12.0f)
    {
        diff = 24.0f - diff;
    }
    if (diff >= width)
    {
        return 0.0f;
    }
    float t = 1.0f - diff / width;
    return t * t * (3.0f - 2.0f * t);  // smoothstep
}
}  // namespace

void ParticleSystem::UpdateAmbientSpawning(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize)
{
    // Count current ambient particles (per type and total).
    int totalAmbient = 0;
    int countLeaf = 0, countDust = 0, countPollen = 0;
    for (const auto& p : m_Particles)
    {
        switch (p.type)
        {
            case ParticleType::DriftingLeaf:
                ++countLeaf;
                ++totalAmbient;
                break;
            case ParticleType::DustMote:
                ++countDust;
                ++totalAmbient;
                break;
            case ParticleType::Pollen:
                ++countPollen;
                ++totalAmbient;
                break;
            default:
                break;
        }
    }
    if (totalAmbient >= ambience::AMBIENT_PARTICLE_TOTAL_CAP)
    {
        return;
    }

    // Time-of-day biasing. Each curve peaks at 1.0 at the named hour.
    // Leaves: any daylight (peak midday, half-strength dawn/dusk).
    // Dust motes: dawn/midday sunbeams.
    // Pollen: golden hour only (dawn ~6h or dusk ~19h).
    float leafBias = TimeOfDayBump(m_TimeOfDay, 13.0f, 8.0f);
    float dustBias =
        std::max(TimeOfDayBump(m_TimeOfDay, 6.5f, 2.5f), TimeOfDayBump(m_TimeOfDay, 12.0f, 4.0f));
    float pollenBias =
        std::max(TimeOfDayBump(m_TimeOfDay, 6.5f, 1.5f), TimeOfDayBump(m_TimeOfDay, 19.0f, 1.5f));

    auto tickType = [&](ParticleType type, float ratePerSec, float bias)
    {
        int idx = static_cast<int>(type);
        m_AmbientSpawnTimers[idx] += deltaTime * std::max(0.0f, bias);
        float interval = (ratePerSec > 0.0f) ? (1.0f / ratePerSec) : 1e9f;
        while (m_AmbientSpawnTimers[idx] >= interval &&
               totalAmbient < ambience::AMBIENT_PARTICLE_TOTAL_CAP)
        {
            m_AmbientSpawnTimers[idx] -= interval;
            SpawnAmbientParticle(type, cameraPos, viewSize);
            ++totalAmbient;
        }
    };

    tickType(ParticleType::DriftingLeaf, ambience::AMBIENT_LEAF_SPAWN_PER_SEC, leafBias);
    tickType(ParticleType::DustMote, ambience::AMBIENT_DUST_SPAWN_PER_SEC, dustBias);
    tickType(ParticleType::Pollen, ambience::AMBIENT_POLLEN_SPAWN_PER_SEC, pollenBias);

    (void)countLeaf;
    (void)countDust;
    (void)countPollen;
}

void ParticleSystem::SpawnAmbientParticle(ParticleType type,
                                          glm::vec2 cameraPos,
                                          glm::vec2 viewSize)
{
    // Build a fake camera-rect zone so the per-type Spawn function (single
    // source of truth) handles all the type-specific initialization.
    // zoneIndex = -1 marks the particle as ambient (exempt from zone-orphan
    // cleanup and uncounted by per-zone caps).
    const float margin = ambience::AMBIENT_PARTICLE_SPAWN_MARGIN;
    ParticleZone fakeZone;
    fakeZone.position = cameraPos - glm::vec2(margin);
    fakeZone.size = viewSize + glm::vec2(margin * 2.0f);
    fakeZone.type = type;
    fakeZone.enabled = true;
    fakeZone.noProjection = false;
    SpawnParticleInZone(-1, fakeZone);
}

void ParticleSystem::SpawnOne(ParticleType type, glm::vec2 worldPos)
{
    // 1x1 ad-hoc zone at the requested world position; the per-type spawn
    // initialiser samples a position inside the zone so the particle lands
    // (within sub-pixel jitter) on @p worldPos. zoneIndex = -1 keeps it out
    // of the orphan-cleanup pass when zones get added or removed later.
    ParticleZone fakeZone;
    fakeZone.position = worldPos;
    fakeZone.size = glm::vec2(1.0f, 1.0f);
    fakeZone.type = type;
    fakeZone.enabled = true;
    fakeZone.noProjection = false;
    SpawnParticleInZone(-1, fakeZone);
}

void ParticleSystem::Render(IRenderer& renderer,
                            glm::vec2 cameraPos,
                            bool noProjectionOnly,
                            bool renderAll)
{
    // For noProjection particles, we need to:
    // 1. Calculate positions while perspective is enabled
    // 2. Suspend perspective
    // 3. Draw at calculated positions
    // 4. Resume perspective

    m_NoProjectionBatch.clear();
    m_RegularBatch.clear();

    // First pass: Calculate all positions (ProjectPoint works while perspective enabled)
    for (const Particle& p : m_Particles)
    {
        bool isNoProjection = false;
        if (m_Zones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(m_Zones->size()))
        {
            isNoProjection = (*m_Zones)[p.zoneIndex].noProjection;
        }

        // Filter particles based on noProjection flag
        if (!renderAll)
        {
            if (noProjectionOnly && !isNoProjection)
                continue;
            if (!noProjectionOnly && isNoProjection)
                continue;
        }

        ParticleRenderData data;
        data.size = glm::vec2(p.size, p.size);
        data.color = p.color;
        data.rotation = p.rotation;
        data.phase = p.phase;
        data.additive = p.additive;
        data.type = p.type;

        // Convert world position to screen position
        data.screenPos = p.position - cameraPos;
        glm::vec2 rawScreenPos = data.screenPos;

        // Get perspective state for viewport checking
        auto perspState = renderer.GetPerspectiveState();

        if (isNoProjection)
        {
            bool projectedOnStructure = false;
            if (m_Tilemap)
            {
                glm::vec2 structureScreenPos;
                projectedOnStructure = m_Tilemap->ProjectNoProjectionStructurePoint(
                    renderer, p.position, cameraPos, structureScreenPos);
                if (projectedOnStructure)
                {
                    data.screenPos = structureScreenPos;
                }
            }

            if (!projectedOnStructure)
            {
                bool inViewport = renderer.IsPointInExpandedViewport(data.screenPos);
                if (inViewport)
                {
                    data.screenPos = renderer.ProjectPoint(data.screenPos);
                }
            }

            if (renderer.IsPointBehindSphere(rawScreenPos) ||
                renderer.IsPointBehindSphere(data.screenPos))
                continue;
            m_NoProjectionBatch.push_back(data);
        }
        else
        {
            // Cull regular particles that are outside viewport or behind sphere
            // Use generous padding to account for particle size and partial visibility
            float padding = std::max(data.size.x, data.size.y) * 2.0f + 50.0f;

            bool outsideViewport =
                data.screenPos.x < -padding || data.screenPos.x > perspState.viewWidth + padding ||
                data.screenPos.y < -padding || data.screenPos.y > perspState.viewHeight + padding;

            if (outsideViewport)
                continue;

            // Check if particle is behind the sphere (only when globe/fisheye is enabled)
            if (renderer.IsPointBehindSphere(data.screenPos))
                continue;

            m_RegularBatch.push_back(data);
        }
    }

    // Lambda to draw a particle using the texture atlas
    auto drawParticle = [&](const ParticleRenderData& data)
    {
        if (m_TexturesLoaded)
        {
            int typeIndex = static_cast<int>(data.type);
            if (typeIndex < 0 || typeIndex >= static_cast<int>(std::size(m_AtlasRegions)))
                return;
            const AtlasRegion& region = m_AtlasRegions[typeIndex];

            glm::vec2 renderSize = data.size;
            // Sunshine uses elongated beam texture (48x192 aspect ratio = 1:4)
            if (data.type == ParticleType::Sunshine)
            {
                renderSize = glm::vec2(data.size.x, data.size.x * 4.0f);
            }
            // Rain uses stretched vertical texture with per-droplet variation
            else if (data.type == ParticleType::Rain)
            {
                // Vary stretch between 1.0x and 1.4x based on particle phase
                float stretch = 1.0f + 0.4f * (std::sin(data.phase) * 0.5f + 0.5f);
                renderSize = glm::vec2(data.size.x, data.size.x * stretch);
            }
            // Snow flips like a coin
            else if (data.type == ParticleType::Snow)
            {
                float flipScale = std::cos(m_Time * 3.0f + data.phase);
                renderSize.x *= flipScale;
            }
            glm::vec2 centeredPos = data.screenPos - renderSize * 0.5f;
            renderer.DrawSpriteAtlas(m_AtlasTexture,
                                     centeredPos,
                                     renderSize,
                                     region.uvMin,
                                     region.uvMax,
                                     data.rotation,
                                     data.color,
                                     data.additive);
        }
        else
        {
            glm::vec2 size = data.size;
            if (data.type == ParticleType::Rain)
                size = glm::vec2(1.0f, 8.0f);
            renderer.DrawColoredRect(data.screenPos, size, data.color, data.additive);
        }
    };

    // Sort batches by blend mode to minimize draw calls
    // Non-additive (false) sorts before additive (true)
    auto sortByBlendMode = [](const ParticleRenderData& a, const ParticleRenderData& b)
    { return a.additive < b.additive; };

    // Partition by blend mode (O(n)) instead of sorting (O(n log n)).
    // Non-additive particles come first, then additive ones.
    std::partition(m_NoProjectionBatch.begin(),
                   m_NoProjectionBatch.end(),
                   [](const ParticleRenderData& d) { return !d.additive; });
    std::partition(m_RegularBatch.begin(),
                   m_RegularBatch.end(),
                   [](const ParticleRenderData& d) { return !d.additive; });

    // Draw noProjection particles with perspective suspended
    if (!m_NoProjectionBatch.empty())
    {
        IRenderer::PerspectiveSuspendGuard guard(renderer);
        for (const auto& data : m_NoProjectionBatch)
        {
            drawParticle(data);
        }
    }

    // Draw regular particles normally
    for (const auto& data : m_RegularBatch)
    {
        drawParticle(data);
    }
}

void ParticleSystem::OnZoneRemoved(int zoneIndex)
{
    if (zoneIndex < 0)
    {
        return;
    }

    // Remove particles from the deleted zone in one pass
    std::erase_if(m_Particles, [zoneIndex](const Particle& p) { return p.zoneIndex == zoneIndex; });

    // Adjust indices for particles from higher-indexed zones
    for (auto& p : m_Particles)
    {
        if (p.zoneIndex > zoneIndex)
        {
            p.zoneIndex--;
        }
    }

    if (zoneIndex < static_cast<int>(m_ZoneSpawnTimers.size()))
    {
        m_ZoneSpawnTimers.erase(m_ZoneSpawnTimers.begin() + zoneIndex);
    }
}
