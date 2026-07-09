#include "ParticleSystem.hpp"

#include "AmbienceConfig.hpp"
#include "Logger.hpp"
#include "MathConstants.hpp"
#include "ProceduralTexture.hpp"
#include "ProjectManifest.hpp"
#include "TextureStore.hpp"
#include "Tilemap.hpp"
#include "WeatherBlend.hpp"
#include "WeatherDefinitions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <tuple>
#include <utility>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Particle";

// Playback mode for strip-backed particle sprites (see BuildAtlas).
enum class ParticleAnimMode : uint8_t
{
    Loop,       // Cycle frames on global time, offset + rate-jittered per particle.
    LifeMapped  // Play the strip exactly once across the particle's lifetime.
};

// Visual spec per ParticleType: each non-null entry is a logical sprite name resolved to an
// on-disk GUID file via the manifest's "particles" links. BuildAtlas prefers the "_strip" sheet,
// else the single frame, else a procedural soft circle; empty lists (Lantern, Sunshine) are
// procedural. A type's multiple entries are variants rolled at spawn by AssignSpawnVariants.
struct ParticleVisuals
{
    const char* variants[ParticleSystem::MAX_PARTICLE_VARIANTS];
    float animFps;  // Loop playback rate; ignored for LifeMapped strips.
    ParticleAnimMode animMode;
    // 0 = spawn rolls among all variants; N = roll among only the first N,
    // reserving later variants as runtime states a behavior switches to
    // (e.g. Bubble pins spawns to variant 0 and flips to the bubblepop
    // strip at the end of its life).
    uint8_t spawnVariantCount{0};
};

