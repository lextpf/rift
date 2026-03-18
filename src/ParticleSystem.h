#pragma once

#include "EnumTraits.h"
#include "IRenderer.h"
#include "Texture.h"

#include <glm/glm.hpp>
#include <random>
#include <vector>

class Tilemap;

/**
 * @enum ParticleType
 * @brief Categories of particle effects with distinct visual behaviors.
 * @author Alex (https://github.com/lextpf)
 *
 * Each type has unique spawn, movement, and rendering characteristics.
 *
 * | Type     | Movement        | Blending | Use Case              |
 * |----------|-----------------|----------|-----------------------|
 * | Firefly  | Drifting, pulse | Additive | Night ambiance        |
 * | Rain     | Fast downward   | Alpha    | Weather               |
 * | Snow     | Slow drift down | Additive | Weather               |
 * | Fog      | Slow drift      | Alpha    | Atmosphere            |
 * | Sparkles | Stationary      | Additive | Magic/treasure        |
 * | Wisp     | Spiral wander   | Additive | Magical areas         |
 * | Lantern  | Stationary glow | Additive | Night lighting        |
 * | Sunshine | Angled rays     | Additive | Forest clearings      |
 */
enum class ParticleType
{
    Firefly = 0,   ///< Pulsing yellow-green glow, gentle drift
    Rain = 1,      ///< Fast falling droplets, slight angle
    Snow = 2,      ///< Slow falling flakes with side drift
    Fog = 3,       ///< Large translucent patches, very slow
    Sparkles = 4,  ///< Brief bright flashes, stationary
    Wisp = 5,      ///< Magical spiraling orbs, color variety
    Lantern = 6,   ///< Warm glow, night-only visibility
    Sunshine = 7   ///< Sun rays (day=yellow) / moon beams (night=blue)
};

/// Compile-time reflection for ParticleType.
template <>
struct EnumTraits<ParticleType> : EnumTraitsBase<ParticleType, EnumTraits<ParticleType>>
{
    static constexpr size_t Count = 8;
    static constexpr std::string_view Names[] = {
        "Firefly", "Rain", "Snow", "Fog", "Sparkles", "Wisp", "Lantern", "Sunshine"};

    static_assert(std::to_underlying(ParticleType::Sunshine) == Count - 1,
                  "Update EnumTraits<ParticleType> when adding new ParticleType values");
};

/**
 * @struct Particle
 * @brief Runtime state for a single active particle.
 * @author Alex (https://github.com/lextpf)
 *
 * Particles are spawned by zones and updated each frame until their
 * lifetime expires. The `type` field is stored directly to handle
 * cases where the spawning zone is deleted mid-flight.
 */
struct Particle
{
    glm::vec2 position;  ///< World position (pixels).
    glm::vec2 velocity;  ///< Movement per second (pixels/s).
    glm::vec4 color;     ///< RGBA color (alpha may animate).
    float size;          ///< Sprite size in pixels.
    float lifetime;      ///< Remaining life (seconds).
    float maxLifetime;   ///< Original lifetime for fade calculations.
    float phase;         ///< Random phase offset for oscillation effects.
    float rotation;      ///< Sprite rotation (degrees).
    bool additive;       ///< Use additive blending for glow.
    bool noProjection;   ///< Render without perspective distortion.
    int zoneIndex;       ///< Spawning zone index (-1 for orphaned).
    ParticleType type;   ///< Particle behavior type.
};

/**
 * @struct ParticleZone
 * @brief Rectangular region that spawns particles of a specific type.
 * @author Alex (https://github.com/lextpf)
 *
 * Zones are placed in the level editor and stored in the Tilemap.
 * The ParticleSystem holds a pointer to the zone list and spawns
 * particles within visible zones each frame.
 *
 * @par Tile Alignment
 * Zone position and size are in world pixels but typically aligned
 * to tile boundaries for easy placement.
 */
struct ParticleZone
{
    glm::vec2 position;  ///< Top-left corner (world pixels).
    glm::vec2 size;      ///< Width and height (world pixels).
    ParticleType type;   ///< Type of particles to emit.
    bool enabled;        ///< Whether spawning is active.
    bool noProjection;   ///< Particles ignore perspective.

    ParticleZone()
        : position(0.0f),
          size(32.0f),
          type(ParticleType::Firefly),
          enabled(true),
          noProjection(false)
    {
    }
    ParticleZone(glm::vec2 pos, glm::vec2 sz, ParticleType t)
        : position(pos),
          size(sz),
          type(t),
          enabled(true),
          noProjection(false)
    {
    }
};

