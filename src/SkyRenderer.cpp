#include "SkyRenderer.h"
#include "TimeManager.h"

#include <cmath>
#include <random>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SkyRenderer::SkyRenderer()
    : m_Time(0.0f)
    , m_ShootingStarTimer(0.0f)
    , m_LastScreenWidth(0.0f)
    , m_LastScreenHeight(0.0f)
    , m_Initialized(false)
{
}

SkyRenderer::~SkyRenderer()
{
}

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

            // Multi-layered glow falloff for realistic light bloom
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

    m_Initialized = true;
}

void SkyRenderer::UploadTextures(IRenderer &renderer)
{
    if (!m_Initialized)
        return;

    renderer.UploadTexture(m_RayTexture);
    renderer.UploadTexture(m_StarTexture);
    renderer.UploadTexture(m_StarGlowTexture);
    renderer.UploadTexture(m_ShootingStarTexture);
    renderer.UploadTexture(m_GlowTexture);
}

void SkyRenderer::Update(float deltaTime, const TimeManager &time)
{
    m_Time += deltaTime;

    // Update shooting stars during night
    if (time.GetStarVisibility() > 0.3f && time.GetWeather() == WeatherState::Clear)
    {
        UpdateShootingStars(deltaTime, static_cast<int>(m_LastScreenWidth), static_cast<int>(m_LastScreenHeight));
    }
}

void SkyRenderer::Render(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    if (!m_Initialized)
        return;

    // Store screen size for shooting star spawning
    m_LastScreenWidth = static_cast<float>(screenWidth);
    m_LastScreenHeight = static_cast<float>(screenHeight);

    // Disable ambient color for sky rendering
    renderer.SetAmbientColor(glm::vec3(1.0f));

    // Dawn/morning gradient effects (rendered first as background)
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

    // Render stars (background, only at night - fades during dawn)
    if (starVisibility > 0.01f)
    {
        RenderStars(renderer, time, screenWidth, screenHeight);
        RenderShootingStars(renderer, time, screenWidth, screenHeight);
    }

    // Dew sparkles during early morning
    float sunArc = time.GetSunArc();
    if (sunArc >= 0.0f && sunArc < 0.25f)
    {
        RenderDewSparkles(renderer, time, screenWidth, screenHeight);
    }

    // Sun rays with golden hour coloring handled in RenderSunRays
    if (sunArc >= 0.0f)
    {
        RenderSunRays(renderer, time, screenWidth, screenHeight);
    }

    // Moon rays during night
    float moonArc = time.GetMoonArc();
    if (moonArc >= 0.0f && starVisibility > 0.3f)
    {
        RenderMoonRays(renderer, time, screenWidth, screenHeight);
    }
}

void SkyRenderer::GenerateRayTexture()
{
    // Create a VERTICAL ray texture - soft, wide gradient
    std::vector<unsigned char> pixels(RAY_TEXTURE_WIDTH * RAY_TEXTURE_HEIGHT * 4);
    float centerX = RAY_TEXTURE_WIDTH / 2.0f;

    for (int y = 0; y < RAY_TEXTURE_HEIGHT; y++)
    {
        for (int x = 0; x < RAY_TEXTURE_WIDTH; x++)
        {
            int idx = (y * RAY_TEXTURE_WIDTH + x) * 4;

            // Progress along ray (0 = top/start, 1 = bottom/end)
            float progress = static_cast<float>(y) / RAY_TEXTURE_HEIGHT;

            // Distance from center (0 = center, 1 = edge)
            float distFromCenter = std::abs(x - centerX) / centerX;

            // Vertical fade: bright at top, fading toward bottom
            // Smooth curve that starts bright and gradually fades
            float verticalFade = 1.0f - progress;
            verticalFade = std::pow(verticalFade, 0.4f); // Gentle fade curve

            // Horizontal fade: very soft gaussian for diffuse ray edges
            float horizontalFade = std::exp(-distFromCenter * distFromCenter * 3.0f);

            // Combine with smooth multiplication
            float alpha = verticalFade * horizontalFade;

            // Additional softening at the very bottom
            if (progress > 0.7f)
            {
                float bottomFade = 1.0f - (progress - 0.7f) / 0.3f;
                alpha *= bottomFade * bottomFade;
            }

            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(std::max(0.0f, std::min(alpha * 255.0f, 255.0f)));
        }
    }

    m_RayTexture.LoadFromData(pixels.data(), RAY_TEXTURE_WIDTH, RAY_TEXTURE_HEIGHT, 4, false);
}

