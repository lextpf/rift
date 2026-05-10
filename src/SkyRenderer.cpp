#include "SkyRenderer.h"
#include "TimeManager.h"

#include "AmbienceConfig.h"
#include "MathConstants.h"
#include "ProceduralTexture.h"
#include "WeatherDefinitions.h"

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

void SkyRenderer::Initialize()
{
    if (m_Initialized)
        return;

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
    m_GlowTexture.LoadFromData(glowPixels.data(), GLOW_TEXTURE_SIZE, GLOW_TEXTURE_SIZE, 4, false);

    GenerateLightPoolTexture();
    GenerateAuroraCurtainTexture();
    // Hand-painted small aurora particle for the floating wisp layer.
    // Loads from disk; if missing the Texture stays default-constructed and
    // DrawSpriteAlpha gracefully falls back to a colored rectangle.
    m_AuroraSmallTexture.LoadFromFile("assets/particles/bc3ad898-4ba3-406a-af06-63256cbd45b2.png");

    m_Initialized = true;
}

void SkyRenderer::UploadTextures(IRenderer& renderer)
{
    if (!m_Initialized)
        return;

    renderer.UploadTexture(m_RayTexture);
    renderer.UploadTexture(m_StarTexture);
    renderer.UploadTexture(m_StarGlowTexture);
    renderer.UploadTexture(m_ShootingStarTexture);
    renderer.UploadTexture(m_GlowTexture);
    renderer.UploadTexture(m_LightPoolTexture);
    renderer.UploadTexture(m_AuroraCurtainTexture);
    renderer.UploadTexture(m_AuroraSmallTexture);
}

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
    m_LightPoolTexture.LoadFromData(pixels.data(), kSize, kSize, 4, false);
}

void SkyRenderer::GenerateAuroraCurtainTexture()
{
    // Curtain segment: heavy top/bottom + horizontal feathering so chained copies
    // tile seamlessly along a warped path. Max alpha clipped <1 so the curtain
    // reads as translucent gas rather than a solid ribbon.
    constexpr int kW = 128;
    constexpr int kH = 256;
    std::vector<unsigned char> pixels(kW * kH * 4);
    for (int y = 0; y < kH; ++y)
    {
        float ny = static_cast<float>(y) / static_cast<float>(kH);  // 0 = top, 1 = bottom
        // Long, gentle vertical profile - peaks ~y=0.45, fades softly to
        // both ends so segments stack vertically with no banding.
        float topFade = std::clamp(ny / 0.45f, 0.0f, 1.0f);
        topFade = topFade * topFade * (3.0f - 2.0f * topFade);  // smoothstep
        float bottomFade = std::clamp((1.0f - ny) / 0.55f, 0.0f, 1.0f);
        bottomFade = bottomFade * bottomFade * (3.0f - 2.0f * bottomFade);
        float vertical = topFade * bottomFade;

        for (int x = 0; x < kW; ++x)
        {
            float nx = static_cast<float>(x) / static_cast<float>(kW);  // 0..1

            // Subtle vertical streak - single low frequency so segments don't
            // strobe when chained.
            const float kPi = static_cast<float>(rift::Pi);
            float streak = 0.5f + 0.5f * std::sin(nx * 6.0f * kPi);
            streak = 0.7f + 0.3f * streak;  // shallow modulation

            // Strong horizontal edge fade so segments overlap into a smooth
            // ribbon without visible seams.
            float edgeDist = std::abs(nx * 2.0f - 1.0f);  // 0 at center, 1 at edges
            float edge = std::pow(1.0f - edgeDist, 1.4f);

            // Cap max alpha at 0.55 so the texture is inherently translucent.
            float alpha = std::clamp(vertical * streak * edge * 0.55f, 0.0f, 1.0f);
            int idx = (y * kW + x) * 4;
            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(alpha * 255.0f);
        }
    }
    m_AuroraCurtainTexture.LoadFromData(pixels.data(), kW, kH, 4, false);
}

