#pragma once

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <vector>

#include "AmbienceConfig.hpp"
#include "IRenderer.hpp"
#include "Texture.hpp"
#include "TextureHandle.hpp"
#include "TextureStore.hpp"

class TimeManager;

/**
 * @struct Star
 * @brief Represents a single star in the night sky with twinkling animation.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Stars are positioned in normalized sky-space coordinates (0-1) and rendered
 * at screen-space positions. Each star has independent twinkle animation
 * controlled by phase and speed parameters.
 *
 * @par Twinkle Animation
 * Brightness oscillates using a sine wave:
 * @f[
 * brightness = baseBrightness \times (0.5 + 0.5 \times \sin(time \times twinkleSpeed +
 * twinklePhase))
 * @f]
 *
 * @par Color Variation
 * Stars have subtle color tints to simulate different star temperatures:
 * - Blue-white: Hot stars (O/B class)
 * - White: Sun-like (G class)
 * - Yellow/Orange: Cooler stars (K/M class)
 */
struct Star
{
    glm::vec2 position;    ///< Normalized position (0-1) in sky space, mapped to screen on render
    float baseBrightness;  ///< Base brightness (0-1), modulated by twinkle animation
    float twinklePhase;    ///< Phase offset for twinkle sine wave (radians)
    float twinkleSpeed;    ///< Twinkle frequency multiplier (higher = faster flicker)
    float size;            ///< Size multiplier applied to base star texture size
    glm::vec3 color;       ///< RGB color tint (typically near white with subtle hue)
};

/**
 * @struct LightRay
 * @brief Represents a single light ray emanating from the sun or moon.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Light rays create a "god rays" effect radiating outward from the light source.
 * Each ray has its own angle, length, and animation phase for organic movement.
 *
 * @par Ray Geometry
 * Rays are rendered as elongated textured quads with soft falloff:
 * @code
 *        Light Source
 *             *
 *            /|\
 *           / | \
 *          /  |  \    <- Individual rays at different angles
 *         /   |   \
 *        /    |    \
 * @endcode
 *
 * @par Animation
 * Ray brightness pulses subtly using phase offset to prevent uniform appearance.
 */
struct LightRay
{
    float xPosition;     ///< Normalized X position (0-1) relative to light source spread
    float originOffset;  ///< Horizontal offset from sun center (-1 to 1, scaled by SUN_BAND_WIDTH)
    float angle;         ///< Angle in radians from vertical (0 = straight down)
    float length;        ///< Ray length multiplier (1.0 = MAX_RAY_LENGTH pixels)
    float width;         ///< Ray width in pixels
    float brightness;    ///< Base brightness (0-1), modulated by time-of-day
    float phase;         ///< Animation phase offset for pulsing effect
};

/**
 * @struct ShootingStar
 * @brief Represents an animated shooting star (meteor) streaking across the sky.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Shooting stars spawn randomly during night hours and travel in a straight line
 * until their lifetime expires. They fade in at spawn and fade out at death.
 *
 * @par Lifecycle
 * @code
 * Spawn (top of screen)
 *        \
 *         \  <- Trail rendered behind
 *          \
 *           * (current position)
 *            \
 *             (fades out)
 * @endcode
 *
 * @par Brightness Curve
 * Uses a parabolic fade: brightest at midpoint of lifetime, fading at both ends.
 */
struct ShootingStar
{
    glm::vec2 position;  ///< Current screen-space position in pixels
    glm::vec2 velocity;  ///< Movement vector (pixels per second)
    float lifetime;      ///< Remaining lifetime in seconds
    float maxLifetime;   ///< Total lifetime for fade calculations
    float brightness;    ///< Peak brightness at lifetime midpoint
    float length;        ///< Trail length in pixels (stretched behind velocity)
};