void SkyRenderer::GenerateStarTexture()
{
    std::vector<unsigned char> pixels(STAR_TEXTURE_SIZE * STAR_TEXTURE_SIZE * 4);
    float center = STAR_TEXTURE_SIZE / 2.0f;

    for (int y = 0; y < STAR_TEXTURE_SIZE; y++)
    {
        for (int x = 0; x < STAR_TEXTURE_SIZE; x++)
        {
            float dx = x - center;
            float dy = y - center;
            float distance = std::sqrt(dx * dx + dy * dy);
            float maxDist = STAR_TEXTURE_SIZE / 2.0f;
            float normalizedDist = distance / maxDist;

            int idx = (y * STAR_TEXTURE_SIZE + x) * 4;

            // Ultra-bright core with sharp falloff
            float core = std::exp(-normalizedDist * normalizedDist * 50.0f);

            // Inner glow layer
            float inner = std::exp(-normalizedDist * normalizedDist * 12.0f) * 0.7f;

            // Soft outer halo
            float outer = std::exp(-normalizedDist * 3.0f) * 0.25f;

            // 6-point star diffraction spikes (more realistic)
            float angle = std::atan2(dy, dx);
            float spike6 = std::pow(std::abs(std::cos(angle * 3.0f)), 12.0f);
            float spikeIntensity = spike6 * std::exp(-normalizedDist * 0.8f) * 0.5f;

            // Secondary 4-point spikes (subtle, rotated 45 degrees)
            float spike4 = std::pow(std::abs(std::cos(angle * 2.0f + 0.785f)), 16.0f);
            float spike4Intensity = spike4 * std::exp(-normalizedDist * 1.2f) * 0.2f;

            float intensity = core + inner + outer + spikeIntensity + spike4Intensity;
            intensity = std::min(1.0f, intensity);

            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(std::max(0.0f, std::min(intensity * 255.0f, 255.0f)));
        }
    }

    m_StarTexture.LoadFromData(pixels.data(), STAR_TEXTURE_SIZE, STAR_TEXTURE_SIZE, 4, false);
}

void SkyRenderer::GenerateStarGlowTexture()
{
    std::vector<unsigned char> pixels(STAR_GLOW_TEXTURE_SIZE * STAR_GLOW_TEXTURE_SIZE * 4);
    float center = STAR_GLOW_TEXTURE_SIZE / 2.0f;

    for (int y = 0; y < STAR_GLOW_TEXTURE_SIZE; y++)
    {
        for (int x = 0; x < STAR_GLOW_TEXTURE_SIZE; x++)
        {
            float dx = x - center;
            float dy = y - center;
            float distance = std::sqrt(dx * dx + dy * dy);
            float normalizedDist = distance / center;

            int idx = (y * STAR_GLOW_TEXTURE_SIZE + x) * 4;

            // Soft gaussian glow for bright star halos
            float glow = std::exp(-normalizedDist * normalizedDist * 2.5f);

            // Additional soft outer ring
            float ring = std::exp(-normalizedDist * 1.5f) * 0.3f;

            float intensity = glow + ring;
            intensity = std::min(1.0f, intensity);

            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(intensity * 255);
        }
    }

    m_StarGlowTexture.LoadFromData(pixels.data(), STAR_GLOW_TEXTURE_SIZE, STAR_GLOW_TEXTURE_SIZE, 4, false);
}

