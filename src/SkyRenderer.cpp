#include "SkyRenderer.hpp"
#include "TimeManager.hpp"

#include "AmbienceConfig.hpp"
#include "AuroraMath.hpp"
#include "AuroraTextures.hpp"
#include "MathConstants.hpp"
#include "ProceduralTexture.hpp"
#include "TextureStore.hpp"
#include "WeatherDefinitions.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <random>

SkyRenderer::SkyRenderer()
    : m_Time(0.0),
      m_ShootingStarTimer(0.0f),
      m_LastScreenWidth(0.0f),
      m_LastScreenHeight(0.0f),
      m_Initialized(false),
      m_Rng(std::random_device{}())
{
}

SkyRenderer::~SkyRenderer() {}

void SkyRenderer::Initialize(TextureStore& store, const std::string& auroraSpritePath)
{
    if (m_Initialized)
        return;

    m_Store = &store;

    GenerateRayTexture();
    GenerateStarTexture();
    GenerateStarGlowTexture();
    GenerateShootingStarTexture();
    GenerateLightRays();
    GenerateStars(STAR_COUNT);
    GenerateBackgroundStars(BACKGROUND_STAR_COUNT);
    GenerateDewSparkles();

    // Generate glow texture for light source
    std::vector<unsigned char> glowPixels(GLOW_TEXTURE_SIZE * GLOW_TEXTURE_SIZE * 4);
    float center = GLOW_TEXTURE_SIZE / 2.0f;

    for (int y = 0; y < GLOW_TEXTURE_SIZE; y++)
    {
        for (int x = 0; x < GLOW_TEXTURE_SIZE; x++)
        {
            float dx = x - center;
            float dy = y - center;
            float dist = std::sqrt(dx * dx + dy * dy) / center;

            int idx = (y * GLOW_TEXTURE_SIZE + x) * 4;

            // Three concentric glow layers, each with a different falloff curve,
            // blended together to approximate realistic light bloom:
            //   core  - cubic falloff (very sharp), reaches zero at dist=0.5
            //   inner - quadratic falloff, reaches zero at dist~=0.83
            //   outer - exponential falloff, never fully zero (soft halo)
            // The weights (0.8/0.5/0.3) control relative brightness of each ring.
            float core = std::max(0.0f, 1.0f - dist * 2.0f);
            core = core * core * core;

            float inner = std::max(0.0f, 1.0f - dist * 1.2f);
            inner = inner * inner;

            float outer = std::max(0.0f, 1.0f - dist);
            outer = std::exp(-dist * 3.0f);

            float alpha = core * 0.8f + inner * 0.5f + outer * 0.3f;
            alpha = std::min(1.0f, alpha);

            glowPixels[idx + 0] = 255;
            glowPixels[idx + 1] = 255;
            glowPixels[idx + 2] = 255;
            glowPixels[idx + 3] = static_cast<unsigned char>(alpha * 255);
        }
    }
    Texture glowTex;
    glowTex.LoadFromData(glowPixels.data(), GLOW_TEXTURE_SIZE, GLOW_TEXTURE_SIZE, 4, false);
    m_GlowHandle = m_Store->Adopt(std::move(glowTex));

    GenerateLightPoolTexture();
    GenerateAuroraCurtainTexture();
    GenerateAuroraBeamTexture();
    // Hand-painted soft aurora mote for the floating sky-wisp layer (the
    // procedural dot read worse). The path comes from the project manifest's
    // "particles" links (asset file names are opaque GUIDs); falls back to a
    // colored rect when unlinked or missing. Separate from the AuroraNight
    // weather Aurora/Wisp motes (ParticleSystem), which drift through the
    // world below the sky ribbons.
    Texture auroraSmallTex;
    if (!auroraSpritePath.empty())
    {
        auroraSmallTex.LoadFromFile(auroraSpritePath);
    }
    m_AuroraSmallHandle = m_Store->Adopt(std::move(auroraSmallTex));

    m_Initialized = true;
}

void SkyRenderer::DrawLightPool(IRenderer& renderer,
                                glm::vec2 pos,
                                glm::vec2 size,
                                float rotation,
                                glm::vec4 color,
                                bool additive)
{
    // Free function defined below in this file's anonymous namespace.
    // Use the bound atlas if SetAtlasBinding has been called.
    if (m_AtlasTexture != nullptr)
    {
        const float aw = static_cast<float>(m_AtlasTexture->GetWidth());
        const float ah = static_cast<float>(m_AtlasTexture->GetHeight());
        if (aw > 0.0f && ah > 0.0f)
        {
            // See DrawSkyElement: atlas sub-region preserves the source's
            // m_ImageData layout, so uv math is a direct map.
            const glm::vec2 regionSize(
                static_cast<float>(m_Store->Get(m_LightPoolHandle).GetWidth()),
                static_cast<float>(m_Store->Get(m_LightPoolHandle).GetHeight()));
            const glm::vec2 uvMin = m_LightPoolAtlasOffset / glm::vec2(aw, ah);
            const glm::vec2 uvMax = (m_LightPoolAtlasOffset + regionSize) / glm::vec2(aw, ah);
            renderer.DrawSpriteAtlas(
                *m_AtlasTexture, pos, size, uvMin, uvMax, rotation, color, additive);
            return;
        }
    }
    renderer.DrawSpriteAlpha(m_Store->Get(m_LightPoolHandle), pos, size, rotation, color, additive);
}

void SkyRenderer::SetAtlasBinding(const Texture* atlasTex,
                                  glm::vec2 rayOffset,
                                  glm::vec2 starOffset,
                                  glm::vec2 starGlowOffset,
                                  glm::vec2 shootingStarOffset,
                                  glm::vec2 glowOffset,
                                  glm::vec2 lightPoolOffset,
                                  glm::vec2 auroraCurtainOffset,
                                  glm::vec2 auroraSmallOffset)
{
    m_AtlasTexture = atlasTex;
    m_RayAtlasOffset = rayOffset;
    m_StarAtlasOffset = starOffset;
    m_StarGlowAtlasOffset = starGlowOffset;
    m_ShootingStarAtlasOffset = shootingStarOffset;
    m_GlowAtlasOffset = glowOffset;
    m_LightPoolAtlasOffset = lightPoolOffset;
    m_AuroraCurtainAtlasOffset = auroraCurtainOffset;
    m_AuroraSmallAtlasOffset = auroraSmallOffset;
}

namespace
{
// Helper: route a sky-element draw through the bound atlas if any,
// else fall back to the per-element texture. Keeps the existing
// DrawSpriteAlpha semantics (full-texture sample) while collapsing
// the GPU-side flush count when the atlas is bound.
void DrawSkyElement(IRenderer& renderer,
                    const Texture* atlasTex,
                    glm::vec2 atlasOffset,
                    const Texture& fallback,
                    glm::vec2 pos,
                    glm::vec2 size,
                    float rotation,
                    glm::vec4 color,
                    bool additive)
{
    if (atlasTex != nullptr)
    {
        const float aw = static_cast<float>(atlasTex->GetWidth());
        const float ah = static_cast<float>(atlasTex->GetHeight());
        if (aw > 0.0f && ah > 0.0f)
        {
            // The tile atlas is pre-flipped before GPU upload, so the GL row
            // PackAdditionalSheets flips each source on copy so the atlas
            // sub-region preserves the source's m_ImageData layout exactly.
            // That means uv (atlasOffset/atlasSize, (atlasOffset+regionSize)
            // /atlasSize) on the atlas samples the same texels as uv
            // (0,0)-(1,1) on the standalone texture - no Y inversion here.
            const glm::vec2 regionSize(static_cast<float>(fallback.GetWidth()),
                                       static_cast<float>(fallback.GetHeight()));
            const glm::vec2 uvMin = atlasOffset / glm::vec2(aw, ah);
            const glm::vec2 uvMax = (atlasOffset + regionSize) / glm::vec2(aw, ah);
            renderer.DrawSpriteAtlas(*atlasTex, pos, size, uvMin, uvMax, rotation, color, additive);
            return;
        }
    }
    renderer.DrawSpriteAlpha(fallback, pos, size, rotation, color, additive);
}
}  // namespace

void SkyRenderer::GenerateLightPoolTexture()
{
    constexpr int kSize = 128;
    std::vector<unsigned char> pixels(kSize * kSize * 4);
    const float center = kSize / 2.0f;
    for (int y = 0; y < kSize; ++y)
    {
        for (int x = 0; x < kSize; ++x)
        {
            float dx = (x - center) / center;
            float dy = (y - center) / center;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Bright core, soft inner halo, exponential outer falloff.
            float core = std::max(0.0f, 1.0f - dist * 2.0f);
            core = core * core * core * 0.9f;
            float inner = std::max(0.0f, 1.0f - dist * 1.4f);
            inner = inner * inner * 0.6f;
            float outer = std::exp(-dist * 4.0f) * 0.25f;

            float alpha = std::min(1.0f, core + inner + outer);
            int idx = (y * kSize + x) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(alpha * 255.0f);
        }
    }
    Texture lightPoolTex;
    lightPoolTex.LoadFromData(pixels.data(), kSize, kSize, 4, false);
    m_LightPoolHandle = m_Store->Adopt(std::move(lightPoolTex));
}

void SkyRenderer::GenerateAuroraCurtainTexture()
{
    constexpr int kW = 128;
    constexpr int kH = 256;
    std::vector<unsigned char> pixels = AuroraTextures::BuildCurtainPixels(kW, kH);
    Texture tex;
    tex.LoadFromData(pixels.data(), kW, kH, 4, false);
    m_AuroraCurtainHandle = m_Store->Adopt(std::move(tex));
}

void SkyRenderer::GenerateAuroraBeamTexture()
{
    constexpr int kW = 64;
    constexpr int kH = 256;
    std::vector<unsigned char> pixels = AuroraTextures::BuildBeamPixels(kW, kH);
    Texture tex;
    tex.LoadFromData(pixels.data(), kW, kH, 4, false);
    m_AuroraBeamHandle = m_Store->Adopt(std::move(tex));
}