/**
 * @struct DewSparkle
 * @brief Represents a glinting dew drop catching early morning sunlight.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Dew sparkles appear during dawn/morning hours in the lower portion of the
 * screen, simulating sunlight catching morning dew on grass and foliage.
 *
 * @par Sparkle Animation
 * Each sparkle twinkles independently with a sharper, more "glint-like"
 * animation compared to stars (using abs(sin) for sharp peaks).
 */
struct DewSparkle
{
    glm::vec2 position;  ///< Normalized position (0-1), biased to lower screen
    float phase;         ///< Animation phase offset for twinkle timing
    float brightness;    ///< Base brightness (0-1)
    float speed;         ///< Twinkle animation speed multiplier
};

/**
 * @struct VisibleStar
 * @brief Cached per-frame screen data for one rendered foreground star.
 * @ingroup Effects
 *
 * Populated during the compute pass in SkyRenderer::RenderStars and consumed
 * by two emit passes (glow, then core) so the sprite batch doesn't alternate
 * between m_StarGlowTexture and m_StarTexture once per star.
 */
struct VisibleStar
{
    glm::vec2 screenPos;  ///< Camera-relative screen position (top-left of glow/core)
    glm::vec3 color;      ///< Star tint
    float size;           ///< Core sprite size in pixels
    float brightness;     ///< Core alpha multiplier (pre-additive)
    float glowSize;       ///< Glow sprite size in pixels; <=0 means skip glow this frame
    float glowAlpha;      ///< Glow alpha multiplier (pre-additive)
};

/**
 * @class SkyRenderer
 * @brief Renders atmospheric sky effects synchronized with the day/night cycle.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * The SkyRenderer creates an immersive sky atmosphere by rendering multiple
 * layered effects that respond to the current time of day. All effects are
 * rendered as screen-space overlays on top of the game world.
 *
 * @par Features
 * | Effect            | Time Active     | Description                          |
 * |-------------------|-----------------|--------------------------------------|
 * | Stars             | Night/Dusk/Dawn | Twinkling stars with color variation |
 * | Background Stars  | Night           | Dimmer distant star field            |
 * | Shooting Stars    | Night           | Random meteor streaks                |
 * | Sun Rays          | Day             | God rays from sun position           |
 * | Moon Rays         | Night           | Softer rays from moon                |
 * | Atmospheric Glow  | Night           | Subtle horizon/upper-sky wash        |
 * | Dawn Gradient     | Dawn            | Purple-to-orange sky gradient        |
 * | Dawn Horizon Glow | Dawn            | Warm glow at horizon                 |
 * | Dew Sparkles      | Morning         | Glinting ground-level sparkles       |
 *
 * @par Time Integration
 * Effects are driven by the TimeManager which provides:
 * - `GetSunArc()`: Sun position (0 at sunrise, 0.5 at noon, 1.0 at sunset)
 * - `GetMoonArc()`: Moon position for night sky
 * - `GetStarVisibility()`: Fade factor for stars (1.0 at night, 0.0 at day)
 * - `GetTimePeriod()`: Discrete time periods (Dawn, Morning, Day, etc.)
 *
 * @par Render Order
 * Effects are rendered in this order (back to front):
 * 1. Dawn gradient (full-screen color overlay)
 * 2. Dawn horizon glow (bottom of screen)
 * 3. Night atmospheric glow (horizon + subtle upper-sky shimmer)
 * 4. Background stars (dim, distant)
 * 5. Foreground stars (bright, twinkling)
 * 6. Shooting stars (with trails)
 * 7. Dew sparkles (morning only)
 * 8. Sun/Moon rays (god rays effect)
 *
 * @par Procedural Textures
 * All textures are generated procedurally at initialization:
 * - Star texture: Soft circular gradient with glow
 * - Ray texture: Vertical gradient for light rays
 * - Glow texture: Large soft radial gradient
 *
 * @par Usage
 * @code
 * SkyRenderer sky;
 * sky.Initialize();  // Generate textures and populate star/ray arrays
 *
 * // In game loop:
 * sky.Update(deltaTime, timeManager);
 * sky.Render(renderer, timeManager, screenWidth, screenHeight);
 * @endcode
 *
 * @see TimeManager, IRenderer
 */