void SkyRenderer::GenerateShootingStarTexture()
{
    // Create a horizontal streak texture for shooting stars
    const int width = 128;
    const int height = 16;
    std::vector<unsigned char> pixels(width * height * 4);

    float centerY = height / 2.0f;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = (y * width + x) * 4;

            float progress = static_cast<float>(x) / width; // 0 at left (head), 1 at right (tail)
            float distFromCenter = std::abs(y - centerY) / centerY;

            // Bright head fading to dim tail
            float lengthFade = std::exp(-progress * 2.5f);

            // Thin streak
            float widthFade = std::exp(-distFromCenter * distFromCenter * 8.0f);

            float intensity = lengthFade * widthFade;

            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(std::min(intensity * 255.0f, 255.0f));
        }
    }

    m_ShootingStarTexture.LoadFromData(pixels.data(), width, height, 4, false);
}

void SkyRenderer::GenerateLightRays()
{
    m_SunRays.clear();
    m_MoonRays.clear();

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * M_PI));

    // Sun rays - spread across ~2/3 of screen with varying origins
    for (int i = 0; i < SUN_RAY_COUNT; i++)
    {
        LightRay ray;
        // Fan position determines angle within the spread
        float basePos = (i + 0.5f) / SUN_RAY_COUNT;
        float offset = (posDist(rng) - 0.5f) * 0.08f;
        ray.xPosition = std::max(0.05f, std::min(0.95f, basePos + offset));

        // Origin offset - rays originate from different points along the "sun band"
        // Distribute across the band with some randomness
        float baseOrigin = (static_cast<float>(i) / (SUN_RAY_COUNT - 1)) * 2.0f - 1.0f; // -1 to 1
        ray.originOffset = baseOrigin + (posDist(rng) - 0.5f) * 0.3f;
        ray.originOffset = std::max(-1.0f, std::min(1.0f, ray.originOffset));

        // Gentle angles - mostly straight down with slight variation
        ray.angle = (posDist(rng) - 0.5f) * 0.3f;
        ray.length = 0.45f + posDist(rng) * 0.45f;  // 45-90% of screen
        ray.width = 0.7f + posDist(rng) * 0.5f;     // Width variation
        ray.brightness = 0.5f + posDist(rng) * 0.5f;
        ray.phase = phaseDist(rng);
        m_SunRays.push_back(ray);
    }

    // Moon beams - very subtle beams
    for (int i = 0; i < MOON_RAY_COUNT; i++)
    {
        LightRay ray;
        float basePos = (i + 0.5f) / MOON_RAY_COUNT;
        float offset = (posDist(rng) - 0.5f) * 0.15f;
        ray.xPosition = std::max(0.1f, std::min(0.9f, basePos + offset));

        ray.originOffset = (posDist(rng) - 0.5f) * 0.5f; // Slight origin variation
        ray.angle = (posDist(rng) - 0.5f) * 0.3f;
        ray.length = 0.3f + posDist(rng) * 0.4f;
        ray.width = 0.7f + posDist(rng) * 0.3f;
        ray.brightness = 0.4f + posDist(rng) * 0.4f;
        ray.phase = phaseDist(rng);
        m_MoonRays.push_back(ray);
    }
}