// Order MUST match the ParticleType enum.
constexpr ParticleVisuals kParticleVisuals[] = {
    /* Firefly */ {{"firefly", nullptr, nullptr, nullptr}, 7.0f, ParticleAnimMode::Loop},
    /* Rain */ {{"rain", nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* Snow */ {{"snow", "snow2", "snow3", nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Fog */ {{"fog", "fog2", nullptr, nullptr}, 3.0f, ParticleAnimMode::Loop},
    /* Sparkles */
    {{"glitter", "glitter2", "glitter3", nullptr}, 0.0f, ParticleAnimMode::LifeMapped},
    /* Wisp */ {{"wisp", "wisp2", "wisp3", nullptr}, 7.0f, ParticleAnimMode::Loop},
    /* Lantern */ {{nullptr, nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* Sunshine */ {{nullptr, nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* DriftingLeaf */ {{"leaf", "leaf2", "leaf3", nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* DustMote */ {{"dust", "dust2", "dust3", "mote"}, 5.0f, ParticleAnimMode::Loop},
    /* Pollen */ {{"pollen", "pollen2", "pollen3", nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* CherryBlossom */
    {{"cherryblossom", nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* Ash */ {{"ash", nullptr, nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Ember */ {{"ember", "ember2", nullptr, nullptr}, 10.0f, ParticleAnimMode::Loop},
    /* Sand */ {{"sand", nullptr, nullptr, nullptr}, 10.0f, ParticleAnimMode::Loop},
    /* Smoke */ {{"smoke", "smoke2", "smoke3", nullptr}, 5.0f, ParticleAnimMode::Loop},
    /* Steam */ {{"steam", nullptr, nullptr, nullptr}, 8.0f, ParticleAnimMode::Loop},
    /* Aurora */ {{"aurora", "aurora2", "aurora3", nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* Spark */ {{"spark", "spark2", nullptr, nullptr}, 0.0f, ParticleAnimMode::LifeMapped},
    /* PixieDust */
    {{"pixiedust", "pixiedust2", "pixiedust3", nullptr}, 10.0f, ParticleAnimMode::Loop},
    /* Arcane */ {{"arcane", "arcane2", nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Enchant */ {{"enchant", nullptr, nullptr, nullptr}, 7.0f, ParticleAnimMode::Loop},
    /* Runes */ {{"runes", nullptr, nullptr, nullptr}, 4.0f, ParticleAnimMode::Loop},
    /* Hex */ {{"hex", nullptr, nullptr, nullptr}, 5.0f, ParticleAnimMode::Loop},
    /* Curse */ {{"curse", nullptr, nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Void */ {{"void", nullptr, nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Vortex */ {{"vortex", nullptr, nullptr, nullptr}, 10.0f, ParticleAnimMode::Loop},
    /* Soul */ {{"soul", nullptr, nullptr, nullptr}, 5.0f, ParticleAnimMode::Loop},
    /* Fairy */ {{"fairy", nullptr, nullptr, nullptr}, 9.0f, ParticleAnimMode::Loop},
    /* Butterfly */ {{"butterfly", nullptr, nullptr, nullptr}, 8.0f, ParticleAnimMode::Loop},
    /* Bat */ {{"bat", nullptr, nullptr, nullptr}, 10.0f, ParticleAnimMode::Loop},
    /* Bubble */ {{"bubble", "bubblepop", nullptr, nullptr}, 4.0f, ParticleAnimMode::Loop, 1},
    /* Coin */ {{"coin", nullptr, nullptr, nullptr}, 8.0f, ParticleAnimMode::Loop},
    /* Gem */ {{"gem", nullptr, nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Confetti */ {{"confetti", "confetti2", nullptr, nullptr}, 9.0f, ParticleAnimMode::Loop},
    /* Heart */ {{"heart", nullptr, nullptr, nullptr}, 6.0f, ParticleAnimMode::Loop},
    /* Zap */ {{"zap", nullptr, nullptr, nullptr}, 14.0f, ParticleAnimMode::Loop},
    /* Wind */ {{"wind", nullptr, nullptr, nullptr}, 12.0f, ParticleAnimMode::Loop},
    /* Zzz */ {{"zzz", nullptr, nullptr, nullptr}, 3.0f, ParticleAnimMode::Loop},
    /* Constellation */
    {{"constellation", "constellation2", "constellation3", nullptr}, 4.0f, ParticleAnimMode::Loop},
    /* Planet */ {{"planet", nullptr, nullptr, nullptr}, 3.0f, ParticleAnimMode::Loop},
    /* Moon */ {{"moon", nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
    /* Ink */ {{"ink", nullptr, nullptr, nullptr}, 0.0f, ParticleAnimMode::Loop},
};

static_assert(std::size(kParticleVisuals) == EnumTraits<ParticleType>::Count,
              "kParticleVisuals must have one row per ParticleType");

// Fixed atlas width; height grows as needed (see BuildAtlas). Shared with
// the render pass for the per-frame UV inset math.
constexpr int kParticleAtlasWidth = 512;
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

// Cheap 2D pseudo-flow field for billowy drifters (Smoke, Steam, Soul, Ink, ...):
// two incommensurate sine octaves per axis sampled on world position give a smooth
// organic wander that never visibly repeats, with per-particle phase decorrelating
// neighbors. Output is roughly [-1.5, 1.5] per axis - callers scale to px/s.
inline glm::vec2 FlowNoise(glm::vec2 pos, float time, float phase)
{
    const float x = pos.x * 0.020f;
    const float y = pos.y * 0.020f;
    return {std::sin(time * 0.55f + y * 1.7f + phase) +
                0.5f * std::sin(time * 1.31f + y * 3.1f + phase * 2.3f),
            std::cos(time * 0.47f + x * 1.9f + phase * 1.7f) +
                0.5f * std::cos(time * 1.13f + x * 2.7f + phase * 3.1f)};
}

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
            float groundY = zone.position.y + zone.size.y + 20.0f + heightVariation;
            // ONLY for zones taller than the viewport (the title whole-map zone):
            // spread the impact across a wide on-screen band (like HeavyRain/Blizzard
            // bakedGroundY) so the ground reads as an area and splashes aren't off-screen.
            // Normal zones keep their real bottom edge, else splashes appear mid-screen.
            if (zone.size.y > ctx.viewSize.y)
            {
                float spreadT = heightVariation / 60.0f;
                float viewSplashY = ctx.cameraPos.y + ctx.viewSize.y * (0.35f + spreadT * 0.60f);
                groundY = std::min(groundY, viewSplashY);
            }
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
            float groundY = zone.position.y + zone.size.y + 20.0f + heightVariation;
            // Same rule as Rain: only zones taller than the viewport (title
            // whole-map zone) spread impacts across the on-screen band;
            // normal zones puff at their real bottom edge.
            if (zone.size.y > ctx.viewSize.y)
            {
                float spreadT = heightVariation / 60.0f;
                float viewSplashY = ctx.cameraPos.y + ctx.viewSize.y * (0.35f + spreadT * 0.60f);
                groundY = std::min(groundY, viewSplashY);
            }
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

        // Softening multiplier on the puff alpha. Weather fog uses the active weather's
        // fogAlphaMultiplier, clamped to the strongest *designed* softening (0.65) so a
        // fog->clear transition easing that value toward Clear's 1.0 can't flare dying/
        // incoming fog. Editor/console/ambient fog uses a fixed 0.5 (never the 1.0 default).
        constexpr float kMaxFogSoftening = 0.65f;
        const float fogMul = (p.zoneIndex == ParticleSystem::WEATHER_ZONE_INDEX)
                                 ? std::min(ctx.fogAlphaMultiplier, kMaxFogSoftening)
                                 : 0.5f;

        // Atmospheric layering: fog thicker near the ground, thinner up high.
        // Anchored on the player's feet (playerPos.y) so the gradient tracks
        // the scene's ground line as the player moves vertically. Floor at
        // 0.3 keeps the very top of the world at 30% rather than vanishing.
        const float groundRefY = ctx.playerPos.y + 40.0f;
        constexpr float kFadeRange = 200.0f;
        const float verticalFactor =
            std::clamp(1.0f - (groundRefY - p.position.y) / kFadeRange, 0.3f, 1.0f);

        // Master fog knob: base opacity every fog puff starts from, before per-source softening
        // (fogMul) and atmospheric factors above. Raised 0.25 -> 0.40 to undo an anti-"wall"
        // over-thinning while holding the per-puff peak under the old wall (~0.30 vs ~0.36).
        // Weather fog is cap-bound (2500 puffs), so per-puff alpha - not rate - moves fog density.
        constexpr float kBaseAlpha = 0.40f;
        p.color.a = pulse * lifeFade * fadeIn * kBaseAlpha * fogMul * dayBoost * nightReduce *
                    verticalFactor;
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
        // Fast attack, smooth quadratic decay - reads as a twinkle rather
        // than a hard strobe, and doubles as the rain-splash / snow-puff
        // droplet envelope. The 4-frame glitter strip plays once across the
        // same window (life-mapped in the render pass).
        float lifeRatio = 1.0f - (p.lifetime / p.maxLifetime);
        float attack = std::min(1.0f, lifeRatio / 0.12f);
        float decay = 1.0f - lifeRatio;
        p.color.a = attack * decay * decay * 0.85f;
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

        // The attack envelope in Update fades in from zero; spawning at the
        // roulette's alpha 1.0 would render one full-bright frame first.
        p.color.a = 0.0f;

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

        // Downstream lean on the gusted wind (~10 px/s at the calm 0.5
        // anchor, doubling in storms) so AshFall answers windIntensity.
        p.position.x += ctx.windDir.x * 20.0f * ctx.windStrength * ctx.deltaTime;

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

// Smoke - rising, expanding puffs for chimneys and campfires. Distinct from
// Fog: smoke climbs and bends downstream on the wind instead of hanging.

template <>
struct ParticleBehavior<ParticleType::Smoke>
{
    static constexpr float SpawnRate = 6.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Buoyant rise comes from the spawn velocity; flow noise billows the
        // column and the prevailing wind bends it downstream. Sway widens
        // with age so the plume opens up as it climbs.
        const float age = 1.0f - p.lifetime / p.maxLifetime;
        const glm::vec2 flow = FlowNoise(p.position, ctx.time, p.phase);
        p.position.x += (flow.x * (4.0f + 14.0f * age) + ctx.windDir.x * 14.0f * ctx.windStrength) *
                        ctx.deltaTime;
        p.position.y += flow.y * 2.5f * ctx.deltaTime;

        // Puffs expand as they rise and thin out toward the end.
        p.size += (3.0f + 2.0f * (0.5f + 0.5f * std::sin(p.phase))) * ctx.deltaTime;

        float fade = std::min(age / 0.12f, (1.0f - age) / 0.45f);
        float waver = 0.9f + 0.1f * std::sin(ctx.time * 0.8f + p.phase);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * waver * 0.5f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Smoke;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 4.0f;
        p.velocity.y = -(14.0f + ctx.dist(ctx.rng) * 10.0f);  // Buoyant rise.
        // Near-white with a whisper of warmth so campfire smoke reads as light
        // vapor - not a dark grey blob - and doesn't tint blue against warm
        // scenes. The behavior's 0.5 alpha ceiling keeps it a bit see-through.
        float grey = 0.82f + ctx.dist(ctx.rng) * 0.13f;
        p.color = glm::vec4(grey * 1.04f, grey, grey * 0.98f, 0.0f);
        p.size = 8.0f + ctx.dist(ctx.rng) * 6.0f;
        p.lifetime = 6.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Steam - fast-rising, short-lived white vapor for vents and hot springs.

template <>
struct ParticleBehavior<ParticleType::Steam>
{
    static constexpr float SpawnRate = 9.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        const float age = 1.0f - p.lifetime / p.maxLifetime;
        const glm::vec2 flow = FlowNoise(p.position, ctx.time, p.phase);
        p.position.x += flow.x * (3.0f + 6.0f * age) * ctx.deltaTime;

        // Vapor expands quickly and dissipates before it travels far.
        p.size += 5.0f * ctx.deltaTime;

        float fade = std::min(age / 0.10f, (1.0f - age) / 0.55f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * 0.45f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Steam;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 6.0f;
        p.velocity.y = -(28.0f + ctx.dist(ctx.rng) * 14.0f);
        float bright = 0.88f + ctx.dist(ctx.rng) * 0.12f;
        p.color = glm::vec4(bright, bright, bright, 0.0f);
        p.size = 6.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 1.8f + ctx.dist(ctx.rng) * 1.2f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Aurora - soft hand-painted aurora motes riding slow sky ribbons. The three
// sprite variants carry the hue spread; tints stay near-white so the pixel
// art keeps its own colors. Night-gated so noon auroras don't glare.

template <>
struct ParticleBehavior<ParticleType::Aurora>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Slow ribbon drift: long horizontal wave, gentle vertical bob.
        p.position.x += std::sin(ctx.time * 0.30f + p.phase) * 8.0f * ctx.deltaTime;
        p.position.y += std::cos(ctx.time * 0.22f + p.phase * 0.7f) * 4.0f * ctx.deltaTime;

        float wave = 0.55f + 0.45f * std::sin(ctx.time * 0.6f + p.phase * 1.9f);
        float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.30f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);
        float nightBoost = 0.35f + 0.65f * ctx.nightFactor;
        p.color.a = wave * lifeFade * fadeIn * nightBoost * 0.55f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Aurora;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        // Light cool casts over the painted sprite - emerald / cyan / violet.
        float cast = ctx.dist(ctx.rng);
        if (cast < 0.35f)
        {
            p.color = glm::vec4(0.75f, 1.0f, 0.88f, 0.0f);
        }
        else if (cast < 0.70f)
        {
            p.color = glm::vec4(0.72f, 0.92f, 1.0f, 0.0f);
        }
        else
        {
            p.color = glm::vec4(0.88f, 0.78f, 1.0f, 0.0f);
        }
        p.size = 5.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 8.0f + ctx.dist(ctx.rng) * 6.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Spark - energetic darting crackle: random-walk impulses with velocity
// damping, one bright life-mapped burst of the 4-frame strip.

template <>
struct ParticleBehavior<ParticleType::Spark>
{
    static constexpr float SpawnRate = 10.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Jittery dart: per-frame random impulses (shared RNG stream) with damping so sparks
        // skitter then settle before dying. Random-walk variance accumulates per step, so the
        // impulse scales by sqrt(dt) - linear dt scaling would make the skitter energy
        // framerate-dependent (76 * sqrt(1/60) matches the tuned 60 FPS look).
        if (ctx.rng && ctx.dist)
        {
            const float impulse = 76.0f * std::sqrt(ctx.deltaTime);
            p.velocity.x += ((*ctx.dist)(*ctx.rng) - 0.5f) * impulse;
            p.velocity.y += ((*ctx.dist)(*ctx.rng) - 0.5f) * impulse;
        }
        p.velocity *= std::max(0.0f, 1.0f - 3.0f * ctx.deltaTime);

        float lifeRatio = 1.0f - p.lifetime / p.maxLifetime;
        float attack = std::min(1.0f, lifeRatio / 0.08f);
        float decay = 1.0f - lifeRatio;
        p.color.a = attack * decay * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Spark;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 80.0f;
        p.velocity.y = (ctx.dist(ctx.rng) - 0.5f) * 80.0f;
        // 50/50 white-gold forge spark vs electric blue.
        if (ctx.dist(ctx.rng) < 0.5f)
        {
            p.color = glm::vec4(1.0f, 0.92f + ctx.dist(ctx.rng) * 0.08f, 0.55f, 0.0f);
        }
        else
        {
            p.color = glm::vec4(0.70f, 0.85f + ctx.dist(ctx.rng) * 0.10f, 1.0f, 0.0f);
        }
        p.size = 3.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 0.4f + ctx.dist(ctx.rng) * 0.5f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// PixieDust - falling glitter-trail dust with a heavy twinkle. The three
// sprite variants carry the color spread.

template <>
struct ParticleBehavior<ParticleType::PixieDust>
{
    static constexpr float SpawnRate = 8.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.x += std::sin(ctx.time * 2.2f + p.phase) * 6.0f * ctx.deltaTime;

        // High-contrast twinkle at full peak alpha so the per-variant colors
        // read saturated instead of washing out in the additive blend.
        float twinkle = 0.5f + 0.5f * std::abs(std::sin(ctx.time * 6.0f + p.phase * 2.0f));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.3f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.8f);
        p.color.a = twinkle * fadeIn * lifeFade;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::PixieDust;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 0.0f;
        p.velocity.y = 8.0f + ctx.dist(ctx.rng) * 8.0f;  // Gentle glitter-fall.
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);     // Variants carry the color.
        p.size = 4.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 2.0f + ctx.dist(ctx.rng) * 2.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Arcane - violet glyph motes orbiting their spawn point with a slow rise.

template <>
struct ParticleBehavior<ParticleType::Arcane>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Circular orbit expressed as the derivative of a circle so no orbit
        // center needs storing; radius/frequency personalized by phase.
        const float radius = 7.0f + 3.0f * std::sin(p.phase * 2.0f);
        const float omega = 1.6f + 0.3f * std::cos(p.phase);
        const float theta = ctx.time * omega + p.phase;
        p.position.x += -std::sin(theta) * radius * omega * ctx.deltaTime;
        p.position.y += (std::cos(theta) * radius * omega - 6.0f) * ctx.deltaTime;

        float pulse = 0.55f + 0.45f * std::sin(ctx.time * 1.1f + p.phase * 1.6f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.6f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = pulse * fadeIn * lifeFade * 0.7f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Arcane;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(0.82f + ctx.dist(ctx.rng) * 0.15f,
                            0.72f + ctx.dist(ctx.rng) * 0.12f,
                            1.0f,
                            0.0f);  // Light violet cast over the glyph art.
        p.size = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Enchant - enchantment-table glyphs: launch upward, decelerate to a hover,
// fade out while still climbing softly.

template <>
struct ParticleBehavior<ParticleType::Enchant>
{
    static constexpr float SpawnRate = 6.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Ease the launch velocity toward a slow terminal climb.
        const float easeRate = std::min(1.0f, 1.4f * ctx.deltaTime);
        p.velocity.y += (-6.0f - p.velocity.y) * easeRate;
        p.position.x += std::sin(ctx.time * 2.4f + p.phase) * 4.0f * ctx.deltaTime;

        float shimmer = 0.85f + 0.15f * std::sin(ctx.time * 5.0f + p.phase * 2.0f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.2f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.9f);
        p.color.a = shimmer * fadeIn * lifeFade * 0.8f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Enchant;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 0.0f;
        p.velocity.y = -(30.0f + ctx.dist(ctx.rng) * 16.0f);  // Launch, then ease.
        p.color = glm::vec4(0.75f, 0.95f + ctx.dist(ctx.rng) * 0.05f, 1.0f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 2.5f;
        p.lifetime = 3.0f + ctx.dist(ctx.rng) * 2.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Runes - near-stationary sigils turning slowly with a deep glow pulse.

template <>
struct ParticleBehavior<ParticleType::Runes>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y -= 3.0f * ctx.deltaTime;
        float rotSpeed = (std::fmod(p.phase, 2.0f) < 1.0f) ? 12.0f : -12.0f;
        p.rotation += rotSpeed * ctx.deltaTime;

        float pulse = 0.50f + 0.50f * std::sin(ctx.time * 0.8f + p.phase);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.8f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.2f);
        p.color.a = (0.25f + 0.55f * pulse) * fadeIn * lifeFade;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Runes;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(1.0f, 0.80f + ctx.dist(ctx.rng) * 0.10f, 0.45f, 0.0f);  // Warm gold.
        p.size = 6.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Hex - counter-orbiting witch-magic motes with an eerie double pulse.

template <>
struct ParticleBehavior<ParticleType::Hex>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Counter-clockwise orbit (mirror of Arcane) - hex zones swirling the
        // opposite way reads as opposing schools of magic side by side.
        const float radius = 9.0f + 3.0f * std::cos(p.phase * 1.4f);
        const float omega = 1.1f + 0.2f * std::sin(p.phase);
        const float theta = -(ctx.time * omega + p.phase);
        p.position.x += -std::sin(theta) * radius * omega * ctx.deltaTime;
        p.position.y += (std::cos(theta) * radius * omega - 3.0f) * ctx.deltaTime;

        float slowPulse = 0.5f + 0.5f * std::sin(ctx.time * 0.9f + p.phase);
        float fastPulse = 0.8f + 0.2f * std::sin(ctx.time * 3.7f + p.phase * 2.0f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.7f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = slowPulse * fastPulse * fadeIn * lifeFade * 0.65f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Hex;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        // Sickly green / witch purple split.
        if (ctx.dist(ctx.rng) < 0.5f)
        {
            p.color = glm::vec4(0.60f + ctx.dist(ctx.rng) * 0.15f, 1.0f, 0.50f, 0.0f);
        }
        else
        {
            p.color = glm::vec4(0.80f + ctx.dist(ctx.rng) * 0.10f, 0.50f, 1.0f, 0.0f);
        }
        p.size = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Curse - dark taint that wobbles upward with an irregular, unsettling
// flicker. Alpha-blended dark tones (additive would wash the darkness out).

template <>
struct ParticleBehavior<ParticleType::Curse>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.x += std::sin(ctx.time * 1.3f + p.phase) * 10.0f * ctx.deltaTime;
        p.position.y -= 8.0f * ctx.deltaTime;

        // Two incommensurate flickers multiply into an uneasy stutter.
        float flicker = (0.70f + 0.30f * std::sin(ctx.time * 6.3f + p.phase * 3.0f)) *
                        (0.75f + 0.25f * std::sin(ctx.time * 1.7f + p.phase));
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.5f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = flicker * fadeIn * lifeFade * 0.75f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Curse;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(0.35f + ctx.dist(ctx.rng) * 0.10f,
                            0.20f,
                            0.45f + ctx.dist(ctx.rng) * 0.10f,
                            0.0f);  // Bruised purple-black.
        p.size = 6.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 3.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Void - dark matter on a decaying spiral: the tangential spawn velocity is
// rotated and damped every frame, so each mote curls inward and stalls.

template <>
struct ParticleBehavior<ParticleType::Void>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Rotate the velocity vector while bleeding speed - an inward spiral
        // without needing to store an orbit center.
        const float turn = 2.4f * ctx.deltaTime;
        const float cs = std::cos(turn);
        const float sn = std::sin(turn);
        p.velocity = glm::vec2(p.velocity.x * cs - p.velocity.y * sn,
                               p.velocity.x * sn + p.velocity.y * cs) *
                     std::max(0.0f, 1.0f - 0.55f * ctx.deltaTime);

        // Motes contract as they wind down.
        p.size = std::max(2.0f, p.size - 1.2f * ctx.deltaTime);

        float age = 1.0f - p.lifetime / p.maxLifetime;
        float fade = std::min(age / 0.25f, (1.0f - age) / 0.30f);
        float pulse = 0.85f + 0.15f * std::sin(ctx.time * 2.2f + p.phase);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * pulse * 0.8f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Void;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        // Tangential launch in a random direction; Update curls it inward.
        float angle = ctx.dist(ctx.rng) * 6.28f;
        float speed = 25.0f + ctx.dist(ctx.rng) * 20.0f;
        p.velocity = glm::vec2(std::cos(angle), std::sin(angle)) * speed;
        p.color = glm::vec4(0.30f, 0.18f + ctx.dist(ctx.rng) * 0.07f, 0.50f, 0.0f);
        p.size = 6.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 3.0f + ctx.dist(ctx.rng) * 2.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Vortex - fast bright swirl: same rotate-and-damp trick as Void but quicker,
// lighter, and climbing.

template <>
struct ParticleBehavior<ParticleType::Vortex>
{
    static constexpr float SpawnRate = 6.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        const float turn = 5.5f * ctx.deltaTime;
        const float cs = std::cos(turn);
        const float sn = std::sin(turn);
        p.velocity = glm::vec2(p.velocity.x * cs - p.velocity.y * sn,
                               p.velocity.x * sn + p.velocity.y * cs) *
                     std::max(0.0f, 1.0f - 0.25f * ctx.deltaTime);
        p.position.y -= 8.0f * ctx.deltaTime;  // The swirl climbs.

        float age = 1.0f - p.lifetime / p.maxLifetime;
        float fade = std::min(age / 0.15f, (1.0f - age) / 0.25f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * 0.7f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Vortex;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        float angle = ctx.dist(ctx.rng) * 6.28f;
        float speed = 40.0f + ctx.dist(ctx.rng) * 30.0f;
        p.velocity = glm::vec2(std::cos(angle), std::sin(angle)) * speed;
        p.color = glm::vec4(0.80f, 0.95f, 1.0f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 2.0f + ctx.dist(ctx.rng) * 2.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Soul - ghostly wisp climbing in a slow S-curve with breathing pauses.

template <>
struct ParticleBehavior<ParticleType::Soul>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Two-octave wander plus a slow speed pulse - the soul hesitates,
        // then presses on.
        p.position.x += (std::sin(ctx.time * 0.8f + p.phase) * 14.0f +
                         std::sin(ctx.time * 2.1f + p.phase * 2.3f) * 4.0f) *
                        ctx.deltaTime;
        float climb = 0.6f + 0.4f * std::sin(ctx.time * 0.5f + p.phase);
        p.position.y += p.velocity.y * (climb - 1.0f) * ctx.deltaTime;

        float breathe = 0.55f + 0.45f * std::sin(ctx.time * 1.4f + p.phase * 1.7f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 1.0f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.5f);
        p.color.a = breathe * fadeIn * lifeFade * 0.65f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Soul;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 0.0f;
        p.velocity.y = -(10.0f + ctx.dist(ctx.rng) * 8.0f);
        // Pale spirit blue-green, occasionally warmer.
        if (ctx.dist(ctx.rng) < 0.75f)
        {
            p.color = glm::vec4(0.78f, 0.95f, 1.0f, 0.0f);
        }
        else
        {
            p.color = glm::vec4(0.80f, 1.0f, 0.85f, 0.0f);
        }
        p.size = 6.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 5.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Fairy - hover-and-dash: mostly a tight hover, with an occasional darting
// surge whose direction is fixed per particle.

template <>
struct ParticleBehavior<ParticleType::Fairy>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Dash gate: cubed positive sine spends most of its time near zero,
        // then surges - a quick darting burst every few seconds.
        float gate = std::max(0.0f, std::sin(ctx.time * 0.7f + p.phase));
        gate = gate * gate * gate;
        const float dashDir = (std::sin(p.phase * 3.0f) >= 0.0f) ? 1.0f : -1.0f;

        p.position.x +=
            (std::sin(ctx.time * 2.6f + p.phase) * 18.0f + gate * 70.0f * dashDir) * ctx.deltaTime;
        p.position.y += std::cos(ctx.time * 3.1f + p.phase * 1.3f) * 14.0f * ctx.deltaTime;

        float glow = 0.60f + 0.40f * std::sin(ctx.time * 9.0f + p.phase * 2.0f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.5f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = glow * fadeIn * lifeFade * 0.85f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Fairy;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(1.0f, 0.95f + ctx.dist(ctx.rng) * 0.05f, 0.85f, 0.0f);
        p.size = 4.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Butterfly - daylight meadow flier: slow cruise with wing-beat bob and a
// lazy wander arc. Occasionally spawns as a pair.

template <>
struct ParticleBehavior<ParticleType::Butterfly>
{
    static constexpr float SpawnRate = 2.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Wing-beat bob (fast, small) over a lazy wander arc (slow, wide).
        p.position.y += (std::sin(ctx.time * 5.5f + p.phase) * 10.0f +
                         std::cos(ctx.time * 0.6f + p.phase * 0.8f) * 8.0f) *
                        ctx.deltaTime;

        // Butterflies dim (but stay readable) at night.
        float dayFactor = 1.0f - ctx.nightFactor * 0.4f;
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.6f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.2f);
        p.color.a = fadeIn * lifeFade * dayFactor;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        auto spawnOne = [&](glm::vec2 offset)
        {
            Particle p;
            p.zoneIndex = zoneIndex;
            p.type = ParticleType::Butterfly;
            p.noProjection = zone.noProjection;
            p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x + offset.x;
            p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y + offset.y;
            float dir = (ctx.dist(ctx.rng) < 0.5f) ? -1.0f : 1.0f;
            p.velocity.x = dir * (12.0f + ctx.dist(ctx.rng) * 10.0f);
            p.velocity.y = 0.0f;
            p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);  // The sprite carries its own colors.
            p.size = 8.0f + ctx.dist(ctx.rng) * 4.0f;
            p.lifetime = 8.0f + ctx.dist(ctx.rng) * 6.0f;
            p.maxLifetime = p.lifetime;
            p.phase = ctx.dist(ctx.rng) * 6.28f;
            p.rotation = 0.0f;
            p.additive = false;
            ctx.particles.push_back(p);
        };
        spawnOne(glm::vec2(0.0f));
        // Butterflies often travel in pairs - 25% chance of a companion.
        if (ctx.dist(ctx.rng) < 0.25f)
        {
            spawnOne(
                glm::vec2((ctx.dist(ctx.rng) - 0.5f) * 24.0f, (ctx.dist(ctx.rng) - 0.5f) * 16.0f));
        }
    }
};

// Bat - dusk flier: fast cruise, deep swoops, quick jinks.

template <>
struct ParticleBehavior<ParticleType::Bat>
{
    static constexpr float SpawnRate = 2.5f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y += std::sin(ctx.time * 2.3f + p.phase) * 38.0f * ctx.deltaTime;  // Swoop.
        p.position.x +=
            std::sin(ctx.time * 7.0f + p.phase * 2.0f) * 10.0f * ctx.deltaTime;  // Jink.

        // Dusk/night fliers; faint in daylight.
        float duskFactor = 0.30f + 0.70f * ctx.nightFactor;
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.5f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.0f);
        p.color.a = fadeIn * lifeFade * duskFactor * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Bat;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        float dir = (ctx.dist(ctx.rng) < 0.5f) ? -1.0f : 1.0f;
        p.velocity.x = dir * (45.0f + ctx.dist(ctx.rng) * 30.0f);
        p.velocity.y = 0.0f;
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        p.size = 8.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 6.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Bubble - buoyant wobbling bubble that accelerates gently upward, then
// converts itself into its pop: spawns pin variant 0 (the looping bubble
// sheen), and the final 0.28s switches to variant 1 (the bubblepop strip),
// which the draw pass plays life-mapped.

template <>
struct ParticleBehavior<ParticleType::Bubble>
{
    static constexpr float SpawnRate = 5.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Pop phase: hold position and collapse alpha while the strip plays.
        if (p.variant == 1)
        {
            p.color.a = (p.lifetime / p.maxLifetime) * 0.9f;
            return;
        }

        p.position.x += std::sin(ctx.time * 3.0f + p.phase) * 7.0f * ctx.deltaTime;
        p.velocity.y -= 3.0f * ctx.deltaTime;  // Buoyancy: rise speeds up.

        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.3f);
        p.color.a = fadeIn * 0.55f;

        // Expiring: convert in place to the pop. The update loop decrements lifetime BEFORE
        // dispatching and skips dead particles, so gating on this frame's dt alone would drop
        // the pop when a larger next dt crosses zero in the skip branch. Converting once life is
        // within the frame clamp (MAX_DELTA_TIME 0.1s) closes it; the <=0.1s early pop is unseen.
        const float popWindow = std::max(ctx.deltaTime, 0.1f);
        if (p.lifetime <= popWindow)
        {
            p.variant = 1;
            p.velocity = glm::vec2(0.0f);
            p.color = glm::vec4(0.90f, 0.97f, 1.0f, 0.9f);
            p.size *= 1.15f;
            p.lifetime = 0.28f;
            p.maxLifetime = p.lifetime;
        }
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Bubble;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = (ctx.dist(ctx.rng) - 0.5f) * 8.0f;
        p.velocity.y = -(15.0f + ctx.dist(ctx.rng) * 13.0f);
        p.color = glm::vec4(0.85f, 0.95f, 1.0f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 4.0f;
        // Short-ish lives keep pops frequent enough to read as fizz.
        p.lifetime = 3.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Coin - treasure glint: the strip carries the spin; motion is a soft bob.

template <>
struct ParticleBehavior<ParticleType::Coin>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y += std::sin(ctx.time * 2.0f + p.phase) * 5.0f * ctx.deltaTime;

        float glint = 0.85f + 0.15f * std::sin(ctx.time * 4.0f + p.phase * 2.0f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.3f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.5f);
        p.color.a = glint * fadeIn * lifeFade * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Coin;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);  // Gold lives in the sprite.
        p.size = 6.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 3.0f + ctx.dist(ctx.rng) * 3.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Gem - floating jewel with a slow bob and periodic sparkle pulse.

template <>
struct ParticleBehavior<ParticleType::Gem>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y += std::sin(ctx.time * 1.3f + p.phase) * 4.0f * ctx.deltaTime;

        float sparkle = 0.75f + 0.25f * std::sin(ctx.time * 4.0f + p.phase * 3.0f);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.4f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.7f);
        p.color.a = sparkle * fadeIn * lifeFade * 0.9f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Gem;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        p.size = 7.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 4.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Confetti - celebration popper: rare spawn ticks, each firing a whole fan
// of scraps from one point. The scraps launch up and outward, gravity reins
// them in, then they flutter down with growing zigzag and fast tumble.

template <>
struct ParticleBehavior<ParticleType::Confetti>
{
    // Deliberately sparse: each tick is a full burst, so the rate is the
    // poppers-per-second cadence, not a scraps-per-second trickle.
    static constexpr float SpawnRate = 0.4f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Gravity pulls the launch into a gentle terminal flutter-fall.
        p.velocity.y = std::min(p.velocity.y + 220.0f * ctx.deltaTime, 40.0f);
        p.velocity.x *= std::max(0.0f, 1.0f - 1.8f * ctx.deltaTime);  // Air drag.

        // Zigzag grows as the launch energy is spent, so scraps fly clean
        // out of the pop and only start swaying once they flutter.
        const float age = 1.0f - p.lifetime / p.maxLifetime;
        const float flutter = std::min(1.0f, age * 2.5f);
        p.position.x += std::sin(ctx.time * 3.3f + p.phase) * 26.0f * flutter * ctx.deltaTime;

        float rotSpeed = 180.0f + (p.phase / 6.28f) * 140.0f;
        if (std::fmod(p.phase, 2.0f) < 1.0f)
        {
            rotSpeed = -rotSpeed;
        }
        p.rotation += rotSpeed * ctx.deltaTime;

        float fadeIn = std::min(1.0f, age / 0.03f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.5f);
        p.color.a = fadeIn * lifeFade * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        // One popper: a fan of scraps ejected from a single point, angled
        // mostly upward with a wide spread.
        const glm::vec2 origin(zone.position.x + ctx.dist(ctx.rng) * zone.size.x,
                               zone.position.y + ctx.dist(ctx.rng) * zone.size.y);
        const int burst = 14 + static_cast<int>(ctx.dist(ctx.rng) * 7.0f);  // 14-20
        for (int i = 0; i < burst; ++i)
        {
            Particle p;
            p.zoneIndex = zoneIndex;
            p.type = ParticleType::Confetti;
            p.noProjection = zone.noProjection;
            p.position = origin;
            // Launch cone: straight up +/- ~55 degrees, speed 90-190 px/s.
            const float spread = (ctx.dist(ctx.rng) - 0.5f) * 1.92f;
            const float speed = 90.0f + ctx.dist(ctx.rng) * 100.0f;
            p.velocity.x = std::sin(spread) * speed;
            p.velocity.y = -std::cos(spread) * speed;
            static constexpr glm::vec3 kTints[6] = {
                {1.00f, 0.35f, 0.40f},  // Red
                {0.35f, 0.60f, 1.00f},  // Blue
                {1.00f, 0.85f, 0.30f},  // Yellow
                {0.40f, 0.90f, 0.50f},  // Green
                {1.00f, 0.50f, 0.90f},  // Pink
                {0.60f, 0.45f, 1.00f},  // Purple
            };
            const int tint = std::min(5, static_cast<int>(ctx.dist(ctx.rng) * 6.0f));
            p.color = glm::vec4(kTints[tint], 0.0f);
            p.size = 4.0f + ctx.dist(ctx.rng) * 3.0f;
            p.lifetime = 2.2f + ctx.dist(ctx.rng) * 1.3f;
            p.maxLifetime = p.lifetime;
            p.phase = ctx.dist(ctx.rng) * 6.28f;
            p.rotation = ctx.dist(ctx.rng) * 360.0f;
            p.additive = false;
            ctx.particles.push_back(p);
        }
    }
};

// Heart - affection emote: eases from a launch into a slow float, swaying.

template <>
struct ParticleBehavior<ParticleType::Heart>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        const float easeRate = std::min(1.0f, 2.0f * ctx.deltaTime);
        p.velocity.y += (-8.0f - p.velocity.y) * easeRate;
        p.position.x += std::sin(ctx.time * 2.8f + p.phase) * 6.0f * ctx.deltaTime;
        p.size += 2.0f * ctx.deltaTime;  // Swells gently as it floats away.

        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.15f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.6f);
        p.color.a = fadeIn * lifeFade * 0.95f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Heart;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 0.0f;
        p.velocity.y = -(26.0f + ctx.dist(ctx.rng) * 12.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        p.size = 6.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 1.6f + ctx.dist(ctx.rng) * 1.2f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Zap - electric arc: hard strobe alpha plus positional jitter, very brief.

template <>
struct ParticleBehavior<ParticleType::Zap>
{
    static constexpr float SpawnRate = 8.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        // Arcs teleport in tiny hops rather than glide. sqrt(dt) scaling
        // keeps the random-walk wander framerate-independent (see Spark).
        if (ctx.rng && ctx.dist)
        {
            const float hop = 20.0f * std::sqrt(ctx.deltaTime);
            p.position.x += ((*ctx.dist)(*ctx.rng) - 0.5f) * hop;
            p.position.y += ((*ctx.dist)(*ctx.rng) - 0.5f) * hop;
        }

        float strobe = (std::sin(ctx.time * 22.0f + p.phase * 7.0f) > 0.2f) ? 0.95f : 0.15f;
        float lifeFade = std::min(1.0f, p.lifetime / 0.1f);
        p.color.a = strobe * lifeFade;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Zap;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(0.75f, 0.85f + ctx.dist(ctx.rng) * 0.10f, 1.0f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 0.35f + ctx.dist(ctx.rng) * 0.35f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Wind - fast gust streaks. Same +X travel convention as Sand (the weather
// spawn edge assumes rightward travel); the draw pass stretches the sprite
// along the travel axis.

template <>
struct ParticleBehavior<ParticleType::Wind>
{
    static constexpr float SpawnRate = 6.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y += std::sin(ctx.time * 4.0f + p.phase) * 6.0f * ctx.deltaTime;

        float fade = std::min(p.lifetime / 0.25f, (p.maxLifetime - p.lifetime) / 0.15f);
        p.color.a = std::clamp(fade, 0.0f, 1.0f) * 0.40f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Wind;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        const float windScale = 2.0f * ctx.windStrength;
        p.velocity.x = (110.0f + ctx.dist(ctx.rng) * 120.0f) * windScale;
        p.velocity.y = (ctx.dist(ctx.rng) - 0.5f) * 10.0f;
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        p.size = 7.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 0.9f + ctx.dist(ctx.rng) * 0.7f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Zzz - sleep emote: eases upward while drifting aside, swelling slightly.

template <>
struct ParticleBehavior<ParticleType::Zzz>
{
    static constexpr float SpawnRate = 2.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        const float easeRate = std::min(1.0f, 1.2f * ctx.deltaTime);
        p.velocity.y += (-5.0f - p.velocity.y) * easeRate;
        p.position.x += std::sin(ctx.time * 1.6f + p.phase) * 3.0f * ctx.deltaTime;
        p.size += 2.5f * ctx.deltaTime;

        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.2f);
        float lifeFade = std::min(1.0f, p.lifetime / 0.5f);
        p.color.a = fadeIn * lifeFade * 0.9f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Zzz;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity.x = 6.0f + ctx.dist(ctx.rng) * 6.0f;  // Classic up-and-right drift.
        p.velocity.y = -(20.0f + ctx.dist(ctx.rng) * 8.0f);
        p.color = glm::vec4(0.90f, 0.88f, 1.0f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 2.0f;
        p.lifetime = 2.5f + ctx.dist(ctx.rng) * 1.5f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Constellation - near-stationary star twinkle for night events; brightest
// after dark, whisper-faint by day.

template <>
struct ParticleBehavior<ParticleType::Constellation>
{
    static constexpr float SpawnRate = 3.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.x += std::sin(ctx.time * 0.5f + p.phase) * 1.5f * ctx.deltaTime;

        float twinkle = 0.35f + 0.40f * std::abs(std::sin(ctx.time * 2.3f + p.phase * 1.6f));
        float nightBoost = 0.30f + 0.70f * ctx.nightFactor;
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 1.5f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.5f);
        p.color.a = twinkle * nightBoost * fadeIn * lifeFade;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Constellation;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(0.90f, 0.95f, 1.0f, 0.0f);
        p.size = 4.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 6.0f + ctx.dist(ctx.rng) * 6.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = true;
        ctx.particles.push_back(p);
    }
};

// Planet - very slow celestial accent, night-leaning steady glow.

template <>
struct ParticleBehavior<ParticleType::Planet>
{
    static constexpr float SpawnRate = 1.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.x += std::sin(ctx.time * 0.2f + p.phase) * 3.0f * ctx.deltaTime;
        p.position.y += std::cos(ctx.time * 0.15f + p.phase * 0.7f) * 1.5f * ctx.deltaTime;

        float pulse = 0.85f + 0.15f * std::sin(ctx.time * 0.7f + p.phase);
        float nightBoost = 0.25f + 0.75f * ctx.nightFactor;
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);
        float lifeFade = std::min(1.0f, p.lifetime / 2.0f);
        p.color.a = pulse * nightBoost * fadeIn * lifeFade * 0.85f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Planet;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        p.size = 8.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 10.0f + ctx.dist(ctx.rng) * 6.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Moon - stationary crescent accent with a soft night-gated glow pulse.

template <>
struct ParticleBehavior<ParticleType::Moon>
{
    static constexpr float SpawnRate = 0.8f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        p.position.y += std::sin(ctx.time * 0.8f + p.phase) * 2.0f * ctx.deltaTime;

        float pulse = 0.80f + 0.20f * std::sin(ctx.time * 1.1f + p.phase);
        float nightBoost = 0.20f + 0.80f * ctx.nightFactor;
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);
        float lifeFade = std::min(1.0f, p.lifetime / 2.0f);
        p.color.a = pulse * nightBoost * fadeIn * lifeFade * 0.9f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Moon;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        // Lunar palette roulette: pale gold, silver-blue, harvest orange,
        // blood moon, and a rare lavender.
        float lunarRoll = ctx.dist(ctx.rng);
        if (lunarRoll < 0.35f)
        {
            p.color = glm::vec4(1.0f, 0.97f, 0.88f, 0.0f);  // Pale gold.
        }
        else if (lunarRoll < 0.60f)
        {
            p.color = glm::vec4(0.80f, 0.88f, 1.0f, 0.0f);  // Silver-blue.
        }
        else if (lunarRoll < 0.80f)
        {
            p.color = glm::vec4(1.0f, 0.75f, 0.50f, 0.0f);  // Harvest orange.
        }
        else if (lunarRoll < 0.92f)
        {
            p.color = glm::vec4(1.0f, 0.50f, 0.42f, 0.0f);  // Blood moon.
        }
        else
        {
            p.color = glm::vec4(0.88f, 0.78f, 1.0f, 0.0f);  // Lavender (rare).
        }
        p.size = 8.0f + ctx.dist(ctx.rng) * 4.0f;
        p.lifetime = 10.0f + ctx.dist(ctx.rng) * 6.0f;
        p.maxLifetime = p.lifetime;
        p.phase = ctx.dist(ctx.rng) * 6.28f;
        p.rotation = 0.0f;
        p.additive = false;
        ctx.particles.push_back(p);
    }
};

// Ink - dark blot hovering in place like a rune sigil, billowing softly on
// the flow field with a slow climb and a gentle pulse.

template <>
struct ParticleBehavior<ParticleType::Ink>
{
    static constexpr float SpawnRate = 4.0f;

    static void Update(Particle& p, const ParticleUpdateContext& ctx)
    {
        const glm::vec2 flow = FlowNoise(p.position, ctx.time, p.phase);
        p.position += flow * 3.0f * ctx.deltaTime;
        p.position.y -= 3.0f * ctx.deltaTime;  // Floats up slowly, like Runes.

        float pulse = 0.75f + 0.25f * std::sin(ctx.time * 0.9f + p.phase);
        float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.8f);
        float lifeFade = std::min(1.0f, p.lifetime / 1.2f);
        p.color.a = pulse * fadeIn * lifeFade * 0.85f;
    }

    static void Spawn(int zoneIndex, const ParticleZone& zone, ParticleSpawnContext& ctx)
    {
        Particle p;
        p.zoneIndex = zoneIndex;
        p.type = ParticleType::Ink;
        p.noProjection = zone.noProjection;
        p.position.x = zone.position.x + ctx.dist(ctx.rng) * zone.size.x;
        p.position.y = zone.position.y + ctx.dist(ctx.rng) * zone.size.y;
        p.velocity = glm::vec2(0.0f);
        p.color = glm::vec4(0.12f, 0.12f, 0.18f + ctx.dist(ctx.rng) * 0.05f, 0.0f);
        p.size = 5.0f + ctx.dist(ctx.rng) * 3.0f;
        p.lifetime = 4.0f + ctx.dist(ctx.rng) * 3.0f;
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

    // One valid variant per type until BuildAtlas fills the real counts, so
    // spawn-time variant rolls stay in range in texture-less contexts
    // (unit tests never call LoadTextures).
    std::fill(std::begin(m_VariantCounts), std::end(m_VariantCounts), uint8_t{1});
}

bool ParticleSystem::LoadTextures(TextureStore& store, const ProjectManifest& manifest)
{
    m_Store = &store;
    BuildAtlas(manifest);
    m_TexturesLoaded = true;
    return true;
}

void ParticleSystem::BuildAtlas(const ProjectManifest& manifest)
{
    // Every ParticleType contributes one or more variant sprites (see kParticleVisuals):
    // "<base>_strip.png" frame strips are preferred over single-frame "<base>.png", and types
    // with no asset list (Lantern, Sunshine, Sand) generate procedurally. All variants pack
    // into one 512-wide row-layout atlas so the particle pass stays a single texture bind.

    struct TextureSource
    {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        int frameCount = 1;
        int typeIndex = 0;
        int variantIndex = 0;
    };

    // Load file-based textures temporarily to get their pixel data. All sources are
    // normalized to RGBA (4 channels) so the atlas copy loop can safely read 4 bytes per
    // pixel regardless of the original format. Returns false when the file is missing/
    // unsupported so the caller can chain fallbacks (strip -> static -> procedural).
    auto loadPng = [](const char* path, TextureSource& src) -> bool
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
                return true;
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
                return true;
            }
        }
        return false;
    };

    // Generic soft circle: a smooth radial alpha falloff. Fallback sprite for
    // any missing asset, so a bad path never renders as an opaque square.
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

    // Normalize authored-faint sprites: some assets (fog peaks at alpha ~0.28) sit below the
    // shader's 0.1 texture-alpha discard and render invisible once the behavior's vertex alpha
    // multiplies on top. Behaviors own the intended faintness, so lift low-peak sources to a
    // ~0.9 peak; properly-authored sprites (peak >= 0.5) pass untouched.
    auto normalizeFaintAlpha = [](TextureSource& src)
    {
        uint8_t peak = 0;
        for (size_t i = 3; i < src.pixels.size(); i += 4)
        {
            peak = std::max(peak, src.pixels[i]);
        }
        if (peak == 0 || peak >= 128)
        {
            return;
        }
        const float scale = 230.0f / static_cast<float>(peak);
        for (size_t i = 3; i < src.pixels.size(); i += 4)
        {
            src.pixels[i] = static_cast<unsigned char>(
                std::min(255.0f, static_cast<float>(src.pixels[i]) * scale));
        }
    };

    // Build the source list: one entry per declared variant of every type
    // (strip preferred, then static, then soft-circle fallback); types with
    // an empty variant list generate their sprite procedurally.
    std::vector<TextureSource> sources;
    sources.reserve(EnumTraits<ParticleType>::Count * 2);
    for (size_t t = 0; t < EnumTraits<ParticleType>::Count; ++t)
    {
        int variantCount = 0;
        for (const char* base : kParticleVisuals[t].variants)
        {
            if (base == nullptr)
            {
                break;
            }
            TextureSource src;
            src.typeIndex = static_cast<int>(t);
            src.variantIndex = variantCount;

            // Resolve the sprite through the manifest's "particles" links - asset file names
            // are opaque GUIDs, so the manifest is the only name-to-path mapping. The linked
            // file may be the static frame or the "_strip" sheet; derive both candidates from
            // it so strips stay preferred regardless of which one is linked.
            std::string stripPath;
            std::string staticPath;
            if (const auto link = manifest.particleSprites.find(base);
                link != manifest.particleSprites.end())
            {
                std::string stem = manifest.ResolvePathString(link->second);
                if (stem.ends_with(".png"))
                {
                    stem.resize(stem.size() - 4);
                }
                if (stem.ends_with("_strip"))
                {
                    stem.resize(stem.size() - 6);
                }
                stripPath = stem + "_strip.png";
                staticPath = stem + ".png";
            }
            else
            {
                Logger::ErrorF(LOG_SUBSYSTEM,
                               "Project manifest has no \"particles\" link for '{}'; using "
                               "soft-circle fallback",
                               base);
            }

            // Existence-check before loading: many variants ship only a
            // static frame, and probing for the strip via LoadFromFile would
            // log a scary (but expected) texture error on every boot.
            const bool stripLoaded = !stripPath.empty() && std::filesystem::exists(stripPath) &&
                                     loadPng(stripPath.c_str(), src);
            if (stripLoaded)
            {
                // Horizontal strip: frame count from the width/height ratio
                // (64x16 -> 4 frames). Unexpected dimensions degrade to a
                // single stretched frame rather than corrupt slicing.
                if (src.height > 0 && src.width > src.height && src.width % src.height == 0)
                {
                    src.frameCount = src.width / src.height;
                }
            }
            else if (!(!staticPath.empty() && std::filesystem::exists(staticPath) &&
                       loadPng(staticPath.c_str(), src)))
            {
                if (!staticPath.empty())
                {
                    Logger::ErrorF(LOG_SUBSYSTEM,
                                   "Missing particle asset for '{}' (tried {} and {}); using "
                                   "soft-circle fallback",
                                   base,
                                   stripPath,
                                   staticPath);
                }
                generateSoftCircle(src, 16, 1.5f);
            }
            // The packer's rows are kParticleAtlasWidth wide; a wider source
            // would place with out-of-range UVs and silently clip its right
            // side, so degrade to the fallback sprite loudly instead.
            if (src.width > kParticleAtlasWidth)
            {
                Logger::ErrorF(LOG_SUBSYSTEM,
                               "Particle asset '{}' is {}px wide (atlas width is {}); using "
                               "soft-circle fallback",
                               base,
                               src.width,
                               kParticleAtlasWidth);
                src.pixels.clear();
                src.frameCount = 1;
                generateSoftCircle(src, 16, 1.5f);
            }
            normalizeFaintAlpha(src);
            sources.push_back(std::move(src));
            ++variantCount;
        }
        if (variantCount == 0)
        {
            // Procedural sprite types (no asset list in kParticleVisuals).
            TextureSource src;
            src.typeIndex = static_cast<int>(t);
            src.variantIndex = 0;
            switch (static_cast<ParticleType>(t))
            {
                case ParticleType::Lantern:
                    GenerateLanternPixels(src.pixels, src.width, src.height);
                    break;
                case ParticleType::Sunshine:
                    GenerateSunshinePixels(src.pixels, src.width, src.height);
                    break;
                default:
                    generateSoftCircle(src, 16, 1.5f);
                    break;
            }
            sources.push_back(std::move(src));
            variantCount = 1;
        }
        m_VariantCounts[t] = static_cast<uint8_t>(
            std::min<int>(variantCount, static_cast<int>(MAX_PARTICLE_VARIANTS)));
    }

    // Calculate atlas layout - simple horizontal packing with rows.
    // Pre-scan texture sizes to compute required atlas height so the
    // atlas is always tall enough for all particle textures.
    const int atlasWidth = kParticleAtlasWidth;
    int requiredHeight = 0;
    {
        int scanX = 0;
        int scanRowHeight = 0;
        for (const TextureSource& src : sources)
        {
            if (scanX + src.width > atlasWidth)
            {
                scanX = 0;
                requiredHeight += scanRowHeight + 1;
                scanRowHeight = 0;
            }
            scanX += src.width + 1;
            if (src.height > scanRowHeight)
            {
                scanRowHeight = src.height;
            }
        }
        requiredHeight += scanRowHeight;
    }
    const int atlasHeight = std::max(512, requiredHeight);
    std::vector<unsigned char> atlasPixels(atlasWidth * atlasHeight * 4, 0);

    int currentX = 0;
    int currentY = 0;
    int rowHeight = 0;

    for (const TextureSource& source : sources)
    {
        const int w = source.width;
        const int h = source.height;
        AtlasSlot& slot = m_AtlasSlots[source.typeIndex][source.variantIndex];
        slot.frameCount = source.frameCount;

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
                           "Atlas overflow: type {} variant {} ({}x{}) does not fit at row {} "
                           "(atlas height={})",
                           source.typeIndex,
                           source.variantIndex,
                           w,
                           h,
                           currentY,
                           atlasHeight);
            // Store degenerate UV region so this sprite renders as a small corner pixel.
            slot.region.uvMin = glm::vec2(0.0f);
            slot.region.uvMax = glm::vec2(1.0f / atlasWidth, 1.0f / atlasHeight);
            slot.frameCount = 1;
            continue;
        }

        // Store UV coordinates (normalized)
        slot.region.uvMin = glm::vec2(static_cast<float>(currentX) / atlasWidth,
                                      static_cast<float>(currentY) / atlasHeight);
        slot.region.uvMax = glm::vec2(static_cast<float>(currentX + w) / atlasWidth,
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

                if (srcIdx + 3 < static_cast<int>(source.pixels.size()))
                {
                    atlasPixels[dstIdx + 0] = source.pixels[srcIdx + 0];
                    atlasPixels[dstIdx + 1] = source.pixels[srcIdx + 1];
                    atlasPixels[dstIdx + 2] = source.pixels[srcIdx + 2];
                    atlasPixels[dstIdx + 3] = source.pixels[srcIdx + 3];
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

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Atlas built: {}x{} ({} sprites across {} types)",
                  atlasWidth,
                  atlasHeight,
                  sources.size(),
                  EnumTraits<ParticleType>::Count);
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

    // Merge any deferred spawns (e.g., Rain splashes, Bubble pops). Done
    // before the cull so newly-spawned-but-dead particles would still be
    // removed cleanly. Deferred spawns roll their sprite variants here since
    // they bypass the SpawnParticleInZone path.
    if (!m_PendingSpawns.empty())
    {
        const size_t firstMerged = m_Particles.size();
        m_Particles.insert(m_Particles.end(), m_PendingSpawns.begin(), m_PendingSpawns.end());
        AssignSpawnVariants(firstMerged);
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
    const size_t before = m_Particles.size();
    kSpawnDispatch[typeIndex](zoneIndex, zone, ctx);
    AssignSpawnVariants(before);
}

void ParticleSystem::AssignSpawnVariants(size_t firstIndex)
{
    for (size_t i = firstIndex; i < m_Particles.size(); ++i)
    {
        Particle& p = m_Particles[i];
        const auto typeIndex = static_cast<size_t>(p.type);
        if (typeIndex >= EnumTraits<ParticleType>::Count)
        {
            continue;
        }
        // Types with a spawnVariantCount pin the roll to their leading
        // variants and switch to the later ones at runtime (Bubble's pop).
        int count = std::max<int>(1, m_VariantCounts[typeIndex]);
        const uint8_t spawnCount = kParticleVisuals[typeIndex].spawnVariantCount;
        if (spawnCount > 0)
        {
            count = std::min<int>(count, spawnCount);
        }
        p.variant = (count > 1) ? static_cast<uint8_t>(std::min<int>(
                                      count - 1, static_cast<int>(m_Dist01(m_Rng) * count)))
                                : uint8_t{0};
    }
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
        case WeatherParticleType::Smoke:
            return ParticleType::Smoke;
        case WeatherParticleType::Zap:
            return ParticleType::Zap;
        case WeatherParticleType::Wind:
            return ParticleType::Wind;
        case WeatherParticleType::Aurora:
            return ParticleType::Aurora;
        case WeatherParticleType::Constellation:
            return ParticleType::Constellation;
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
    // SpawnWeatherType increments its slot locally as it spawns. Sized from
    // EnumTraits so new ParticleTypes are covered automatically.
    std::array<int, EnumTraits<ParticleType>::Count> liveByType{};
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
                                      std::array<int, EnumTraits<ParticleType>::Count>& liveByType,
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
        case ParticleType::Wind:
            // Wind blows right by default; spawn at the upwind (left) edge.
            // Wind gust streaks share Sand's +X travel convention.
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
    AssignSpawnVariants(before);

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
        // Live zone particles follow their zone's (editable) flag; zoneless
        // carriers - splash droplets, snow puffs, bubble pops - keep the
        // flag their parent baked into the particle, so a pop from a
        // noProjection bubble doesn't jump projection for its last frames.
        bool isNoProjection = p.noProjection;
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
        data.lifeT = (p.maxLifetime > 1e-4f)
                         ? std::clamp(1.0f - p.lifetime / p.maxLifetime, 0.0f, 1.0f)
                         : 0.0f;
        data.additive = p.additive;
        data.type = p.type;
        data.variant = p.variant;

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
            if (typeIndex < 0 || typeIndex >= static_cast<int>(EnumTraits<ParticleType>::Count))
                return;
            const uint8_t variantCount = std::max<uint8_t>(uint8_t{1}, m_VariantCounts[typeIndex]);
            const uint8_t variant = (data.variant < variantCount) ? data.variant : uint8_t{0};
            const AtlasSlot& slot = m_AtlasSlots[typeIndex][variant];

            // Strip-backed sprites select one horizontal frame as a UV sub-rect. Loop mode runs
            // on global time with per-particle rate jitter + start offset (phase) so same-type
            // particles don't animate in lockstep; LifeMapped plays the strip exactly once
            // across the particle's lifetime (pops, twinkles).
            glm::vec2 uvMin = slot.region.uvMin;
            glm::vec2 uvMax = slot.region.uvMax;
            if (slot.frameCount > 1)
            {
                const ParticleVisuals& vis = kParticleVisuals[typeIndex];
                // Bubble's variant 1 is its pop strip: the behavior remaps
                // the particle's remaining life onto the pop window, so the
                // strip plays once even though the type loops while afloat.
                const ParticleAnimMode animMode =
                    (data.type == ParticleType::Bubble && variant == 1)
                        ? ParticleAnimMode::LifeMapped
                        : vis.animMode;
                int frame = 0;
                if (animMode == ParticleAnimMode::LifeMapped)
                {
                    frame = std::min(
                        slot.frameCount - 1,
                        static_cast<int>(data.lifeT * static_cast<float>(slot.frameCount)));
                }
                else
                {
                    const float rate = vis.animFps * (0.85f + 0.30f * (data.phase / 6.2832f));
                    const float cursor = m_Time * rate + data.phase * 2.0f;
                    frame = static_cast<int>(cursor) % slot.frameCount;
                }
                const float frameWidth = (slot.region.uvMax.x - slot.region.uvMin.x) /
                                         static_cast<float>(slot.frameCount);
                // Quarter-texel inset: frames inside a strip pack with no
                // padding, so exact-boundary UVs could sample the neighbor
                // frame's edge texels under rotation/scaling.
                const float inset = 0.25f / static_cast<float>(kParticleAtlasWidth);
                uvMin.x = slot.region.uvMin.x + frameWidth * static_cast<float>(frame) + inset;
                uvMax.x = slot.region.uvMin.x + frameWidth * static_cast<float>(frame + 1) - inset;
            }

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
            // Wind gust streaks stretch along their travel axis to sell speed
            else if (data.type == ParticleType::Wind)
            {
                renderSize = glm::vec2(data.size.x * 2.2f, data.size.x * 0.75f);
            }
            glm::vec2 centeredPos = data.screenPos - renderSize * 0.5f;
            renderer.DrawSpriteAtlas(m_Store->Get(m_AtlasHandle),
                                     centeredPos,
                                     renderSize,
                                     uvMin,
                                     uvMax,
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