class SkyRenderer
{
public:
    /**
     * @brief Construct a new SkyRenderer with default state.
     *
     * Does not allocate GPU resources; call Initialize() separately.
     */
    SkyRenderer();
    ~SkyRenderer();

    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;
    SkyRenderer(SkyRenderer&&) noexcept = default;
    SkyRenderer& operator=(SkyRenderer&&) noexcept = default;

    /**
     * @brief Initialize all sky rendering resources.
     *
     * Generates procedural textures and populates star/ray arrays.
     * Must be called before Render().
     *
     * @par Initialization Steps
     * 1. Generate ray texture (soft vertical gradient)
     * 2.
     * Generate star textures (point + glow)
     * 3. Generate shooting star texture (elongated
     * streak)
     * 4. Generate atmospheric glow, glow pool, light-pool, and aurora textures
     * 5. Populate star arrays with random positions/properties
     * 6. Populate light ray arrays for
     * sun and moon
     * 7. Generate dew sparkle positions
     *
     * Safe to call more than
     * once; generated resources are replaced by the
     * current run's textures and arrays.
     *
     * @param store TextureStore that adopts the generated sky textures; its
     *              UploadAll re-uploads them on a renderer switch.
     */
    void Initialize(TextureStore& store);

    /**
     * @brief Bind each sky texture to a region of a shared atlas.
     *
     * When bound, the per-element sky draws read from @p atlasTex with
     * UVs computed from each region's pixel offset, instead of binding
     * the per-element textures individually. This collapses the sky's
     * 1-5 separate-texture flushes into the same batch as the tiles.
     *
     * Pass @c atlasTex = nullptr to revert to per-element textures.
     *
     * @param atlasTex Shared atlas texture (typically the tile atlas).
     * @param rayOffset Pixel offset of @ref m_RayTexture within @p atlasTex.
     * @param starOffset Pixel offset of @ref m_StarTexture.
     * @param starGlowOffset Pixel offset of @ref m_StarGlowTexture.
     * @param shootingStarOffset Pixel offset of @ref m_ShootingStarTexture.
     * @param glowOffset Pixel offset of @ref m_GlowTexture.
     * @param lightPoolOffset Pixel offset of @ref m_LightPoolTexture.
     * @param auroraCurtainOffset Pixel offset of @ref m_AuroraCurtainTexture.
     * @param auroraSmallOffset Pixel offset of @ref m_AuroraSmallTexture.
     */
    void SetAtlasBinding(const Texture* atlasTex,
                         glm::vec2 rayOffset,
                         glm::vec2 starOffset,
                         glm::vec2 starGlowOffset,
                         glm::vec2 shootingStarOffset,
                         glm::vec2 glowOffset,
                         glm::vec2 lightPoolOffset,
                         glm::vec2 auroraCurtainOffset,
                         glm::vec2 auroraSmallOffset);

    /**
     * Accessor for textures so callers can register them with an atlas
     * packer that copies pixel data into the atlas image at load time.
     * (GetLightPoolTexture is declared below alongside its original
     * Game::Render call site.)
     */
    const Texture& GetRayTexture() const { return m_Store->Get(m_RayHandle); }
    const Texture& GetStarTexture() const { return m_Store->Get(m_StarHandle); }
    const Texture& GetStarGlowTexture() const { return m_Store->Get(m_StarGlowHandle); }
    const Texture& GetShootingStarTexture() const { return m_Store->Get(m_ShootingStarHandle); }
    const Texture& GetGlowTexture() const { return m_Store->Get(m_GlowHandle); }
    const Texture& GetAuroraCurtainTexture() const { return m_Store->Get(m_AuroraCurtainHandle); }
    const Texture& GetAuroraSmallTexture() const { return m_Store->Get(m_AuroraSmallHandle); }