void SkyRenderer::GenerateStars(int count)
{
    m_Stars.clear();
    m_Stars.reserve(count);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.0f, 1.0f);
    std::uniform_real_distribution<float> brightDist(0.1f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * M_PI));
    std::uniform_real_distribution<float> speedDist(1.0f, 4.0f);
    std::uniform_real_distribution<float> sizeDist(0.2f, 0.9f);
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

    for (int i = 0; i < count; i++)
    {
        Star star;

        // Fully random position across entire 0-1 range for both axes
        star.position = glm::vec2(posDistX(rng), posDistY(rng));

        // Varied brightness
        float rawBright = brightDist(rng);
        star.baseBrightness = rawBright * rawBright;

        // Rare bright stars (3%)
        if (posDistX(rng) < 0.03f)
            star.baseBrightness = 0.8f + posDistX(rng) * 0.2f;

        star.twinklePhase = phaseDist(rng);
        star.twinkleSpeed = speedDist(rng);
        star.size = sizeDist(rng);

        // Star colors
        float colorVar = colorDist(rng);
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

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.0f, 1.0f);
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * M_PI));
    std::uniform_real_distribution<float> speedDist(1.5f, 5.0f);
    std::uniform_real_distribution<float> sizeDist(0.08f, 0.25f);
    std::uniform_real_distribution<float> brightDist(0.04f, 0.2f);

    for (int i = 0; i < count; i++)
    {
        Star star;

        // Fully random position across entire viewport
        star.position = glm::vec2(posDistX(rng), posDistY(rng));

        star.baseBrightness = brightDist(rng);
        star.twinklePhase = phaseDist(rng);
        star.twinkleSpeed = speedDist(rng);
        star.size = sizeDist(rng);
        star.color = glm::vec3(1.0f, 1.0f, 1.0f);

        m_BackgroundStars.push_back(star);
    }
}

glm::vec2 SkyRenderer::GetLightSourcePosition(float arc, int screenWidth, int screenHeight) const
{
    float x = screenWidth * (1.0f - arc);
    // Position at top of screen, arcing slightly
    float arcHeight = 1.0f - std::pow(2.0f * arc - 1.0f, 2.0f);
    float y = 20.0f - arcHeight * 40.0f; // Near top of viewport
    return glm::vec2(x, y);
}

void SkyRenderer::RenderStars(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.01f)
        return;

    if (time.GetWeather() == WeatherState::Overcast)
        visibility *= 0.05f;

    // Reduce overall star intensity - dimmer stars
    visibility *= 0.35f;

    // Stars appear gradually - brightest first, then dimmer ones fade in
    // visibility goes 0->1 as night falls, use it to threshold which stars appear
    float appearThreshold = 1.0f - visibility * 2.0f; // At full night, threshold is -1 (all visible)

    // First pass: Background stars - only show some, gradually
    int bgCount = 0;
    int maxBgStars = static_cast<int>(m_BackgroundStars.size() * visibility * 0.4f);

    for (const auto &star : m_BackgroundStars)
    {
        if (bgCount >= maxBgStars)
            break;

        // Stars with higher brightness appear first
        if (star.baseBrightness < appearThreshold)
            continue;

        float twinkle = 0.6f + 0.4f * std::sin(m_Time * star.twinkleSpeed * 1.5f + star.twinklePhase);
        float brightness = star.baseBrightness * twinkle * visibility * 0.3f;

        if (brightness < 0.01f)
            continue;

        glm::vec2 screenPos(
            star.position.x * screenWidth,
            star.position.y * screenHeight);

        float size = 1.0f + star.size * 1.2f;

        renderer.DrawSpriteAlpha(
            m_StarTexture,
            screenPos - glm::vec2(size * 0.5f),
            glm::vec2(size),
            0.0f,
            glm::vec4(star.color, brightness),
            true);
        bgCount++;
    }

    // Second pass: Main stars - gradual appearance, sparkly twinkle
    int starCount = 0;
    int maxStars = static_cast<int>(m_Stars.size() * visibility * 0.6f);

    for (const auto &star : m_Stars)
    {
        if (starCount >= maxStars)
            break;

        // Brighter stars appear earlier in the evening
        if (star.baseBrightness < appearThreshold * 0.8f)
            continue;

        // Sparkly twinkle - more variation, sharper peaks
        float twinkle1 = std::sin(m_Time * star.twinkleSpeed * 1.2f + star.twinklePhase);
        float twinkle2 = std::sin(m_Time * star.twinkleSpeed * 2.7f + star.twinklePhase * 1.3f);
        float twinkle3 = std::sin(m_Time * star.twinkleSpeed * 0.5f + star.twinklePhase * 2.1f);

        // Sharp sparkle effect - occasionally flare brighter
        float sparkle = std::max(0.0f, twinkle1 * twinkle2);
        float twinkle = 0.4f + 0.35f * twinkle1 + 0.15f * twinkle3 + 0.25f * sparkle;

        float brightness = star.baseBrightness * twinkle * visibility;

        if (brightness < 0.01f)
            continue;

        glm::vec2 screenPos(
            star.position.x * screenWidth,
            star.position.y * screenHeight);

        // Subtle glow on bright sparkle moments
        if (brightness > 0.25f && sparkle > 0.3f)
        {
            float glowSize = (6.0f + star.size * 8.0f);
            float glowAlpha = (brightness - 0.25f) * 0.1f;

            renderer.DrawSpriteAlpha(
                m_StarGlowTexture,
                screenPos - glm::vec2(glowSize * 0.5f),
                glm::vec2(glowSize),
                0.0f,
                glm::vec4(star.color, glowAlpha),
                true);
        }

        // Main star - small
        float size = (1.5f + star.size * 3.0f) * (0.5f + brightness * 0.5f);

        renderer.DrawSpriteAlpha(
            m_StarTexture,
            screenPos - glm::vec2(size * 0.5f),
            glm::vec2(size),
            0.0f,
            glm::vec4(star.color, brightness * 0.7f),
            true);
        starCount++;
    }
}