void SkyRenderer::Update(float deltaTime, const TimeManager& time)
{
    m_Time += static_cast<double>(deltaTime);

    // Cache weather-driven flags for the upcoming Render pass. The effective
    // definition is the director's blended def mid-transition.
    const WeatherDefinition& def = time.GetEffectiveWeatherDefinition();
    m_AuroraFade = time.GetAuroraFade();
    m_CelestialFade = time.GetCelestialFade();
    m_AuroraVisible = m_AuroraFade > 0.01f;
    m_MeteorRateMultiplier = def.meteorRateMultiplier;

    // Lightning: only when the weather wants flashes. Decrement countdown,
    // trigger a flash, jitter the next interval.
    if (def.lightningIntervalSeconds > 0.0f)
    {
        std::uniform_real_distribution<float> jitter(0.7f, 1.3f);
        if (m_LightningTimer <= 0.0f)
        {
            // Just enabled (0 is the disabled sentinel): arm a full countdown
            // so a ramped-in storm doesn't open with a flash.
            m_LightningTimer = def.lightningIntervalSeconds * jitter(m_Rng);
        }
        // The blended interval starts huge (frequency-space blend) and tightens
        // as a transition progresses; clamp so the countdown follows it down -
        // without this the first flash of a ramped-in storm arrives minutes late.
        m_LightningTimer = std::min(m_LightningTimer, def.lightningIntervalSeconds * 1.3f);
        m_LightningTimer -= deltaTime;
        if (m_LightningTimer <= 0.0f)
        {
            m_LightningFlashTimer = 0.08f;
            // Bolt visibility outlasts the flash by ~0.1s so the jagged
            // streak lingers briefly after the screen goes back to normal.
            m_LightningBoltTimer = 0.18f;
            GenerateLightningBolt(static_cast<int>(m_LastScreenWidth),
                                  static_cast<int>(m_LastScreenHeight));
            // +/-30% jitter on the configured interval.
            m_LightningTimer = def.lightningIntervalSeconds * jitter(m_Rng);
        }
    }
    else
    {
        m_LightningTimer = 0.0f;
    }
    if (m_LightningFlashTimer > 0.0f)
    {
        m_LightningFlashTimer = std::max(0.0f, m_LightningFlashTimer - deltaTime);
    }
    if (m_LightningBoltTimer > 0.0f)
    {
        m_LightningBoltTimer = std::max(0.0f, m_LightningBoltTimer - deltaTime);
    }

    // Shooting stars: gated only by darkness now. Density scales by the
    // weather's meteor multiplier (MeteorShower bumps it 8x, etc.).
    if (m_LastScreenWidth > 0.0f && m_LastScreenHeight > 0.0f && time.GetStarVisibility() > 0.3f)
    {
        UpdateShootingStars(
            deltaTime, static_cast<int>(m_LastScreenWidth), static_cast<int>(m_LastScreenHeight));
    }
}

void SkyRenderer::Render(IRenderer& renderer,
                         const TimeManager& time,
                         glm::vec2 cameraPos,
                         int screenWidth,
                         int screenHeight)
{
    if (!m_Initialized)
        return;

    // Store screen size for shooting star spawning
    m_LastScreenWidth = static_cast<float>(screenWidth);
    m_LastScreenHeight = static_cast<float>(screenHeight);

    // Disable ambient color for sky rendering
    renderer.SetAmbientColor(glm::vec3(1.0f));

    // Dawn/morning gradient effects (rendered first as background) - these
    // are full-screen washes; no parallax.
    float dawnIntensity = time.GetDawnIntensity();
    if (dawnIntensity > 0.01f)
    {
        RenderDawnGradient(renderer, time, screenWidth, screenHeight);
        RenderDawnHorizonGlow(renderer, time, screenWidth, screenHeight);
    }

    // Render atmospheric glow (subtle night sky color)
    float starVisibility = time.GetStarVisibility();
    if (starVisibility > 0.1f)
    {
        RenderAtmosphericGlow(renderer, time, screenWidth, screenHeight);
    }

    // Aurora bands behind stars when active.
    if (m_AuroraVisible)
    {
        RenderAurora(renderer, time, cameraPos, screenWidth, screenHeight);
    }

    // Render stars (background, only at night - fades during dawn)
    if (starVisibility > 0.01f)
    {
        RenderStars(renderer, time, cameraPos, screenWidth, screenHeight);
        RenderShootingStars(renderer, time, cameraPos, screenWidth, screenHeight);
    }

    // Dew sparkles during early morning (ground-level effect, no parallax)
    float sunArc = time.GetSunArc();
    if (sunArc >= 0.0f && sunArc < 0.25f)
    {
        RenderDewSparkles(renderer, time, screenWidth, screenHeight);
    }

    // Sun rays - hidden when weather covers celestial bodies.
    if (sunArc >= 0.0f && m_CelestialFade > 0.01f)
    {
        RenderSunRays(renderer, time, cameraPos, screenWidth, screenHeight);
    }

    // Moon rays during night - same gating.
    float moonArc = time.GetMoonArc();
    if (moonArc >= 0.0f && starVisibility > 0.3f && m_CelestialFade > 0.01f)
    {
        RenderMoonRays(renderer, time, cameraPos, screenWidth, screenHeight);
    }

    // Lightning flash overlay - soft cool-white, no parallax. Tinted slightly
    // toward blue (1.0, 1.0, 1.1) so the flash reads as electric / atmospheric
    // rather than a flat white blow-out, and capped at 0.25 alpha so the flash
    // illuminates the sky without strobing the player's eyes.
    if (m_LightningFlashTimer > 0.0f)
    {
        constexpr float kFlashDuration = 0.08f;
        float alpha = (m_LightningFlashTimer / kFlashDuration) * 0.25f;
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_GlowAtlasOffset,
                       m_Store->Get(m_GlowHandle),
                       glm::vec2(-static_cast<float>(screenWidth) * 0.5f,
                                 -static_cast<float>(screenHeight) * 0.5f),
                       glm::vec2(static_cast<float>(screenWidth) * 2.0f,
                                 static_cast<float>(screenHeight) * 2.0f),
                       0.0f,
                       glm::vec4(0.92f, 0.95f, 1.0f, alpha),
                       true);
    }

    // Jagged bolt drawn after the flash so it sits on top of the cool-white
    // wash. Lingers briefly after the flash ends.
    if (m_LightningBoltTimer > 0.0f)
    {
        RenderLightningBolt(renderer, screenWidth, screenHeight);
    }
}