    /**
     * @brief Update time-based animations.
     *
     * Updates shooting star positions and spawns new ones randomly.
     * Also advances internal time for twinkle animations.
     *
     * @param deltaTime Frame time in seconds.
     * @param time      TimeManager for current time-of-day state.
     */
    void Update(float deltaTime, const TimeManager& time);

    /**
     * @brief Render all sky effects for the current frame.
     *
     * Renders effects in correct order based on current time of day.
     * Sky elements are positioned via layered distant parallax: each
     * category subtracts `cameraPos * SKY_PARALLAX_*` from its sky-frame
     * neutral position, so player movement drifts the sky slowly without
     * making it appear walkable-past.
     *
     * @param renderer     Renderer interface for draw calls.
     * @param time         TimeManager for time-based visibility.
     * @param cameraPos    World-space camera position (top-left of viewport).
     * @param screenWidth  Current screen width in pixels.
     * @param screenHeight Current screen height in pixels.
     */
    void Render(IRenderer& renderer,
                const TimeManager& time,
                glm::vec2 cameraPos,
                int screenWidth,
                int screenHeight);

    /**
     * @brief Get the procedural soft-circle texture used for world light pools.
     *
     * Generated in Initialize() and re-uploaded by UploadTextures(). Used by
     * Game::Render to draw additive light pools at WorldLight positions.
     */
    const Texture& GetLightPoolTexture() const { return m_Store->Get(m_LightPoolHandle); }

    /**
     * @brief Draw the light-pool quad at @p pos, routing through the bound
     *        atlas if @ref SetAtlasBinding has been called so the draw can
     *        batch with other atlas-textured sprites.
     */
    void DrawLightPool(IRenderer& renderer,
                       glm::vec2 pos,
                       glm::vec2 size,
                       float rotation,
                       glm::vec4 color,
                       bool additive);

    /**
     * @brief Render slowly-drifting cloud shadows on the world (multiplicative-style darkening).
     *
     * Drawn AFTER world+particle rendering and BEFORE the screen-space sky
     * overlay, so shadows darken ground tiles + entities but never the sun
     * rays / stars / atmospheric glow that pierce the sky. Disabled when
     * `nightFactor` is high (no shadows at night).
     *
     * @param renderer    Renderer interface (world projection still active).
     * @param cameraPos   World-space camera position (top-left).
     * @param viewSize    Visible world rect in pixels.
     * @param time        Current time, drives drift along the wind direction.
     * @param nightFactor 0=day (full intensity), 1=night (disabled).
     */
    void RenderCloudShadows(IRenderer& renderer,
                            glm::vec2 cameraPos,
                            glm::vec2 viewSize,
                            float time,
                            float nightFactor);

    /**
     * @brief Pure-math helper: world-space cloud shadow position at time `t`.
     *
     * Public for test access. Returns the (x, y) world-space position of the
     * cloud-shadow blob at slot `index` after `t` seconds of drift; the
     * deterministic per-slot phase is folded into the return value.
     *
     * Inlined so tests can link without pulling in the full SkyRenderer.cpp
     * (and its IRenderer/Texture stack) into the test binary.
     */
    static glm::vec2 ComputeCloudShadowPosition(int index, float t, glm::vec2 origin)
    {
        constexpr float kCellSize = 480.0f;
        constexpr float kLoop = 2.0f * kCellSize;

        // Per-slot world-space anchor inside a 2x2 grid.
        const float gridX = static_cast<float>(index % 2) * kCellSize;
        const float gridY = static_cast<float>((index / 2) % 2) * kCellSize;

        // Absolute drifted position: keeps drifting linearly in wind direction.
        const glm::vec2 wind = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);
        const glm::vec2 drift = wind * (ambience::CLOUD_SHADOW_DRIFT_SPEED * t);
        const glm::vec2 absolute = glm::vec2(gridX, gridY) + drift;

        // Fold to the equivalent position (mod kLoop) closest to `origin`. This
        // is what keeps shadows visible around the camera even after long drift,
        // without the discontinuity that a [0, kLoop) wrap creates near zero.
        // std::remainder(x, p) returns a value in (-p/2, p/2] congruent to x mod p.
        auto wrapNearest = [kLoop](float a, float ref)
        { return ref + std::remainder(a - ref, kLoop); };

