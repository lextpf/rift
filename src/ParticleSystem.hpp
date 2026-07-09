#pragma once

#include "EnumTraits.hpp"
#include "IRenderer.hpp"
#include "Texture.hpp"
#include "TextureHandle.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <random>
#include <vector>

class Tilemap;
class TextureStore;
struct ProjectManifest;
struct WeatherDefinition;
enum class WeatherParticleType;

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
    Firefly = 0,         ///< Pulsing yellow-green glow, gentle drift
    Rain = 1,            ///< Fast falling droplets, slight angle
    Snow = 2,            ///< Slow falling flakes with side drift
    Fog = 3,             ///< Large translucent patches, very slow
    Sparkles = 4,        ///< Brief bright twinkles, stationary
    Wisp = 5,            ///< Magical spiraling orbs, color variety
    Lantern = 6,         ///< Warm glow, night-only visibility
    Sunshine = 7,        ///< Sun rays (day=yellow) / moon beams (night=blue)
    DriftingLeaf = 8,    ///< Ambient cozy: small green/yellow leaf drifting on wind
    DustMote = 9,        ///< Ambient cozy: tiny golden mote in sunbeams
    Pollen = 10,         ///< Ambient cozy: yellow pollen during golden hour
    CherryBlossom = 11,  ///< Weather: drifting pink petals, gentle spiral
    Ash = 12,            ///< Weather: gray-white particles, slow fall + flutter
    Ember = 13,          ///< Weather: orange particles rising upward, additive flicker
    Sand = 14,           ///< Weather: tan-gold particles, fast horizontal wind

    // Appended types (map JSON stores the underlying int - append only, never
    // reorder the values above).
    Smoke = 15,          ///< Rising, expanding puffs for chimneys/campfires (wind-bent)
    Steam = 16,          ///< Fast-rising short-lived white vapor (vents, hot springs)
    Aurora = 17,         ///< Soft aurora motes drifting on slow ribbons (night skies)
    Spark = 18,          ///< Energetic darting crackle, brief and bright
    PixieDust = 19,      ///< Falling glittering trail dust with heavy twinkle
    Arcane = 20,         ///< Violet glyph motes orbiting their spawn point
    Enchant = 21,        ///< Rising enchantment glyphs with easing deceleration
    Runes = 22,          ///< Slow-turning rune sigils with strong glow pulse
    Hex = 23,            ///< Counter-orbiting witch-magic motes, eerie pulse
    Curse = 24,          ///< Dark wobbling taint, slow rise, unsettling flicker
    Void = 25,           ///< Dark matter spiraling inward toward the spawn point
    Vortex = 26,         ///< Fast circular swirl tightening over lifetime
    Soul = 27,           ///< Ghostly wisp rising in a slow S-curve wander
    Fairy = 28,          ///< Darting hover-and-dash glow (quicker than fireflies)
    Butterfly = 29,      ///< Wandering flappy flight, daylight meadows
    Bat = 30,            ///< Swooping erratic night flier
    Bubble = 31,         ///< Buoyant wobbling bubble; converts to its pop strip on expiry
    Coin = 32,           ///< Spinning coin glint (treasure rooms)
    Gem = 33,            ///< Floating gem with periodic sparkle glints
    Confetti = 34,       ///< Celebration popper: rare bursts of tumbling scraps
    Heart = 35,          ///< Affection emote floating up with a sway
    Zap = 36,            ///< Electric arc strobe with positional jitter
    Wind = 37,           ///< Fast horizontal gust streaks riding the wind
    Zzz = 38,            ///< Sleep emote drifting up in an easing arc
    Constellation = 39,  ///< Near-stationary star twinkle (night events)
    Planet = 40,         ///< Very slow drifting celestial body accent
    Moon = 41,           ///< Stationary crescent accent with soft glow pulse
    Ink = 42             ///< Dark blot hovering in place, billowing softly
};