void SkyRenderer::RenderAurora(IRenderer& renderer,
                               const TimeManager& /*time*/,
                               glm::vec2 cameraPos,
                               int screenWidth,
                               int screenHeight)
{
    // World-anchored aurora: full horizontal parallax so curtains/wisps walk past
    // the player, but Y is sky-relative so the band stays in the upper viewport.
    // Four layers below: diffuse base wash, 12 tangent-rotated curtains, oval
    // beams rising from lit sections, and 48 floating wisps.
    const float t = static_cast<float>(m_Time);
    const float sw = static_cast<float>(screenWidth);
    const float sh = static_cast<float>(screenHeight);

    // Aurora motion / beam tunables (see the overhaul spec, section 11).
    constexpr float kSweepSpeed = 0.15f;    // brightness hot-spot travel speed along a band
    constexpr float kSweepWidth = 0.20f;    // hot-spot half-width (fraction of band length)
    constexpr int kBeamsPerCurtain = 5;     // floating oval beams per ribbon
    constexpr float kBeamLifeSpeed = 0.4f;  // beam fade in/out speed (slow ~12-22s cycle)

    // Palette, path-tangent and brightness-sweep math live in AuroraMath
    // (pure, unit-tested in tests/AuroraMathTests.cpp).

    // Wrap a world-X anchor so it stays close to the camera modulo period.
    // This keeps curtains/wisps tiling across the world without needing
    // millions of them, while preserving "true world position" feel within
    // each period.
    auto wrapNearCamera = [](float anchorX, float cameraX, float period)
    { return cameraX + std::remainder(anchorX - cameraX, period); };

    // Layer 1: world-anchored noodle curtains.
    // Each "curtain" is a horizontal aurora ribbon built from a varying
    // number of overlapping curtain-texture slices. Every ribbon is unique:
    // segment count, segment size, ribbon height, baseline Y, X jitter, wave
    // amplitude, wave frequency, drift speed, and tilt are all driven by
    // per-curtain seeds so no two read the same.
    //
    // BOTH X and Y are now world-anchored (previously Y was screen-relative).
    // Curtains tile across world X and world Y via wrapNearCamera; as the
    // camera pans, individual curtains scroll naturally and rebound through
    // the wrap rather than being pinned to the upper viewport.
    constexpr int kCurtainCount = 12;
    const float curtainPeriod = sw * 4.5f;
    const float curtainSpacing = curtainPeriod / static_cast<float>(kCurtainCount);
    // Vertical wrap. A ~1-screen period keeps the wrap subtle during normal
    // play while still tiling correctly under free-cam pans. The wrap is
    // biased toward the upper third of the visible area so most curtains
    // still land in the sky portion of the screen at any camera position.
    const float curtainYPeriod = sh * 1.0f;
    const float curtainYWrapCenter = cameraPos.y + sh * 0.30f;
    for (int i = 0; i < kCurtainCount; ++i)
    {
        float c = static_cast<float>(i);
        float ti = c * 0.83f;

        // 5 deterministic per-curtain "random" values in [0,1) derived from
        // the index; using primes avoids visible alignment between knobs.
        float r1 = std::fmod(c * 7.31f + 0.13f, 1.0f);
        float r2 = std::fmod(c * 13.47f + 0.41f, 1.0f);
        float r3 = std::fmod(c * 19.83f + 0.67f, 1.0f);
        float r4 = std::fmod(c * 23.51f + 0.29f, 1.0f);
        float r5 = std::fmod(c * 31.07f + 0.83f, 1.0f);

        // Per-curtain segment count, width, spacing -> varied ribbon length and thickness.
        const int segments = 12 + static_cast<int>(r1 * 18.0f);    // 12-30
        const float segWidth = 36.0f + r2 * 36.0f;                 // 36-72px
        const float segSpacing = segWidth * (0.34f + r3 * 0.16f);  // tighter overlap (rotated)
        const float ribbonSpan = segSpacing * static_cast<float>(segments - 1);

        // X anchor jittered around the even spacing so positions don't form a grid.
        float anchorWorldX = c * curtainSpacing + (r4 - 0.5f) * curtainSpacing * 0.5f;
        float worldCenterX = wrapNearCamera(anchorWorldX, cameraPos.x, curtainPeriod);

        // Y anchor: per-curtain world Y wrapped near the camera so curtains
        // tile across world Y. The wrap center sits 30% down the screen so
        // wrapped values cluster in the upper portion of the visible area.
        float anchorWorldY = (c + 0.5f) * (curtainYPeriod / static_cast<float>(kCurtainCount)) +
                             (r5 - 0.5f) * curtainYPeriod * 0.3f;
        float worldCenterYBase =
            curtainYWrapCenter + std::remainder(anchorWorldY - curtainYWrapCenter, curtainYPeriod);
        // Slight overall tilt so the ribbon leans up or down across its span.
        float ribbonTilt = (r3 - 0.5f) * sh * 0.10f;
        // Curtain height varies - some thin ribbons, some tall sheets.
        float curtainH = sh * (0.16f + r1 * 0.28f);

        // Wave amplitudes vary per curtain: some are nearly flat ribbons,
        // others are heavily warped noodles.
        float ampMajor = sh * (0.020f + r2 * 0.090f);
        float ampMid = sh * (0.008f + r4 * 0.035f);
        float ampMicro = sh * (0.003f + r5 * 0.015f);
        // Spatial frequency (low -> long sweeping waves, high -> quick wiggle)
        // and per-curtain time-drift speed.
        float waveSpatial = 0.25f + r3 * 0.55f;
        float waveSpeed = 0.20f + r4 * 0.30f;
        float midMul = 2.0f + r1 * 1.5f;
        float microMul = 4.0f + r5 * 2.5f;

        // Per-ribbon overall pulse (slow breath); some breathe faster.
        float pulseFreq = 0.20f + r3 * 0.20f;
        float ribbonPulse = 0.5f + 0.5f * std::sin(t * pulseFreq + ti * 1.7f);

        // Render this curtain at 3 vertically-wrapped copies (-1, 0, +1
        // periods). When the camera moves and one copy exits the top of the
        // screen, the +1 copy below is already drawn, so the field tiles
        // continuously - the player sees curtains scroll naturally past in
        // world coords rather than "snap" or "disappear" at wrap boundaries.
        const float maxAmp = ampMajor + ampMid + ampMicro + std::abs(ribbonTilt) + curtainH * 0.5f;
        for (int wrapDy = -1; wrapDy <= 1; ++wrapDy)
        {
            float baseY =
                (worldCenterYBase + static_cast<float>(wrapDy) * curtainYPeriod) - cameraPos.y;
            // Cull: skip entirely off-screen copies. The curtain spans roughly
            // baseY..baseY+curtainH after wave warp; allow maxAmp slack on
            // both sides for warp/tilt before skipping.
            if (baseY + curtainH + maxAmp < 0.0f || baseY - maxAmp > sh)
            {
                continue;
            }

            // Warped path point for a fractional segment index, factoring the
            // multi-frequency Y warp + tilt + x-jitter. Used to place each
            // segment and to derive the local tangent so segments rotate to
            // follow the curve (no staircase stepping).
            auto samplePoint = [&](float fseg) -> glm::vec2
            {
                float sn = fseg / static_cast<float>(segments - 1);
                float segOffset = fseg * segSpacing - ribbonSpan * 0.5f;
                float sx = (worldCenterX - cameraPos.x) + segOffset;
                float wT = fseg * waveSpatial + ti + t * waveSpeed;
                float yw = std::sin(wT) * ampMajor + std::sin(wT * midMul + ti) * ampMid +
                           std::sin(wT * microMul + ti * 0.5f) * ampMicro;
                float tl = (sn - 0.5f) * 2.0f * ribbonTilt;
                float xj = std::sin(wT * 1.9f + ti * 0.7f) * 6.0f;
                // Return the ribbon CENTERLINE (band center), so a segment whose
                // quad-center sits here rotates about its own anchor - adjacent
                // segments then stay aligned along the curve (no staircase).
                return glm::vec2(sx + xj, baseY + yw + tl + curtainH * 0.5f);
            };

            for (int s = 0; s < segments; ++s)
            {
                float fs = static_cast<float>(s);
                float segNorm = fs / static_cast<float>(segments - 1);  // 0..1 along ribbon
                float waveT = fs * waveSpatial + ti + t * waveSpeed;

                glm::vec2 here = samplePoint(fs);
                glm::vec2 prev = samplePoint(std::max(0.0f, fs - 1.0f));
                glm::vec2 next = samplePoint(std::min(static_cast<float>(segments - 1), fs + 1.0f));
                float angleDeg = AuroraMath::TangentAngleDeg(prev, next);

                // Color shifts gently along the noodle so sections glow different hues.
                glm::vec3 color = AuroraMath::AuroraColor(t * 0.06f + ti * 0.20f + fs * 0.04f);

                // Per-segment alpha: segment-local pulse x ribbon pulse x ends fade.
                // Translucent; overlap stacks but never becomes opaque.
                float segPulse = 0.5f + 0.5f * std::sin(waveT * 1.3f);
                float endsFade = 1.0f - std::abs(segNorm - 0.5f) * 2.1f;
                endsFade = std::pow(std::clamp(endsFade, 0.0f, 1.0f), 0.7f);
                // Lateral sweep: a travelling hot-spot brightens sections of the
                // band in sequence (the "dancing curtain"); 0.6 floor so the band
                // never fully vanishes between sweeps. Beams share this envelope.
                float sweep = AuroraMath::SweepBoost(segNorm, t, kSweepSpeed, kSweepWidth, ti);
                float alpha = (0.26f + 0.40f * segPulse) * (0.60f + 0.40f * ribbonPulse) *
                              endsFade * (0.78f + 0.22f * sweep);

                // Master fade: weather transitions ramp the aurora in/out.
                DrawSkyElement(renderer,
                               m_AtlasTexture,
                               m_AuroraCurtainAtlasOffset,
                               m_Store->Get(m_AuroraCurtainHandle),
                               glm::vec2(here.x - segWidth * 0.5f, here.y - curtainH * 0.5f),
                               glm::vec2(segWidth, curtainH),
                               angleDeg,
                               glm::vec4(color, alpha * m_AuroraFade),
                               true);
            }

            // Soft glow halo following the warped path during bright pulses so
            // bloom hugs the noodle rather than a flat rectangle. Threshold
            // lowered (was 0.55) and per-halo alpha factor bumped (was 0.40)
            // so the glow reads earlier and brighter through the breathing
            // cycle.
            if (ribbonPulse > 0.40f)
            {
                for (int s = 2; s < segments - 2; s += 4)
                {
                    float fs = static_cast<float>(s);
                    float segNorm = fs / static_cast<float>(segments - 1);
                    float waveT = fs * waveSpatial + ti + t * waveSpeed;
                    float yWarp =
                        std::sin(waveT) * ampMajor + std::sin(waveT * midMul + ti) * ampMid;
                    float tilt = (segNorm - 0.5f) * 2.0f * ribbonTilt;
                    float screenX =
                        (worldCenterX - cameraPos.x) + fs * segSpacing - ribbonSpan * 0.5f;
                    float segY = baseY + yWarp + tilt;
                    glm::vec3 color = AuroraMath::AuroraColor(t * 0.06f + ti * 0.20f + fs * 0.04f);
                    float glowSize = curtainH * 1.3f;
                    renderer.DrawSpriteAlpha(
                        m_Store->Get(m_GlowHandle),
                        glm::vec2(screenX - glowSize * 0.5f, segY - curtainH * 0.10f),
                        glm::vec2(glowSize, glowSize * 0.85f),
                        0.0f,
                        glm::vec4(color, (ribbonPulse - 0.40f) * 0.50f * m_AuroraFade),
                        true);
                }
            }

            // Beams: soft, feathered vertical glows that float above the band and
            // breathe slowly in/out on an independent lifecycle (no hard gate, no
            // fast shimmer) with a gentle drift - so they float rather than flicker.
            for (int b = 0; b < kBeamsPerCurtain; ++b)
            {
                float bn = (static_cast<float>(b) + 0.5f) / static_cast<float>(kBeamsPerCurtain);
                float bseed = ti * 1.7f + static_cast<float>(b) * 2.3f;
                float segNorm = std::clamp(bn + (r4 - 0.5f) * 0.12f, 0.05f, 0.95f);
                float fseg = segNorm * static_cast<float>(segments - 1);

                // Slow smooth lifecycle (~12-22s), staggered per beam so some are
                // always softly present; squared for a gentle bloom, never a spike.
                float lifeFreq = kBeamLifeSpeed * (0.7f + 0.6f * std::fmod(bseed, 1.0f));
                float life = 0.5f + 0.5f * std::sin(t * lifeFreq + bseed);
                float alpha = life * life * 0.5f;
                if (alpha < 0.01f)
                {
                    continue;
                }

                glm::vec2 base = samplePoint(fseg);
                // Gentle floating drift so beams aren't rigidly pinned to the band.
                float driftX = std::sin(t * 0.06f + bseed) * sw * 0.025f;
                float driftY = std::cos(t * 0.045f + bseed * 1.3f) * sh * 0.03f;

                float beamW = 20.0f + r2 * 14.0f;
                float beamH = 85.0f + r1 * 55.0f;
                glm::vec3 color = AuroraMath::AuroraColor(t * 0.05f + ti * 0.20f + fseg * 0.04f);

                // Soft oval floats centered just above the band; the texture fades
                // on every side so there is no hard edge anywhere.
                glm::vec2 pos(base.x - beamW * 0.5f + driftX, base.y - beamH * 0.55f + driftY);
                renderer.DrawSpriteAlpha(m_Store->Get(m_AuroraBeamHandle),
                                         pos,
                                         glm::vec2(beamW, beamH),
                                         0.0f,
                                         glm::vec4(color, alpha * m_AuroraFade),
                                         true);
            }
        }
    }

    // Layer 2: floating wisps (world-anchored, hand-painted texture).
    // Smaller and lighter accents - used to be 22-60 px which dominated the
    // sky; now 6-22 px so they read as sparks framing the ribbons. Wisp count
    // bumped to 48 for a denser sparkle field. Each wisp also renders at 3
    // wrapped Y positions (-1, 0, +1 periods) so the field tiles continuously
    // - wisps don't snap or vanish when the camera moves vertically past them.
    constexpr int kWispCount = 48;
    const float wispPeriod = sw * 4.0f;
    const float wispSpacing = wispPeriod / static_cast<float>(kWispCount);
    const float wispYPeriod = sh * 1.0f;
    const float wispYWrapCenter = cameraPos.y + sh * 0.25f;
    for (int i = 0; i < kWispCount; ++i)
    {
        float wi = static_cast<float>(i);
        float seed = wi * 1.913f;

        // Stable world X anchor with slight per-wisp jitter so they aren't
        // perfectly evenly spaced. Wrapped near the camera.
        float jitter = (std::fmod(seed * 7.31f, 1.0f) - 0.5f) * wispSpacing;
        float anchorWorldX = wi * wispSpacing + jitter;
        float worldX = wrapNearCamera(anchorWorldX, cameraPos.x, wispPeriod);

        // Two-frequency floating drift so wisps actually wander instead of
        // bobbing in place. A larger slow component sweeps across the sky;
        // a smaller faster component adds organic wobble. Per-wisp speeds
        // (derived from seed) keep neighboring wisps from moving in sync.
        float driftSpeedX = 0.10f + std::fmod(seed * 5.13f, 1.0f) * 0.10f;  // 0.10-0.20
        float driftSpeedY = 0.08f + std::fmod(seed * 7.91f, 1.0f) * 0.10f;  // 0.08-0.18
        float orbitX = std::sin(t * driftSpeedX + seed) * sw * 0.14f +
                       std::sin(t * 0.04f + seed * 2.3f) * sw * 0.07f;
        float orbitY = std::cos(t * driftSpeedY + seed * 1.3f) * sh * 0.20f +
                       std::cos(t * 0.05f + seed * 1.7f) * sh * 0.09f;

        // Y anchor: per-wisp world Y wrapped near the camera so wisps tile
        // across world Y, mirroring the X tiling above.
        float anchorWorldY = (wi + 0.5f) * (wispYPeriod / static_cast<float>(kWispCount)) +
                             (std::fmod(seed * 3.71f, 1.0f) - 0.5f) * wispYPeriod * 0.4f;
        float worldYBase =
            wispYWrapCenter + std::remainder(anchorWorldY - wispYWrapCenter, wispYPeriod);

        // Lifecycle pulse - slow ~6-8s cycle with a sharp peak. pow(1.8)
        // means each wisp spends most of its cycle invisible (alpha=0) and
        // briefly blooms before fading out, giving the "spawn / pulse /
        // disappear" feel. No persistent state - phase is derived from time
        // and seed so wisps stay world-anchored.
        float pulseFreq = 0.16f + 0.06f * std::sin(seed);  // ~5-7s period
        float pulsePhase = t * pulseFreq + seed * 2.0f;
        float rawPulse = 0.5f + 0.5f * std::sin(pulsePhase);
        float life = std::pow(rawPulse, 1.8f);

        // Tiny accent sparks: 2-6px base. Pulse barely affects size so wisps
        // don't visually shrink to a dot - alpha does the disappearing.
        float sizeBase = 2.0f + std::fmod(seed * 11.7f, 1.0f) * 4.0f;
        float wispSize = sizeBase * (0.85f + 0.15f * life);

        // Color cycles fast enough to noticeably shift each pulse cycle.
        // Per-wisp seed offset (x 0.61) gives each wisp a distinct starting
        // hue, and the t x 0.20 advance means a wisp's color drifts ~1.4
        // palette stops over a 7s pulse cycle - so when it re-blooms it's
        // visibly a different color than last time.
        float colorPhase = t * 0.20f + seed * 0.61f;
        glm::vec3 color = AuroraMath::AuroraColor(colorPhase);

        // Alpha fully fades to 0 at troughs, peaks ~0.85 at bloom (was 0.65).
        float alpha = life * 0.85f;
        if (alpha < 0.005f)
            continue;

        for (int wrapDy = -1; wrapDy <= 1; ++wrapDy)
        {
            float anchorY = (worldYBase + static_cast<float>(wrapDy) * wispYPeriod) - cameraPos.y;
            glm::vec2 pos(worldX - cameraPos.x + orbitX, anchorY + orbitY);
            // Cull copies that drift entirely off-screen (orbitY can land them
            // far from anchorY, so check the actual draw position with a small
            // wispSize-based margin).
            if (pos.y + wispSize < 0.0f || pos.y > sh)
            {
                continue;
            }
            DrawSkyElement(renderer,
                           m_AtlasTexture,
                           m_AuroraSmallAtlasOffset,
                           m_Store->Get(m_AuroraSmallHandle),
                           pos - glm::vec2(wispSize * 0.5f),
                           glm::vec2(wispSize),
                           0.0f,
                           glm::vec4(color, alpha * m_AuroraFade),
                           true);
        }
    }
}