        return glm::vec2(wrapNearest(absolute.x, origin.x), wrapNearest(absolute.y, origin.y));
    }

private:
    /**
     * @name Texture Generation
     * @brief Procedural texture creation for sky effects.
     * @{
     */

    /**
     * @brief Generate the light ray texture.
     *
     * Creates a vertical gradient texture used for sun/moon rays.
     * Bright at top, fading to transparent at bottom.
     */
    void GenerateRayTexture();

    /**
     * @brief Generate the main star texture.
     *
     * Creates a small soft circular gradient for star rendering.
     */
    void GenerateStarTexture();

    /**
     * @brief Generate the star glow texture.
     *
     * Creates a larger, softer glow rendered behind bright stars.
     */
    void GenerateStarGlowTexture();

    /**
     * @brief Generate the shooting star texture.
     *
     * Creates an elongated streak texture for meteor trails.
     */
    void GenerateShootingStarTexture();

    /// @}

    /**
     * @name Object Generation
     * @brief Populate arrays with randomized sky objects.
     * @{
     */

    /**
     * @brief Generate light ray configurations for sun and moon.
     *
     * Populates m_SunRays and m_MoonRays with randomized angles and properties.
     */
    void GenerateLightRays();

    /**
     * @brief Generate foreground star array.
     *
     * Creates bright, prominent stars with full twinkle animation.
     *
     * @param count Number of stars to generate.
     */
    void GenerateStars(int count);

    /**
     * @brief Generate background star array.
     *
     * Creates dimmer, smaller stars for depth. Less prominent twinkle.
     *
     * @param count Number of background stars to generate.
     */
    void GenerateBackgroundStars(int count);

    /**
     * @brief Generate dew sparkle positions.
     *
     * Creates sparkle points biased toward the lower portion of the screen.
     */
    void GenerateDewSparkles();

    /// @}

    /**
     * @name Shooting Star Management
     * @brief Lifecycle management for meteor effects.
     * @{
     */

    /**
     * @brief Update all active shooting stars.
     *
     * Moves shooting stars along their velocity vectors and removes
     * expired ones. May trigger new spawns based on timer.
     *
     * @param deltaTime    Frame time in seconds.
     * @param screenWidth  Screen width for bounds checking.
     * @param screenHeight Screen height for bounds checking.
     */
    void UpdateShootingStars(float deltaTime, int screenWidth, int screenHeight);

    /**
     * @brief Spawn a new shooting star.
     *
     * Creates a shooting star at a random position along the top/side
     * of the screen with randomized velocity and lifetime.
     *
     * @param screenWidth  Screen width for spawn position calculation.
     * @param screenHeight Screen height for spawn position calculation.
     */
    void SpawnShootingStar(int screenWidth, int screenHeight);

    /// @}

    /**
     * @name Render Functions
     * @brief Individual effect rendering routines.
     * @{
     */

    /**
     * @brief Render all stars (foreground and background).
     *
     * Renders stars with brightness modulated by twinkle animation
     * and overall visibility from TimeManager.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for star visibility factor.
     * @param screenWidth  Screen width for position mapping.
     * @param screenHeight Screen height for position mapping.
     */
    void RenderStars(IRenderer& renderer,
                     const TimeManager& time,
                     glm::vec2 cameraPos,
                     int screenWidth,
                     int screenHeight);

    /**
     * @brief Render active shooting stars with trails.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for visibility.
     * @param cameraPos    World-space camera position for parallax offset.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderShootingStars(IRenderer& renderer,
                             const TimeManager& time,
                             glm::vec2 cameraPos,
                             int screenWidth,
                             int screenHeight);

    /**
     * @brief Render shimmering aurora bands in the upper sky.
     *
     * Active when the current weather is AuroraNight. Bands are drawn with
     * the world projection (no swap), parallax factor SKY_PARALLAX_AURORA.
     */
    void RenderAurora(IRenderer& renderer,
                      const TimeManager& time,
                      glm::vec2 cameraPos,
                      int screenWidth,
                      int screenHeight);

    /**
     * @brief Render subtle nighttime atmospheric glow.
     *
     * Draws a faint horizon wash and occasional top-edge shimmer while
     * star visibility is high.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for night visibility.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderAtmosphericGlow(IRenderer& renderer,
                               const TimeManager& time,
                               int screenWidth,
                               int screenHeight);

    /**
     * @brief Render god rays emanating from the sun.
     *
     * Renders warm-colored rays during day. Intensity based on sun arc
     * (strongest when sun is lower in sky).
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for sun position and intensity.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderSunRays(IRenderer& renderer,
                       const TimeManager& time,
                       glm::vec2 cameraPos,
                       int screenWidth,
                       int screenHeight);

    /**
     * @brief Render softer rays from the moon.
     *
     * Similar to sun rays but cooler color and lower intensity.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for moon position.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderMoonRays(IRenderer& renderer,
                        const TimeManager& time,
                        glm::vec2 cameraPos,
                        int screenWidth,
                        int screenHeight);

    /**
     * Generate a fresh jagged lightning bolt + 0-3 sub-branches into
     * m_LightningBolt, sized to the cached viewport. Called once per
     * flash trigger from Update().
     */
    void GenerateLightningBolt(int screenWidth, int screenHeight);

    /**
     * Draw the previously-generated lightning bolt while m_LightningBoltTimer
     * is positive. Camera-locked (the bolt sits in screen space; world-anchor
     * parallax is a possible future polish).
     */
    void RenderLightningBolt(IRenderer& renderer, int screenWidth, int screenHeight);

    /// @}

    /**
     * @name Morning/Dawn Effects
     * @brief Special effects for sunrise period.
     * @{
     */

    /**
     * @brief Render warm glow at the horizon during dawn.
     *
     * Creates an orange/pink gradient at the bottom of the screen
     * simulating the pre-sunrise glow.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for dawn intensity.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderDawnHorizonGlow(IRenderer& renderer,
                               const TimeManager& time,
                               int screenWidth,
                               int screenHeight);

    /**
     * @brief Render vertical dawn gradient overlay.
     *
     * Full-screen gradient from purple (top) to orange (bottom)
     * during dawn transition.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for dawn intensity.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderDawnGradient(IRenderer& renderer,
                            const TimeManager& time,
                            int screenWidth,
                            int screenHeight);

    /**
     * @brief Render morning dew sparkle effects.
     *
     * Small bright glints in the lower screen area during morning hours.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for morning visibility.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderDewSparkles(IRenderer& renderer,
                           const TimeManager& time,
                           int screenWidth,
                           int screenHeight);

    /// @}

    /**
     * @name Utility Functions
     * @{
     */

    /**
     * @brief Calculate screen position of sun or moon.
     *
     * Maps the arc value (0-1) to a screen position along a curved path.
     *
     * @param arc          Sun/moon arc value (0 = horizon, 0.5 = zenith).
     * @param screenWidth  Screen width for position calculation.
     * @param screenHeight Screen height for position calculation.
     * @return Screen-space position of light source.
     */
    glm::vec2 GetLightSourcePosition(float arc,
                                     int screenWidth,
                                     int screenHeight,
                                     glm::vec2 cameraPos,
                                     float parallaxFactor) const;

    /// @}

    /**
     * @name Procedural Textures
     * @brief GPU textures generated at initialization.
     * @{
     */
    /**
     * Sky textures live in this store (set in Initialize); UploadAll re-uploads
     * them on a renderer switch, replacing the old IGpuResourceOwner hook.
     */
    TextureStore* m_Store = nullptr;
    TextureHandle m_RayHandle;            ///< Vertical gradient for light rays
    TextureHandle m_StarHandle;           ///< Small soft circle for stars
    TextureHandle m_StarGlowHandle;       ///< Larger glow behind bright stars
    TextureHandle m_ShootingStarHandle;   ///< Elongated streak for meteors
    TextureHandle m_GlowHandle;           ///< Large soft glow for atmosphere
    TextureHandle m_LightPoolHandle;      ///< Soft circle for WorldLight pools
    TextureHandle m_AuroraCurtainHandle;  ///< Vertical streaked curtain for aurora bands
    TextureHandle m_AuroraBeamHandle;     ///< Vertical oval ray/beam for aurora beams
    TextureHandle m_AuroraSmallHandle;    ///< Procedural soft dot for aurora wisps

    /**
     * Atlas binding: when @ref m_AtlasTexture is non-null, sky draws sample
     * from the atlas at the per-element pixel offsets recorded below. The
     * pixel size of each region is read from the corresponding @c m_*Texture
     * at draw time, so resize-safe.
     */
    const Texture* m_AtlasTexture{nullptr};
    glm::vec2 m_RayAtlasOffset{0.0f};
    glm::vec2 m_StarAtlasOffset{0.0f};
    glm::vec2 m_StarGlowAtlasOffset{0.0f};
    glm::vec2 m_ShootingStarAtlasOffset{0.0f};
    glm::vec2 m_GlowAtlasOffset{0.0f};
    glm::vec2 m_LightPoolAtlasOffset{0.0f};
    glm::vec2 m_AuroraCurtainAtlasOffset{0.0f};
    glm::vec2 m_AuroraSmallAtlasOffset{0.0f};
    /// @}

    /**
     * @name Sky Object Arrays
     * @brief Collections of sky elements to render.
     * @{
     */
    std::vector<Star> m_Stars;                       ///< Foreground stars (bright, prominent)
    std::vector<Star> m_BackgroundStars;             ///< Background stars (dim, distant)
    std::vector<VisibleStar> m_VisibleStarsScratch;  ///< Per-frame scratch for two-pass star emit
    std::vector<LightRay> m_SunRays;                 ///< Sun god ray configurations
    std::vector<LightRay> m_MoonRays;                ///< Moon ray configurations
    std::vector<ShootingStar> m_ShootingStars;       ///< Active shooting stars
    std::vector<DewSparkle> m_DewSparkles;           ///< Morning dew sparkle points
    /// @}

    /**
     * @name Animation State
     * @brief Time tracking for animations.
     * @{
     */
    double m_Time;              ///< Accumulated time for twinkle animations (seconds)
    float m_ShootingStarTimer;  ///< Countdown to next shooting star spawn
    float m_LastScreenWidth;    ///< Cached screen width for resize detection
    float m_LastScreenHeight;   ///< Cached screen height for resize detection
    /// @}

    /**
     * @name Weather-driven sky state
     * @brief Cached weather data refreshed each Update.
     * @{
     */
    float m_LightningTimer{0.0f};        ///< Countdown to next lightning flash (s).
    float m_LightningFlashTimer{0.0f};   ///< Remaining flash visibility (s).
    bool m_AuroraVisible{false};         ///< True when current weather wants aurora.
    float m_AuroraFade{1.0f};            ///< Master aurora alpha (transition fade).
    float m_CelestialFade{1.0f};         ///< Sun/moon ray alpha (transition fade).
    float m_MeteorRateMultiplier{1.0f};  ///< Shooting-star spawn rate multiplier.

    /**
     * Procedurally generated lightning bolt path, regenerated each flash.
     * Coordinates are screen-space; the bolt is drawn camera-locked (no
     * parallax) for the brief duration of the strike.
     */
    struct LightningBolt
    {
        std::vector<glm::vec2> mainPath;               ///< Top-to-bottom jagged polyline.
        std::vector<std::vector<glm::vec2>> branches;  ///< 0-3 shorter sub-bolts off main.
    };
    LightningBolt m_LightningBolt;
    float m_LightningBoltTimer{
        0.0f};  ///< Remaining bolt visibility (s); slightly longer than the flash.
    /// @}

    /**
     * @name Texture Size Constants
     * @brief Dimensions for procedurally generated textures.
     * @{
     */
    static constexpr int RAY_TEXTURE_WIDTH = 64;        ///< Ray texture width (narrow)
    static constexpr int RAY_TEXTURE_HEIGHT = 512;      ///< Ray texture height (tall for length)
    static constexpr int STAR_TEXTURE_SIZE = 64;        ///< Star point texture size
    static constexpr int STAR_GLOW_TEXTURE_SIZE = 128;  ///< Star glow texture size
    static constexpr int GLOW_TEXTURE_SIZE = 256;       ///< Atmospheric glow texture size
    /// @}

    /**
     * @name Rendering Constants
     * @brief Configuration for effect counts and sizes.
     * @{
     */
    // Star arrays cover a star-field 3 viewports wide x 2 tall (the wrap
    // window in RenderStars). Counts are scaled up so per-viewport density
    // stays roughly the same as the original 600/400 single-viewport layout.
    static constexpr int STAR_COUNT = 1800;             ///< Number of foreground stars
    static constexpr int BACKGROUND_STAR_COUNT = 1200;  ///< Number of background stars
    static constexpr int SUN_RAY_COUNT = 3;   ///< Number of sun rays (spread across ~2/3 of screen)
    static constexpr int MOON_RAY_COUNT = 3;  ///< Number of moon rays (very subtle)
    static constexpr int DEW_SPARKLE_COUNT = 4;       ///< Number of dew sparkles
    static constexpr float MAX_RAY_LENGTH = 1200.0f;  ///< Maximum ray length in pixels
    static constexpr float RAY_WIDTH = 80.0f;         ///< Base ray width in pixels
    static constexpr float SUN_RAY_SPREAD =
        120.0f;  ///< Total fan spread angle in degrees (~2/3 screen)
    static constexpr float SUN_BAND_WIDTH =
        0.35f;  ///< Width of sun origin band (fraction of screen width)

    /**
     * @name World Parallax (full world anchoring)
     * @brief Per-element fraction of camera movement applied to sky positions.
     *
     * 0.0 = locked to screen, 1.0 = locked to world. Sky elements are now
     * anchored to world coordinates so they cover the entire map and walk
     * past as the player moves, instead of feeling like a screen overlay.
     * Stars and shooting stars use a wrap-around field (see RenderStars)
     * so they always remain visible in the upper sky regardless of where
     * the camera is.
     * @{
     */
    static constexpr float SKY_PARALLAX_STARS_BG = 1.0f;  ///< Background stars (world)
    static constexpr float SKY_PARALLAX_STARS_FG = 1.0f;  ///< Foreground stars (world)
    static constexpr float SKY_PARALLAX_SUN = 1.0f;       ///< Sun + sun rays (world)
    static constexpr float SKY_PARALLAX_MOON = 1.0f;      ///< Moon + moon rays (world)
    static constexpr float SKY_PARALLAX_AURORA = 1.0f;    ///< Aurora bands (world)

    /**
     * Width of the star-field tile (in viewports). Stars wrap around the
     * camera modulo this period so the whole map gets stars without
     * requiring a per-tile generation pass.
     */
    static constexpr float STAR_FIELD_X_PERIODS = 3.0f;
    static constexpr float STAR_FIELD_Y_PERIODS = 2.0f;
    /// @}
    /// @}

    /// @brief Generate the procedural soft-circle texture for WorldLights.
    void GenerateLightPoolTexture();

    /// @brief Generate the procedural aurora curtain texture (vertical streaks).
    void GenerateAuroraCurtainTexture();

    /// @brief Generate the procedural aurora beam texture (vertical oval ray).
    void GenerateAuroraBeamTexture();

    bool m_Initialized;  ///< True after Initialize() completes successfully
    std::mt19937 m_Rng;  ///< Shared RNG for all procedural generation
};