/// Compile-time reflection for ParticleType.
template <>
struct EnumTraits<ParticleType> : EnumTraitsBase<ParticleType, EnumTraits<ParticleType>>
{
    static constexpr size_t Count = 43;
    static constexpr std::string_view Names[] = {
        "Firefly",  "Rain",         "Snow",     "Fog",    "Sparkles",      "Wisp",      "Lantern",
        "Sunshine", "DriftingLeaf", "DustMote", "Pollen", "CherryBlossom", "Ash",       "Ember",
        "Sand",     "Smoke",        "Steam",    "Aurora", "Spark",         "PixieDust", "Arcane",
        "Enchant",  "Runes",        "Hex",      "Curse",  "Void",          "Vortex",    "Soul",
        "Fairy",    "Butterfly",    "Bat",      "Bubble", "Coin",          "Gem",       "Confetti",
        "Heart",    "Zap",          "Wind",     "Zzz",    "Constellation", "Planet",    "Moon",
        "Ink"};

    static_assert(std::to_underlying(ParticleType::Ink) == Count - 1,
                  "Update EnumTraits<ParticleType> when adding new ParticleType values");
    static_assert(std::size(Names) == Count,
                  "EnumTraits<ParticleType>::Names must have one entry per enumerator");
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
    glm::vec2 position;        ///< World position (pixels).
    glm::vec2 velocity;        ///< Movement per second (pixels/s).
    glm::vec4 color;           ///< RGBA color (alpha may animate).
    float size;                ///< Sprite size in pixels.
    float lifetime;            ///< Remaining life (seconds).
    float maxLifetime;         ///< Original lifetime for fade calculations.
    float phase;               ///< Random phase offset for oscillation effects.
    float rotation;            ///< Sprite rotation (degrees).
    float bakedGroundY{0.0f};  ///< Per-particle ground reference (used by Rain weather for splash
                               ///< impact); 0 means unset.
    bool additive;             ///< Use additive blending for glow.
    bool noProjection;         ///< Render without perspective distortion.
    int zoneIndex;             ///< Spawning zone index, or -1 for zoneless one-shots/ambient.
    ParticleType type;         ///< Particle behavior type.
    uint8_t variant{0};        ///< Sprite variant index (e.g. smoke/smoke2/smoke3), rolled at
                               ///< spawn by AssignSpawnVariants against the atlas variant count.
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
 *     W[Weather state]:::zone --> PS
 *     A[Ambient emitters]:::zone --> PS
 *     PS --> P1[Particle Pool]:::particle
 *     P1 --> R[Renderer]
 * </pre>
 * @endhtmlonly
 *
 * @section particle_types Particle Type Behaviors
 * | Type          | Spawn Rate | Lifetime | Size    | Special Behavior           |
 * |---------------|------------|----------|---------|----------------------------|
 * | Firefly       | 8/s        | 4-9s     | 2-4px   | Pulsing alpha, drift       |
 * | Rain          | 25/s       | 2s       | 10-14px | Fast fall, angled sprite   |
 * | Snow          | 25/s       | 15s      | 1.5-3px | Slow fall, rotation        |
 * | Fog           | 5/s        | 18-30s   | 48-96px | Very slow drift, low alpha |
 * | Sparkles      | 28/s       | 0.5-1s   | 2-4px   | Brief flash, stationary    |
 * | Wisp          | 11/s       | 4-7s     | 2-4px   | Spiral movement, colors    |
 * | Lantern       | 0.5/s      | 10-15s   | 4x zone | Night-only glow            |
 * | Sunshine      | 1.3/s      | 5-9s     | 40-64px | Angled rays, day/night     |
 * | DriftingLeaf  | weather    | varied   | varied  | Wind-blown leaves          |
 * | DustMote      | weather    | varied   | small   | Ambient dust motes         |
 * | Pollen        | weather    | varied   | small   | Slow floating pollen       |
 * | CherryBlossom | weather    | varied   | varied  | Blossom petal drift        |
 * | Ash           | weather    | varied   | small   | Falling ash                |
 * | Ember         | weather    | varied   | small   | Glowing ember drift        |
 * | Sand          | weather    | varied   | small   | Wind-driven sand           |
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
 * 1. Asks `Tilemap::ProjectNoProjectionStructurePoint()` to project particles
 *    covered by a no-projection structure.
 * 2. Uses the returned screen point so particles stay aligned with the
 *    structure's stepped/projected mesh.
 * 3. Falls back to regular renderer projection when no structure covers the
 *    particle point.
 * 4. Renders no-projection and regular particles through separate batches.
 *
 * @section particle_textures Texture System
 * Each particle type owns one or more sprite variants (e.g. smoke/smoke2/
 * smoke3), packed into a single atlas at initialization. Variant names
 * resolve to on-disk files (opaque GUIDs) through the project manifest's
 * "particles" links; from the linked file, BuildAtlas derives the
 * `<...>_strip.png` sibling (horizontal 4-frame animation strip, sliced into
 * per-frame UVs at draw time) and the single-frame `<...>.png`, preferring
 * the strip. Unlinked or missing assets fall back to a procedural soft
 * circle, and Lantern and Sunshine are fully procedural. Particles roll a
 * random variant at spawn (some types pin the roll and use later variants as
 * runtime states, e.g. Bubble's pop strip). Strip playback either loops on
 * global time (offset per particle) or maps onto the particle's lifetime for
 * one-shots such as bubble pops and Sparkles twinkles.
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
     * Attempts to load each texture independently, resolving asset paths
     * through the project manifest's "particles" links. Missing links or
     * files fall back to procedural sprites.
     *
     * @return Always true (individual failures are non-fatal).
     *
     * @param store    TextureStore that adopts the built atlas; its UploadAll re-uploads
     *                 it on a renderer switch (no per-object IGpuResourceOwner hook).
     * @param manifest Project manifest supplying the particle sprite links.
     */
    bool LoadTextures(TextureStore& store, const ProjectManifest& manifest);

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
     * @brief Toggle rendering of all particles (weather, zone, ambient).
     *
     * Simulation keeps running while rendering is disabled, so toggling back
     * on does not show a populate-in delay. Used by the `particles` console
     * command to A/B-compare scenes with and without particle layers.
     */
    void SetRenderEnabled(bool enabled) { m_RenderEnabled = enabled; }
    bool IsRenderEnabled() const { return m_RenderEnabled; }

    /**
     * @brief Number of particles that survived culling and were drawn on the
     * most recent @c Render call. Reported as zero while rendering is
     * disabled. Used by the debug overlay to gauge live load.
     */
    size_t GetLastDrawnCount() const { return m_LastDrawnCount; }

    /**
     * @brief Remove all active particles.
     */
    void Clear() { m_Particles.clear(); }

    /**
     * @brief Spawn a single one-shot particle of @p type at @p worldPos.
     *
     * Bypasses the zone system: builds a 1x1 ad-hoc zone at the requested
     * position and runs the same per-type initializer used by zone spawns,
     * then tags the resulting particle with @c zoneIndex = -1 so the
     * orphan-cleanup pass leaves it alone. Intended for the developer
     * console's `particle.spawn` command.
     *
     * @param type     Particle type to spawn.
     * @param worldPos World pixel position for the spawn.
     */
    void SpawnOne(ParticleType type, glm::vec2 worldPos);

    /**
     * @brief Handle zone deletion by cleaning up orphaned particles.
     *
     * Removes particles belonging to the deleted zone and adjusts
     * zone indices for particles from higher-indexed zones.
     *
     * @param zoneIndex Index of the removed zone.
     */
    void OnZoneRemoved(int zoneIndex);

    /**
     * @brief Set the time-of-day used for ambient-particle spawn biasing.
     *
     * DriftingLeaf, DustMote, and Pollen spawn at different rates depending
     * on the time of day (motes prefer daylight, pollen golden hour, etc.).
     *
     * @param timeOfDay Current TimeManager hour in [0, 24].
     */
    void SetTimeOfDay(float timeOfDay) { m_TimeOfDay = timeOfDay; }

    /**
     * @brief Set the active weather definition used for global particle spawning.
     *
     * Drives the per-frame "weather emitter" that spawns rain, snow, ash, etc.
     * across the visible viewport at `def->baseSpawnRate * intensity`. Pass
     * `nullptr` to disable weather spawning.
     *
     * @param def       Pointer to a stable WeatherDefinition (e.g. from
     *                  GetWeatherDefinition). Lifetime must outlive the next
     *                  Update call.
     * @param intensity Density multiplier in [0, 1].
     */
    void SetWeatherState(const WeatherDefinition* def, float intensity);

    /**
     * @brief Set transition spawn streams. While outgoing is non-null the
     * weather spawner runs FOUR streams - outgoing primary/secondary at
     * rate*(1-weight), incoming primary/secondary at rate*weight - each
     * stream spawning with ITS OWN definition (size scale etc.), while
     * SetWeatherState's def keeps feeding the live-read channels (fog
     * alpha). Null outgoing = normal two-stream spawning.
     *
     * @param outgoing Outgoing endpoint definition, or `nullptr` to disable
     *                 transition spawning. Pointer lifetime contract matches
     *                 @ref SetWeatherState.
     * @param incoming Incoming endpoint definition, or `nullptr`.
     * @param weight   Incoming stream weight in [0, 1]; outgoing weight is
     *                 `1 - weight`.
     */
    void SetWeatherTransition(const WeatherDefinition* outgoing,
                              const WeatherDefinition* incoming,
                              float weight);

    /**
     * @brief Set the prevailing wind used by weather particle behaviors.
     *
     * Wind is a global rendering input like the night factor: fed per-frame
     * by Game from the WeatherDirector's gust envelope, and read by weather
     * particle boosts (e.g. Snow's directional drift) rather than derived
     * from the active WeatherDefinition directly.
     *
     * @param direction Wind direction; normalized on use. Near-zero vectors
     *                  leave the previous direction unchanged.
     * @param strength  Gusted wind strength, clamped to >= 0. 0.5 is the
     *                  engine-wide calm default.
     */
    void SetWind(glm::vec2 direction, float strength);

    /**
     * @brief Set the player's bottom-center world position.
     *
     * Used by PollenStorm / FallingLeaves so weather particles can "avoid"
     * the player's 16x32 hitbox (the standard 16x16 hitbox plus the tile
     * directly above the player) when the player is moving. Called once per
     * frame from Game::Update.
     *
     * @param pos Bottom-center of the player sprite in world pixels.
     */
    void SetPlayerPosition(glm::vec2 pos) { m_PlayerPosition = pos; }

private:
    void SpawnParticleInZone(int zoneIndex, const ParticleZone& zone);

    /**
     * @brief Maintain global ambient particle population (leaves/dust/pollen)
     * independent of editor zones, biased by time of day.
     * @param deltaTime Frame time in seconds.
     * @param cameraPos Camera position for spawning rect.
     * @param viewSize Viewport size for spawning rect.
     */
    void UpdateAmbientSpawning(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize);

    /// @brief Spawn one global (zoneIndex = -1) ambient particle of the given type.
    void SpawnAmbientParticle(ParticleType type, glm::vec2 cameraPos, glm::vec2 viewSize);

    /**
     * @brief Maintain weather-driven particle spawning (rain/snow/ash/etc.)
     * across the visible viewport. Spawned particles are tagged with
     * zoneIndex = WEATHER_ZONE_INDEX (-2) so they coexist with zone particles.
     * Drives both the primary `particleType` and any optional secondary
     * declared on the active WeatherDefinition (e.g., Blizzard layers Fog
     * on top of Snow).
     */
    void UpdateWeatherSpawning(float deltaTime, glm::vec2 cameraPos, glm::vec2 viewSize);

    /**
     * @brief Scale a weather stream's base spawn rate by intensity and the
     * visible-area (zoom) ratio so density per visible pixel stays roughly
     * constant as the player zooms in or out.
     */
    float EffectiveRate(float baseSpawnRate, glm::vec2 viewSize) const;

    /**
     * @brief Spawn one weather-particle stream. Shared between the primary
     * and secondary slots on WeatherDefinition. @p spawnTimer is the
     * accumulator for this slot (caller passes a different one per slot
     * so the two streams don't share state). @p effectiveRate is the
     * already-scaled rate (see EffectiveRate); @p liveByType is the
     * per-ParticleType census hoisted by the caller and incremented here
     * as particles spawn. @p streamDef is the definition this stream spawns
     * from (may differ from m_CurrentWeatherDef during a transition) and is
     * forwarded to SpawnWeatherParticle for per-stream tuning (size scale).
     */
    void SpawnWeatherType(WeatherParticleType wpt,
                          float effectiveRate,
                          int maxWeatherParticles,
                          float& spawnTimer,
                          float deltaTime,
                          glm::vec2 cameraPos,
                          glm::vec2 viewSize,
                          std::array<int, EnumTraits<ParticleType>::Count>& liveByType,
                          const WeatherDefinition* streamDef);

    /**
     * @brief Spawn one weather particle. Implementation chooses spawn rect
     * edge based on the weather particle type (top for precipitation,
     * upwind edge for sand, anywhere for fog/ash). @p streamDef supplies the
     * per-stream size scale (see SpawnWeatherType).
     */
    void SpawnWeatherParticle(ParticleType type,
                              glm::vec2 cameraPos,
                              glm::vec2 viewSize,
                              const WeatherDefinition* streamDef);

public:
    /**
     * Sentinel zoneIndex for weather-spawned particles. Public so the
     * per-type ParticleBehavior templates (defined at namespace scope in
     * ParticleSystem.cpp) can discriminate weather-driven particles from
     * zone/ambient spawns when their Update behavior needs to differ
     * (e.g. FallingLeaves applying camera-velocity drag).
     */
    static constexpr int WEATHER_ZONE_INDEX = -2;

private:
    /**
     * @name Particle Pool
     * @{
     */

    std::vector<Particle> m_Particles;         ///< Active particle pool.
    std::vector<Particle> m_PendingSpawns;     ///< Mid-update spawns (e.g., Rain splashes), merged
                                               ///< after the update loop.
    const std::vector<ParticleZone>* m_Zones;  ///< Zone list (owned by Tilemap).
    const Tilemap* m_Tilemap;                  ///< Tilemap for structure queries.

    /// @}

    /**
     * @name Configuration
     * @{
     */

    int m_TileWidth;                           ///< Tile width for projection math.
    int m_TileHeight;                          ///< Tile height for projection math.
    size_t m_MaxParticlesPerZone;              ///< Per-zone particle cap.
    float m_Time;                              ///< Elapsed time for oscillation effects.
    float m_NightFactor;                       ///< Day/night factor (0-1) for lanterns.
    float m_TimeOfDay = 12.0f;                 ///< Hour in [0, 24] for ambient spawn biasing.
    std::vector<float> m_ZoneSpawnTimers;      ///< Per-zone spawn accumulators.
    std::vector<size_t> m_ZoneParticleCounts;  ///< Per-zone active particle counts.

    /**
     * Per-type ambient spawn timers (only DriftingLeaf, DustMote, Pollen used).
     * Indexed by ParticleType enum value. Sized via EnumTraits to auto-grow
     * when new ParticleType values are added.
     */
    float m_AmbientSpawnTimers[EnumTraits<ParticleType>::Count] = {};

    /**
     * @name Weather Spawning
     * @{
     */
    const WeatherDefinition* m_CurrentWeatherDef{nullptr};  ///< Active weather (or null).
    float m_WeatherIntensity{1.0f};                         ///< 0-1 density scalar.
    float m_WeatherSpawnTimer{0.0f};                        ///< Primary spawn accumulator.
    float m_WeatherSpawnTimerSecondary{0.0f};               ///< Secondary spawn accumulator.
    glm::vec2 m_WindDir{-1.0f, 0.0f};  ///< Prevailing wind direction (normalized on use).
    float m_WindStrength{0.5f};        ///< Gusted wind strength; 0.5 = calm engine default.

    /**
     * @name Transition Spawning
     * While m_TransitionOut is non-null, UpdateWeatherSpawning runs four
     * streams (outgoing + incoming, primary + secondary) instead of the
     * normal two. See SetWeatherTransition.
     * @{
     */
    const WeatherDefinition* m_TransitionOut{nullptr};  ///< Outgoing endpoint (null = idle).
    const WeatherDefinition* m_TransitionIn{nullptr};   ///< Incoming endpoint.
    float m_TransitionWeight{0.0f};                     ///< Incoming stream weight [0, 1].
    float m_WeatherSpawnTimerOut{0.0f};                 ///< Outgoing primary accumulator.
    float m_WeatherSpawnTimerOutSecondary{0.0f};        ///< Outgoing secondary accumulator.
    /// @}
    /// @}

    /**
     * @name Camera Tracking
     * Smoothed camera velocity, derived from per-frame deltas in Update(),
     * used to gate weather-particle avoidance behavior to "player is moving".
     * Player position is set per-frame from Game::Update and drives the
     * hitbox-anchored avoidance zone used by PollenStorm / FallingLeaves.
     * @{
     */
    glm::vec2 m_PrevCameraPos{0.0f};
    glm::vec2 m_CameraVelocity{0.0f};
    bool m_HasPrevCameraPos{false};  ///< False until the first Update() seeds m_PrevCameraPos.
    glm::vec2 m_PlayerPosition{0.0f};
    /// @}

    /// @}

    /**
     * @name Random Number Generation
     * @{
     */

    std::mt19937 m_Rng;                              ///< Mersenne Twister RNG.
    std::uniform_real_distribution<float> m_Dist01;  ///< Uniform [0, 1) distribution.

    /// @}

    /**
     * @name Texture Atlas
     * @{
     */

    /**
     * @brief UV region for a particle sprite in the atlas.
     *
     * Stores normalized UV coordinates (0-1) for sampling from the atlas.
     */
    struct AtlasRegion
    {
        glm::vec2 uvMin;  ///< Top-left UV coordinate.
        glm::vec2 uvMax;  ///< Bottom-right UV coordinate.
    };

    /**
     * @brief One packed sprite variant: UV region plus horizontal frame count.
     *
     * frameCount == 1 is a static sprite; > 1 is a horizontal animation strip
     * (e.g. 64x16 = four 16x16 frames) sliced into per-frame sub-UVs at draw
     * time. Which frame plays is decided per type (loop on global time, or
     * mapped onto the particle's lifetime for one-shots).
     */
    struct AtlasSlot
    {
        AtlasRegion region;  ///< UVs of the whole variant sprite (all frames).
        int frameCount{1};   ///< Horizontal frames inside the region.
    };

public:
    /// Max sprite variants per ParticleType (e.g. dust/dust2/dust3/mote).
    static constexpr size_t MAX_PARTICLE_VARIANTS = 4;

private:
    TextureStore* m_Store = nullptr;  ///< Owns the adopted atlas (set in LoadTextures).
    TextureHandle m_AtlasHandle;      ///< Handle to the combined particle atlas in m_Store.

    /// Per-type variant sprites. Only the first m_VariantCounts[type] entries
    /// are valid; BuildAtlas fills them from the per-type asset list.
    AtlasSlot m_AtlasSlots[EnumTraits<ParticleType>::Count][MAX_PARTICLE_VARIANTS];

    /// Valid variant count per type (always >= 1, even before LoadTextures,
    /// so spawn-time variant rolls stay in range in texture-less contexts
    /// such as unit tests).
    uint8_t m_VariantCounts[EnumTraits<ParticleType>::Count];

    bool m_TexturesLoaded;  ///< Whether LoadTextures() succeeded.

    /**
     * @brief Roll a random sprite variant for every particle appended at or
     * after @p firstIndex (called right after a spawn dispatch so weather,
     * zone, ambient, and console spawns all share the same variant mix).
     */
    void AssignSpawnVariants(size_t firstIndex);

    /**
     * @brief Build the texture atlas from individual particle textures.
     *
     * Loads all particle textures (paths resolved through the manifest's
     * "particles" links), packs them into a single atlas, and calculates
     * UV regions for each particle type and variant.
     */
    void BuildAtlas(const ProjectManifest& manifest);

    /// @}

    /**
     * @name Render Batch Data
     * @{
     */

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
        float lifeT;  ///< Normalized age in [0, 1] for life-mapped strip playback.
        bool additive;
        ParticleType type;
        uint8_t variant;  ///< Atlas variant index (bounds-checked at draw).
    };

    std::vector<ParticleRenderData>
        m_NoProjectionBatch;                         ///< Particles rendered without perspective.
    std::vector<ParticleRenderData> m_RegularBatch;  ///< Particles rendered with perspective.

    bool m_RenderEnabled = true;  ///< When false, Render() early-outs and reports zero drawn.
    size_t m_LastDrawnCount = 0;  ///< Drawn count from the most recent Render() pass.

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