void SkyRenderer::Update(float deltaTime, const TimeManager& time)
{
    m_Time += static_cast<double>(deltaTime);

    // Cache weather-driven flags for the upcoming Render pass.
    const WeatherDefinition& def = GetWeatherDefinition(time.GetWeather());
    m_AuroraVisible = def.showAurora;
    m_MeteorRateMultiplier = def.meteorRateMultiplier;

    // Lightning: only when the weather wants flashes. Decrement countdown,
    // trigger a flash, jitter the next interval.
    if (def.lightningIntervalSeconds > 0.0f)
    {
        m_LightningTimer -= deltaTime;
        if (m_LightningTimer <= 0.0f)
        {
            m_LightningFlashTimer = 0.08f;
            // +/-30% jitter on the configured interval.
            std::uniform_real_distribution<float> jitter(0.7f, 1.3f);
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

    const WeatherDefinition& def = GetWeatherDefinition(time.GetWeather());

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
    if (sunArc >= 0.0f && def.showCelestialBodies)
    {
        RenderSunRays(renderer, time, cameraPos, screenWidth, screenHeight);
    }

    // Moon rays during night - same gating.
    float moonArc = time.GetMoonArc();
    if (moonArc >= 0.0f && starVisibility > 0.3f && def.showCelestialBodies)
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
        renderer.DrawSpriteAlpha(m_GlowTexture,
                                 glm::vec2(-static_cast<float>(screenWidth) * 0.5f,
                                           -static_cast<float>(screenHeight) * 0.5f),
                                 glm::vec2(static_cast<float>(screenWidth) * 2.0f,
                                           static_cast<float>(screenHeight) * 2.0f),
                                 0.0f,
                                 glm::vec4(0.92f, 0.95f, 1.0f, alpha),
                                 true);
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
    // Three layers below: background wash, 14 curtains, ~36 floating wisps.
    const float t = static_cast<float>(m_Time);
    const float sw = static_cast<float>(screenWidth);
    const float sh = static_cast<float>(screenHeight);

    // Richer 8-stop palette - saturated greens, cyans, blues, violets, magentas.
    auto auroraColor = [](float phase) -> glm::vec3
    {
        float p = phase - std::floor(phase);
        constexpr int kStops = 8;
        const glm::vec3 stops[kStops] = {
            {0.10f, 1.00f, 0.45f},  // saturated emerald
            {0.20f, 1.00f, 0.75f},  // mint
            {0.35f, 0.95f, 1.00f},  // cyan-aqua
            {0.40f, 0.55f, 1.00f},  // electric blue
            {0.65f, 0.30f, 1.00f},  // deep violet
            {0.95f, 0.35f, 0.95f},  // hot magenta
            {1.00f, 0.55f, 0.85f},  // pink
            {0.30f, 1.00f, 0.85f},  // teal-mint
        };
        float pos = p * static_cast<float>(kStops);
        int i = static_cast<int>(pos) % kStops;
        int j = (i + 1) % kStops;
        float f = pos - std::floor(pos);
        return glm::mix(stops[i], stops[j], f);
    };

    // Wrap a world-X anchor so it stays close to the camera modulo period.
    // This keeps curtains/wisps tiling across the world without needing
    // millions of them, while preserving "true world position" feel within
    // each period.
    auto wrapNearCamera = [](float anchorX, float cameraX, float period)
    { return cameraX + std::remainder(anchorX - cameraX, period); };

    // (Previously a giant screen-anchored "wash" sprite was drawn here as a
    // soft sky glow; removed because it read as a fixed oval pinned to the
    // top of the viewport and broke the world-anchored feel of the curtains.)

    // ---- Layer 1: world-anchored noodle curtains ----
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
    const float curtainPeriod = sw * 6.5f;
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
        const float segSpacing = segWidth * (0.42f + r3 * 0.20f);  // overlap factor
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

            for (int s = 0; s < segments; ++s)
            {
                float fs = static_cast<float>(s);
                float segNorm = fs / static_cast<float>(segments - 1);  // 0..1 along ribbon
                float segOffset = fs * segSpacing - ribbonSpan * 0.5f;
                float screenX = (worldCenterX - cameraPos.x) + segOffset;

                // Multi-frequency Y warp using per-curtain spatial frequency,
                // amplitude, and time-drift speed.
                float waveT = fs * waveSpatial + ti + t * waveSpeed;
                float yWarp = std::sin(waveT) * ampMajor + std::sin(waveT * midMul + ti) * ampMid +
                              std::sin(waveT * microMul + ti * 0.5f) * ampMicro;
                float tilt = (segNorm - 0.5f) * 2.0f * ribbonTilt;
                float xJitter = std::sin(waveT * 1.9f + ti * 0.7f) * 6.0f;
                float segY = baseY + yWarp + tilt;

                // Color shifts gently along the noodle so different sections glow
                // different aurora hues.
                float colorPhase = t * 0.06f + ti * 0.20f + fs * 0.04f;
                glm::vec3 color = auroraColor(colorPhase);

                // Per-segment alpha: ribbonPulse x segment-local pulse x ends fade.
                // Translucent (max ~0.40 per segment); overlap stacks but never
                // becomes opaque. Bumped from 0.08+0.22 -> 0.13+0.27 for stronger
                // ribbons during AuroraNight.
                float segPulse = 0.5f + 0.5f * std::sin(waveT * 1.3f);
                float endsFade = 1.0f - std::abs(segNorm - 0.5f) * 1.7f;
                endsFade = std::clamp(endsFade, 0.0f, 1.0f);
                float alpha = (0.13f + 0.27f * segPulse) * (0.55f + 0.45f * ribbonPulse) * endsFade;

                renderer.DrawSpriteAlpha(m_AuroraCurtainTexture,
                                         glm::vec2(screenX - segWidth * 0.5f + xJitter, segY),
                                         glm::vec2(segWidth, curtainH),
                                         0.0f,
                                         glm::vec4(color, alpha),
                                         true);
            }

            // Soft glow halo following the warped path during bright pulses so
            // bloom hugs the noodle rather than a flat rectangle.
            if (ribbonPulse > 0.55f)
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
                    glm::vec3 color = auroraColor(t * 0.06f + ti * 0.20f + fs * 0.04f);
                    float glowSize = curtainH * 1.3f;
                    renderer.DrawSpriteAlpha(
                        m_GlowTexture,
                        glm::vec2(screenX - glowSize * 0.5f, segY - curtainH * 0.10f),
                        glm::vec2(glowSize, glowSize * 0.85f),
                        0.0f,
                        glm::vec4(color, (ribbonPulse - 0.55f) * 0.40f),
                        true);
                }
            }
        }
    }

    // ---- Layer 2: floating wisps (world-anchored, hand-painted texture) ----
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
        glm::vec3 color = auroraColor(colorPhase);

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
            renderer.DrawSpriteAlpha(m_AuroraSmallTexture,
                                     pos - glm::vec2(wispSize * 0.5f),
                                     glm::vec2(wispSize),
                                     0.0f,
                                     glm::vec4(color, alpha),
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

    m_RayTexture.LoadFromData(pixels.data(), RAY_TEXTURE_WIDTH, RAY_TEXTURE_HEIGHT, 4, false);
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

    m_StarTexture.LoadFromData(pixels.data(), STAR_TEXTURE_SIZE, STAR_TEXTURE_SIZE, 4, false);
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

    m_StarGlowTexture.LoadFromData(
        pixels.data(), STAR_GLOW_TEXTURE_SIZE, STAR_GLOW_TEXTURE_SIZE, 4, false);
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

    m_ShootingStarTexture.LoadFromData(pixels.data(), width, height, 4, false);
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

        renderer.DrawSpriteAlpha(m_StarTexture,
                                 screenPos - glm::vec2(size * 0.5f),
                                 glm::vec2(size),
                                 0.0f,
                                 glm::vec4(star.color, brightness),
                                 true);
        bgCount++;
    }

    // Second pass: Main stars - gradual appearance, sparkly twinkle.
    //
    // Each visible star can produce a glow (m_StarGlowTexture) and a core
    // (m_StarTexture). Emitting them inline alternates textures per star and
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

    // Emit pass 1: glows for qualifying stars - single m_StarGlowTexture binding.
    for (const auto& v : m_VisibleStarsScratch)
    {
        if (v.glowSize <= 0.0f)
        {
            continue;
        }
        renderer.DrawSpriteAlpha(m_StarGlowTexture,
                                 v.screenPos - glm::vec2(v.glowSize * 0.5f),
                                 glm::vec2(v.glowSize),
                                 0.0f,
                                 glm::vec4(v.color, v.glowAlpha),
                                 true);
    }

    // Emit pass 2: cores for every visible star - single m_StarTexture binding.
    for (const auto& v : m_VisibleStarsScratch)
    {
        renderer.DrawSpriteAlpha(m_StarTexture,
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

        renderer.DrawSpriteAlpha(m_RayTexture,
                                 glowPos,
                                 glm::vec2(glowWidth, glowLength),
                                 rayAngleDeg,
                                 glm::vec4(rayColor, alpha * 0.4f),
                                 true);

        // Main ray
        renderer.DrawSpriteAlpha(m_RayTexture,
                                 rayPos,
                                 glm::vec2(rayWidth, rayLength),
                                 rayAngleDeg,
                                 glm::vec4(rayColor, alpha),
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

        renderer.DrawSpriteAlpha(m_RayTexture,
                                 glowPos,
                                 glm::vec2(glowWidth, glowLength),
                                 rayAngleDeg,
                                 glm::vec4(moonColor, alpha * 0.5f),
                                 true);

        // Main beam
        renderer.DrawSpriteAlpha(m_RayTexture,
                                 rayPos,
                                 glm::vec2(rayWidth, rayLength),
                                 rayAngleDeg,
                                 glm::vec4(moonColor, alpha),
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

        renderer.DrawSpriteAlpha(m_ShootingStarTexture,
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
        renderer.DrawColoredRect(glm::vec2(0, 0),
                                 glm::vec2(static_cast<float>(screenWidth), screenHeight * 0.04f),
                                 glm::vec4(0.15f, 0.3f, 0.25f, auroraAlpha),
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
    renderer.DrawSpriteAlpha(m_GlowTexture,
                             glm::vec2(sw * 0.5f - glowSize * 0.5f, sh - glowSize * 0.3f),
                             glm::vec2(glowSize, glowSize),
                             0.0f,
                             glm::vec4(1.0f, 0.6f, 0.4f, dawnIntensity * 0.15f),
                             true);

    // Secondary softer glow higher up
    renderer.DrawSpriteAlpha(m_GlowTexture,
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
    renderer.DrawSpriteAlpha(m_GlowTexture,
                             glm::vec2(sw * 0.5f - glowSize * 0.5f, -glowSize * 0.6f),
                             glm::vec2(glowSize, glowSize),
                             0.0f,
                             glm::vec4(0.6f, 0.4f, 0.7f, dawnIntensity * 0.1f),
                             true);

    // Overall soft pink tint across screen
    renderer.DrawSpriteAlpha(m_GlowTexture,
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

        renderer.DrawSpriteAlpha(m_StarTexture,
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

        renderer.DrawSpriteAlpha(m_GlowTexture,
                                 screenPos,
                                 glm::vec2(blobSize),
                                 0.0f,
                                 glm::vec4(0.0f, 0.0f, 0.0f, baseAlpha),
                                 false);
    }
}