/**
 * @class ParticleSystem
 * @brief Manages spawning, updating, and rendering of zone-based particles.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * The particle system provides ambient visual effects through zone-based
 * emitters placed in the level editor. Each zone spawns particles of a
 * specific type within its bounds.
 *
 * @section particle_architecture System Architecture
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef zone fill:#164e54,stroke:#06b6d4,color:#e2e8f0
 *     classDef system fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef particle fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *
 *     Z1[Zone: Firefly]:::zone --> PS[ParticleSystem]:::system
 *     Z2[Zone: Rain]:::zone --> PS
 *     Z3[Zone: Lantern]:::zone --> PS
 *     PS --> P1[Particle Pool]:::particle
 *     P1 --> R[Renderer]
 * </pre>
 * @endhtmlonly
 *
 * @section particle_types Particle Type Behaviors
 * | Type     | Spawn Rate | Lifetime | Size    | Special Behavior           |
 * |----------|------------|----------|---------|----------------------------|
 * | Firefly  | 3/s        | 4-9s     | 2-4px   | Pulsing alpha, drift       |
 * | Rain     | 50/s       | 2s       | 10-14px | Fast fall, angled sprite   |
 * | Snow     | 12/s       | 15s      | 1.5-3px | Slow fall, rotation        |
 * | Fog      | 3/s        | 18-30s   | 48-96px | Very slow drift, low alpha |
 * | Sparkles | 18/s       | 0.5-1s   | 2-4px   | Brief flash, stationary    |
 * | Wisp     | 4/s        | 4-7s     | 2-4px   | Spiral movement, colors    |
 * | Lantern  | 0.5/s      | 10-15s   | 4x zone | Night-only glow            |
 * | Sunshine | 0.8/s      | 5-9s     | 40-64px | Angled rays, day/night     |
 *
 * @section particle_lifecycle Particle Lifecycle
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     classDef spawn fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef active fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef dead fill:#4a2020,stroke:#ef4444,color:#e2e8f0
 *
 *     [*] --> Spawned: Zone visible
 *     Spawned --> Active: Initialize
 *     Active --> Active: Update position/alpha
 *     Active --> Dead: lifetime <= 0
 *     Active --> Dead: Zone deleted
 *     Dead --> [*]: Remove from pool
 *
 *     class Spawned spawn
 *     class Active active
 *     class Dead dead
 * </pre>
 * @endhtmlonly
 *
 * @section particle_noprojection No-Projection Particles
 * Particles in zones marked `noProjection` are rendered without perspective
 * distortion, matching the behavior of no-projection structures. This ensures
 * effects like lantern glows stay aligned with their parent structures.
 *
 * @par Projection Calculation
 * For no-projection particles, the system:
 * 1. Finds the parent structure bounds via `Tilemap::FindNoProjectionStructureBounds()`
 * 2. Projects the structure's base corners
 * 3. Calculates horizontal scale from projected width
 * 4. Applies the same exponential Y offset as tile rendering
 * 5. Positions the particle relative to the projected structure
 *
 * @section particle_textures Texture System
 * Each particle type has a dedicated texture loaded at initialization.
 * Missing textures fall back to colored rectangles. Some textures are
 * procedurally generated.
 *
 * @section particle_performance Performance Notes
 * - Particles are pooled in a single vector (reserved for 500)
 * - Only zones within camera view (+margin) spawn particles
 * - Per-zone particle cap prevents runaway spawning
 * - Spawn rate scales with zone area (0.5x to 3x multiplier)
 *
 * @see ParticleZone, Particle, Tilemap::GetParticleZones()
 */
class ParticleSystem
{
public:
    ParticleSystem();

    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;
    ParticleSystem(ParticleSystem&&) noexcept = default;
    ParticleSystem& operator=(ParticleSystem&&) noexcept = default;

    /**
     * @brief Load all particle textures from disk.
     *
     * Attempts to load each texture independently. Missing textures
     * will fall back to colored rectangles during rendering.
     *
     * @return Always true (individual failures are non-fatal).
     */
    bool LoadTextures();

    /**
     * @brief Re-upload all particle textures to the renderer.
     *
     * Called after switching rendering backends to ensure textures
     * are available in the new renderer's GPU memory.
     *
     * @param renderer The renderer to upload textures to.
     */
    void UploadTextures(IRenderer& renderer);

    /**
     * @brief Set the zone list for particle spawning.
     * @param zones Pointer to zone vector (owned by Tilemap).
     */
    void SetZones(const std::vector<ParticleZone>* zones) { m_Zones = zones; }

    /**
     * @brief Set tile dimensions for no-projection calculations.
     * @param width  Tile width in pixels.
     * @param height Tile height in pixels.
     */
    void SetTileSize(int width, int height)
    {
        m_TileWidth = width;
        m_TileHeight = height;
    }

    /**
     * @brief Set tilemap reference for structure bound queries.
     * @param tilemap Pointer to tilemap (for no-projection lookups).
     */
    void SetTilemap(const Tilemap* tilemap) { m_Tilemap = tilemap; }

    /**
     * @brief Set maximum particles allowed per zone.
     * @param count Maximum particle count per zone.
     */
    void SetMaxParticlesPerZone(size_t count) { m_MaxParticlesPerZone = count; }

    /**
     * @brief Set the night visibility factor for lantern effects.
     *
     * Controls lantern glow intensity based on time of day.
     *
     * @param factor 0.0 = day (invisible), 1.0 = full night (max glow).
     */
    void SetNightFactor(float factor) { m_NightFactor = factor; }