void SkyRenderer::RenderSunRays(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float sunArc = time.GetSunArc();
    if (sunArc < 0.0f)
        return;

    glm::vec3 sunColor = time.GetSunColor();

    // Get the sun's actual position on screen
    glm::vec2 sunPos = GetLightSourcePosition(sunArc, screenWidth, screenHeight);

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
    for (const auto &ray : m_SunRays)
    {
        // Staggered cycles - each ray on its own timeline
        float rayStartDelay = rayIndex * 4.0f;
        float rayTime = m_Time - rayStartDelay;

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
            fadeAlpha = fadeAlpha * fadeAlpha; // Ease in
        }
        else if (cycle < 0.70f)
        {
            fadeAlpha = 1.0f;
        }
        else
        {
            float fadeProgress = (cycle - 0.70f) / 0.30f;
            fadeAlpha = 1.0f - fadeProgress * fadeProgress; // Ease out
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
        float rayAngleRad = rayAngleDeg * static_cast<float>(M_PI) / 180.0f;

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

        renderer.DrawSpriteAlpha(
            m_RayTexture,
            glowPos,
            glm::vec2(glowWidth, glowLength),
            rayAngleDeg,
            glm::vec4(rayColor, alpha * 0.4f),
            true);

        // Main ray
        renderer.DrawSpriteAlpha(
            m_RayTexture,
            rayPos,
            glm::vec2(rayWidth, rayLength),
            rayAngleDeg,
            glm::vec4(rayColor, alpha),
            true);

        rayIndex++;
    }
}

