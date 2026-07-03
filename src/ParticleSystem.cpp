#include "ParticleSystem.hpp"

#include "AmbienceConfig.hpp"
#include "Logger.hpp"
#include "MathConstants.hpp"
#include "ProceduralTexture.hpp"
#include "TextureStore.hpp"
#include "Tilemap.hpp"
#include "WeatherBlend.hpp"
#include "WeatherDefinitions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <tuple>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Particle";
}  // namespace

// Particle behavior dispatch.

// Context passed to per-type Update specializations.
struct ParticleUpdateContext
{
    float time;
    float deltaTime;
    float nightFactor;
    const std::vector<ParticleZone>* zones;
    bool hasZones;
    // Per-weather scaling on Fog particle base alpha. 1.0 = unchanged;
    // values < 1 soften the fog wall without changing density.
    float fogAlphaMultiplier;
    // Smoothed camera velocity (world px/s). Read by Pollen / DriftingLeaf
    // to gate the player-avoidance push on "player is currently walking".
    glm::vec2 cameraVelocity;
    // Current camera position (world px); kept here so weather-particle
    // spawn helpers can read it from the same context as the avoidance code.
    glm::vec2 cameraPos;
    // Visible camera area in world pixels. Used by Rain/Snow update paths
    // to clamp groundY into the viewport when an editor zone is larger
    // than the visible region (e.g., the title screen's whole-map zones -
    // without the clamp, splashes happen off-screen below).
    glm::vec2 viewSize;
    // Bottom-center of the player's 16x32 avoidance box (player hitbox +
    // the tile directly above). Pollen and DriftingLeaf use this box as
    // the repulsion source so particles slide around the actual player
    // rectangle rather than around the bottom-center anchor.
    glm::vec2 playerPos;
    // Out-buffer for particles that an Update wants to spawn mid-frame
    // (e.g., Rain splashes). Direct append to m_Particles during the
    // update loop would reallocate the vector and invalidate iterators,
    // so behaviors push here and ParticleSystem::Update merges after.
    std::vector<Particle>* pendingSpawns;
    // Shared RNG (same source as ParticleSpawnContext) so any randomized
    // per-frame behaviors (e.g., Rain splash count + droplet jitter) draw
    // from the same deterministic stream as the Spawn dispatchers.
    std::mt19937* rng;
    std::uniform_real_distribution<float>* dist;
    // Prevailing wind direction (normalized) and gusted strength (>= 0;
    // 0.5 = calm default). Mirrors ParticleSystem::m_WindDir/m_WindStrength.
    glm::vec2 windDir;
    float windStrength;
    // Exact per-frame camera displacement (world px). Weather Rain/Snow
    // re-base their bakedGroundY band by this so the splash band keeps its
    // screen-relative position instead of lagging a moving camera.
    glm::vec2 cameraDelta;
};

// Context passed to per-type Spawn specializations.
struct ParticleSpawnContext
{
    std::mt19937& rng;
    std::uniform_real_distribution<float>& dist;
    std::vector<Particle>& particles;
    glm::vec2 windDir;   // Prevailing wind direction (normalized).
    float windStrength;  // Gusted wind strength (>= 0; 0.5 = calm default).
};

// Slip a weather particle around the player's 16x32 hitbox by nudging only
// its POSITION outward while overlapping (velocity/wind drift is untouched,
// so its trajectory is preserved). The nudge is capped at the penetration
// depth so it never overshoots, and only fires while the player is moving.
inline void ApplyPlayerHitboxRepulsion(Particle& p,
                                       const ParticleUpdateContext& ctx,
                                       float* outProximity = nullptr)
{
    if (outProximity)
        *outProximity = 0.0f;

    // Only intervene while the player is actively moving. A stationary
    // player lets all particles drift past unaffected.
    const float camSpeed = glm::length(ctx.cameraVelocity);
    if (camSpeed <= 5.0f)
    {
        return;
    }

    // Player hitbox: 16 wide x 32 tall, with playerPos at bottom-center.
    const float boxMinX = ctx.playerPos.x - 8.0f;
    const float boxMaxX = ctx.playerPos.x + 8.0f;
    const float boxMinY = ctx.playerPos.y - 32.0f;
    const float boxMaxY = ctx.playerPos.y;
    const float nearestX = std::clamp(p.position.x, boxMinX, boxMaxX);
    const float nearestY = std::clamp(p.position.y, boxMinY, boxMaxY);
    const glm::vec2 fromBox = p.position - glm::vec2(nearestX, nearestY);
    const float dist = glm::length(fromBox);

    // Outward surface normal (from box edge toward particle). If the leaf
    // is essentially inside the box, fall back to "opposite the player's
    // motion" so the correction nudges it behind the player rather than
    // toward whatever face it happens to be nearest.
    const glm::vec2 motionDir = ctx.cameraVelocity / camSpeed;
    glm::vec2 outward;
    if (dist > 0.01f)
    {
        outward = fromBox / dist;
    }
    else
    {
        outward = -motionDir;
    }

    // Three concentric effect zones, all position-only so the wind-driven
    // velocity.x sign-flag convention used by weather spawns is preserved.
    // Magnitudes scale with player speed so a slow walk gives a subtle
    // disturbance and a sprint creates a dramatic swirl + wake.
    const float motionFactor = std::clamp(camSpeed / 100.0f, 0.3f, 1.5f);

    // Zone A - hard shell: keep particles off the sprite. Outward push at
    // 50 px/s clamped to the remaining gap so the leaf slides cleanly to
    // the shell boundary instead of popping out of it.
    constexpr float kHardShellRadius = 6.0f;
    if (dist < kHardShellRadius)
    {
        const float depth = kHardShellRadius - dist;
        constexpr float kHardShellPush = 50.0f;
        const float pushAmount = std::min(kHardShellPush * ctx.deltaTime, depth);
        p.position += outward * pushAmount;
    }

    // Zone B - tangential swirl: soft band from the hard shell out to 20px
    // that curls particles toward the trailing side. The tangent is whichever
    // 90-degree rotation of `outward` points more backward along motion, so
    // leaves on both sides end up trailing behind (flow-around-a-sphere).
    constexpr float kSwirlOuterRadius = 20.0f;
    constexpr float kSwirlPeak = 40.0f;
    float proximity = 0.0f;
    if (dist < kSwirlOuterRadius)
    {
        proximity = std::clamp(
            (kSwirlOuterRadius - dist) / (kSwirlOuterRadius - kHardShellRadius), 0.0f, 1.0f);
        const glm::vec2 tangentA(outward.y, -outward.x);
        const glm::vec2 tangentB(-outward.y, outward.x);
        const glm::vec2 backward = -motionDir;
        const glm::vec2 tangent = (glm::dot(tangentA, backward) > 0.0f) ? tangentA : tangentB;
        // Per-particle speed variance in the swirl so multiple leaves on the
        // same side don't curl in perfect lockstep - faster-tangent ones
        // overtake slower-tangent ones and the bundle breaks up before it
        // reaches the wake region.
        const float tangentVariance = 1.0f + 0.35f * std::sin(p.phase * 1.3f);  // [0.65, 1.35]
        p.position +=
            tangent * (kSwirlPeak * tangentVariance * proximity * motionFactor * ctx.deltaTime);
    }
    if (outProximity)
        *outProximity = proximity;

    // Zone C - slipstream wake: elliptical region trailing the player along
    // the motion axis (15px half-length, 8px half-width). Particles inside
    // get a position push ALONG motion (drag, not push-away), catching leaves
    // that exit Zone B behind the player for a moment before they peel off.
    constexpr float kWakeOffset = 15.0f;
    constexpr float kWakeHalfLen = 15.0f;
    constexpr float kWakeHalfWidth = 8.0f;
    constexpr float kWakePeak = 30.0f;
    const glm::vec2 wakeCenter = ctx.playerPos - motionDir * kWakeOffset;
    const glm::vec2 perpDir(-motionDir.y, motionDir.x);
    const glm::vec2 wakeOffset = p.position - wakeCenter;
    const float alongN = glm::dot(wakeOffset, motionDir) / kWakeHalfLen;
    const float perpN = glm::dot(wakeOffset, perpDir) / kWakeHalfWidth;
    const float wakeR = std::sqrt(alongN * alongN + perpN * perpN);
    if (wakeR < 1.0f)
    {
        const float wakeFactor = 1.0f - wakeR;
        // Per-particle fan-out so wake-caught particles don't converge into
        // one line behind the player. Spawn-randomized phase gives each its
        // own lateral offset and drag rate (spread across motion, overtake/lag
        // along it). Position-only, so the drift ends cleanly on exit.
        const float lateralSign = std::sin(p.phase * 2.0f);                 // [-1, 1]
        const float dragVariance = 1.0f + 0.3f * std::cos(p.phase * 1.7f);  // [0.7, 1.3]
        constexpr float kLateralScatter = 12.0f;
        p.position +=
            motionDir * (kWakePeak * dragVariance * wakeFactor * motionFactor * ctx.deltaTime);
        p.position +=
            perpDir * (lateralSign * kLateralScatter * wakeFactor * motionFactor * ctx.deltaTime);
    }
}