void SkyRenderer::GenerateRayTexture()
{
    // Create a VERTICAL ray texture - soft, wide gradient
    std::vector<unsigned char> pixels;
    GeneratePixels(pixels,
                   RAY_TEXTURE_WIDTH,
                   RAY_TEXTURE_HEIGHT,
                   [](int x, int y, int w, int h) -> Pixel
                   {
                       float centerX = w / 2.0f;
                       float progress = static_cast<float>(y) / h;
                       float distFromCenter = std::abs(x - centerX) / centerX;

                       // Vertical fade: pow(0.4) stays bright longer before dropping off
                       float verticalFade = std::pow(1.0f - progress, 0.4f);

                       // Horizontal fade: gaussian profile for soft diffuse edges
                       float horizontalFade = std::exp(-distFromCenter * distFromCenter * 3.0f);

                       float alpha = verticalFade * horizontalFade;

                       // Additional softening at the very bottom
                       if (progress > 0.7f)
                       {
                           float bottomFade = 1.0f - (progress - 0.7f) / 0.3f;
                           alpha *= bottomFade * bottomFade;
                       }

                       auto a =
                           static_cast<uint8_t>(std::max(0.0f, std::min(alpha * 255.0f, 255.0f)));
                       return {255, 255, 255, a};
                   });

    Texture rayTex;
    rayTex.LoadFromData(pixels.data(), RAY_TEXTURE_WIDTH, RAY_TEXTURE_HEIGHT, 4, false);
    m_RayHandle = m_Store->Adopt(std::move(rayTex));
}

void SkyRenderer::GenerateStarTexture()
{
    std::vector<unsigned char> pixels;
    GeneratePixels(
        pixels,
        STAR_TEXTURE_SIZE,
        STAR_TEXTURE_SIZE,
        [](int x, int y, int w, int) -> Pixel
        {
            float center = w / 2.0f;
            float dx = x - center;
            float dy = y - center;
            float normalizedDist = std::sqrt(dx * dx + dy * dy) / center;

            // Gaussian core (bright point source)
            float core = std::exp(-normalizedDist * normalizedDist * 50.0f);

            // Wider gaussian glow, 70% brightness
            float inner = std::exp(-normalizedDist * normalizedDist * 12.0f) * 0.7f;

            // Exponential halo for soft glow at distance
            float outer = std::exp(-normalizedDist * 3.0f) * 0.25f;

            // 6-point diffraction spikes
            float angle = std::atan2(dy, dx);
            float spike6 = std::pow(std::abs(std::cos(angle * 3.0f)), 12.0f);
            float spikeIntensity = spike6 * std::exp(-normalizedDist * 0.8f) * 0.5f;

            // Secondary 4-point spikes rotated 45 deg
            float spike4 = std::pow(std::abs(std::cos(angle * 2.0f + 0.785f)), 16.0f);
            float spike4Intensity = spike4 * std::exp(-normalizedDist * 1.2f) * 0.2f;

            float intensity =
                std::min(1.0f, core + inner + outer + spikeIntensity + spike4Intensity);

            auto a = static_cast<uint8_t>(std::max(0.0f, std::min(intensity * 255.0f, 255.0f)));
            return {255, 255, 255, a};
        });

    Texture starTex;
    starTex.LoadFromData(pixels.data(), STAR_TEXTURE_SIZE, STAR_TEXTURE_SIZE, 4, false);
    m_StarHandle = m_Store->Adopt(std::move(starTex));
}

void SkyRenderer::GenerateStarGlowTexture()
{
    std::vector<unsigned char> pixels;
    GeneratePixels(pixels,
                   STAR_GLOW_TEXTURE_SIZE,
                   STAR_GLOW_TEXTURE_SIZE,
                   [](int x, int y, int w, int) -> Pixel
                   {
                       float center = w / 2.0f;
                       float dx = x - center;
                       float dy = y - center;
                       float normalizedDist = std::sqrt(dx * dx + dy * dy) / center;

                       // Soft gaussian glow for bright star halos
                       float glow = std::exp(-normalizedDist * normalizedDist * 2.5f);

                       // Additional soft outer ring
                       float ring = std::exp(-normalizedDist * 1.5f) * 0.3f;

                       float intensity = std::min(1.0f, glow + ring);

                       return {255, 255, 255, static_cast<uint8_t>(intensity * 255)};
                   });

    Texture starGlowTex;
    starGlowTex.LoadFromData(
        pixels.data(), STAR_GLOW_TEXTURE_SIZE, STAR_GLOW_TEXTURE_SIZE, 4, false);
    m_StarGlowHandle = m_Store->Adopt(std::move(starGlowTex));
}

void SkyRenderer::GenerateShootingStarTexture()
{
    // Create a horizontal streak texture for shooting stars
    const int width = 128;
    const int height = 16;
    std::vector<unsigned char> pixels;
    GeneratePixels(
        pixels,
        width,
        height,
        [](int x, int y, int w, int h) -> Pixel
        {
            float centerY = h / 2.0f;
            float progress = static_cast<float>(x) / w;
            float distFromCenter = std::abs(y - centerY) / centerY;

            // Bright head fading to dim tail
            float lengthFade = std::exp(-progress * 2.5f);

            // Thin streak
            float widthFade = std::exp(-distFromCenter * distFromCenter * 8.0f);

            float intensity = lengthFade * widthFade;

            return {255, 255, 255, static_cast<uint8_t>(std::min(intensity * 255.0f, 255.0f))};
        });

    Texture shootingStarTex;
    shootingStarTex.LoadFromData(pixels.data(), width, height, 4, false);
    m_ShootingStarHandle = m_Store->Adopt(std::move(shootingStarTex));
}