    /**
     * @brief Update all particles and spawn new ones.
     *
     * Performs per-frame updates:
     * 1. Decrement lifetimes, remove dead particles
     * 2. Update positions based on velocity and type-specific behavior
     * 3. Update alpha/color for effects (pulsing, fading)
     * 4. Spawn new particles in visible zones
     *
     * @param deltaTime Frame time in seconds.
     * @param cameraPos Camera position for visibility culling.
     * @param viewSize Viewport dimensions.
     */
    void Update(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize);

    /**
     * @brief Render particles to the screen.
     *
     * Renders in two passes: no-projection particles (with perspective
     * suspended) and regular particles. Textures are used when available,
     * falling back to colored rectangles.
     *
     * @param renderer Active renderer.
     * @param cameraPos Camera position for world-to-screen conversion.
     * @param noProjectionOnly If true, only render no-projection particles.
     * @param renderAll If true, render all particles regardless of projection flag.
     */
    void Render(IRenderer& renderer,
                glm::vec2 cameraPos,
                bool noProjectionOnly = false,
                bool renderAll = true);

    /**
     * @brief Get read-only access to the particle pool.
     * @return Reference to particle vector.
     */
    const std::vector<Particle>& GetParticles() const { return m_Particles; }

    /**
     * @brief Remove all active particles.
     */
    void Clear() { m_Particles.clear(); }

    /**
     * @brief Handle zone deletion by cleaning up orphaned particles.
     *
     * Removes particles belonging to the deleted zone and adjusts
     * zone indices for particles from higher-indexed zones.
     *
     * @param zoneIndex Index of the removed zone.
     */
    void OnZoneRemoved(int zoneIndex);

private:
    void SpawnParticleInZone(int zoneIndex, const ParticleZone& zone);

    /// @name Particle Pool
    /// @{

    std::vector<Particle> m_Particles;         ///< Active particle pool.
    const std::vector<ParticleZone>* m_Zones;  ///< Zone list (owned by Tilemap).
    const Tilemap* m_Tilemap;                  ///< Tilemap for structure queries.

    /// @}

    /// @name Configuration
    /// @{

    int m_TileWidth;                           ///< Tile width for projection math.
    int m_TileHeight;                          ///< Tile height for projection math.
    size_t m_MaxParticlesPerZone;              ///< Per-zone particle cap.
    float m_Time;                              ///< Elapsed time for oscillation effects.
    float m_NightFactor;                       ///< Day/night factor (0-1) for lanterns.
    std::vector<float> m_ZoneSpawnTimers;      ///< Per-zone spawn accumulators.
    std::vector<size_t> m_ZoneParticleCounts;  ///< Per-zone active particle counts.

    /// @}

    /// @name Random Number Generation
    /// @{

    std::mt19937 m_Rng;                              ///< Mersenne Twister RNG.
    std::uniform_real_distribution<float> m_Dist01;  ///< Uniform [0, 1) distribution.

    /// @}

    /// @name Texture Atlas
    /// @{

    /**
     * @brief UV region for a particle type in the atlas.
     *
     * Stores normalized UV coordinates (0-1) for sampling from the atlas.
     */
    struct AtlasRegion
    {
        glm::vec2 uvMin;  ///< Top-left UV coordinate.
        glm::vec2 uvMax;  ///< Bottom-right UV coordinate.
    };

    Texture m_AtlasTexture;         ///< Combined particle texture atlas.
    AtlasRegion m_AtlasRegions[8];  ///< UV regions indexed by ParticleType.
    bool m_TexturesLoaded;          ///< Whether LoadTextures() succeeded.

    /**
     * @brief Build the texture atlas from individual particle textures.
     *
     * Loads all particle textures, packs them into a single atlas,
     * and calculates UV regions for each particle type.
     */
    void BuildAtlas();

    /// @}

    /// @name Render Batch Data
    /// @{

    /**
     * @brief Pre-computed render state for a single particle.
     *
     * Populated during the projection pass in Render() and consumed
     * by the draw pass. Stored as member vectors to avoid per-frame
     * heap allocation.
     */
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

    std::vector<ParticleRenderData>
        m_NoProjectionBatch;                         ///< Particles rendered without perspective.
    std::vector<ParticleRenderData> m_RegularBatch;  ///< Particles rendered with perspective.

    /**
     * @brief Generate the lantern glow texture procedurally.
     * @param[out] pixels Output pixel buffer (256x256 RGBA).
     * @param[out] width  Output width.
     * @param[out] height Output height.
     */
    void GenerateLanternPixels(std::vector<unsigned char>& pixels, int& width, int& height);

    /**
     * @brief Generate the sunshine ray texture procedurally.
     * @param[out] pixels Output pixel buffer (48x192 RGBA).
     * @param[out] width  Output width.
     * @param[out] height Output height.
     */
    void GenerateSunshinePixels(std::vector<unsigned char>& pixels, int& width, int& height);

    /// @}
};