// Spawn the rain-splash droplet burst at an impact point. Called by
// Rain::Update from both the editor-zone ground check AND the weather
// bakedGroundY check so the title screen and gameplay weather share the
// same visual.
inline void SpawnRainSplash(const Particle& parent, float impactY, const ParticleUpdateContext& ctx)
{
    if (!ctx.pendingSpawns || !ctx.rng || !ctx.dist)
        return;
    auto& rng = *ctx.rng;
    auto& dist = *ctx.dist;
    const int splashCount = 3 + static_cast<int>(dist(rng) * 3.0f);  // 3-5
    for (int i = 0; i < splashCount; ++i)
    {
        Particle s;
        s.zoneIndex = -1;
        s.type = ParticleType::Sparkles;
        s.noProjection = parent.noProjection;
        s.position = glm::vec2(parent.position.x + (dist(rng) - 0.5f) * 16.0f, impactY);
        s.velocity = glm::vec2((dist(rng) - 0.5f) * 40.0f, -20.0f - dist(rng) * 20.0f);
        s.color = glm::vec4(0.8f, 0.85f, 1.0f, 0.0f);
        s.phase = 0.0f;
        s.size = 1.5f + dist(rng) * 1.5f;
        s.lifetime = 0.2f + dist(rng) * 0.15f;
        s.maxLifetime = s.lifetime;
        s.rotation = 0.0f;
        s.bakedGroundY = 0.0f;
        s.additive = true;
        ctx.pendingSpawns->push_back(s);
    }
}

// Spawn the snow-puff sparkle burst at an impact point. Same pattern as
// SpawnRainSplash, tuned smaller / whiter / gentler for snow.
inline void SpawnSnowPuff(const Particle& parent, float impactY, const ParticleUpdateContext& ctx)
{
    if (!ctx.pendingSpawns || !ctx.rng || !ctx.dist)
        return;
    auto& rng = *ctx.rng;
    auto& dist = *ctx.dist;
    const int puffCount = 2 + static_cast<int>(dist(rng) * 3.0f);  // 2-4
    for (int i = 0; i < puffCount; ++i)
    {
        Particle s;
        s.zoneIndex = -1;
        s.type = ParticleType::Sparkles;
        s.noProjection = parent.noProjection;
        s.position = glm::vec2(parent.position.x + (dist(rng) - 0.5f) * 12.0f, impactY);
        s.velocity = glm::vec2((dist(rng) - 0.5f) * 30.0f, -5.0f - dist(rng) * 10.0f);
        s.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        s.phase = 0.0f;
        s.size = 1.0f + dist(rng) * 1.0f;
        s.lifetime = 0.8f + dist(rng) * 0.4f;
        s.maxLifetime = s.lifetime;
        s.rotation = 0.0f;
        s.bakedGroundY = 0.0f;
        s.additive = true;
        ctx.pendingSpawns->push_back(s);
    }
}

// Primary template - specialize for each ParticleType enumerator.
template <ParticleType PT>
struct ParticleBehavior
{
    static constexpr float SpawnRate = 5.0f;
    static void Update(Particle& p, const ParticleUpdateContext& ctx);
    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx);
};

// Firefly.

template <>
struct ParticleBehavior<ParticleType::Firefly>
{
    static constexpr float SpawnRate = 8.0f;

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

// Rain.

template <>
struct ParticleBehavior<ParticleType::Rain>
{
    static constexpr float SpawnRate = 25.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Fade in smoothly over first 0.15 seconds
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.15f);
        // Target alpha stored in phase
        p.color.a = fadeIn * p.phase;

        // Editor-zone rain (editor- or title-ambient-placed): die and splash
        // when crossing the zone's bottom edge, same visual as weather rain.
        // For zones larger than the viewport (e.g. the title whole-map zone),
        // groundY is clamped into the visible area so the splash isn't off-screen.
        if (ctx.hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(ctx.zones->size()))
        {
            const auto& zone = (*ctx.zones)[p.zoneIndex];

            // Vary ground height per particle using position.x as seed
            float heightVariation =
                std::fmod(std::abs(p.position.x * 7.3f + p.phase * 100.0f), 60.0f);
            float baseGround = zone.position.y + zone.size.y + 20.0f + heightVariation;
            // For zones past the viewport (title whole-map zone), spread the
            // impact across a wide vertical band (like HeavyRain / Blizzard
            // bakedGroundY) so the ground reads as an area, not one line. The
            // ~35%-95% viewport range keeps splashes below the title UI.
            float spreadT = heightVariation / 60.0f;
            float viewSplashY = ctx.cameraPos.y + ctx.viewSize.y * (0.35f + spreadT * 0.60f);
            float groundY = std::min(baseGround, viewSplashY);
            if (p.position.y > groundY)
            {
                SpawnRainSplash(p, groundY, ctx);
                p.lifetime = 0.0f;
            }
        }

        // Weather-spawned rain: bakedGroundY (from SpawnWeatherParticle) is a
        // screen-relative band, so it re-bases to the camera each frame. Else
        // a camera sprinting down outruns the band and rain lands mid-screen
        // while the revealed bottom half starves (maintainer-reported).
        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX && p.bakedGroundY > 0.0f)
        {
            p.bakedGroundY += ctx.cameraDelta.y;
            if (p.position.y > p.bakedGroundY)
            {
                SpawnRainSplash(p, p.bakedGroundY, ctx);
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

        // Weather-spawned rain gets bakedGroundY (and a per-particle lifetime
        // to reach it) later in SpawnWeatherParticle, so its splash spans the
        // full viewport height. Editor-zone rain keeps bakedGroundY == 0 and
        // uses the zone-based ground detection above.

        ctx.particles.push_back(p);
    }
};

// Snow.

template <>
struct ParticleBehavior<ParticleType::Snow>
{
    static constexpr float SpawnRate = 25.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Snow drifts side to side
        float drift = std::sin(ctx.time * 1.5f + p.phase) * 20.0f;
        p.position.x += drift * ctx.deltaTime;