void SkyRenderer::GenerateLightRays()
{
    m_SunRays.clear();
    m_MoonRays.clear();

    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * rift::Pi));

    // Sun rays - spread across ~2/3 of screen with varying origins
    for (int i = 0; i < SUN_RAY_COUNT; i++)
    {
        LightRay ray;
        // Fan position determines angle within the spread
        float basePos = (i + 0.5f) / SUN_RAY_COUNT;
        float offset = (posDist(m_Rng) - 0.5f) * 0.08f;
        ray.xPosition = std::max(0.05f, std::min(0.95f, basePos + offset));

        // Origin offset - rays originate from different points along the "sun band"
        // Distribute across the band with some randomness
        float baseOrigin =
            (SUN_RAY_COUNT > 1)
                ? (static_cast<float>(i) / (SUN_RAY_COUNT - 1)) * 2.0f - 1.0f  // -1 to 1
                : 0.0f;
        ray.originOffset = baseOrigin + (posDist(m_Rng) - 0.5f) * 0.3f;
        ray.originOffset = std::max(-1.0f, std::min(1.0f, ray.originOffset));

        // Gentle angles - mostly straight down with slight variation
        ray.angle = (posDist(m_Rng) - 0.5f) * 0.3f;
        ray.length = 0.45f + posDist(m_Rng) * 0.45f;  // 45-90% of screen
        ray.width = 0.7f + posDist(m_Rng) * 0.5f;     // Width variation
        ray.brightness = 0.5f + posDist(m_Rng) * 0.5f;
        ray.phase = phaseDist(m_Rng);
        m_SunRays.push_back(ray);
    }

    // Moon beams - very subtle beams
    for (int i = 0; i < MOON_RAY_COUNT; i++)
    {
        LightRay ray;
        float basePos = (i + 0.5f) / MOON_RAY_COUNT;
        float offset = (posDist(m_Rng) - 0.5f) * 0.15f;
        ray.xPosition = std::max(0.1f, std::min(0.9f, basePos + offset));

        ray.originOffset = (posDist(m_Rng) - 0.5f) * 0.5f;  // Slight origin variation
        ray.angle = (posDist(m_Rng) - 0.5f) * 0.3f;
        ray.length = 0.3f + posDist(m_Rng) * 0.4f;
        ray.width = 0.7f + posDist(m_Rng) * 0.3f;
        ray.brightness = 0.4f + posDist(m_Rng) * 0.4f;
        ray.phase = phaseDist(m_Rng);
        m_MoonRays.push_back(ray);
    }
}

void SkyRenderer::GenerateStars(int count)
{
    m_Stars.clear();
    m_Stars.reserve(count);

    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.0f, 1.0f);
    std::uniform_real_distribution<float> brightDist(0.1f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * rift::Pi));
    std::uniform_real_distribution<float> speedDist(1.0f, 4.0f);
    std::uniform_real_distribution<float> sizeDist(0.2f, 0.9f);
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

    for (int i = 0; i < count; i++)
    {
        Star star;

        // Fully random position across entire 0-1 range for both axes
        star.position = glm::vec2(posDistX(m_Rng), posDistY(m_Rng));

        // Square the random value to bias the distribution toward dim stars,
        // matching real night-sky brightness distribution (many faint, few bright).
        float rawBright = brightDist(m_Rng);
        star.baseBrightness = rawBright * rawBright;

        // Rare bright stars (3%)
        if (posDistX(m_Rng) < 0.03f)
            star.baseBrightness = 0.8f + posDistX(m_Rng) * 0.2f;

        star.twinklePhase = phaseDist(m_Rng);
        star.twinkleSpeed = speedDist(m_Rng);
        star.size = sizeDist(m_Rng);

        // Star colors
        float colorVar = colorDist(m_Rng);
        if (colorVar < 0.45f)
            star.color = glm::vec3(1.0f, 1.0f, 1.0f);
        else if (colorVar < 0.65f)
            star.color = glm::vec3(0.88f, 0.92f, 1.0f);
        else if (colorVar < 0.80f)
            star.color = glm::vec3(1.0f, 1.0f, 0.92f);
        else if (colorVar < 0.92f)
            star.color = glm::vec3(1.0f, 0.94f, 0.8f);
        else
            star.color = glm::vec3(1.0f, 0.88f, 0.75f);

        m_Stars.push_back(star);
    }
}

void SkyRenderer::GenerateBackgroundStars(int count)
{
    m_BackgroundStars.clear();
    m_BackgroundStars.reserve(count);

    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.0f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * rift::Pi));
    std::uniform_real_distribution<float> speedDist(1.5f, 5.0f);
    std::uniform_real_distribution<float> sizeDist(0.08f, 0.25f);
    std::uniform_real_distribution<float> brightDist(0.04f, 0.2f);

    for (int i = 0; i < count; i++)
    {
        Star star;

        // Fully random position across entire viewport
        star.position = glm::vec2(posDistX(m_Rng), posDistY(m_Rng));

        star.baseBrightness = brightDist(m_Rng);
        star.twinklePhase = phaseDist(m_Rng);
        star.twinkleSpeed = speedDist(m_Rng);
        star.size = sizeDist(m_Rng);
        star.color = glm::vec3(1.0f, 1.0f, 1.0f);

        m_BackgroundStars.push_back(star);
    }
}

glm::vec2 SkyRenderer::GetLightSourcePosition(
    float arc, int screenWidth, int screenHeight, glm::vec2 cameraPos, float parallaxFactor) const
{
    (void)screenHeight;  // height doesn't affect the formula but is kept for symmetry
    // The sun/moon "world X" arcs across a band 3 viewports wide so the body
    // travels a substantial distance over the day rather than crossing a
    // single viewport. Anchor that band to the camera's current world X
    // (rounded to band steps) so the body is always reachable from the
    // camera while still being world-anchored - walking past it as the player
    // moves, but never permanently scrolling out of reach.
    const float bandWidth = static_cast<float>(screenWidth) * 3.0f;
    const float bandLeftWorld = std::floor(cameraPos.x / bandWidth) * bandWidth - bandWidth * 0.5f;
    float worldX = bandLeftWorld + (1.0f - arc) * bandWidth;

    // Y is sky-relative (above the viewport) - celestial bodies don't
    // descend with vertical camera motion.
    float arcHeight = 1.0f - std::pow(2.0f * arc - 1.0f, 2.0f);
    float worldY = cameraPos.y + 20.0f - arcHeight * 40.0f;

    // Apply the parallax factor (1.0 = full world stick) by subtracting the
    // appropriate fraction of camera position from the world anchor.
    return glm::vec2(worldX - cameraPos.x * parallaxFactor, worldY - cameraPos.y * parallaxFactor);
}

void SkyRenderer::RenderStars(IRenderer& renderer,
                              const TimeManager& time,
                              glm::vec2 cameraPos,
                              int screenWidth,
                              int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.01f)
        return;

    // Reduce overall star intensity - dimmer stars
    visibility *= 0.35f;

    // Stars appear gradually - brightest first, then dimmer ones fade in
    // visibility goes 0->1 as night falls, use it to threshold which stars appear
    float appearThreshold =
        1.0f - visibility * 2.0f;  // At full night, threshold is -1 (all visible)

    // Stars live in a fixed world-space tile; std::remainder wraps the world
    // position near the camera so we get parallax without per-region generation.
    const float fieldW = static_cast<float>(screenWidth) * STAR_FIELD_X_PERIODS;
    const float fieldH = static_cast<float>(screenHeight) * STAR_FIELD_Y_PERIODS;
    auto wrap1D = [](float anchor, float ref, float period)
    { return ref + std::remainder(anchor - ref, period); };

    // First pass: Background stars - only show some, gradually
    int bgCount = 0;
    int maxBgStars = static_cast<int>(m_BackgroundStars.size() * visibility * 0.4f);

    for (const auto& star : m_BackgroundStars)
    {
        if (bgCount >= maxBgStars)
            break;

        // Stars with higher brightness appear first
        if (star.baseBrightness < appearThreshold)
            continue;

        float twinkle =
            0.6f + 0.4f * std::sin(static_cast<float>(m_Time) * star.twinkleSpeed * 1.5f +
                                   star.twinklePhase);
        float brightness = star.baseBrightness * twinkle * visibility * 0.3f;

        if (brightness < 0.01f)
            continue;

        // World position inside the star-field tile, then wrapped near the camera.
        float worldX = wrap1D(star.position.x * fieldW, cameraPos.x, fieldW);
        float worldY = wrap1D(star.position.y * fieldH, cameraPos.y, fieldH);
        glm::vec2 screenPos(worldX - cameraPos.x, worldY - cameraPos.y);

        float size = 1.0f + star.size * 1.2f;

        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_StarAtlasOffset,
                       m_Store->Get(m_StarHandle),
                       screenPos - glm::vec2(size * 0.5f),
                       glm::vec2(size),
                       0.0f,
                       glm::vec4(star.color, brightness),
                       true);
        bgCount++;
    }

    // Second pass: Main stars - gradual appearance, sparkly twinkle.
    //
    // Each visible star can produce a glow (m_Store->Get(m_StarGlowHandle)) and a core
    // (m_Store->Get(m_StarHandle)). Emitting them inline alternates textures per star and
    // forces the OpenGL particle batch to flush between every pair, so a few
    // hundred stars become a few hundred draw calls. Instead we collect the
    // per-star screen data in one compute pass, then emit all glows in one
    // batch and all cores in another. Both blends are additive, so reordering
    // the emits does not change the rendered result.
    m_VisibleStarsScratch.clear();
    int maxStars = static_cast<int>(m_Stars.size() * visibility * 0.6f);
    if (m_VisibleStarsScratch.capacity() < static_cast<size_t>(maxStars))
    {
        m_VisibleStarsScratch.reserve(static_cast<size_t>(maxStars));
    }

    int starCount = 0;
    for (const auto& star : m_Stars)
    {
        if (starCount >= maxStars)
            break;

        // Brighter stars appear earlier in the evening
        if (star.baseBrightness < appearThreshold * 0.8f)
            continue;

        // Three sine waves at incommensurate frequencies create a non-repeating
        // twinkle pattern. The frequency ratios (1.2, 2.7, 0.5) are chosen to
        // avoid simple harmonic relationships so stars don't twinkle in unison.
        float twinkle1 =
            std::sin(static_cast<float>(m_Time) * star.twinkleSpeed * 1.2f + star.twinklePhase);
        float twinkle2 = std::sin(static_cast<float>(m_Time) * star.twinkleSpeed * 2.7f +
                                  star.twinklePhase * 1.3f);
        float twinkle3 = std::sin(static_cast<float>(m_Time) * star.twinkleSpeed * 0.5f +
                                  star.twinklePhase * 2.1f);

        // Multiplying two sines gives a burst when both are positive at once,
        // creating sharp brief flares that mimic atmospheric scintillation.
        float sparkle = std::max(0.0f, twinkle1 * twinkle2);
        float twinkle = 0.4f + 0.35f * twinkle1 + 0.15f * twinkle3 + 0.25f * sparkle;

        float brightness = star.baseBrightness * twinkle * visibility;

        if (brightness < 0.01f)
            continue;

        // World position inside the star-field tile, wrapped near the camera.
        float worldX = wrap1D(star.position.x * fieldW, cameraPos.x, fieldW);
        float worldY = wrap1D(star.position.y * fieldH, cameraPos.y, fieldH);

        VisibleStar v;
        v.screenPos = glm::vec2(worldX - cameraPos.x, worldY - cameraPos.y);
        v.color = star.color;
        v.size = (1.5f + star.size * 3.0f) * (0.5f + brightness * 0.5f);
        v.brightness = brightness;
        if (brightness > 0.25f && sparkle > 0.3f)
        {
            v.glowSize = (6.0f + star.size * 8.0f);
            v.glowAlpha = (brightness - 0.25f) * 0.1f;
        }
        else
        {
            v.glowSize = 0.0f;
            v.glowAlpha = 0.0f;
        }
        m_VisibleStarsScratch.push_back(v);
        starCount++;
    }

    // Emit pass 1: glows for qualifying stars - single m_Store->Get(m_StarGlowHandle) binding.
    for (const auto& v : m_VisibleStarsScratch)
    {
        if (v.glowSize <= 0.0f)
        {
            continue;
        }
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_StarGlowAtlasOffset,
                       m_Store->Get(m_StarGlowHandle),
                       v.screenPos - glm::vec2(v.glowSize * 0.5f),
                       glm::vec2(v.glowSize),
                       0.0f,
                       glm::vec4(v.color, v.glowAlpha),
                       true);
    }

    // Emit pass 2: cores for every visible star - single m_Store->Get(m_StarHandle) binding.
    for (const auto& v : m_VisibleStarsScratch)
    {
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_StarAtlasOffset,
                       m_Store->Get(m_StarHandle),
                       v.screenPos - glm::vec2(v.size * 0.5f),
                       glm::vec2(v.size),
                       0.0f,
                       glm::vec4(v.color, v.brightness * 0.7f),
                       true);
    }
}