void SkyRenderer::RenderMoonRays(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float moonArc = time.GetMoonArc();
    if (moonArc < 0.0f)
        return;

    // Get the moon's actual position on screen
    glm::vec2 moonPos = GetLightSourcePosition(moonArc, screenWidth, screenHeight);

    // Soft blue-white moonlight color
    glm::vec3 moonColor(0.75f, 0.85f, 1.0f);

    // Moon phase affects intensity
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
    for (const auto &ray : m_MoonRays)
    {
        // Staggered cycles
        float rayStartDelay = rayIndex * 6.0f;
        float rayTime = m_Time - rayStartDelay;

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
        float rayAngleRad = rayAngleDeg * static_cast<float>(M_PI) / 180.0f;

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

        renderer.DrawSpriteAlpha(
            m_RayTexture,
            glowPos,
            glm::vec2(glowWidth, glowLength),
            rayAngleDeg,
            glm::vec4(moonColor, alpha * 0.5f),
            true);

        // Main beam
        renderer.DrawSpriteAlpha(
            m_RayTexture,
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

    // Spawn new shooting stars occasionally
    m_ShootingStarTimer += deltaTime;
    float spawnInterval = 4.0f + std::sin(m_Time * 0.1f) * 2.0f; // 2-6 seconds between spawns

    if (m_ShootingStarTimer >= spawnInterval && m_ShootingStars.size() < 2)
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

    // Random starting position
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> posDist(0.0f, 1.0f);

    if (posDist(rng) < 0.6f)
    {
        // Start from top
        star.position.x = posDist(rng) * screenWidth;
        star.position.y = -10.0f;
    }
    else
    {
        // Start from right side
        star.position.x = screenWidth + 10.0f;
        star.position.y = posDist(rng) * screenHeight * 0.4f;
    }

    // Diagonal downward velocity
    float speed = 350.0f + posDist(rng) * 250.0f;
    float angle = 0.4f + posDist(rng) * 0.7f;
    star.velocity.x = -std::cos(angle) * speed;
    star.velocity.y = std::sin(angle) * speed;

    star.lifetime = 0.3f + posDist(rng) * 0.35f;
    star.maxLifetime = star.lifetime;
    star.brightness = 0.3f + posDist(rng) * 0.35f;
    star.length = 50.0f + posDist(rng) * 50.0f;

    m_ShootingStars.push_back(star);
}

void SkyRenderer::RenderShootingStars(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.3f)
        return;

    for (const auto &star : m_ShootingStars)
    {
        float fadeIn = std::min(1.0f, (star.maxLifetime - star.lifetime) / 0.08f);
        float fadeOut = std::min(1.0f, star.lifetime / 0.12f);
        float alpha = star.brightness * fadeIn * fadeOut * visibility;

        if (alpha < 0.01f)
            continue;

        float angle = std::atan2(star.velocity.y, star.velocity.x) * 180.0f / static_cast<float>(M_PI);
        glm::vec2 size(star.length, 3.0f);

        renderer.DrawSpriteAlpha(
            m_ShootingStarTexture,
            star.position - glm::vec2(0, 1.5f),
            size,
            angle,
            glm::vec4(1.0f, 1.0f, 1.0f, alpha),
            true);
    }
}

void SkyRenderer::RenderAtmosphericGlow(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float visibility = time.GetStarVisibility();
    if (visibility < 0.2f)
        return;

    // Subtle blue atmospheric glow at horizon during night
    float horizonGlowAlpha = visibility * 0.025f;

    // Bottom horizon glow
    float glowHeight = screenHeight * 0.12f;
    renderer.DrawColoredRect(
        glm::vec2(0, screenHeight - glowHeight),
        glm::vec2(static_cast<float>(screenWidth), glowHeight),
        glm::vec4(0.08f, 0.12f, 0.25f, horizonGlowAlpha),
        true);

    // Occasional subtle shimmer at top
    float shimmer = std::sin(m_Time * 0.25f) * 0.5f + 0.5f;
    float auroraAlpha = visibility * 0.01f * shimmer;

    if (auroraAlpha > 0.003f)
    {
        renderer.DrawColoredRect(
            glm::vec2(0, 0),
            glm::vec2(static_cast<float>(screenWidth), screenHeight * 0.04f),
            glm::vec4(0.15f, 0.3f, 0.25f, auroraAlpha),
            true);
    }
}

void SkyRenderer::GenerateDewSparkles()
{
    m_DewSparkles.clear();
    m_DewSparkles.reserve(DEW_SPARKLE_COUNT);

    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> posDistX(0.0f, 1.0f);
    std::uniform_real_distribution<float> posDistY(0.55f, 1.0f); // Lower screen band
    std::uniform_real_distribution<float> phaseDist(0.0f, static_cast<float>(2.0 * M_PI));
    std::uniform_real_distribution<float> brightDist(0.4f, 1.0f);
    std::uniform_real_distribution<float> speedDist(1.5f, 5.0f);

    for (int i = 0; i < DEW_SPARKLE_COUNT; i++)
    {
        DewSparkle sparkle;
        sparkle.position = glm::vec2(posDistX(rng), posDistY(rng));
        sparkle.phase = phaseDist(rng);
        sparkle.brightness = brightDist(rng);
        sparkle.speed = speedDist(rng);
        m_DewSparkles.push_back(sparkle);
    }
}

void SkyRenderer::RenderDawnHorizonGlow(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float dawnIntensity = time.GetDawnIntensity();
    if (dawnIntensity < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // Soft warm wash over entire screen using stretched glow texture
    float glowSize = std::max(sw, sh) * 2.5f;

    // Large soft glow from bottom center (sunrise direction)
    renderer.DrawSpriteAlpha(
        m_GlowTexture,
        glm::vec2(sw * 0.5f - glowSize * 0.5f, sh - glowSize * 0.3f),
        glm::vec2(glowSize, glowSize),
        0.0f,
        glm::vec4(1.0f, 0.6f, 0.4f, dawnIntensity * 0.15f),
        true);

    // Secondary softer glow higher up
    renderer.DrawSpriteAlpha(
        m_GlowTexture,
        glm::vec2(sw * 0.5f - glowSize * 0.5f, sh * 0.3f - glowSize * 0.5f),
        glm::vec2(glowSize, glowSize),
        0.0f,
        glm::vec4(1.0f, 0.7f, 0.55f, dawnIntensity * 0.08f),
        true);
}

void SkyRenderer::RenderDawnGradient(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float dawnIntensity = time.GetDawnIntensity();
    if (dawnIntensity < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    // Soft purple/pink wash from top using stretched glow texture
    float glowSize = std::max(sw, sh) * 2.0f;

    // Large soft glow from top (pre-dawn sky color)
    renderer.DrawSpriteAlpha(
        m_GlowTexture,
        glm::vec2(sw * 0.5f - glowSize * 0.5f, -glowSize * 0.6f),
        glm::vec2(glowSize, glowSize),
        0.0f,
        glm::vec4(0.6f, 0.4f, 0.7f, dawnIntensity * 0.1f),
        true);

    // Overall soft pink tint across screen
    renderer.DrawSpriteAlpha(
        m_GlowTexture,
        glm::vec2(sw * 0.5f - glowSize * 0.5f, sh * 0.5f - glowSize * 0.5f),
        glm::vec2(glowSize, glowSize),
        0.0f,
        glm::vec4(1.0f, 0.65f, 0.6f, dawnIntensity * 0.06f),
        true);
}

void SkyRenderer::RenderDewSparkles(IRenderer &renderer, const TimeManager &time, int screenWidth, int screenHeight)
{
    float sunArc = time.GetSunArc();

    // Only visible during early morning (sunrise to mid-morning)
    if (sunArc < 0.0f || sunArc > 0.25f)
        return;

    // Fade in at sunrise, peak around sunArc 0.1, fade out by 0.25
    float visibility;
    if (sunArc < 0.1f)
        visibility = sunArc / 0.1f; // Fade in
    else
        visibility = 1.0f - (sunArc - 0.1f) / 0.15f; // Fade out

    visibility = std::max(0.0f, std::min(1.0f, visibility));
    if (visibility < 0.01f)
        return;

    float sw = static_cast<float>(screenWidth);
    float sh = static_cast<float>(screenHeight);

    for (const auto &sparkle : m_DewSparkles)
    {
        // Sharp twinkle - brief bright flashes
        float twinkle = std::sin(m_Time * sparkle.speed + sparkle.phase);
        twinkle = std::max(0.0f, twinkle - 0.5f) / 0.5f; // Only top 50% of sine wave
        twinkle = twinkle * twinkle;                     // Sharp peaks

        float brightness = sparkle.brightness * twinkle * visibility * 0.8f;
        if (brightness < 0.08f)
            continue;

        glm::vec2 screenPos(sparkle.position.x * sw, sparkle.position.y * sh);

        // Small bright point with warm golden color
        float size = 2.0f + brightness * 3.0f;

        renderer.DrawSpriteAlpha(
            m_StarTexture,
            screenPos - glm::vec2(size * 0.5f),
            glm::vec2(size),
            0.0f,
            glm::vec4(1.0f, 0.92f, 0.65f, brightness),
            true);
    }
}