        // Blizzard gusts: weather-zone snow layers a ~4 Hz per-particle jerk
        // and a slow ~0.7 Hz surge on top of the baseline drift. Three
        // frequencies driving X motion give a jerky, gusty feel; editor-zone
        // snow keeps just the smooth drift.
        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            float gust = std::sin(ctx.time * 4.0f + p.phase * 2.3f) * 12.0f +
                         std::sin(ctx.time * 0.7f + p.phase * 0.4f) * 8.0f;
            p.position.x += gust * ctx.deltaTime;
        }

        // Rotate as it falls
        float rotationSpeed = 30.0f + (p.phase / 6.28f) * 60.0f;  // 30-90 degrees per second
        if (std::fmod(p.phase, 2.0f) < 1.0f)
            rotationSpeed = -rotationSpeed;  // Half rotate clockwise, half counter-clockwise
        p.rotation += rotationSpeed * ctx.deltaTime;

        // Editor-zone snow (editor- or title-ambient-placed): puff at impact
        // on the zone's bottom edge, matching weather snow, with per-particle
        // ground-Y scatter. For zones larger than the viewport, groundY is
        // clamped into the visible area so the puff isn't off-screen below.
        if (ctx.hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(ctx.zones->size()))
        {
            const auto& zone = (*ctx.zones)[p.zoneIndex];
            float heightVariation =
                std::fmod(std::abs(p.position.x * 5.7f + p.phase * 80.0f), 60.0f);
            float baseGround = zone.position.y + zone.size.y + 20.0f + heightVariation;
            // Same wide vertical spread Rain uses when the zone extends past
            // the viewport, so the puff line reads as an area, not a stripe.
            // The ~35%-95% viewport range keeps puffs below the title UI,
            // matching the Rain ground band.
            float spreadT = heightVariation / 60.0f;
            float viewSplashY = ctx.cameraPos.y + ctx.viewSize.y * (0.35f + spreadT * 0.60f);
            float groundY = std::min(baseGround, viewSplashY);
            if (p.position.y > groundY)
            {
                SpawnSnowPuff(p, groundY, ctx);
                p.lifetime = 0.0f;
            }
        }

        // Weather-spawned snow: bakedGroundY (from SpawnWeatherParticle) is a
        // screen-relative band, so it re-bases to the camera each frame. Else
        // a camera sprinting down outruns the band and snow lands mid-screen
        // while the revealed bottom half starves (maintainer-reported).
        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX && p.bakedGroundY > 0.0f)
        {
            p.bakedGroundY += ctx.cameraDelta.y;
            if (p.position.y > p.bakedGroundY)
            {
                SpawnSnowPuff(p, p.bakedGroundY, ctx);
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

// Fog.

template <>
struct ParticleBehavior<ParticleType::Fog>
{
    // Zone / ambient fog density. Halved from 5.0 so fewer large puffs overlap
    // into a solid wall (weather fog density is the per-weather baseSpawnRate).
    static constexpr float SpawnRate = 2.5f;

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

        // More visible during day, significantly less at night. The day boost is
        // kept gentle (peaks at 1.15x, not 1.4x) so daytime fog stays a light
        // haze instead of an opaque wall.
        float dayBoost = 1.0f + (1.0f - ctx.nightFactor) * 0.15f;
        float nightReduce = 1.0f - ctx.nightFactor * 0.6f;

        // Softening multiplier on the puff alpha. Weather fog follows the
        // active weather's fogAlphaMultiplier (0.4-0.65); editor/console/ambient
        // fog uses a fixed 0.5 and must NOT borrow the weather value, which
        // defaults to 1.0 and would render zone fog at full strength.
        const float fogMul =
            (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX) ? ctx.fogAlphaMultiplier : 0.5f;

        // Atmospheric layering: fog thicker near the ground, thinner up high.
        // Anchored on the player's feet (playerPos.y) so the gradient tracks
        // the scene's ground line as the player moves vertically. Floor at
        // 0.3 keeps the very top of the world at 30% rather than vanishing.
        const float groundRefY = ctx.playerPos.y + 40.0f;
        constexpr float kFadeRange = 200.0f;
        const float verticalFactor =
            std::clamp(1.0f - (groundRefY - p.position.y) / kFadeRange, 0.3f, 1.0f);

        p.color.a =
            pulse * lifeFade * fadeIn * 0.25f * fogMul * dayBoost * nightReduce * verticalFactor;
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
        // Shorter lifetime (12-18s, was 18-30s) means fewer simultaneously-live
        // puffs at the same spawn rate, further reducing stacked-alpha buildup.
        p.lifetime = 12.0f + ctx.dist(ctx.rng) * 6.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;

        ctx.particles.push_back(p);
    }
};

// Sparkles.

template <>
struct ParticleBehavior<ParticleType::Sparkles>
{
    static constexpr float SpawnRate = 28.0f;

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

// Wisp.

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

// Lantern.

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

// Sunshine.

template <>
struct ParticleBehavior<ParticleType::Sunshine>
{
    static constexpr float SpawnRate = 1.3f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Sun & moon rays, yellow during day, blue during night
        float shimmer = 0.95f + 0.05f * std::sin(ctx.time * 1.2f + p.phase);
        float flicker = 0.97f + 0.03f * std::sin(ctx.time * 3.0f + p.phase * 1.5f);

        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.4f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);

        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            // GodRays weather: each Sunshine beam picks a fixed rainbow
            // palette tier from its phase. Phase is set once at spawn in
            // [0, 2pi] so the tier stays stable for the particle's life -
            // cycling hues mid-flight would read as flicker, not prism.
            static constexpr glm::vec3 kRainbow[7] = {
                {1.00f, 0.20f, 0.20f},  // Red
                {1.00f, 0.55f, 0.15f},  // Orange
                {1.00f, 0.95f, 0.30f},  // Yellow
                {0.30f, 0.95f, 0.40f},  // Green
                {0.30f, 0.85f, 1.00f},  // Cyan
                {0.30f, 0.45f, 1.00f},  // Blue
                {0.75f, 0.30f, 1.00f},  // Violet
            };
            const int hue = std::min(6, static_cast<int>(p.phase * (7.0f / 6.2832f)));
            p.color.r = kRainbow[hue].r;
            p.color.g = kRainbow[hue].g;
            p.color.b = kRainbow[hue].b;
        }
        else
        {
            // Editor zones: interpolate between golden yellow (day) and pale blue (night).
            float nightBlend = ctx.nightFactor;
            p.color.r = 1.0f * (1.0f - nightBlend) + 0.5f * nightBlend;
            p.color.g = 0.9f * (1.0f - nightBlend) + 0.7f * nightBlend;
            p.color.b = 0.5f * (1.0f - nightBlend) + 1.0f * nightBlend;
        }

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

// DriftingLeaf - small leaf drifting on prevailing wind.

template <>
struct ParticleBehavior<ParticleType::DriftingLeaf>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Wind drift with gentle Y oscillation, scaled by strength (0.5 = 18
        // px/s baseline). velocity.x is a sign-flag from SpawnWeatherParticle:
        // > 0 means drift counter to the global wind, so weather spawns enter
        // from both edges. Ambient (zoneless) spawns stay at zero (default wind).
        glm::vec2 wind = glm::normalize(ctx.windDir);
        if (p.velocity.x > 0.0f)
        {
            wind.x = -wind.x;
        }
        p.position += wind * (18.0f * 2.0f * ctx.windStrength) * ctx.deltaTime;
        p.position.y += std::sin(ctx.time * 1.4f + p.phase) * 6.0f * ctx.deltaTime;

        // Weather-spawned leaves slide off the player's hitbox like water on
        // a moving surface, with a swirl band and a slipstream wake layered
        // on top (see ApplyPlayerHitboxRepulsion). Ambient (zoneless) leaves
        // drift through without interaction.
        float proximity = 0.0f;
        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            ApplyPlayerHitboxRepulsion(p, ctx, &proximity);
        }

        // Rotation rate gets a boost (up to 3x) when a leaf is caught in the
        // swirl band, so disturbed leaves visibly tumble harder than ambient
        // ones drifting on the wind.
        p.rotation += 25.0f * (1.0f + 2.0f * proximity) * ctx.deltaTime;

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

// DustMote - tiny golden mote in sunbeams.

template <>
struct ParticleBehavior<ParticleType::DustMote>
{
    static constexpr float SpawnRate = 5.5f;

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

// Pollen - yellow drift during golden hour.

template <>
struct ParticleBehavior<ParticleType::Pollen>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Horizontal drift on the gusted wind, very gentle vertical sway,
        // scaled continuously by strength (0.5 = today's 8 px/s baseline).
        // velocity.x is a sign-flag set by SpawnWeatherParticle so weather
        // pollen can drift in from either edge (see DriftingLeaf::Update).
        glm::vec2 wind = glm::normalize(ctx.windDir);
        if (p.velocity.x > 0.0f)
        {
            wind.x = -wind.x;
        }
        p.position += wind * (8.0f * 2.0f * ctx.windStrength) * ctx.deltaTime;
        p.position.y += std::sin(ctx.time * 0.9f + p.phase) * 3.0f * ctx.deltaTime;

        // Weather-spawned pollen slides off the player's hitbox like water
        // on a moving surface. Ambient (zoneless) pollen drifts through
        // without interaction.
        if (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
        {
            ApplyPlayerHitboxRepulsion(p, ctx);
        }

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

// CherryBlossom - drifting pink petals, gentle spiral, additive blend.

template <>
struct ParticleBehavior<ParticleType::CherryBlossom>
{
    static constexpr float SpawnRate = 12.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Per-particle drift amplitude/frequency derived from phase so each
        // blossom flutters with its own personality.
        float ampX = 14.0f + 10.0f * std::abs(std::sin(p.phase * 0.7f));
        float ampY = 4.0f + 6.0f * std::abs(std::cos(p.phase * 0.9f));
        float freqX = 0.6f + 0.4f * std::sin(p.phase * 1.3f);
        float freqY = 0.4f + 0.3f * std::cos(p.phase * 1.7f);

        float driftX = std::sin(ctx.time * (0.7f + freqX) + p.phase) * ampX;
        float driftY = std::cos(ctx.time * (0.5f + freqY) + p.phase * 1.4f) * ampY;
        p.position.x += driftX * ctx.deltaTime;
        // Net downward drift varies (some blossoms fall faster than others).
        float fallSpeed = 14.0f + 14.0f * std::abs(std::sin(p.phase * 2.1f));
        p.position.y += (fallSpeed + driftY) * ctx.deltaTime;

        // Per-particle rotation rate (-65..+65 deg/s) derived from phase.
        float rotSpeed = (15.0f + 50.0f * std::abs(std::sin(p.phase * 1.7f))) *
                         (std::cos(p.phase * 0.9f) > 0.0f ? 1.0f : -1.0f);
        p.rotation += rotSpeed * ctx.deltaTime;

        // Fade in/out for smooth lifetime endings. Peak alpha is encoded in
        // the X velocity (re-purposed as a static per-particle scalar - the
        // Update path never reads velocity for CherryBlossom). A subtle
        // shimmer pulse layered on top adds life.
        float peak = std::clamp(p.velocity.x, 0.2f, 1.0f);
        float fade = std::min(p.lifetime / 1.2f, (p.maxLifetime - p.lifetime) / 0.7f);
        float shimmer = 0.85f + 0.15f * std::sin(ctx.time * 1.8f + p.phase * 2.3f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * peak * shimmer;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        // Tiered spawning gives blossoms variety in size, hue, and presence.
        // 60%: small background petals (subtle, pale)
        // 30%: medium showcase petals (mid-pink)
        // 10%: large glow petals (hot pink, with halo halo spawn).
        float tierRoll = ctx.dist(ctx.rng);

        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::CherryBlossom;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        // velocity.x stores the per-particle peak alpha (read by Update);
        // velocity.y stays 0 since Update applies its own fall speed.
        p.velocity = glm::vec2(0.0f);
        p.lifetime = 8.0f + ctx.dist(ctx.rng) * 7.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = ctx.dist(ctx.rng) * 360.0f;
        p.additive = true;

        // Pink-forward palette: the bulk of petals are saturated sakura pink
        // and hot pink. Pale and white variants are deliberately rare so the
        // overall flurry reads as a confident pink wash, not washed-out.
        float hueRoll = ctx.dist(ctx.rng);
        if (hueRoll < 0.40f)
        {
            // Saturated sakura pink - primary, vivid hue.
            p.color = glm::vec4(
                1.00f, 0.55f + ctx.dist(ctx.rng) * 0.10f, 0.78f + ctx.dist(ctx.rng) * 0.06f, 0.0f);
        }
        else if (hueRoll < 0.65f)
        {
            // Hot pink / deep magenta accent.
            p.color = glm::vec4(
                1.00f, 0.40f + ctx.dist(ctx.rng) * 0.10f, 0.70f + ctx.dist(ctx.rng) * 0.08f, 0.0f);
        }
        else if (hueRoll < 0.80f)
        {
            // Soft pale pink - supporting hue, less common than before.
            p.color = glm::vec4(
                1.00f, 0.78f + ctx.dist(ctx.rng) * 0.08f, 0.86f + ctx.dist(ctx.rng) * 0.05f, 0.0f);
        }
        else if (hueRoll < 0.88f)
        {
            // Peachy blush - warm coral accent.
            p.color = glm::vec4(
                1.00f, 0.72f + ctx.dist(ctx.rng) * 0.05f, 0.66f + ctx.dist(ctx.rng) * 0.06f, 0.0f);
        }
        else if (hueRoll < 0.94f)
        {
            // Deep crimson red - dramatic accent.
            p.color = glm::vec4(
                1.00f, 0.28f + ctx.dist(ctx.rng) * 0.10f, 0.45f + ctx.dist(ctx.rng) * 0.10f, 0.0f);
        }
        else if (hueRoll < 0.98f)
        {
            // Violet/purple - rare cool accent.
            p.color = glm::vec4(0.85f + ctx.dist(ctx.rng) * 0.08f,
                                0.50f + ctx.dist(ctx.rng) * 0.08f,
                                0.95f + ctx.dist(ctx.rng) * 0.05f,
                                0.0f);
        }
        else
        {
            // Almost-white highlight - very rare, used as visual punctuation.
            p.color = glm::vec4(
                1.00f, 0.92f + ctx.dist(ctx.rng) * 0.05f, 0.94f + ctx.dist(ctx.rng) * 0.04f, 0.0f);
        }

        // Halo defaults to a saturated pink so the bloom amplifies the petal's
        // hue. For crimson and violet variants we tilt the halo toward those
        // families so the bloom complements rather than fights the petal.
        glm::vec4 haloColor = glm::vec4(1.00f, 0.55f, 0.78f, 0.0f);  // pink halo
        if (hueRoll >= 0.88f && hueRoll < 0.94f)
            haloColor = glm::vec4(1.00f, 0.40f, 0.55f, 0.0f);  // pink-red halo
        else if (hueRoll >= 0.94f && hueRoll < 0.98f)
            haloColor = glm::vec4(0.85f, 0.55f, 1.00f, 0.0f);  // violet halo

        // Shared helper: append a halo particle behind @p source for the
        // "shine glow" look. Halo is larger, dimmer, additive, with phase
        // offset so its shimmer doesn't lockstep with the petal.
        auto appendHalo = [&](const Particle& source, float sizeMul, float peakAlpha)
        {
            Particle halo = source;
            halo.size = source.size * sizeMul;
            halo.velocity.x = peakAlpha;
            halo.color = haloColor;
            halo.phase = source.phase + 0.7f;
            ctx.particles.push_back(halo);
        };

        if (tierRoll < 0.55f)
        {
            // Background petal: small but bright enough to read.
            p.size = 1.6f + ctx.dist(ctx.rng) * 1.6f;
            p.velocity.x = 0.65f + ctx.dist(ctx.rng) * 0.20f;  // Peak alpha 0.65-0.85.
            ctx.particles.push_back(p);
        }
        else if (tierRoll < 0.88f)
        {
            // Mid-tier showcase petal: gets a soft shine glow.
            p.size = 2.8f + ctx.dist(ctx.rng) * 1.8f;
            p.velocity.x = 0.85f + ctx.dist(ctx.rng) * 0.12f;  // Peak alpha 0.85-0.97.
            ctx.particles.push_back(p);
            appendHalo(p, 1.7f, 0.15f + ctx.dist(ctx.rng) * 0.08f);
        }
        else
        {
            // Premium glow petal: brightest core + a stronger shine halo.
            p.size = 4.5f + ctx.dist(ctx.rng) * 2.5f;
            p.velocity.x = 0.95f + ctx.dist(ctx.rng) * 0.05f;  // Peak alpha 0.95-1.00.
            ctx.particles.push_back(p);
            appendHalo(p, 2.2f, 0.25f + ctx.dist(ctx.rng) * 0.12f);
        }
    }
};

// Ash - gray particles, slow fall + horizontal flutter, alpha blend.

template <>
struct ParticleBehavior<ParticleType::Ash>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        float flutter = std::sin(ctx.time * 1.2f + p.phase) * 8.0f;
        p.position.x += flutter * ctx.deltaTime;
        // velocity.y is set at spawn for a slow constant fall.

        float fade = std::min(p.lifetime / 2.0f, (p.maxLifetime - p.lifetime) / 1.0f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * 0.75f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Ash;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 0.0f;
        p.velocity.y = 12.0f + ctx.dist(ctx.rng) * 10.0f;  // Slow, steady fall.
        p.color = glm::vec4(0.70f, 0.70f, 0.72f + ctx.dist(ctx.rng) * 0.05f, 0.0f);
        p.size = 2.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 10.0f + ctx.dist(ctx.rng) * 10.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Ember - orange particles rising upward, additive flicker.

template <>
struct ParticleBehavior<ParticleType::Ember>
{
    static constexpr float SpawnRate = 7.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Slight horizontal wobble.
        float wobble = std::sin(ctx.time * 4.0f + p.phase) * 4.0f;
        p.position.x += wobble * ctx.deltaTime;

        // Rapid alpha flicker simulating a glowing ember.
        float flicker = 0.6f + 0.4f * std::sin(ctx.time * 12.0f + p.phase * 2.0f);
        float life = std::min(p.lifetime / 0.6f, (p.maxLifetime - p.lifetime) / 0.3f);
        p.color.a = std::clamp(life, 0.0f, 1.0f) * flicker * 0.9f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Ember;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 8.0f;
        p.velocity.y = -(30.0f + ctx.dist(ctx.rng) * 30.0f);  // Rises (Y- is up).
        p.color = glm::vec4(
            0.95f, 0.45f + ctx.dist(ctx.rng) * 0.15f, 0.15f + ctx.dist(ctx.rng) * 0.10f, 0.0f);
        p.size = 4.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 1.5f + ctx.dist(ctx.rng) * 1.5f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Sand - fast horizontal wind-driven streaks, alpha blend.

template <>
struct ParticleBehavior<ParticleType::Sand>
{
    static constexpr float SpawnRate = 25.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // velocity is set at spawn; just fade.
        float fade = std::min(p.lifetime / 0.2f, (p.maxLifetime - p.lifetime) / 0.1f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * 0.6f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Sand;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        // Wind blows right (+X) by default; the gusted strength scales the
        // streak speed (0.5 = today's 100-200 px/s anchor). Sand keeps its
        // own +X axis this phase - the spawn-edge bias in SpawnWeatherParticle
        // assumes rightward travel; direction unification is deferred.
        const float windScale = 2.0f * ctx.windStrength;
        p.velocity.x = (100.0f + ctx.dist(ctx.rng) * 100.0f) * windScale;
        p.velocity.y = 10.0f + ctx.dist(ctx.rng) * 15.0f;  // Slight downward drift.
        p.color = glm::vec4(0.85f, 0.72f, 0.45f + ctx.dist(ctx.rng) * 0.10f, 0.0f);
        p.size = 3.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 0.3f + ctx.dist(ctx.rng) * 0.5f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Dispatch tables - auto-generated from ParticleBehavior specializations.

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

// ParticleSystem implementation.

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
    // Reserve enough for heavy weather (Thunderstorm: 800 rain particles)
    // plus ambient + zone particles, without reallocating on the fast path.
    m_Particles.reserve(1000);
}

bool ParticleSystem::LoadTextures(TextureStore& store)
{
    m_Store = &store;
    BuildAtlas();
    m_TexturesLoaded = true;
    return true;
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
                // Already RGBA, straight copy.
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

    // Procedural soft-edge textures for the weather-only particle types
    // (CherryBlossom, Ash, Ember, Sand) so tinting doesn't read as opaque squares.

    // Generic soft circle: a smooth radial alpha falloff used by Ash and Ember.
    auto generateSoftCircle = [](TextureSource& src, int size, float falloffPow)
    {
        src.width = size;
        src.height = size;
        GeneratePixels(src.pixels,
                       size,
                       size,
                       [falloffPow](int x, int y, int w, int h) -> Pixel
                       {
                           float cx = w * 0.5f;
                           float cy = h * 0.5f;
                           float dx = (x - cx) / cx;
                           float dy = (y - cy) / cy;
                           float dist = std::sqrt(dx * dx + dy * dy);
                           float a = std::pow(std::max(0.0f, 1.0f - dist), falloffPow);
                           auto alpha = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
                           return Pixel{255, 255, 255, alpha};
                       });
    };

    // Sand: elongated horizontal soft streak - wind-driven look.
    auto generateStreak = [](TextureSource& src)
    {
        constexpr int kW = 32;
        constexpr int kH = 8;
        src.width = kW;
        src.height = kH;
        GeneratePixels(src.pixels,
                       kW,
                       kH,
                       [](int x, int y, int w, int h) -> Pixel
                       {
                           float cx = w * 0.5f;
                           float cy = h * 0.5f;
                           float nx = (x - cx) / cx;  // -1..1 along streak
                           float ny = (y - cy) / cy;  // -1..1 across streak
                           // Tighter falloff across (vertical), looser along (horizontal).
                           float along = std::pow(std::max(0.0f, 1.0f - std::abs(nx)), 1.4f);
                           float across = std::exp(-ny * ny * 4.0f);
                           float a = std::clamp(along * across, 0.0f, 1.0f);
                           auto alpha = static_cast<uint8_t>(a * 255.0f);
                           return Pixel{255, 255, 255, alpha};
                       });
    };

    {
        const int blossomIdx = static_cast<int>(ParticleType::CherryBlossom);
        const int ashIdx = static_cast<int>(ParticleType::Ash);
        const int emberIdx = static_cast<int>(ParticleType::Ember);
        const int sandIdx = static_cast<int>(ParticleType::Sand);
        // Cherry blossom uses a hand-painted PNG asset; loadPng falls back to
        // a 16x16 white texture if the asset is missing, then the soft-circle
        // belt-and-braces below replaces that with a soft-edged sprite.
        loadPng("assets/particles/f21d2941-7f0f-4bcd-9aa5-fa90696f816a.png", sources[blossomIdx]);
        generateSoftCircle(sources[ashIdx], 24, 1.6f);    // Soft, ash-like puff.
        generateSoftCircle(sources[emberIdx], 24, 2.4f);  // Tight bright dot for additive glow.
        generateStreak(sources[sandIdx]);
    }

    // Belt-and-braces: any source still empty (e.g. a future ParticleType added
    // without a generator) gets a soft fallback rather than an opaque square.
    for (int i = 0; i < kAtlasSourceCount; ++i)
    {
        if (sources[i].width == 0 || sources[i].height == 0 || sources[i].pixels.empty())
        {
            generateSoftCircle(sources[i], 16, 1.5f);
        }
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

        // Guard against atlas overflow, skip textures that don't fit.
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
    Texture atlas;
    atlas.LoadFromData(atlasPixels.data(), atlasWidth, atlasHeight, 4, false);
    m_AtlasHandle = m_Store->Adopt(std::move(atlas));

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

    // Smoothed camera velocity. First frame seeds m_PrevCameraPos so we don't
    // emit a huge spike from (0,0) -> cameraPos. The 0.25 lerp gives ~4-frame
    // smoothing - enough to ride out single-frame stalls without lag.
    if (!m_HasPrevCameraPos)
    {
        m_PrevCameraPos = cameraPos;
        m_HasPrevCameraPos = true;
    }
    glm::vec2 rawCamDelta = cameraPos - m_PrevCameraPos;
    // Teleports / world loads produce a jump larger than the viewport; treat
    // those as a scene cut (no band re-base - stale particles cull anyway).
    if (std::abs(rawCamDelta.x) > viewSize.x || std::abs(rawCamDelta.y) > viewSize.y)
    {
        rawCamDelta = glm::vec2(0.0f);
    }
    const glm::vec2 rawCamVel = (deltaTime > 1e-4f) ? rawCamDelta / deltaTime : glm::vec2(0.0f);
    m_CameraVelocity = glm::mix(m_CameraVelocity, rawCamVel, 0.25f);
    m_PrevCameraPos = cameraPos;

    const float fogAlphaMul = m_CurrentWeatherDef ? m_CurrentWeatherDef->fogAlphaMultiplier : 1.0f;
    m_PendingSpawns.clear();
    const ParticleUpdateContext updateCtx{m_Time,
                                          deltaTime,
                                          m_NightFactor,
                                          m_Zones,
                                          hasZones,
                                          fogAlphaMul,
                                          m_CameraVelocity,
                                          cameraPos,
                                          viewSize,
                                          m_PlayerPosition,
                                          &m_PendingSpawns,
                                          &m_Rng,
                                          &m_Dist01,
                                          m_WindDir,
                                          m_WindStrength,
                                          rawCamDelta};

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

        // Cull weather particles that drifted far outside the spawn rect.
        // Without this, fast camera moves leave a "wake" of stale particles
        // that reappear all at once when the player backtracks. Bounds are the
        // spawn overspray edge plus a half-viewport margin, leaving drift room.
        if (p.zoneIndex == WEATHER_ZONE_INDEX)
        {
            constexpr float kSpawnOverspray = 0.20f;
            const float marginX = viewSize.x * 0.5f;
            const float marginY = viewSize.y * 0.5f;
            const float spawnLeft = cameraPos.x - viewSize.x * kSpawnOverspray - marginX;
            const float spawnRight = cameraPos.x + viewSize.x * (1.0f + kSpawnOverspray) + marginX;
            const float spawnTop = cameraPos.y - viewSize.y * kSpawnOverspray - marginY;
            const float spawnBottom = cameraPos.y + viewSize.y * (1.0f + kSpawnOverspray) + marginY;
            if (p.position.x < spawnLeft || p.position.x > spawnRight || p.position.y < spawnTop ||
                p.position.y > spawnBottom)
            {
                p.lifetime = 0.0f;
                continue;
            }
        }

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

    // Merge any deferred spawns (e.g., Rain splashes). Done before the cull
    // so newly-spawned-but-dead particles would still be removed cleanly.
    if (!m_PendingSpawns.empty())
    {
        m_Particles.insert(m_Particles.end(), m_PendingSpawns.begin(), m_PendingSpawns.end());
        m_PendingSpawns.clear();
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

    // Maintain weather-driven particle population (rain, snow, ash, etc.).
    UpdateWeatherSpawning(deltaTime, cameraPos, viewSize);

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
        return;  // Invalid particle type, skip silently.
    }
    ParticleSpawnContext ctx{m_Rng, m_Dist01, m_Particles, m_WindDir, m_WindStrength};
    kSpawnDispatch[typeIndex](zoneIndex, zone, ctx);
}

namespace
{
// Returns a smoothstep ramp peaking near `center` (in 0-24h time).
// Width controls the half-width of the bump.
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

void ParticleSystem::SetWeatherState(const WeatherDefinition* def, float intensity)
{
    m_CurrentWeatherDef = def;
    m_WeatherIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

void ParticleSystem::SetWeatherTransition(const WeatherDefinition* outgoing,
                                          const WeatherDefinition* incoming,
                                          float weight)
{
    m_TransitionOut = outgoing;
    m_TransitionIn = incoming;
    m_TransitionWeight = std::clamp(weight, 0.0f, 1.0f);
}

void ParticleSystem::SetWind(glm::vec2 direction, float strength)
{
    if (glm::length(direction) > 1e-4f)
    {
        m_WindDir = glm::normalize(direction);
    }
    m_WindStrength = std::max(0.0f, strength);
}

namespace
{
// Map WeatherParticleType -> concrete ParticleType. Returns nullopt for None.
std::optional<ParticleType> ResolveWeatherParticle(WeatherParticleType wpt)
{
    switch (wpt)
    {
        case WeatherParticleType::None:
            return std::nullopt;
        case WeatherParticleType::Rain:
            return ParticleType::Rain;
        case WeatherParticleType::Snow:
            return ParticleType::Snow;
        case WeatherParticleType::Fog:
            return ParticleType::Fog;
        case WeatherParticleType::Leaf:
            return ParticleType::DriftingLeaf;
        case WeatherParticleType::Blossom:
            return ParticleType::CherryBlossom;
        case WeatherParticleType::Pollen:
            return ParticleType::Pollen;
        case WeatherParticleType::Ash:
            return ParticleType::Ash;
        case WeatherParticleType::Ember:
            return ParticleType::Ember;
        case WeatherParticleType::Sand:
            return ParticleType::Sand;
        case WeatherParticleType::Firefly:
            return ParticleType::Firefly;
        case WeatherParticleType::Wisp:
            return ParticleType::Wisp;
        case WeatherParticleType::Sunshine:
            return ParticleType::Sunshine;
    }
    return std::nullopt;
}
}  // namespace

void ParticleSystem::UpdateWeatherSpawning(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize)
{
    if (m_CurrentWeatherDef == nullptr)
    {
        return;
    }

    // Hoisted per-type live census: one O(n) pass per frame instead of one
    // per spawned particle (Thunderstorm spawns ~1000/s into a 10k pool).
    // SpawnWeatherType increments its slot locally as it spawns.
    std::array<int, 32> liveByType{};
    static_assert(std::tuple_size_v<decltype(kSpawnDispatch)> <= 32,
                  "widen liveByType to cover all ParticleTypes");
    for (const auto& p : m_Particles)
    {
        const auto idx = static_cast<size_t>(p.type);
        if (p.zoneIndex == WEATHER_ZONE_INDEX && idx < liveByType.size())
        {
            ++liveByType[idx];
        }
    }

    if (m_TransitionOut != nullptr && m_TransitionIn != nullptr)
    {
        // Transition: four streams (outgoing fades out, incoming fades in).
        // Each spawns with its OWN definition so per-weather size tuning stays
        // correct on both sides of the cross-fade; the blended effective def
        // (SetWeatherState) still feeds live-read channels.

        // Cap rule (spec 4.3): for a type both endpoints spawn, the stream cap
        // is the min of the two endpoints' caps (0 = uncapped). Otherwise the
        // larger-capped stream fills the SHARED live count past the smaller
        // endpoint's ceiling - the bug the blend-side floor exists to prevent.
        const auto streamCap =
            [](const WeatherDefinition& other, WeatherParticleType type, int ownSlotCap)
        {
            const int otherCap = WeatherCapForType(other, type);
            if (otherCap == 0)
            {
                return ownSlotCap;
            }
            if (ownSlotCap == 0)
            {
                return otherCap;
            }
            return std::min(ownSlotCap, otherCap);
        };
        const WeatherDefinition& out = *m_TransitionOut;
        const WeatherDefinition& in = *m_TransitionIn;
        const float outWeight = 1.0f - m_TransitionWeight;
        SpawnWeatherType(out.particleType,
                         EffectiveRate(out.baseSpawnRate, viewSize) * outWeight,
                         streamCap(in, out.particleType, out.maxWeatherParticles),
                         m_WeatherSpawnTimerOut,
                         deltaTime,
                         cameraPos,
                         viewSize,
                         liveByType,
                         m_TransitionOut);
        SpawnWeatherType(out.secondaryParticleType,
                         EffectiveRate(out.secondaryBaseSpawnRate, viewSize) * outWeight,
                         streamCap(in, out.secondaryParticleType, out.secondaryMaxWeatherParticles),
                         m_WeatherSpawnTimerOutSecondary,
                         deltaTime,
                         cameraPos,
                         viewSize,
                         liveByType,
                         m_TransitionOut);
        SpawnWeatherType(in.particleType,
                         EffectiveRate(in.baseSpawnRate, viewSize) * m_TransitionWeight,
                         streamCap(out, in.particleType, in.maxWeatherParticles),
                         m_WeatherSpawnTimer,
                         deltaTime,
                         cameraPos,
                         viewSize,
                         liveByType,
                         m_TransitionIn);
        SpawnWeatherType(in.secondaryParticleType,
                         EffectiveRate(in.secondaryBaseSpawnRate, viewSize) * m_TransitionWeight,
                         streamCap(out, in.secondaryParticleType, in.secondaryMaxWeatherParticles),
                         m_WeatherSpawnTimerSecondary,
                         deltaTime,
                         cameraPos,
                         viewSize,
                         liveByType,
                         m_TransitionIn);
        return;
    }

    // Idle: the two-stream path (Task 3), passing m_CurrentWeatherDef as the
    // stream def.
    // Scale base rate by intensity and visible-area ratio (see EffectiveRate).
    SpawnWeatherType(m_CurrentWeatherDef->particleType,
                     EffectiveRate(m_CurrentWeatherDef->baseSpawnRate, viewSize),
                     m_CurrentWeatherDef->maxWeatherParticles,
                     m_WeatherSpawnTimer,
                     deltaTime,
                     cameraPos,
                     viewSize,
                     liveByType,
                     m_CurrentWeatherDef);
    SpawnWeatherType(m_CurrentWeatherDef->secondaryParticleType,
                     EffectiveRate(m_CurrentWeatherDef->secondaryBaseSpawnRate, viewSize),
                     m_CurrentWeatherDef->secondaryMaxWeatherParticles,
                     m_WeatherSpawnTimerSecondary,
                     deltaTime,
                     cameraPos,
                     viewSize,
                     liveByType,
                     m_CurrentWeatherDef);
}

float ParticleSystem::EffectiveRate(float baseSpawnRate, glm::vec2 viewSize) const
{
    // Scale base rate by visible-area ratio so density per visible pixel stays
    // roughly constant across zoom. Reference is the 320x180 world-px window at
    // zoom=1: zooming in shrinks viewSize and drops the rate; zooming out
    // raises it so a downpour still feels like a downpour.
    constexpr float kReferenceArea = 320.0f * 180.0f;
    const float visibleArea = std::max(1.0f, viewSize.x * viewSize.y);
    const float zoomScale = std::clamp(visibleArea / kReferenceArea, 0.25f, 4.0f);
    return baseSpawnRate * m_WeatherIntensity * zoomScale;
}

void ParticleSystem::SpawnWeatherType(WeatherParticleType wpt,
                                      float effectiveRate,
                                      int maxWeatherParticles,
                                      float& spawnTimer,
                                      float deltaTime,
                                      glm::vec2 cameraPos,
                                      glm::vec2 viewSize,
                                      std::array<int, 32>& liveByType,
                                      const WeatherDefinition* streamDef)
{
    auto particleTypeOpt = ResolveWeatherParticle(wpt);
    if (!particleTypeOpt.has_value())
    {
        return;
    }
    if (effectiveRate <= 0.0f)
    {
        return;
    }

    // Hard cap counts only this slot's own particles (matched by ParticleType)
    // so primary and secondary streams don't fight each other for the cap.
    // The census is hoisted to the caller; spawns increment the slot locally.
    int& live = liveByType[static_cast<size_t>(*particleTypeOpt)];
    if (maxWeatherParticles > 0 && live >= maxWeatherParticles)
    {
        spawnTimer = 0.0f;  // Throttle until population drops.
        return;
    }

    spawnTimer += deltaTime;
    float interval = 1.0f / effectiveRate;
    while (spawnTimer >= interval)
    {
        spawnTimer -= interval;
        const size_t before = m_Particles.size();
        SpawnWeatherParticle(*particleTypeOpt, cameraPos, viewSize, streamDef);
        live += static_cast<int>(m_Particles.size() - before);
        if (maxWeatherParticles > 0 && live >= maxWeatherParticles)
        {
            break;
        }
    }
}

void ParticleSystem::SpawnWeatherParticle(ParticleType type,
                                          glm::vec2 cameraPos,
                                          glm::vec2 viewSize,
                                          const WeatherDefinition* streamDef)
{
    // Spawn rect: viewport with 20% overspray. Bias by particle type.
    const float overspray = 0.20f;
    glm::vec2 rectPos = cameraPos - viewSize * overspray;
    glm::vec2 rectSize = viewSize * (1.0f + 2.0f * overspray);

    // Type-specific spawn-edge bias.
    bool leafOrPollenFromLeft = false;
    switch (type)
    {
        case ParticleType::Rain:
        case ParticleType::Snow:
        case ParticleType::Ash:
            // Spawn in the top 10% of the rect so particles fall into view.
            rectSize.y *= 0.10f;
            break;
        case ParticleType::Sand:
            // Wind blows right by default; spawn at the upwind (left) edge.
            rectSize.x *= 0.10f;
            break;
        case ParticleType::Ember:
            // Embers rise from the bottom 20% so they enter from below.
            rectPos.y += rectSize.y * 0.80f;
            rectSize.y *= 0.20f;
            break;
        case ParticleType::DriftingLeaf:
        case ParticleType::Pollen:
            // FallingLeaves / PollenStorm approach from BOTH sides: pick the
            // left or right 10% strip (50/50). Left-edge spawns get velocity.x
            // = 1 so their Update flips the wind X and they drift inward;
            // right-edge spawns ride the default leftward wind into view.
            leafOrPollenFromLeft = m_Dist01(m_Rng) < 0.5f;
            if (leafOrPollenFromLeft)
            {
                rectSize.x *= 0.10f;
            }
            else
            {
                rectPos.x += rectSize.x * 0.90f;
                rectSize.x *= 0.10f;
            }
            break;
        case ParticleType::Fog:
        case ParticleType::CherryBlossom:
        case ParticleType::Firefly:
        default:
            // Spawn anywhere in the visible rect.
            break;
    }

    ParticleZone fakeZone;
    fakeZone.position = rectPos;
    fakeZone.size = rectSize;
    fakeZone.type = type;
    fakeZone.enabled = true;
    fakeZone.noProjection = false;

    int typeIndex = static_cast<int>(type);
    if (typeIndex < 0 || typeIndex >= static_cast<int>(kSpawnDispatch.size()))
        return;
    ParticleSpawnContext ctx{m_Rng, m_Dist01, m_Particles, m_WindDir, m_WindStrength};
    size_t before = m_Particles.size();
    kSpawnDispatch[typeIndex](WEATHER_ZONE_INDEX, fakeZone, ctx);

    // Apply weather size scale to anything the type's Spawn just appended.
    // Allows per-weather "make it bigger" tuning without modifying per-type
    // spawn defaults (used for atmosphere weathers that need denser/larger
    // fog blobs and swarm weathers that benefit from chunkier sprites).
    const float sizeScale = streamDef ? streamDef->particleSizeScale : 1.0f;
    if (sizeScale != 1.0f)
    {
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            m_Particles[i].size *= sizeScale;
        }
    }

    // Wind-driven velocity boost for Snow. Base spawn velocity reads as calm
    // flurries; this ramps it (smoothstep 0.3-0.9) up to the Blizzard look at
    // full strength (7x horizontal along wind, 3.5x fall). The continuous ramp
    // reaches mid-strength states, and all flakes share one drift direction.
    if (type == ParticleType::Snow)
    {
        const float ramp = glm::smoothstep(0.3f, 0.9f, m_WindStrength);
        const float boostX = glm::mix(1.0f, 7.0f, ramp);
        const float boostY = glm::mix(1.0f, 3.5f, ramp);
        const float dirSign = (m_WindDir.x < 0.0f) ? -1.0f : 1.0f;
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            m_Particles[i].velocity.x = std::abs(m_Particles[i].velocity.x) * boostX * dirSign;
            m_Particles[i].velocity.y *= boostY;
        }
    }

    // FallingLeaves / PollenStorm: tag left-edge spawns so their Update flips
    // the wind X component (drifting INTO view from the left), and stretch
    // per-particle lifetime relative to ambient so a leaf or mote can cross
    // a larger fraction of the screen before fading.
    if (type == ParticleType::DriftingLeaf || type == ParticleType::Pollen)
    {
        constexpr float kWeatherLifetimeBoost = 1.5f;
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            if (leafOrPollenFromLeft)
                m_Particles[i].velocity.x = 1.0f;
            m_Particles[i].lifetime *= kWeatherLifetimeBoost;
            m_Particles[i].maxLifetime *= kWeatherLifetimeBoost;
        }
    }

    // Weather-rain splash impact: spread bakedGroundY uniformly across the
    // full visible viewport (camera-relative) so splashes aren't one line and
    // the player can't outrun them vertically. Per-particle lifetime is sized
    // to the travel distance so the splash always fires (re-based each frame).
    if (type == ParticleType::Rain)
    {
        const float minSplash = cameraPos.y + viewSize.y * 0.10f;
        const float maxSplash = cameraPos.y + viewSize.y * 1.05f;
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            Particle& rp = m_Particles[i];
            rp.bakedGroundY = minSplash + m_Dist01(m_Rng) * (maxSplash - minSplash);
            const float travel = std::max(0.0f, rp.bakedGroundY - rp.position.y);
            const float requiredTime = travel / std::max(1.0f, rp.velocity.y);
            rp.lifetime = std::max(2.0f, requiredTime * 1.2f);
            rp.maxLifetime = rp.lifetime;
        }
    }

    // Weather-snow puff impact: same camera-relative wide-Y spread as rain so
    // flakes "land" across the screen instead of on one line. Snow's 15s
    // lifetime covers the travel at Blizzard fall speed (42-77 px/s), so no
    // per-particle lifetime sizing is needed (re-based each frame in Update).
    if (type == ParticleType::Snow)
    {
        const float minImpact = cameraPos.y + viewSize.y * 0.10f;
        const float maxImpact = cameraPos.y + viewSize.y * 1.05f;
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            m_Particles[i].bakedGroundY = minImpact + m_Dist01(m_Rng) * (maxImpact - minImpact);
        }
    }

    // Pre-warm STREAMING weathers (falling Rain/Snow/Ash, rising Ember):
    // pre-advance each particle along its velocity by ageFraction * maxLifetime
    // and cut remaining lifetime by the same fraction, so spawns fill the whole
    // travel column at once instead of only the entry strip.

    // DRIFTING weathers (Fog / Leaf / Pollen) are intentionally NOT pre-aged:
    // they already spawn anywhere in the rect, and full lifetime lets their
    // alpha fade in smoothly instead of popping in at full strength.
    const bool isStreaming = (type == ParticleType::Rain || type == ParticleType::Snow ||
                              type == ParticleType::Ash || type == ParticleType::Ember);
    if (isStreaming)
    {
        for (size_t i = before; i < m_Particles.size(); ++i)
        {
            Particle& p = m_Particles[i];
            const float ageFraction = m_Dist01(m_Rng) * 0.80f;
            const float ageTime = ageFraction * p.maxLifetime;
            p.position += p.velocity * ageTime;
            p.lifetime = p.maxLifetime * (1.0f - ageFraction);
        }
    }
}

void ParticleSystem::Render(IRenderer& renderer,
                            glm::vec2 cameraPos,
                            bool noProjectionOnly,
                            bool renderAll)
{
    // Console toggle: simulation keeps running while rendering is disabled
    // so toggling back on doesn't show a populate-in delay. The debug
    // overlay reports zero drawn while disabled.
    if (!m_RenderEnabled)
    {
        m_LastDrawnCount = 0;
        return;
    }

    // noProjection particles: compute positions while perspective is enabled,
    // then suspend perspective, draw at the computed positions, and resume.

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
            renderer.DrawSpriteAtlas(m_Store->Get(m_AtlasHandle),
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

    m_LastDrawnCount = m_NoProjectionBatch.size() + m_RegularBatch.size();
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