void SkyRenderer::RenderSunRays(IRenderer& renderer,
                                const TimeManager& time,
                                glm::vec2 cameraPos,
                                int screenWidth,
                                int screenHeight)
{
    float sunArc = time.GetSunArc();
    if (sunArc < 0.0f)
        return;

    glm::vec3 sunColor = time.GetSunColor();

    // Get the sun's actual position on screen (with sun-layer parallax applied)
    glm::vec2 sunPos =
        GetLightSourcePosition(sunArc, screenWidth, screenHeight, cameraPos, SKY_PARALLAX_SUN);

    // Subtle intensity for god rays
    float baseIntensity = 0.006f;

    // Stronger during golden hour (low sun), softer at midday
    float goldenHourFactor = 1.0f - std::abs(sunArc - 0.5f);
    goldenHourFactor = 0.6f + goldenHourFactor * 0.4f;
    baseIntensity *= goldenHourFactor;

    // Fade near horizon (use <= and >= for continuous transitions at boundaries)
    if (sunArc <= 0.1f)
        baseIntensity *= sunArc / 0.1f;
    else if (sunArc >= 0.9f)
        baseIntensity *= (1.0f - sunArc) / 0.1f;

    // Warm, soft color - warmer during golden hour
    glm::vec3 rayColor = sunColor * glm::vec3(1.0f, 0.97f, 0.92f);
    if (sunArc < 0.15f)
    {
        // Golden hour - warmer orange tones
        rayColor = glm::vec3(1.0f, 0.75f, 0.45f);
    }

    int rayIndex = 0;
    for (const auto& ray : m_SunRays)
    {
        // Staggered cycles - each ray on its own timeline
        float rayStartDelay = rayIndex * 4.0f;
        float rayTime = static_cast<float>(m_Time) - rayStartDelay;

        if (rayTime < 0.0f)
        {
            rayIndex++;
            continue;
        }

        // Moderate cycle: 15-25 seconds per ray
        float cycleTime = 15.0f + ray.phase * 3.5f;
        float cycle = std::fmod(rayTime, cycleTime) / cycleTime;

        // Simple fade in/out animation - rays stay in place, just change opacity
        // 0.00-0.20: Fade in
        // 0.20-0.70: Hold
        // 0.70-1.00: Fade out
        float fadeAlpha;
        if (cycle < 0.20f)
        {
            fadeAlpha = cycle / 0.20f;
            fadeAlpha = fadeAlpha * fadeAlpha;  // Ease in
        }
        else if (cycle < 0.70f)
        {
            fadeAlpha = 1.0f;
        }
        else
        {
            float fadeProgress = (cycle - 0.70f) / 0.30f;
            fadeAlpha = 1.0f - fadeProgress * fadeProgress;  // Ease out
        }

        float alpha = baseIntensity * ray.brightness * fadeAlpha;
        if (alpha < 0.002f)
        {
            rayIndex++;
            continue;
        }

        // Fixed ray dimensions - no extension animation
        float rayWidth = 50.0f + ray.width * RAY_WIDTH;
        float rayLength = screenHeight * (0.5f + ray.length * 0.4f);

        // Calculate ray angle - fan pattern radiating from sun
        // ray.xPosition (0-1) maps to spread angle, ray.angle adds small variation
        float rayAngleDeg = (ray.xPosition - 0.5f) * SUN_RAY_SPREAD + ray.angle * 10.0f;
        float rayAngleRad = rayAngleDeg * static_cast<float>(rift::Pi) / 180.0f;

        // Apply origin offset - rays emanate from different points along the sun band
        float originOffsetPx = ray.originOffset * (screenWidth * SUN_BAND_WIDTH * 0.5f);
        glm::vec2 rayOrigin = sunPos + glm::vec2(originOffsetPx, 0.0f);

        // Position the ray so its top (origin) is at the offset sun position
        // Rotation happens around sprite center, so we offset to keep top at origin
        float halfLength = rayLength * 0.5f;
        float halfWidth = rayWidth * 0.5f;
        glm::vec2 rayPos;
        rayPos.x = rayOrigin.x - std::sin(rayAngleRad) * halfLength - halfWidth;
        rayPos.y = rayOrigin.y + std::cos(rayAngleRad) * halfLength - halfLength;

        // Soft outer glow
        float glowWidth = rayWidth * 2.0f;
        float glowHalfWidth = glowWidth * 0.5f;
        float glowLength = rayLength * 0.9f;
        float glowHalfLength = glowLength * 0.5f;
        glm::vec2 glowPos;
        glowPos.x = rayOrigin.x - std::sin(rayAngleRad) * glowHalfLength - glowHalfWidth;
        glowPos.y = rayOrigin.y + std::cos(rayAngleRad) * glowHalfLength - glowHalfLength;

        // Master fade: weather transitions ramp the sun rays in/out.
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_RayAtlasOffset,
                       m_Store->Get(m_RayHandle),
                       glowPos,
                       glm::vec2(glowWidth, glowLength),
                       rayAngleDeg,
                       glm::vec4(rayColor, alpha * 0.4f * m_CelestialFade),
                       true);

        // Main ray
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_RayAtlasOffset,
                       m_Store->Get(m_RayHandle),
                       rayPos,
                       glm::vec2(rayWidth, rayLength),
                       rayAngleDeg,
                       glm::vec4(rayColor, alpha * m_CelestialFade),
                       true);

        rayIndex++;
    }
}

void SkyRenderer::RenderMoonRays(IRenderer& renderer,
                                 const TimeManager& time,
                                 glm::vec2 cameraPos,
                                 int screenWidth,
                                 int screenHeight)
{
    float moonArc = time.GetMoonArc();
    if (moonArc < 0.0f)
        return;

    // Get the moon's actual position on screen (with moon-layer parallax)
    glm::vec2 moonPos =
        GetLightSourcePosition(moonArc, screenWidth, screenHeight, cameraPos, SKY_PARALLAX_MOON);

    // Soft blue-white moonlight color
    glm::vec3 moonColor(0.75f, 0.85f, 1.0f);

    // Moon phases 0-8 (0=new, 4=full, 8=new). Convert to brightness:
    // full moon (phase 4) -> phaseFactor 1.0, new moon (phase 0/8) -> 0.3 (dim but not invisible).
    int phase = time.GetMoonPhase();
    float phaseFactor = 1.0f - std::abs(phase - 4) / 4.0f;
    phaseFactor = std::max(0.3f, phaseFactor);

    // Subtle intensity for moon beams
    float baseIntensity = 0.004f * phaseFactor;

    // Fade near horizon
    if (moonArc < 0.1f)
        baseIntensity *= moonArc / 0.1f;
    else if (moonArc > 0.9f)
        baseIntensity *= (1.0f - moonArc) / 0.1f;

    int rayIndex = 0;
    for (const auto& ray : m_MoonRays)
    {
        // Staggered cycles
        float rayStartDelay = rayIndex * 6.0f;
        float rayTime = static_cast<float>(m_Time) - rayStartDelay;

        if (rayTime < 0.0f)
        {
            rayIndex++;
            continue;
        }

        // Moderate cycle: 20-30 seconds
        float cycleTime = 20.0f + ray.phase * 3.5f;
        float cycle = std::fmod(rayTime, cycleTime) / cycleTime;

        // Simple fade in/out animation - rays stay in place
        float fadeAlpha;
        if (cycle < 0.20f)
        {
            fadeAlpha = cycle / 0.20f;
            fadeAlpha = fadeAlpha * fadeAlpha;
        }
        else if (cycle < 0.70f)
        {
            fadeAlpha = 1.0f;
        }
        else
        {
            float fadeProgress = (cycle - 0.70f) / 0.30f;
            fadeAlpha = 1.0f - fadeProgress * fadeProgress;
        }

        float alpha = baseIntensity * ray.brightness * fadeAlpha;
        if (alpha < 0.002f)
        {
            rayIndex++;
            continue;
        }

        // Fixed ray dimensions - no extension animation
        float rayWidth = 50.0f + ray.width * 70.0f;
        float rayLength = screenHeight * (0.35f + ray.length * 0.45f);

        // Calculate ray angle - fan pattern radiating from moon
        float spreadAngle = 60.0f;
        float rayAngleDeg = (ray.xPosition - 0.5f) * spreadAngle + ray.angle * 8.0f;
        float rayAngleRad = rayAngleDeg * static_cast<float>(rift::Pi) / 180.0f;

        // Apply origin offset for moon rays (subtle, less than sun)
        float originOffsetPx = ray.originOffset * (screenWidth * 0.15f);
        glm::vec2 rayOrigin = moonPos + glm::vec2(originOffsetPx, 0.0f);

        // Position the ray so its top (origin) is at the moon position
        float halfLength = rayLength * 0.5f;
        float halfWidth = rayWidth * 0.5f;
        glm::vec2 rayPos;
        rayPos.x = rayOrigin.x - std::sin(rayAngleRad) * halfLength - halfWidth;
        rayPos.y = rayOrigin.y + std::cos(rayAngleRad) * halfLength - halfLength;

        // Soft outer glow
        float glowWidth = rayWidth * 1.8f;
        float glowHalfWidth = glowWidth * 0.5f;
        float glowLength = rayLength * 0.85f;
        float glowHalfLength = glowLength * 0.5f;
        glm::vec2 glowPos;
        glowPos.x = rayOrigin.x - std::sin(rayAngleRad) * glowHalfLength - glowHalfWidth;
        glowPos.y = rayOrigin.y + std::cos(rayAngleRad) * glowHalfLength - glowHalfLength;

        // Master fade: weather transitions ramp the moon rays in/out.
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_RayAtlasOffset,
                       m_Store->Get(m_RayHandle),
                       glowPos,
                       glm::vec2(glowWidth, glowLength),
                       rayAngleDeg,
                       glm::vec4(moonColor, alpha * 0.5f * m_CelestialFade),
                       true);

        // Main beam
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_RayAtlasOffset,
                       m_Store->Get(m_RayHandle),
                       rayPos,
                       glm::vec2(rayWidth, rayLength),
                       rayAngleDeg,
                       glm::vec4(moonColor, alpha * m_CelestialFade),
                       true);

        rayIndex++;
    }
}

