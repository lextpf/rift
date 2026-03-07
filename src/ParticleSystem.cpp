#include "ParticleSystem.h"
#include "Tilemap.h"

#include "MathConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

ParticleSystem::ParticleSystem()
    : m_Zones(nullptr)                             // Particle zones from tilemap
    , m_Tilemap(nullptr)                           // Reference to tilemap for structure queries
    // TODO: Retrieve tile size from tilemap
    , m_TileWidth(32)                              // Tile dimensions for coordinate conversion
    , m_TileHeight(32), m_MaxParticlesPerZone(25)  // Particle density cap per zone
    , m_Time(0.0f)                                 // Accumulated time for animation cycles
    , m_NightFactor(0.0f)                          // Day & night blend (0 = day, 1 = night)
    , m_Rng(std::random_device{}())                // Seeded Mersenne Twister RNG
    , m_Dist01(0.0f, 1.0f)                         // Uniform distribution for random values
    , m_TexturesLoaded(false)                      // Lazy-load flag for particle sprites
{
    m_Particles.reserve(500);  // Pre-allocate to reduce reallocations
}

bool ParticleSystem::LoadTextures()
{
    BuildAtlas();
    m_TexturesLoaded = true;
    return true;
}

void ParticleSystem::UploadTextures(IRenderer &renderer)
{
    if (!m_TexturesLoaded)
        return;

    renderer.UploadTexture(m_AtlasTexture);
}

void ParticleSystem::BuildAtlas()
{
    // Particle texture sources: 6 files + 2 procedural
    // We'll pack them in a 512x512 atlas with a simple row layout

    struct TextureSource
    {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
    };

    TextureSource sources[8];
    const char *filePaths[6] = {
        "assets/particles/304502d7-426b-4abc-a608-ff01a185df96.png", // Firefly
        "assets/particles/9509e404-2fce-4fbf-a082-720f85e7244e.png", // Rain
        "assets/particles/6f9d2bcf-8e79-493f-b468-85040a945d06.png", // Snow
        "assets/particles/14b6ffec-3289-417b-b99c-82d1ed2a9944.png", // Fog
        "assets/particles/536fa219-58a1-4220-9171-a8520d126f44.png", // Sparkles
        "assets/particles/ead11602-6c24-45dc-b657-03d637e2a543.png"  // Wisp
    };

    // Load file-based textures temporarily to get their pixel data
    for (int i = 0; i < 6; i++)
    {
        Texture temp;
        if (temp.LoadFromFile(filePaths[i]))
        {
            sources[i].width = temp.GetWidth();
            sources[i].height = temp.GetHeight();
            size_t dataSize = temp.GetWidth() * temp.GetHeight() * temp.GetChannels();
            sources[i].pixels.resize(dataSize);
            if (!temp.GetImageData().empty())
            {
                memcpy(sources[i].pixels.data(), temp.GetImageData().data(), dataSize);
                // Convert to RGBA if needed
                if (temp.GetChannels() == 3)
                {
                    std::vector<unsigned char> rgba(temp.GetWidth() * temp.GetHeight() * 4);
                    for (int j = 0; j < temp.GetWidth() * temp.GetHeight(); j++)
                    {
                        rgba[j * 4 + 0] = sources[i].pixels[j * 3 + 0];
                        rgba[j * 4 + 1] = sources[i].pixels[j * 3 + 1];
                        rgba[j * 4 + 2] = sources[i].pixels[j * 3 + 2];
                        rgba[j * 4 + 3] = 255;
                    }
                    sources[i].pixels = std::move(rgba);
                }
            }
        }
        else
        {
            // Fallback: 16x16 white texture
            sources[i].width = 16;
            sources[i].height = 16;
            sources[i].pixels.resize(16 * 16 * 4, 255);
        }
    }

    // Generate procedural textures
    GenerateLanternPixels(sources[6].pixels, sources[6].width, sources[6].height);
    GenerateSunshinePixels(sources[7].pixels, sources[7].width, sources[7].height);

    // Calculate atlas layout - simple horizontal packing with rows
    // Atlas size: 512x512 should be plenty
    const int atlasWidth = 512;
    const int atlasHeight = 512;
    std::vector<unsigned char> atlasPixels(atlasWidth * atlasHeight * 4, 0);

    int currentX = 0;
    int currentY = 0;
    int rowHeight = 0;

    for (int i = 0; i < 8; i++)
    {
        int w = sources[i].width;
        int h = sources[i].height;

        // Move to next row if needed
        if (currentX + w > atlasWidth)
        {
            currentX = 0;
            currentY += rowHeight + 1; // 1px padding
            rowHeight = 0;
        }

        // Store UV coordinates (normalized)
        m_AtlasRegions[i].uvMin = glm::vec2(
            static_cast<float>(currentX) / atlasWidth,
            static_cast<float>(currentY) / atlasHeight);
        m_AtlasRegions[i].uvMax = glm::vec2(
            static_cast<float>(currentX + w) / atlasWidth,
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

        currentX += w + 1; // 1px padding
        rowHeight = std::max(rowHeight, h);
    }

    // Create the atlas texture
    m_AtlasTexture.LoadFromData(atlasPixels.data(), atlasWidth, atlasHeight, 4, false);

    std::cout << "Particle atlas built: " << atlasWidth << "x" << atlasHeight << std::endl;
}

void ParticleSystem::GenerateLanternPixels(std::vector<unsigned char> &pixels, int &width, int &height)
{
    width = 256;
    height = 256;
    pixels.resize(width * height * 4);
    float center = width / 2.0f;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = (y * width + x) * 4;

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

            pixels[idx + 0] = 255;
            pixels[idx + 1] = static_cast<unsigned char>(220 + alpha * 35);
            pixels[idx + 2] = static_cast<unsigned char>(160 + alpha * 50);
            pixels[idx + 3] = static_cast<unsigned char>(alpha * 120);
        }
    }
}