void SkyRenderer::UpdateShootingStars(float deltaTime, int screenWidth, int screenHeight)
{
    // Update existing shooting stars
    for (auto it = m_ShootingStars.begin(); it != m_ShootingStars.end();)
    {
        it->position += it->velocity * deltaTime;
        it->lifetime -= deltaTime;

        if (it->lifetime <= 0.0f)
            it = m_ShootingStars.erase(it);
        else
            ++it;
    }

    // Spawn new shooting stars occasionally. The base 2-6s interval is divided
    // by the meteor multiplier (Clear=1.0, MeteorShower=8.0) so weather can
    // dramatically increase density.
    m_ShootingStarTimer += deltaTime;
    float baseInterval = 4.0f + std::sin(static_cast<float>(m_Time) * 0.1f) * 2.0f;
    float spawnInterval = baseInterval / std::max(0.1f, m_MeteorRateMultiplier);
    size_t maxConcurrent =
        (m_MeteorRateMultiplier > 2.0f) ? static_cast<size_t>(m_MeteorRateMultiplier * 1.5f) : 2;

    if (m_ShootingStarTimer >= spawnInterval && m_ShootingStars.size() < maxConcurrent)
    {
        m_ShootingStarTimer = 0.0f;
        SpawnShootingStar(screenWidth, screenHeight);
    }
}

void SkyRenderer::SpawnShootingStar(int screenWidth, int screenHeight)
{
    if (screenWidth <= 0 || screenHeight <= 0)
        return;

    ShootingStar star;

    // Spawn the shooting star anywhere in the world's star-field tile so
    // RenderShootingStars' wrap places it close to the camera. Using the
    // larger field (3x viewport wide, 2x tall) means the trail can begin
    // and end in different "wrap cells", and meteor showers don't all
    // streak out of the same screen edge.
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    const float fieldW = static_cast<float>(screenWidth) * STAR_FIELD_X_PERIODS;
    const float fieldH = static_cast<float>(screenHeight) * STAR_FIELD_Y_PERIODS;

    if (posDist(m_Rng) < 0.6f)
    {
        // Spawn near the top of the star-field tile so the streak falls
        // into view from above.
        star.position.x = posDist(m_Rng) * fieldW;
        star.position.y = -10.0f + posDist(m_Rng) * fieldH * 0.05f;
    }
    else
    {
        // Spawn near the right edge so the streak crosses inward.
        star.position.x = fieldW + 10.0f - posDist(m_Rng) * fieldW * 0.05f;
        star.position.y = posDist(m_Rng) * fieldH * 0.4f;
    }

    // Diagonal downward velocity. During MeteorShower (multiplier > 2) speed up
    // and lengthen the streak so individual meteors actually register; the base
    // (Clear-weather) values stay subtle.
    const bool isMeteorShower = m_MeteorRateMultiplier > 2.0f;
    const float speedBoost = isMeteorShower ? 1.4f : 1.0f;
    float speed = (350.0f + posDist(m_Rng) * 250.0f) * speedBoost;
    float angle = 0.4f + posDist(m_Rng) * 0.7f;
    star.velocity.x = -std::cos(angle) * speed;
    star.velocity.y = std::sin(angle) * speed;

    star.lifetime = (isMeteorShower ? 0.55f : 0.3f) + posDist(m_Rng) * 0.4f;
    star.maxLifetime = star.lifetime;
    // Brightness peaks higher and the dim end is lifted so even short-lived
    // streaks are clearly visible against a busy sky during MeteorShower.
    star.brightness =
        isMeteorShower ? 0.65f + posDist(m_Rng) * 0.35f : 0.3f + posDist(m_Rng) * 0.35f;
    star.length = (isMeteorShower ? 110.0f : 50.0f) + posDist(m_Rng) * 70.0f;

    m_ShootingStars.push_back(star);
}

void SkyRenderer::RenderShootingStars(IRenderer& renderer,
                                      const TimeManager& time,
                                      glm::vec2 cameraPos,
                                      int screenWidth,
                                      int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.3f)
        return;

    // Same star-field wrap as RenderStars: shooting stars are anchored in
    // the world's star-field tile and wrap around the camera so they always
    // streak across the visible sky regardless of where the player walks.
    const float fieldW = static_cast<float>(screenWidth) * STAR_FIELD_X_PERIODS;
    const float fieldH = static_cast<float>(screenHeight) * STAR_FIELD_Y_PERIODS;
    auto wrap1D = [](float anchor, float ref, float period)
    { return ref + std::remainder(anchor - ref, period); };

    for (const auto& star : m_ShootingStars)
    {
        float fadeIn = std::min(1.0f, (star.maxLifetime - star.lifetime) / 0.08f);
        float fadeOut = std::min(1.0f, star.lifetime / 0.12f);
        float alpha = star.brightness * fadeIn * fadeOut * visibility;

        if (alpha < 0.01f)
            continue;

        float angle =
            std::atan2(star.velocity.y, star.velocity.x) * 180.0f / static_cast<float>(rift::Pi);
        // Thicker trail during MeteorShower so streaks read against the sky.
        const float trailThickness = (m_MeteorRateMultiplier > 2.0f) ? 6.0f : 3.0f;
        glm::vec2 size(star.length, trailThickness);

        // star.position is its world coordinate inside the star-field tile.
        float worldX = wrap1D(star.position.x, cameraPos.x, fieldW);
        float worldY = wrap1D(star.position.y, cameraPos.y, fieldH);
        glm::vec2 screenPos(worldX - cameraPos.x, worldY - cameraPos.y - 1.5f);

        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_ShootingStarAtlasOffset,
                       m_Store->Get(m_ShootingStarHandle),
                       screenPos,
                       size,
                       angle,
                       glm::vec4(1.0f, 1.0f, 1.0f, alpha),
                       true);
    }
}

void SkyRenderer::RenderAtmosphericGlow(IRenderer& renderer,
                                        const TimeManager& time,
                                        int screenWidth,
                                        int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.2f)
        return;

    // Subtle blue atmospheric glow at horizon during night
    float horizonGlowAlpha = visibility * 0.025f;

    // Bottom horizon glow
    float glowHeight = screenHeight * 0.12f;
    renderer.DrawColoredRect(glm::vec2(0, screenHeight - glowHeight),
                             glm::vec2(static_cast<float>(screenWidth), glowHeight),
                             glm::vec4(0.08f, 0.12f, 0.25f, horizonGlowAlpha),
                             true);

    // Occasional subtle shimmer at top
    float shimmer = std::sin(static_cast<float>(m_Time) * 0.25f) * 0.5f + 0.5f;
    float auroraAlpha = visibility * 0.01f * shimmer;

    if (auroraAlpha > 0.003f)
    {
        // When an aurora is active, drift the wash through the same palette the
        // bands use (dimmed) so the base arc matches the live curtains; otherwise
        // keep the cool default tint.
        glm::vec3 washTint(0.15f, 0.3f, 0.25f);
        if (m_AuroraVisible)
        {
            washTint = AuroraMath::AuroraColor(static_cast<float>(m_Time) * 0.02f) * 0.35f;
        }
        renderer.DrawColoredRect(glm::vec2(0, 0),
                                 glm::vec2(static_cast<float>(screenWidth), screenHeight * 0.04f),
                                 glm::vec4(washTint, auroraAlpha),
                                 true);
    }
}

void SkyRenderer::GenerateDewSparkles()
{
    m_DewSparkles.clear();
    m_DewSparkles.reserve(DEW_SPARKLE_COUNT);

    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.55f, 1.0f);  // Lower screen band
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * rift::Pi));
    std::uniform_real_distribution<float> brightDist(0.4f, 1.0f);
    std::uniform_real_distribution<float> speedDist(1.5f, 5.0f);

    for (int i = 0; i < DEW_SPARKLE_COUNT; i++)
    {
        DewSparkle sparkle;
        sparkle.position = glm::vec2(posDistX(m_Rng), posDistY(m_Rng));
        sparkle.phase = phaseDist(m_Rng);
        sparkle.brightness = brightDist(m_Rng);
        sparkle.speed = speedDist(m_Rng);
        m_DewSparkles.push_back(sparkle);
    }
}

void SkyRenderer::RenderDawnHorizonGlow(IRenderer& renderer,
                                        const TimeManager& time,
                                        int screenWidth,
                                        int screenHeight)
{
    float dawnIntensity = time.GetDawnIntensity();
    if (dawnIntensity < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // Soft warm wash over entire screen using stretched glow texture
    float glowSize = std::max(sw, sh) * 2.5f;

    // Large soft glow from bottom center (sunrise direction)
    DrawSkyElement(renderer,
                   m_AtlasTexture,
                   m_GlowAtlasOffset,
                   m_Store->Get(m_GlowHandle),
                   glm::vec2(sw * 0.5f - glowSize * 0.5f, sh - glowSize * 0.3f),
                   glm::vec2(glowSize, glowSize),
                   0.0f,
                   glm::vec4(1.0f, 0.6f, 0.4f, dawnIntensity * 0.15f),
                   true);

    // Secondary softer glow higher up
    DrawSkyElement(renderer,
                   m_AtlasTexture,
                   m_GlowAtlasOffset,
                   m_Store->Get(m_GlowHandle),
                   glm::vec2(sw * 0.5f - glowSize * 0.5f, sh * 0.3f - glowSize * 0.5f),
                   glm::vec2(glowSize, glowSize),
                   0.0f,
                   glm::vec4(1.0f, 0.7f, 0.55f, dawnIntensity * 0.08f),
                   true);
}

void SkyRenderer::RenderDawnGradient(IRenderer& renderer,
                                     const TimeManager& time,
                                     int screenWidth,
                                     int screenHeight)
{
    float dawnIntensity = time.GetDawnIntensity();
    if (dawnIntensity < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // Soft purple/pink wash from top using stretched glow texture
    float glowSize = std::max(sw, sh) * 2.0f;

    // Large soft glow from top (pre-dawn sky color)
    DrawSkyElement(renderer,
                   m_AtlasTexture,
                   m_GlowAtlasOffset,
                   m_Store->Get(m_GlowHandle),
                   glm::vec2(sw * 0.5f - glowSize * 0.5f, -glowSize * 0.6f),
                   glm::vec2(glowSize, glowSize),
                   0.0f,
                   glm::vec4(0.6f, 0.4f, 0.7f, dawnIntensity * 0.1f),
                   true);

    // Overall soft pink tint across screen
    DrawSkyElement(renderer,
                   m_AtlasTexture,
                   m_GlowAtlasOffset,
                   m_Store->Get(m_GlowHandle),
                   glm::vec2(sw * 0.5f - glowSize * 0.5f, sh * 0.5f - glowSize * 0.5f),
                   glm::vec2(glowSize, glowSize),
                   0.0f,
                   glm::vec4(1.0f, 0.65f, 0.6f, dawnIntensity * 0.06f),
                   true);
}

void SkyRenderer::RenderDewSparkles(IRenderer& renderer,
                                    const TimeManager& time,
                                    int screenWidth,
                                    int screenHeight)
{
    float sunArc = time.GetSunArc();

    // Only visible during early morning (sunrise to mid-morning)
    if (sunArc < 0.0f || sunArc > 0.25f)
        return;

    // Fade in at sunrise, peak around sunArc 0.1, fade out by 0.25
    float visibility;
    if (sunArc < 0.1f)
        visibility = sunArc / 0.1f;  // Fade in
    else
        visibility = 1.0f - (sunArc - 0.1f) / 0.15f;  // Fade out

    visibility = std::max(0.0f, std::min(1.0f, visibility));
    if (visibility < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    for (const auto& sparkle : m_DewSparkles)
    {
        // Sharp twinkle - brief bright flashes
        float twinkle = std::sin(static_cast<float>(m_Time) * sparkle.speed + sparkle.phase);
        twinkle = std::max(0.0f, twinkle - 0.5f) / 0.5f;  // Only top 50% of sine wave
        twinkle = twinkle * twinkle;                      // Sharp peaks

        float brightness = sparkle.brightness * twinkle * visibility * 0.8f;
        if (brightness < 0.08f)
            continue;

        glm::vec2 screenPos(sparkle.position.x * sw, sparkle.position.y * sh);

        // Small bright point with warm golden color
        float size = 2.0f + brightness * 3.0f;

        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_StarAtlasOffset,
                       m_Store->Get(m_StarHandle),
                       screenPos - glm::vec2(size * 0.5f),
                       glm::vec2(size),
                       0.0f,
                       glm::vec4(1.0f, 0.92f, 0.65f, brightness),
                       true);
    }
}

// Cloud shadows: alpha-blended black at 4-8% approximates a multiplicative
// dim without a dedicated render-state path. Fades off at night.

void SkyRenderer::RenderCloudShadows(
    IRenderer& renderer, glm::vec2 cameraPos, glm::vec2 viewSize, float time, float nightFactor)
{
    if (!m_Initialized)
    {
        return;
    }

    // Disable at night - no shadows when the sun isn't out.
    float dayFactor = 1.0f - std::clamp(nightFactor, 0.0f, 1.0f);
    if (dayFactor < 0.05f)
    {
        return;
    }

    const float blobSize = ambience::CLOUD_SHADOW_SIZE_PX;
    const float baseAlpha = ambience::CLOUD_SHADOW_INTENSITY * dayFactor;

    for (int i = 0; i < ambience::CLOUD_SHADOW_COUNT; ++i)
    {
        glm::vec2 worldPos = ComputeCloudShadowPosition(i, time, cameraPos);
        // Convert to camera-local screen coordinates (renderer's world projection
        // expects positions relative to camera).
        glm::vec2 screenPos = worldPos - cameraPos - glm::vec2(blobSize * 0.5f);

        // Cull aggressively - only render blobs whose bounding box overlaps view.
        if (screenPos.x + blobSize < 0.0f || screenPos.x > viewSize.x)
            continue;
        if (screenPos.y + blobSize < 0.0f || screenPos.y > viewSize.y)
            continue;

        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_GlowAtlasOffset,
                       m_Store->Get(m_GlowHandle),
                       screenPos,
                       glm::vec2(blobSize),
                       0.0f,
                       glm::vec4(0.0f, 0.0f, 0.0f, baseAlpha),
                       false);
    }
}

void SkyRenderer::GenerateLightningBolt(int screenWidth, int screenHeight)
{
    m_LightningBolt.mainPath.clear();
    m_LightningBolt.branches.clear();

    const float fw = static_cast<float>(std::max(screenWidth, 1));
    const float fh = static_cast<float>(std::max(screenHeight, 1));

    // Origin somewhere in the middle 60% of the screen so the bolt crosses
    // the visible viewport rather than clipping at an edge.
    std::uniform_real_distribution<float> originDist(0.2f, 0.8f);
    float currentX = originDist(m_Rng) * fw;
    float currentY = 0.0f;

    // ~25 segments regardless of viewport, so very tall screens still feel
    // dense and short screens don't get a polyline made of two giant chunks.
    constexpr int kStepCount = 25;
    const float stepY = fh / static_cast<float>(kStepCount);
    constexpr float kJitterX = 20.0f;

    std::uniform_real_distribution<float> sym(-1.0f, 1.0f);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    m_LightningBolt.mainPath.emplace_back(currentX, currentY);
    for (int i = 0; i < kStepCount; ++i)
    {
        currentY += stepY;
        currentX += sym(m_Rng) * kJitterX;
        m_LightningBolt.mainPath.emplace_back(currentX, currentY);

        // Spawn a sub-branch at this point with low probability. Cap at 3
        // total branches per bolt so the strike stays visually readable.
        if (m_LightningBolt.branches.size() < 3 && i > 1 && i < kStepCount - 2 &&
            uni(m_Rng) < 0.15f)
        {
            std::vector<glm::vec2> branch;
            branch.emplace_back(currentX, currentY);
            const int branchSteps = 4 + static_cast<int>(uni(m_Rng) * 5.0f);  // 4-8
            const float angle = (uni(m_Rng) < 0.5f ? -1.0f : 1.0f) * glm::radians(30.0f);
            const float dirX = std::sin(angle);
            const float dirY = std::cos(angle);
            const float subStep = stepY * 0.8f;
            float bx = currentX;
            float by = currentY;
            for (int s = 0; s < branchSteps; ++s)
            {
                bx += dirX * subStep + sym(m_Rng) * 10.0f;
                by += dirY * subStep;
                branch.emplace_back(bx, by);
            }
            m_LightningBolt.branches.push_back(std::move(branch));
        }
    }
}

void SkyRenderer::RenderLightningBolt(IRenderer& renderer, int screenWidth, int screenHeight)
{
    (void)screenWidth;
    (void)screenHeight;

    if (m_LightningBolt.mainPath.size() < 2)
    {
        return;
    }

    constexpr float kBoltDuration = 0.18f;
    const float fade = std::clamp(m_LightningBoltTimer / kBoltDuration, 0.0f, 1.0f);
    const glm::vec3 boltTint(0.92f, 0.95f, 1.0f);
    const float baseAlpha = 0.9f * fade;

    auto drawSegment = [&](glm::vec2 A, glm::vec2 B, float thickness, float alphaScale)
    {
        const glm::vec2 d = B - A;
        const float length = glm::length(d);
        if (length < 1e-3f)
        {
            return;
        }
        const glm::vec2 size(thickness, length);
        const glm::vec2 center = (A + B) * 0.5f;
        const glm::vec2 pos = center - size * 0.5f;
        // The rect at rotation=0 has its long axis pointing +Y. Rotate so
        // that long axis aligns with (B - A): angle = atan2(-Dx, Dy).
        const float angleDeg = glm::degrees(std::atan2(-d.x, d.y));
        DrawSkyElement(renderer,
                       m_AtlasTexture,
                       m_GlowAtlasOffset,
                       m_Store->Get(m_GlowHandle),
                       pos,
                       size,
                       angleDeg,
                       glm::vec4(boltTint, baseAlpha * alphaScale),
                       true);
    };

    // Branches first, thinner and dimmer so the main bolt reads on top.
    for (const auto& branch : m_LightningBolt.branches)
    {
        for (size_t i = 1; i < branch.size(); ++i)
        {
            drawSegment(branch[i - 1], branch[i], 4.0f, 0.6f);
        }
    }
    // Main bolt - full thickness and brightness.
    for (size_t i = 1; i < m_LightningBolt.mainPath.size(); ++i)
    {
        drawSegment(m_LightningBolt.mainPath[i - 1], m_LightningBolt.mainPath[i], 6.0f, 1.0f);
    }
}