void ParticleSystem::GenerateSunshinePixels(std::vector<unsigned char> &pixels, int &width, int &height)
{
    width = 48;
    height = 192;
    pixels.resize(width * height * 4);
    float centerX = width / 2.0f;

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int idx = (y * width + x) * 4;

            float dx = std::abs(x - centerX) / centerX;
            float dy = static_cast<float>(y) / static_cast<float>(height);

            float beamWidth = 0.2f + dy * 0.55f;
            float horizontalFalloff = 1.0f - std::min(1.0f, dx / beamWidth);
            horizontalFalloff = std::pow(horizontalFalloff, 1.2f);
            horizontalFalloff *= std::exp(-dx * dx * 1.5f);

            float topFeather = std::min(1.0f, dy / 0.30f);
            topFeather = std::pow(topFeather, 2.0f);
            float bottomFeather = std::min(1.0f, (1.0f - dy) / 0.30f);
            bottomFeather = std::pow(bottomFeather, 2.0f);

            float verticalIntensity = 0.5f + 0.5f * std::sin(dy * rift::PiF);
            float beamAlpha = horizontalFalloff * verticalIntensity * topFeather * bottomFeather;

            float groundGlowY = 1.0f - std::abs(dy - 0.78f) / 0.15f;
            groundGlowY = std::max(0.0f, groundGlowY);
            float groundGlowX = std::exp(-dx * dx * 1.5f);
            float groundGlow = groundGlowY * groundGlowX * 0.35f * bottomFeather;

            float alpha = std::min(1.0f, beamAlpha + groundGlow);

            pixels[idx + 0] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = static_cast<unsigned char>(alpha * 140);
        }
    }
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

    // Update existing particles
    for (auto it = m_Particles.begin(); it != m_Particles.end();)
    {
        Particle &p = *it;

        // Decrease lifetime
        p.lifetime -= deltaTime;
        if (p.lifetime <= 0.0f)
        {
            it = m_Particles.erase(it);
            continue;
        }

        // Remove particle if its zone no longer exists
        if (!hasZones || p.zoneIndex < 0 || p.zoneIndex >= static_cast<int>(m_Zones->size()))
        {
            it = m_Particles.erase(it);
            continue;
        }

        // Update position
        p.position += p.velocity * deltaTime;

        // Use the particle's stored type for behavior
        ParticleType pType = p.type;

        // Type-specific behavior
        switch (pType)
        {
        case ParticleType::Firefly:
        {
            // Gentle random drift
            float driftX = std::sin(m_Time * 2.0f + p.phase) * 10.0f;
            float driftY = std::cos(m_Time * 1.5f + p.phase * 1.3f) * 8.0f;
            p.position.x += driftX * deltaTime;
            p.position.y += driftY * deltaTime;

            // Slow rotation as they drift
            float rotationSpeed = 20.0f + (p.phase / 6.28f) * 40.0f; // 20-60 degrees per second
            if (std::fmod(p.phase, 2.0f) < 1.0f)
                rotationSpeed = -rotationSpeed;
            p.rotation += rotationSpeed * deltaTime;

            // Pulsing glow, alpha oscillates between 0.2 and 0.8
            float pulse = 0.5f + 0.5f * std::sin(m_Time * 4.0f + p.phase);
            float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.3f));
            float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.5f);
            p.color.a = pulse * lifeFade * fadeIn * 0.8f;
            break;
        }
        case ParticleType::Rain:
        {
            // Fade in smoothly over first 0.15 seconds
            float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 0.15f);
            // Target alpha stored in phase
            p.color.a = fadeIn * p.phase;

            // Check if rain has fallen below its zone
            if (hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(m_Zones->size()))
            {
                const auto &zone = (*m_Zones)[p.zoneIndex];

                // Vary ground height per particle using position.x as seed
                // This creates natural variation so rain doesn't end on same line
                float heightVariation = std::fmod(std::abs(p.position.x * 7.3f + p.phase * 100.0f), 60.0f);
                float groundY = zone.position.y + zone.size.y + 20.0f + heightVariation;
                if (p.position.y > groundY)
                {
                    p.lifetime = 0.0f;
                }
            }
            break;
        }
        case ParticleType::Snow:
        {
            // Snow drifts side to side
            float drift = std::sin(m_Time * 1.5f + p.phase) * 20.0f;
            p.position.x += drift * deltaTime;

            // Rotate as it falls
            float rotationSpeed = 30.0f + (p.phase / 6.28f) * 60.0f; // 30-90 degrees per second
            if (std::fmod(p.phase, 2.0f) < 1.0f)
                rotationSpeed = -rotationSpeed; // Half rotate clockwise, half counter-clockwise
            p.rotation += rotationSpeed * deltaTime;

            // Check if snow has fallen below its zone
            if (hasZones && p.zoneIndex >= 0 && p.zoneIndex < static_cast<int>(m_Zones->size()))
            {
                const auto &zone = (*m_Zones)[p.zoneIndex];
                if (p.position.y > zone.position.y + zone.size.y + 50.0f)
                {
                    p.lifetime = 0.0f;
                }
            }
            break;
        }
        case ParticleType::Fog:
        {
            // Fog drifts very slowly
            float driftX = std::sin(m_Time * 0.15f + p.phase) * 2.5f;
            float driftY = std::cos(m_Time * 0.1f + p.phase * 0.5f) * 1.0f;

            // Add subtle swirling motion for smoky effect
            float swirl = std::sin(m_Time * 0.4f + p.phase * 2.0f) * 1.5f;
            p.position.x += (driftX + swirl) * deltaTime;
            p.position.y += driftY * deltaTime;

            // Slow pulsing alpha
            float pulse = 0.9f + 0.1f * std::sin(m_Time * 0.25f + p.phase);

            // Long fade in and fade out for smooth feathered appearance
            float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.4f));
            float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 4.0f);

            // More visible during day, significantly less at night
            float dayBoost = 1.0f + (1.0f - m_NightFactor) * 0.4f;
            float nightReduce = 1.0f - m_NightFactor * 0.6f;
            p.color.a = pulse * lifeFade * fadeIn * 0.28f * dayBoost * nightReduce;
            break;
        }
        case ParticleType::Sparkles:
        {
            // Instant sparkle, bright flash then fade
            float lifeRatio = 1.0f - (p.lifetime / p.maxLifetime); // 0 at start, 1 at end
            float flash = lifeRatio < 0.15f ? 1.0f : 0.0f;         // Bright only in first 15% of life
            p.color.a = flash;
            break;
        }
        case ParticleType::Wisp:
        {
            // Magical spiraling movement
            float spiralX = std::sin(m_Time * 1.5f + p.phase) * 20.0f;
            float spiralY = std::cos(m_Time * 1.2f + p.phase * 0.7f) * 15.0f;
            float wobble = std::sin(m_Time * 3.0f + p.phase * 2.0f) * 8.0f;
            p.position.x += (spiralX + wobble) * deltaTime;
            p.position.y += spiralY * deltaTime;

            // Gentle rotation
            float rotSpeed = 45.0f + (p.phase / 6.28f) * 30.0f; // 45-75 deg/sec
            if (std::fmod(p.phase, 2.0f) < 1.0f)
                rotSpeed = -rotSpeed;
            p.rotation += rotSpeed * deltaTime;

            // Pulsing glow effect
            float twinkle = 0.5f + 0.5f * std::sin(m_Time * 4.0f + p.phase * 3.0f);
            float shimmer = 0.8f + 0.2f * std::sin(m_Time * 7.0f + p.phase);
            float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.25f));
            float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 1.0f);
            p.color.a = twinkle * shimmer * lifeFade * fadeIn * 0.85f;
            break;
        }
        case ParticleType::Lantern:
        {
            // Stationary glow, only visible at night
            // Completely off during daytime
            if (m_NightFactor < 0.05f)
            {
                p.color.a = 0.0f;
                break;
            }
            float pulse = 0.9f + 0.1f * std::sin(m_Time * 1.5f + p.phase);
            float flicker = 0.97f + 0.03f * std::sin(m_Time * 6.0f + p.phase * 2.0f);

            // Night factor controls visibility
            float nightAlpha = m_NightFactor * 0.35f;
            p.color.a = pulse * flicker * nightAlpha;
            break;
        }
        case ParticleType::Sunshine:
        {
            // Sun & moon rays, yellow during day, blue during night
            // Very gentle shimmer effect
            float shimmer = 0.95f + 0.05f * std::sin(m_Time * 1.2f + p.phase);
            float flicker = 0.97f + 0.03f * std::sin(m_Time * 3.0f + p.phase * 1.5f);

            // Fade in and out very smoothly
            float lifeFade = std::min(1.0f, p.lifetime / (p.maxLifetime * 0.4f));
            float fadeIn = std::min(1.0f, (p.maxLifetime - p.lifetime) / 2.0f);

            // Interpolate color between golden yellow (day) and pale blue (night)
            // Day color: warm golden (1.0, 0.9, 0.5)
            // Night color: cool blue (0.5, 0.7, 1.0)
            float nightBlend = m_NightFactor;
            p.color.r = 1.0f * (1.0f - nightBlend) + 0.5f * nightBlend;
            p.color.g = 0.9f * (1.0f - nightBlend) + 0.7f * nightBlend;
            p.color.b = 0.5f * (1.0f - nightBlend) + 1.0f * nightBlend;

            // Subtle alpha
            float baseAlpha = 0.16f + (1.0f - m_NightFactor) * 0.06f;
            p.color.a = shimmer * flicker * lifeFade * fadeIn * baseAlpha;
            break;
        }
        }

        ++it;
    }

    if (!hasZones)
        return;

    // Spawn new particles for each zone
    for (size_t i = 0; i < m_Zones->size(); ++i)
    {
        const ParticleZone &zone = (*m_Zones)[i];
        if (!zone.enabled)
            continue;

        // Check if zone is visible in current view
        float margin = 80.0f; // 5 tiles of margin to spawn offscreen
        bool visible = !(zone.position.x + zone.size.x < cameraPos.x - margin ||
                         zone.position.x > cameraPos.x + viewSize.x + margin ||
                         zone.position.y + zone.size.y < cameraPos.y - margin ||
                         zone.position.y > cameraPos.y + viewSize.y + margin);

        if (!visible)
            continue;

        // Skip spawning lantern glows during daytime to avoid flicker
        if (zone.type == ParticleType::Lantern && m_NightFactor < 0.05f)
            continue;

        // Count particles for this zone
        size_t zoneParticleCount = 0;
        for (const auto &p : m_Particles)
        {
            if (p.zoneIndex == static_cast<int>(i))
                zoneParticleCount++;
        }

        // Spawn rate depends on zone type
        float spawnRate;
        switch (zone.type)
        {
        case ParticleType::Firefly:
            spawnRate = 3.0f;
            break;
        case ParticleType::Rain:
            spawnRate = 50.0f;
            break;
        case ParticleType::Snow:
            spawnRate = 12.0f;
            break;
        case ParticleType::Fog:
            spawnRate = 3.0f; // Sparse, smoky wisps
            break;
        case ParticleType::Sparkles:
            spawnRate = 18.0f;
            break;
        case ParticleType::Wisp:
            spawnRate = 4.0f; // Magical wisps
            break;
        case ParticleType::Lantern:
            spawnRate = 0.5f; // Very slow, just maintain 1-2 glows
            break;
        case ParticleType::Sunshine:
            spawnRate = 0.8f; // Sparse sun & moon rays
            break;
        default:
            spawnRate = 5.0f;
        }

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

void ParticleSystem::SpawnParticleInZone(int zoneIndex, const ParticleZone &zone)
{
    switch (zone.type)
    {
    case ParticleType::Firefly:
        SpawnFirefly(zoneIndex, zone);
        break;
    case ParticleType::Rain:
        SpawnRain(zoneIndex, zone);
        break;
    case ParticleType::Snow:
        SpawnSnow(zoneIndex, zone);
        break;
    case ParticleType::Fog:
        SpawnFog(zoneIndex, zone);
        break;
    case ParticleType::Sparkles:
        SpawnSparkles(zoneIndex, zone);
        break;
    case ParticleType::Wisp:
        SpawnWisp(zoneIndex, zone);
        break;
    case ParticleType::Lantern:
        SpawnLantern(zoneIndex, zone);
        break;
    case ParticleType::Sunshine:
        SpawnSunshine(zoneIndex, zone);
        break;
    }
}

void ParticleSystem::SpawnFirefly(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Firefly;
    p.noProjection = zone.noProjection;

    // Spawn within zone bounds
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * zone.size.y;

    // Very slow random drift
    p.velocity.x = (m_Dist01(m_Rng) - 0.5f) * 5.0f;
    p.velocity.y = (m_Dist01(m_Rng) - 0.5f) * 5.0f;

    // Yellow-green glow color
    p.color = glm::vec4(1.0f, 0.9f + m_Dist01(m_Rng) * 0.1f, 0.3f + m_Dist01(m_Rng) * 0.2f, 0.0f);

    p.size = 2.0f + m_Dist01(m_Rng) * 2.0f;     // 2-4 pixels
    p.lifetime = 4.0f + m_Dist01(m_Rng) * 5.0f; // Live longer
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = 0.0f;
    p.additive = true;

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnRain(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Rain;
    p.noProjection = zone.noProjection;

    // Spawn at top of zone
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * 10.0f; // Near top of zone

    // Fall straight down
    p.velocity.x = 0.0f;
    p.velocity.y = 150.0f + m_Dist01(m_Rng) * 100.0f; // 150-250 downward

    // Light blue-white color, start transparent, fade in
    float targetAlpha = 0.5f + m_Dist01(m_Rng) * 0.3f;
    p.color = glm::vec4(0.8f, 0.85f, 1.0f, 0.0f); // Start invisible
    p.phase = targetAlpha;                        // Store target alpha in phase field

    p.size = 10.0f + m_Dist01(m_Rng) * 4.0f; // 10-14 pixels
    p.lifetime = 2.0f;
    p.maxLifetime = p.lifetime;
    p.rotation = -35.0f - m_Dist01(m_Rng) * 30.0f; // -35 to -65 degrees
    p.additive = false;

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnSnow(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Snow;
    p.noProjection = zone.noProjection;

    // Spawn at top of zone
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * 10.0f; // Near top of zone

    // Gentle slow fall with light drift
    p.velocity.x = (m_Dist01(m_Rng) - 0.5f) * 12.0f;
    p.velocity.y = 12.0f + m_Dist01(m_Rng) * 10.0f; // 12-22 pixels/sec

    // Bright white color with additive blending for glow
    p.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.35f + m_Dist01(m_Rng) * 0.15f);

    p.size = 1.5f + m_Dist01(m_Rng) * 1.5f; // 1.5-3 pixels
    p.lifetime = 15.0f;
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = 0.0f;
    p.additive = true; // Additive blending for brighter snow

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnFog(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Fog;
    p.noProjection = zone.noProjection;

    // Spawn throughout zone
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * zone.size.y;

    // Very slow drift
    p.velocity.x = (m_Dist01(m_Rng) - 0.5f) * 3.0f;
    p.velocity.y = (m_Dist01(m_Rng) - 0.5f) * 1.5f;

    // White/grey color with slight variation
    float grey = 0.88f + m_Dist01(m_Rng) * 0.12f;
    p.color = glm::vec4(grey, grey, grey, 0.0f); // Alpha set by update

    // Large particles for smokey, feathered appearance
    p.size = 48.0f + m_Dist01(m_Rng) * 48.0f;     // 48-96 pixels
    p.lifetime = 18.0f + m_Dist01(m_Rng) * 12.0f; // 18-30 seconds
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = 0.0f;
    p.additive = false;

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnSparkles(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Sparkles;
    p.noProjection = zone.noProjection;

    // Spawn throughout zone
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * zone.size.y;

    // Stationary
    p.velocity.x = 0.0f;
    p.velocity.y = 0.0f;

    // White/yellow sparkle color
    float warmth = m_Dist01(m_Rng) * 0.3f;
    p.color = glm::vec4(1.0f, 1.0f - warmth * 0.2f, 1.0f - warmth, 1.0f);

    p.size = 2.0f + m_Dist01(m_Rng) * 2.0f;     // 2-4 pixels
    p.lifetime = 0.5f + m_Dist01(m_Rng) * 0.5f; // 0.5-1.0 seconds
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = 0.0f;
    p.additive = true; // Additive for glow effect

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnWisp(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Wisp;
    p.noProjection = zone.noProjection;

    // Spawn throughout zone
    p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
    p.position.y = zone.position.y + m_Dist01(m_Rng) * zone.size.y;

    // Gentle base drift
    p.velocity.x = (m_Dist01(m_Rng) - 0.5f) * 8.0f;
    p.velocity.y = (m_Dist01(m_Rng) - 0.5f) * 6.0f - 5.0f; // Slight upward tendency

    // Magical color variation
    float colorChoice = m_Dist01(m_Rng);
    if (colorChoice < 0.33f)
        p.color = glm::vec4(0.6f, 0.8f, 1.0f, 0.0f); // Cyan
    else if (colorChoice < 0.66f)
        p.color = glm::vec4(0.8f, 0.6f, 1.0f, 0.0f); // Purple
    else
        p.color = glm::vec4(0.9f, 0.9f, 1.0f, 0.0f); // White-blue

    p.size = 2.0f + m_Dist01(m_Rng) * 2.0f;     // 2-4 pixels
    p.lifetime = 4.0f + m_Dist01(m_Rng) * 3.0f; // 4-7 seconds
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = m_Dist01(m_Rng) * 360.0f; // Random starting rotation
    p.additive = true;                     // Glowing ethereal effect

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnLantern(int zoneIndex, const ParticleZone &zone)
{
    Particle p;
    p.zoneIndex = zoneIndex;
    p.type = ParticleType::Lantern;
    p.noProjection = zone.noProjection;

    // Spawn at center of zone
    p.position.x = zone.position.x + zone.size.x * 0.5f;
    p.position.y = zone.position.y + zone.size.y * 0.5f;

    // Stationary
    p.velocity.x = 0.0f;
    p.velocity.y = 0.0f;

    // Warm orange/yellow glow color
    p.color = glm::vec4(1.0f, 0.85f, 0.6f, 0.5f);

    // Size based on zone size, glow extends beyond the lantern tile
    // Use min dimension to prevent oversized orbs for wide zones
    p.size = std::min(zone.size.x, zone.size.y) * 4.5f;
    p.lifetime = 10.0f + m_Dist01(m_Rng) * 5.0f;
    p.maxLifetime = p.lifetime;
    p.phase = m_Dist01(m_Rng) * 6.28f;
    p.rotation = 0.0f;
    p.additive = true; // Additive blending for glow effect

    m_Particles.push_back(p);
}

void ParticleSystem::SpawnSunshine(int zoneIndex, const ParticleZone &zone)
{
    // Helper: Check if a point is covered by a sunshine ray
    // Rays are rotated rectangles with 1:4 aspect ratio (width:height)
    auto pointInRay = [](glm::vec2 point, const Particle &ray) -> bool
    {
        float halfWidth = ray.size * 0.5f;
        float halfHeight = ray.size * 2.0f; // 1:4 aspect ratio

        // Transform point to ray's local space (centered, axis-aligned)
        glm::vec2 local = point - ray.position;

        // Rotate point by negative ray rotation
        float radians = glm::radians(-ray.rotation);
        float cosR = std::cos(radians);
        float sinR = std::sin(radians);
        glm::vec2 rotated(
            local.x * cosR - local.y * sinR,
            local.x * sinR + local.y * cosR);

        // Check if within bounds
        return std::abs(rotated.x) <= halfWidth && std::abs(rotated.y) <= halfHeight;
    };

    // Helper: Count how many existing sunshine rays cover a point
    auto countRaysAtPoint = [&](glm::vec2 point) -> int
    {
        int count = 0;
        for (const auto &existing : m_Particles)
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

        // Sample points along the ray's center line and edges
        const int numSamples = 7;
        for (int i = 0; i < numSamples; i++)
        {
            float t = (i / (float)(numSamples - 1)) - 0.5f; // -0.5 to 0.5
            float localY = t * halfHeight * 2.0f;

            // Sample center and both edges at this height
            for (float xOffset : {0.0f, -halfWidth * 0.7f, halfWidth * 0.7f})
            {
                // Transform sample point to world space
                glm::vec2 sampleWorld(
                    pos.x + xOffset * cosR - localY * sinR,
                    pos.y + xOffset * sinR + localY * cosR);

                // If 2+ rays already cover this point, adding another would make 3+
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
        p.position.x = zone.position.x + m_Dist01(m_Rng) * zone.size.x;
        p.position.y = zone.position.y + m_Dist01(m_Rng) * zone.size.y;

        // Stationary rays
        p.velocity.x = 0.0f;
        p.velocity.y = 0.0f;

        // Base color, will be tinted by update based on day/night
        p.color = glm::vec4(1.0f, 0.9f, 0.5f, 0.0f); // Alpha set by update

        // Elongated beam size, texture is 48x192 (1:4 aspect)
        p.size = 40.0f + m_Dist01(m_Rng) * 24.0f; // 40-64 pixels wide

        p.lifetime = 5.0f + m_Dist01(m_Rng) * 4.0f; // 5-9 seconds
        p.maxLifetime = p.lifetime;
        p.phase = m_Dist01(m_Rng) * 6.28f;

        // Angled rays, slight variation around diagonal
        // Rays come from upper-left or upper-right at various angles
        float baseAngle = (m_Dist01(m_Rng) < 0.5f) ? -18.0f : 18.0f; // Left or right leaning
        p.rotation = baseAngle + (m_Dist01(m_Rng) - 0.5f) * 20.0f;   // +/- 10 degree variation

        p.additive = true; // Glowing effect

        // Check if this ray would create overcrowded spots (3+ rays at same point)
        if (!wouldOvercrowd(p.position, p.rotation, p.size))
        {
            m_Particles.push_back(p);
            return;
        }
    }
    // After 3 failed attempts, skip spawning this frame
}

void ParticleSystem::Render(IRenderer &renderer, glm::vec2 cameraPos, bool noProjectionOnly, bool renderAll)
{
    // For noProjection particles, we need to:
    // 1. Calculate positions while perspective is enabled
    // 2. Suspend perspective
    // 3. Draw at calculated positions
    // 4. Resume perspective

    struct ParticleRenderData
    {
        glm::vec2 screenPos;
        glm::vec2 size;
        glm::vec4 color;
        float rotation;
        float phase;
        bool additive;
        ParticleType type;
    };

    std::vector<ParticleRenderData> noProjectionBatch;
    std::vector<ParticleRenderData> regularBatch;

    // First pass: Calculate all positions (ProjectPoint works while perspective enabled)
    for (const Particle &p : m_Particles)
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

        // Get perspective state for viewport checking
        auto perspState = renderer.GetPerspectiveState();

        // NoProjection particles: Use tilemap's actual structure bounds
        if (isNoProjection && m_Tilemap && m_TileWidth > 0 && m_TileHeight > 0)
        {
            // Find which tile the particle is on
            int tileX = static_cast<int>(std::floor(p.position.x / m_TileWidth));
            int tileY = static_cast<int>(std::floor(p.position.y / m_TileHeight));

            // Get the actual structure bounds from the tilemap
            int minTileX, maxTileX, minTileY, maxTileY;
            if (m_Tilemap->FindNoProjectionStructureBounds(tileX, tileY, minTileX, maxTileX, minTileY, maxTileY))
            {
                // Structure bounds in pixels
                float leftPixelX = static_cast<float>(minTileX * m_TileWidth);
                float rightPixelX = static_cast<float>((maxTileX + 1) * m_TileWidth);
                float bottomPixelY = static_cast<float>((maxTileY + 1) * m_TileHeight);

                // Calculate anchor screen position
                float anchorScreenX = leftPixelX - cameraPos.x;
                float anchorScreenY = bottomPixelY - cameraPos.y;

                // Check if anchor is inside expanded 3D viewport - skip projection if outside
                // to prevent globe wrap-around artifacts
                float safeHorizonScale = std::max(perspState.horizonScale, 0.001f);
                float expansion = 1.0f / safeHorizonScale;
                float expandedWidth = perspState.viewWidth * expansion * 1.5f;
                float expandedHeight = perspState.viewHeight * expansion;
                float widthPadding = (expandedWidth - perspState.viewWidth) * 0.5f;
                float heightPadding = (expandedHeight - perspState.viewHeight) * 0.5f;

                bool anchorInViewport = perspState.enabled &&
                    anchorScreenX >= -widthPadding && anchorScreenX <= perspState.viewWidth + widthPadding &&
                    anchorScreenY >= -heightPadding && anchorScreenY <= perspState.viewHeight + heightPadding;

                float scaleX = 1.0f;
                glm::vec2 projectedLeft(anchorScreenX, anchorScreenY);

                if (anchorInViewport)
                {
                    // Project bottom-left and bottom-right corners
                    projectedLeft = renderer.ProjectPoint(glm::vec2(anchorScreenX, anchorScreenY));
                    glm::vec2 projectedRight = renderer.ProjectPoint(
                        glm::vec2(rightPixelX - cameraPos.x, anchorScreenY));

                    // Calculate horizontal scale based on projected width
                    float originalWidth = rightPixelX - leftPixelX;
                    float projectedWidth = projectedRight.x - projectedLeft.x;
                    scaleX = (originalWidth > 0.0f) ? (projectedWidth / originalWidth) : 1.0f;

                    // Apply exponential Y offset
                    float distanceFactor = 1.0f - scaleX;
                    float exponent = 2.0f;
                    float multiplier = static_cast<float>(m_TileHeight) * 4.0f;
                    float exponentialYOffset = std::pow(distanceFactor, exponent) * multiplier;
                    projectedLeft.y += exponentialYOffset;
                }

                // Calculate particle position relative to structure
                float tileLeftX = static_cast<float>(tileX * m_TileWidth);
                float tileTopY = static_cast<float>(tileY * m_TileHeight);

                float tileRelativeX = tileLeftX - leftPixelX;
                float tileRelativeY = tileTopY - bottomPixelY;

                float offsetInTileX = p.position.x - tileLeftX;
                float offsetInTileY = p.position.y - tileTopY;

                data.screenPos.x = projectedLeft.x + (tileRelativeX + offsetInTileX) * scaleX;
                data.screenPos.y = projectedLeft.y + tileRelativeY + offsetInTileY;
            }
            noProjectionBatch.push_back(data);
        }
        else if (isNoProjection)
        {
            // Fallback if no tilemap: Simple projection only if inside expanded 3D viewport
            float safeHorizonScale = std::max(perspState.horizonScale, 0.001f);
            float expansion = 1.0f / safeHorizonScale;
            float expandedWidth = perspState.viewWidth * expansion * 1.5f;
            float expandedHeight = perspState.viewHeight * expansion;
            float widthPadding = (expandedWidth - perspState.viewWidth) * 0.5f;
            float heightPadding = (expandedHeight - perspState.viewHeight) * 0.5f;

            bool inViewport = perspState.enabled &&
                data.screenPos.x >= -widthPadding && data.screenPos.x <= perspState.viewWidth + widthPadding &&
                data.screenPos.y >= -heightPadding && data.screenPos.y <= perspState.viewHeight + heightPadding;

            if (inViewport)
            {
                glm::vec2 projected = renderer.ProjectPoint(data.screenPos);
                data.screenPos = projected;
            }
            noProjectionBatch.push_back(data);
        }
        else
        {
            // Cull regular particles that are outside viewport or behind sphere
            // Use generous padding to account for particle size and partial visibility
            float padding = std::max(data.size.x, data.size.y) * 2.0f + 50.0f;

            bool outsideViewport = data.screenPos.x < -padding ||
                                   data.screenPos.x > perspState.viewWidth + padding ||
                                   data.screenPos.y < -padding ||
                                   data.screenPos.y > perspState.viewHeight + padding;

            if (outsideViewport)
                continue;

            // Check if particle is behind the sphere (only when globe/fisheye is enabled)
            if (renderer.IsPointBehindSphere(data.screenPos))
                continue;

            regularBatch.push_back(data);
        }
    }

    // Lambda to draw a particle using the texture atlas
    auto drawParticle = [&](const ParticleRenderData &data)
    {
        if (m_TexturesLoaded)
        {
            int typeIndex = static_cast<int>(data.type);
            const AtlasRegion &region = m_AtlasRegions[typeIndex];

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
            renderer.DrawSpriteAtlas(m_AtlasTexture, centeredPos, renderSize,
                                     region.uvMin, region.uvMax,
                                     data.rotation, data.color, data.additive);
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
    auto sortByBlendMode = [](const ParticleRenderData &a, const ParticleRenderData &b)
    {
        return a.additive < b.additive;
    };

    std::sort(noProjectionBatch.begin(), noProjectionBatch.end(), sortByBlendMode);
    std::sort(regularBatch.begin(), regularBatch.end(), sortByBlendMode);

    // Draw noProjection particles with perspective suspended
    if (!noProjectionBatch.empty())
    {
        renderer.SuspendPerspective(true);
        for (const auto &data : noProjectionBatch)
        {
            drawParticle(data);
        }
        renderer.SuspendPerspective(false);
    }

    // Draw regular particles normally
    for (const auto &data : regularBatch)
    {
        drawParticle(data);
    }
}

void ParticleSystem::OnZoneRemoved(int zoneIndex)
{
    if (zoneIndex < 0)
        return;

    // Remove particles from the deleted zone and adjust indices for remaining particles
    for (auto it = m_Particles.begin(); it != m_Particles.end();)
    {
        if (it->zoneIndex == zoneIndex)
        {
            // Remove particles from the deleted zone
            it = m_Particles.erase(it);
        }
        else
        {
            // Adjust indices for particles from higher-indexed zones
            if (it->zoneIndex > zoneIndex)
            {
                it->zoneIndex--;
            }
            ++it;
        }
    }

    if (zoneIndex < static_cast<int>(m_ZoneSpawnTimers.size()))
    {
        m_ZoneSpawnTimers.erase(m_ZoneSpawnTimers.begin() + zoneIndex);
    }
}
