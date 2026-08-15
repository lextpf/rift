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
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Effects
 *
 * Stars are positioned in normalized sky-space coordinates (0-1) and rendered
 * at screen-space positions. Each star has independent twinkle animation
 * controlled by phase and speed parameters.
 *
 * @par Twinkle animation
 * The two star layers use different envelopes. SkyRenderer::RenderStars first
 * dims the incoming star visibility, then each layer applies its own factor -
 * the drawn alpha is well below `baseBrightness * twinkle * visibility`:
 * @verbatim
 *   visibility = TimeManager::GetStarVisibility() * 0.35
 *   b          = baseBrightness * twinkle * visibility
 *   background alpha      = b * 0.3
 *   foreground core alpha = b * 0.7
 *   foreground glow alpha = (b - 0.25) * 0.1
 * @endverbatim
 *
 * Background stars (SkyRenderer::RenderStars, first pass) use one sine at 1.5x
 * the star's speed:
 * @f[
 * twinkle = 0.6 + 0.4\sin(t \cdot speed \cdot 1.5 + phase)
 * @f]
 *
 * Foreground stars use three incommensurate sines plus a sparkle burst, so the
 * pattern does not repeat and bright flares are brief:
 * @f[
 * t_1 = \sin(t \cdot speed \cdot 1.2 + phase),\quad
 * t_2 = \sin(t \cdot speed \cdot 2.7 + 1.3\,phase),\quad
 * t_3 = \sin(t \cdot speed \cdot 0.5 + 2.1\,phase)
 * @f]
 * @f[
 * twinkle = 0.4 + 0.35\,t_1 + 0.15\,t_3 + 0.25\max(0,\ t_1 t_2)
 * @f]
 * That same `max(0, t1*t2)` sparkle term also gates the per-star glow sprite:
 * a glow is emitted only when brightness &gt; 0.25 and sparkle &gt; 0.3.
 *
 * @par Color variation
 * Stars have subtle color tints to simulate different star temperatures:
 * - Blue-white: Hot stars (O/B class)
 * - White: Sun-like (G class)
 * - Yellow/Orange: Cooler stars (K/M class)
 */
struct Star
{
    /**
     * @brief Normalized (0-1) position inside the star-field tile, not the screen.
     *
     * RenderStars scales it by the tile size (3 viewports wide x 2 tall) and wraps
     * the result near the camera, so one array covers the whole map.
     */
    glm::vec2 position;
    float baseBrightness;  ///< Base brightness (0-1), modulated by twinkle animation.
    float twinklePhase;    ///< Phase offset for twinkle sine wave (radians).
    float twinkleSpeed;    ///< Twinkle frequency multiplier (higher = faster flicker).
    /**
     * @brief Size input in [0, 1].
     *
     * Background sprite = `1.0 + size * 1.2` px; the foreground core =
     * `(1.5 + size * 3.0) * (0.5 + brightness * 0.5)` px and its glow =
     * `6.0 + size * 8.0` px.
     */
    float size;
    glm::vec3 color;  ///< RGB color tint (typically near white with subtle hue).
};

/**
 * @struct LightRay
 * @brief Represents a single light ray emanating from the sun or moon.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Effects
 *
 * Light rays create a "god rays" effect radiating outward from the light source.
 * Each ray has its own angle, length, and animation phase for organic movement.
 *
 * @par Ray geometry
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
 *
 * @par Field units
 * Every field is a normalized shaping input, not a pixel or radian value; the
 * pixel geometry is derived per frame. Sun rays (RenderSunRays) resolve them
 * as:
 * @verbatim
 *   rayAngleDeg = (xPosition - 0.5) * SUN_RAY_SPREAD + angle * 10   [degrees]
 *   rayWidth    = 50 + width * RAY_WIDTH                            [pixels]
 *   rayLength   = screenHeight * (0.5 + length * 0.4)               [pixels]
 *   originPx    = originOffset * (screenWidth * SUN_BAND_WIDTH * 0.5)
 * @endverbatim
 * Moon rays (RenderMoonRays) use the same shape with their own constants: a
 * 60-degree spread, `angle * 8`, `50 + width * 70`, and
 * `screenHeight * (0.35 + length * 0.45)`.
 */
struct LightRay
{
    /// Fan slot in [0.05, 0.95]. Selects the ray's ANGLE within the fan, not a
    /// position: `(xPosition - 0.5) * SUN_RAY_SPREAD` degrees off vertical.
    float xPosition;
    float originOffset;  ///< Horizontal offset from sun center (-1 to 1, scaled by SUN_BAND_WIDTH).
    /**
     * @brief Dimensionless angle jitter, generated in [-0.15, 0.15].
     *
     * Added to the fan angle as `angle * 10` DEGREES (sun) or `angle * 8` (moon),
     * i.e. roughly +/-1.5 degrees of wobble - despite the name it is not radians.
     */
    float angle;
    /// Length input, 0.45-0.90 (sun) / 0.30-0.70 (moon). Scales screen height,
    /// not an absolute pixel count: sun rays span 68-86% of screen height.
    float length;
    /// Width input, 0.7-1.2. A multiplier on top of a fixed 50 px base, so the
    /// drawn ray is 106-146 px wide for the sun. Not a pixel width itself.
    float width;
    float brightness;  ///< Base brightness (0-1), modulated by time-of-day.
    float phase;       ///< Animation phase offset for pulsing effect.
};

/**
 * @struct ShootingStar
 * @brief Represents an animated shooting star (meteor) streaking across the sky.
 * @author Fable 5 (https://github.com/claude)
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
 * @par Brightness curve
 * A trapezoid, not a parabola: two independent linear clamps multiply, so the
 * streak reaches full brightness after 0.08 s, holds a plateau, then falls
 * over the last 0.12 s. Lifetimes are 0.3-0.7 s (0.55-0.95 s during a meteor
 * shower), so short streaks may never reach the plateau.
 * @verbatim
 *   fadeIn  = min(1, (maxLifetime - lifetime) / 0.08)
 *   fadeOut = min(1, lifetime / 0.12)
 *   alpha   = brightness * fadeIn * fadeOut * starVisibility
 * @endverbatim
 */
struct ShootingStar
{
    /**
     * @brief world position inside the star-field tile (see SkyRenderer::RenderStars),
     *        in pixels - not a screen position.
     *
     * RenderShootingStars wraps it near the camera before drawing, so a streak may
     * cross a wrap seam mid-flight.
     */
    glm::vec2 position;
    glm::vec2 velocity;  ///< Movement vector (pixels per second).
    float lifetime;      ///< Remaining lifetime in seconds.
    float maxLifetime;   ///< Total lifetime for fade calculations.
    float brightness;    ///< Peak alpha reached on the plateau (see the curve above).
    float length;        ///< Trail length in pixels (stretched behind velocity).
};

/**
 * @struct DewSparkle
 * @brief Represents a glinting dew drop catching early morning sunlight.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Effects
 *
 * Dew sparkles appear during dawn/morning hours in the lower portion of the
 * screen, simulating sunlight catching morning dew on grass and foliage.
 *
 * @par Sparkle animation
 * Each sparkle twinkles independently with a sharper, more "glint-like"
 * animation than a star: everything below 0.5 of a sine is clipped away, the
 * remaining top half is rescaled to [0, 1] and squared. A sparkle therefore
 * rests fully dark for most of its cycle and glints once per period - not the
 * two peaks per period an abs(sin) envelope would give.
 * @f[
 * twinkle = \left(\frac{\max(0,\ \sin(t \cdot speed + phase) - 0.5)}{0.5}\right)^2
 * @f]
 */
struct DewSparkle
{
    glm::vec2 position;  ///< Normalized position (0-1), biased to lower screen.
    float phase;         ///< Animation phase offset for twinkle timing.
    float brightness;    ///< Base brightness (0-1).
    float speed;         ///< Twinkle animation speed multiplier.
};

/**
 * @struct VisibleStar
 * @brief Cached per-frame screen data for one rendered foreground star.
 * @ingroup Effects
 *
 * Populated during the compute pass in SkyRenderer::RenderStars and consumed
 * by two emit passes (glow, then core) so the sprite batch doesn't alternate
 * between the star-glow and star textures once per star. Both emits are
 * additive, so splitting them does not change the rendered result.
 */
struct VisibleStar
{
    glm::vec2 screenPos;  ///< Camera-relative screen position (top-left of glow/core).
    glm::vec3 color;      ///< Star tint.
    float size;           ///< Core sprite size in pixels.
    float brightness;     ///< Core alpha multiplier (pre-additive).
    float glowSize;       ///< Glow sprite size in pixels; <=0 means skip glow this frame.
    float glowAlpha;      ///< Glow alpha multiplier (pre-additive).
};

/**
 * @class SkyRenderer
 * @brief Renders atmospheric sky effects synchronized with the day/night cycle.
 * @author Fable 5 (https://github.com/claude)
 * @ingroup Effects
 *
 * The SkyRenderer creates an immersive sky atmosphere by rendering multiple
 * layered effects that respond to the current time of day. Most effects are
 * world-anchored and handed to the renderer camera-relative (see @ref Render);
 * only the full-screen washes - dawn gradient and glow, atmospheric glow,
 * lightning flash and bolt, dew sparkles - are true screen-space overlays.
 *
 * @par Features
 * "Time active" is the gate in Render(); weather can move it, because star
 * visibility and the celestial/aurora fades are weather-resolved.
 * | Effect            | Gate                        | Description                     |
 * |-------------------|-----------------------------|---------------------------------|
 * | Dawn Gradient     | dawnIntensity &gt; 0.01     | Purple-to-orange sky gradient   |
 * | Dawn Horizon Glow | dawnIntensity &gt; 0.01     | Warm glow at horizon            |
 * | Atmospheric Glow  | starVisibility &gt;= 0.2    | Horizon/upper-sky wash          |
 * | Aurora            | auroraFade &gt; 0.01        | Shimmering upper-sky bands      |
 * | Stars             | starVisibility &gt; 0.01    | Twinkling stars, color variety  |
 * | Background Stars  | starVisibility &gt; 0.01    | Dimmer distant star field       |
 * | Shooting Stars    | starVisibility &gt; 0.3     | Random meteor streaks           |
 * | Dew Sparkles      | sunArc in [0, 0.25)         | Glinting ground-level sparkles  |
 * | Sun Rays          | sun up + celestialFade      | God rays from sun position      |
 * | Moon Rays         | moon up + night + celestial | Softer rays from moon           |
 * | Lightning Flash   | weather lightning interval  | Brief cool-white full-screen    |
 * | Lightning Bolt    | same frame as each flash    | Jagged screen-space polyline    |
 *
 * Atmospheric Glow is listed at its effective gate: @ref Render tests 0.1, but
 * @ref RenderAtmosphericGlow returns again below 0.2, so nothing is drawn in
 * the 0.1-0.2 band. The lightning bolt is generated and armed in the same frame
 * as the flash and merely outlives it (0.18 s bolt against 0.08 s flash); no
 * delay is inserted.
 *
 * @par Time integration
 * Effects are driven by the TimeManager which provides:
 * - `GetSunArc()`: Sun position (0 at sunrise, 0.5 at noon, 1.0 at sunset)
 * - `GetMoonArc()`: Moon position for night sky
 * - `GetStarVisibility()`: Fade factor for stars. Weather-resolved, not a pure
 *   clock value - a meteor shower can force it to 1.0 at noon and heavy
 *   precipitation to 0.0 at midnight, which moves every gate that reads it
 *   (stars, shooting stars, atmospheric glow, moon rays)
 * - `GetDawnIntensity()`: Dawn gradient and horizon glow strength
 * - `GetMoonPhase()`: Moon-ray brightness (full 1.0, new floored at 0.3)
 * - `GetSunColor()` / `GetSkyColor()`: Ray and aurora tinting
 * - `GetEffectiveWeatherDefinition()`, `GetCelestialFade()`,
 *   `GetAuroraFade()`, `GetEffectiveMeteorRate()`: weather-transition ramps,
 *   all cached once per frame in @ref Update
 *
 * `GetTimePeriod()` is deliberately not used - every gate here is continuous,
 * so a discrete period would snap.
 *
 * @par Render order
 * Effects are rendered back to front, matching the body of @ref Render.
 * 1. Dawn gradient (full-screen color overlay)
 * 2. Dawn horizon glow (bottom of screen)
 * 3. Night atmospheric glow (horizon + subtle upper-sky shimmer)
 * 4. Aurora bands (behind the stars, world-projected)
 * 5. Background stars (dim, distant), then foreground stars (bright)
 * 6. Shooting stars (with trails)
 * 7. Dew sparkles (morning only)
 * 8. Sun rays, then moon rays (god rays effect)
 * 9. Lightning flash (full-screen cool-white wash)
 * 10. Lightning bolt (jagged polyline, drawn on top of the flash)
 *
 * @par Procedural textures
 * Every sky sprite but one is generated at initialization and adopted by the
 * TextureStore, which owns them and re-uploads them on a renderer switch:
 * - Star texture: Soft circular gradient, plus a larger star-glow variant
 * - Ray texture: Vertical gradient for sun/moon light rays
 * - Shooting-star texture: Elongated streak
 * - Glow texture: Large soft radial gradient (atmosphere, lightning flash)
 * - Light-pool texture: Soft circle for WorldLight pools
 * - Aurora curtain and beam textures (see AuroraTextures)
 *
 * The only loaded asset is the hand-painted aurora mote, whose path the
 * caller resolves from the project manifest; missing or unlinked leaves an
 * empty texture that renders as a colored rect.
 *
 * @par Usage
 * @code
 * SkyRenderer sky;
 * sky.Initialize(textureStore);  // Generate textures, populate star/ray arrays
 *
 * // In game loop:
 * sky.Update(deltaTime, timeManager);
 * sky.Render(renderer, timeManager, cameraPos, screenWidth, screenHeight);
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
     * @par Initialization steps
     * 1. Generate ray texture (soft vertical gradient)
     * 2. Generate star textures (point + glow)
     * 3. Generate shooting star texture (elongated streak)
     * 4. Populate light ray, star, background-star and dew-sparkle arrays
     * 5. Generate the atmospheric glow, light-pool, and aurora textures
     * 6. Load the hand-painted aurora mote (or leave an empty texture)
     *
     * @note Idempotent by early-out, not by regeneration: a second call while
     *       already initialized returns immediately and keeps the first run's
     *       textures, arrays, and @p auroraSpritePath. Nothing here reseeds the
     *       RNG or re-reads the manifest.
     *
     * @param store TextureStore that adopts the generated sky textures; its
     *              UploadAll re-uploads them on a renderer switch.
     * @param auroraSpritePath Hand-painted aurora mote sprite for the sky-wisp
     *              layer, resolved by the caller from the project manifest's
     *              "particles" links. Empty or missing falls back to a
     *              colored rect.
     */
    void Initialize(TextureStore& store, const std::string& auroraSpritePath = std::string());

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
     * Each offset is the top-left pixel coordinate of that sky texture's copy
     * inside @p atlasTex. The region size is not passed: it is read back from
     * the corresponding standalone texture at draw time, so the binding stays
     * valid if a texture is regenerated at a different size. Two aurora draws
     * always bind their own texture instead: the beam (it has no atlas slot)
     * and the ribbon halo (it draws the glow texture standalone even though
     * the glow has a slot). Both still cost a texture flush with an atlas
     * bound.
     *
     * @param atlasTex Shared atlas texture (typically the tile atlas).
     * @param rayOffset Offset of the light-ray texture (@ref m_RayHandle).
     * @param starOffset Offset of the star point texture (@ref m_StarHandle).
     * @param starGlowOffset Offset of the star glow (@ref m_StarGlowHandle).
     * @param shootingStarOffset Offset of the meteor streak
     *                           (@ref m_ShootingStarHandle).
     * @param glowOffset Offset of the atmospheric glow (@ref m_GlowHandle).
     * @param lightPoolOffset Offset of the WorldLight pool
     *                        (@ref m_LightPoolHandle).
     * @param auroraCurtainOffset Offset of the aurora curtain
     *                            (@ref m_AuroraCurtainHandle).
     * @param auroraSmallOffset Offset of the aurora mote
     *                          (@ref m_AuroraSmallHandle).
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
     * @name Sky texture accessors
     * @brief Expose the generated sky textures for atlas packing.
     *
     * Callers register them with an atlas packer that copies pixel data into
     * the atlas image at load time. @ref GetLightPoolTexture is declared below,
     * alongside its original Game::Render call site.
     *
     * @pre @ref Initialize has been called. Each accessor dereferences the
     *      stored TextureStore, which is null until then.
     * @{
     */
    const Texture& GetRayTexture() const { return m_Store->Get(m_RayHandle); }
    const Texture& GetStarTexture() const { return m_Store->Get(m_StarHandle); }
    const Texture& GetStarGlowTexture() const { return m_Store->Get(m_StarGlowHandle); }
    const Texture& GetShootingStarTexture() const { return m_Store->Get(m_ShootingStarHandle); }
    const Texture& GetGlowTexture() const { return m_Store->Get(m_GlowHandle); }
    const Texture& GetAuroraCurtainTexture() const { return m_Store->Get(m_AuroraCurtainHandle); }
    const Texture& GetAuroraSmallTexture() const { return m_Store->Get(m_AuroraSmallHandle); }
    /// @}

    /**
     * @brief Update time-based animations and cache the frame's weather state.
     *
     * Caches the weather-resolved aurora fade, celestial fade and meteor rate
     * that @ref Render gates on, runs the lightning countdown (generating a
     * fresh bolt on each flash), advances shooting stars and spawns new ones,
     * and advances internal time for twinkle animations.
     *
     * @note Both the meteor spawner and the bolt generator use the viewport
     *       cached by the previous @ref Render call. Before the first Render
     *       the spawner is skipped and a bolt would be generated at 1x1.
     *
     * @param deltaTime Frame time in seconds.
     * @param time      TimeManager for current time-of-day state.
     */
    void Update(float deltaTime, const TimeManager& time);

    /**
     * @brief Render all sky effects for the current frame.
     *
     * Renders effects in correct order based on current time of day.
     * Sky elements are world-anchored, not drifted by a parallax fraction.
     * Two schemes:
     * - Stars, shooting stars and aurora live in world space and subtract the
     *   full camera position. Stars and shooting stars wrap to the congruent
     *   copy of a 3 x 2 viewport tile nearest the camera (see @ref RenderStars).
     * - Sun and moon travel a band 3 viewports wide, re-anchored to the
     *   camera's world X in whole band steps (@ref GetLightSourcePosition).
     *
     * Both make the body walkable-past on purpose. Full-screen washes (dawn,
     * atmospheric glow, dew, lightning) ignore the camera entirely.
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

private:
    /**
     * @name Texture generation
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
     * @name Object generation
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
     * @name Shooting star management
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
     * @name Render functions
     * @brief Individual effect rendering routines.
     * @{
     */

    /**
     * @brief Render all stars (foreground and background).
     *
     * Renders stars with brightness modulated by twinkle animation
     * and overall visibility from TimeManager.
     *
     * @par Star-field wrap
     * Both star arrays live in one fixed world-space tile,
     * @ref STAR_FIELD_X_PERIODS x @ref STAR_FIELD_Y_PERIODS viewports
     * (3 x 2). Each star's normalized position scales to that tile, then
     * `std::remainder(anchor - cameraRef, period)` folds it to the congruent
     * copy nearest the camera. That gives full world parallax with no
     * per-region generation pass, and no discontinuity at the origin (which a
     * plain `[0, period)` wrap would introduce).
     * @verbatim
     *   +---------------------------------------------+  star-field tile
     *   |                                             |  = 3 viewports wide
     *   |         +-----------+                       |    2 viewports tall
     *   |  <----  |  camera   |  ---->                |
     *   |         |  viewport |     stars outside the |
     *   |         +-----------+     tile wrap back in |
     *   |            ^     v        along both axes   |
     *   +---------------------------------------------+
     *      wrap x mod 3*screenWidth, y mod 2*screenHeight, nearest camera
     * @endverbatim
     * @ref RenderShootingStars uses the same tile and the same wrap.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for star visibility factor.
     * @param cameraPos    World-space camera position; the wrap reference.
     * @param screenWidth  Screen width; one X period of the star-field tile.
     * @param screenHeight Screen height; one Y period of the star-field tile.
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
     * Called while the cached aurora fade (@ref TimeManager::GetAuroraFade)
     * exceeds 0.01. That is true for the Aurora weather, for a director
     * transition ramping into or out of it, and for an aurora sky overlay over
     * any base weather - it is not a test on the current weather state. Every
     * layer's alpha is scaled by that fade, which makes a transition ramp
     * instead of snap. Bands are drawn with the world projection (no swap) and
     * subtract the full camera position.
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
     * (strongest when sun is lower in sky). See @ref LightRay for how each
     * ray's normalized fields become pixel geometry.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for sun position and intensity.
     * @param cameraPos    World-space camera position, for the sun's parallax
     *                     anchor (@ref SKY_PARALLAX_SUN).
     * @param screenWidth  Screen width; scales the origin band and fan.
     * @param screenHeight Screen height; scales the ray length.
     */
    void RenderSunRays(IRenderer& renderer,
                       const TimeManager& time,
                       glm::vec2 cameraPos,
                       int screenWidth,
                       int screenHeight);

    /**
     * @brief Render softer rays from the moon.
     *
     * Similar to sun rays but cooler color, a narrower 60-degree fan, and
     * lower intensity, additionally scaled by the moon phase (full moon 1.0,
     * new moon floored at 0.3).
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for moon position and phase.
     * @param cameraPos    World-space camera position, for the moon's parallax
     *                     anchor (@ref SKY_PARALLAX_MOON).
     * @param screenWidth  Screen width; scales the origin band and fan.
     * @param screenHeight Screen height; scales the beam length.
     */
    void RenderMoonRays(IRenderer& renderer,
                        const TimeManager& time,
                        glm::vec2 cameraPos,
                        int screenWidth,
                        int screenHeight);

    /**
     * @brief Generate a fresh lightning bolt path.
     *
     * Writes a jagged main polyline plus 0-3 sub-branches into
     * @ref m_LightningBolt, sized to the cached viewport. Called once per
     * flash trigger from @ref Update.
     */
    void GenerateLightningBolt(int screenWidth, int screenHeight);

    /**
     * @brief Draw the previously generated lightning bolt.
     *
     * Draws while @ref m_LightningBoltTimer is positive. Camera-locked (the
     * bolt sits in screen space; world anchoring is possible future polish).
     */
    void RenderLightningBolt(IRenderer& renderer, int screenWidth, int screenHeight);

    /// @}

    /**
     * @name Morning/dawn effects
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
     * @name Utility functions
     * @{
     */

    /**
     * @brief Calculate camera-relative position of the sun or moon.
     *
     * The body travels a band 3 viewports wide, anchored to the camera's
     * current world X rounded down to whole bands, so it is world-anchored
     * (the player walks past it) yet never scrolls permanently out of reach.
     * X runs BACKWARDS along the arc (`1 - arc`), so arc 0 is at the right of
     * the band. Y is sky-relative: 20 px above the camera at the horizon,
     * rising 40 px at the arc's apex, so vertical camera motion does not drag
     * the body down.
     *
     * @param arc          Sun/moon arc value (0 = horizon, 0.5 = zenith).
     * @param screenWidth  Screen width; one third of the travel band.
     * @param screenHeight Unused; kept for call-site symmetry with the other
     *                     screen-size-taking helpers.
     * @param cameraPos    World-space camera position (band anchor + parallax
     *                     subtraction base).
     * @param parallaxFactor Fraction of camera motion subtracted from the world
     *                       anchor: 0 = locked to the screen, 1 = locked to the
     *                       world. Both callers pass 1.0.
     * @return Position relative to the camera, in pixels.
     */
    glm::vec2 GetLightSourcePosition(float arc,
                                     int screenWidth,
                                     int screenHeight,
                                     glm::vec2 cameraPos,
                                     float parallaxFactor) const;

    /// @}

    /**
     * @name Procedural textures
     * @brief GPU textures generated at initialization.
     * @{
     */
    /**
     * @brief Store holding every sky texture; set in @ref Initialize.
     *
     * Non-owning, and it must outlive this SkyRenderer: every draw and every
     * texture accessor dereferences it. Its UploadAll re-uploads the sky
     * textures on a renderer switch.
     */
    TextureStore* m_Store = nullptr;
    TextureHandle m_RayHandle;            ///< Vertical gradient for light rays.
    TextureHandle m_StarHandle;           ///< Small soft circle for stars.
    TextureHandle m_StarGlowHandle;       ///< Larger glow behind bright stars.
    TextureHandle m_ShootingStarHandle;   ///< Elongated streak for meteors.
    TextureHandle m_GlowHandle;           ///< Large soft glow for atmosphere.
    TextureHandle m_LightPoolHandle;      ///< Soft circle for WorldLight pools.
    TextureHandle m_AuroraCurtainHandle;  ///< Vertical streaked curtain for aurora bands.
    TextureHandle m_AuroraBeamHandle;     ///< Vertical oval ray/beam for aurora beams.
    TextureHandle m_AuroraSmallHandle;    ///< Procedural soft dot for aurora wisps.
    /// @}

    /**
     * @name Atlas binding
     * When @ref m_AtlasTexture is non-null, sky draws sample from the atlas at
     * the per-element pixel offsets recorded below. Each region's size is read
     * back from the corresponding standalone texture in @ref m_Store at draw
     * time, so the binding survives a texture being regenerated at a different
     * size. Non-owning; the atlas belongs to the caller.
     * @{
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
     * @name Sky object arrays
     * @brief Collections of sky elements to render.
     * @{
     */
    std::vector<Star> m_Stars;                       ///< Foreground stars (bright, prominent).
    std::vector<Star> m_BackgroundStars;             ///< Background stars (dim, distant).
    std::vector<VisibleStar> m_VisibleStarsScratch;  ///< Per-frame scratch for two-pass star emit.
    std::vector<LightRay> m_SunRays;                 ///< Sun god ray configurations.
    std::vector<LightRay> m_MoonRays;                ///< Moon ray configurations.
    std::vector<ShootingStar> m_ShootingStars;       ///< Active shooting stars.
    std::vector<DewSparkle> m_DewSparkles;           ///< Morning dew sparkle points.
    /// @}

    /**
     * @name Animation state
     * @brief Time tracking for animations.
     * @{
     */
    double m_Time;              ///< Accumulated time for twinkle animations (seconds).
    float m_ShootingStarTimer;  ///< Countdown to next shooting star spawn.
    float m_LastScreenWidth;    ///< Cached screen width for resize detection.
    float m_LastScreenHeight;   ///< Cached screen height for resize detection.
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
     * @struct LightningBolt
     * @brief Procedurally generated bolt path, regenerated on each flash.
     *
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
     * @name Texture size constants
     * @brief Dimensions for procedurally generated textures.
     * @{
     */
    static constexpr int RAY_TEXTURE_WIDTH = 64;        ///< Ray texture width (narrow).
    static constexpr int RAY_TEXTURE_HEIGHT = 512;      ///< Ray texture height (tall for length).
    static constexpr int STAR_TEXTURE_SIZE = 64;        ///< Star point texture size.
    static constexpr int STAR_GLOW_TEXTURE_SIZE = 128;  ///< Star glow texture size.
    static constexpr int GLOW_TEXTURE_SIZE = 256;       ///< Atmospheric glow texture size.
    /// @}

    /**
     * @name Rendering constants
     * @brief Configuration for effect counts and sizes.
     * @{
     */
    /**
     * @brief Number of foreground stars.
     *
     * Both star arrays cover a star-field 3 viewports wide x 2 tall (the wrap
     * window in RenderStars), i.e. 6 viewports of area. These totals therefore
     * work out to 300 foreground / 200 background stars per viewport - HALF the
     * density of the original 600/400 single-viewport layout, not a
     * like-for-like scale-up. RenderStars also caps how many are actually
     * emitted, at `visibility * 0.6` of the foreground array and
     * `visibility * 0.4` of the background one. That `visibility` is the
     * locally dimmed value (`GetStarVisibility() * 0.35`), so the real ceilings
     * at full night are ~21% (~378 stars) and ~14% (~168 stars).
     */
    static constexpr int STAR_COUNT = 1800;
    static constexpr int BACKGROUND_STAR_COUNT = 1200;  ///< Number of background stars.
    static constexpr int SUN_RAY_COUNT = 3;      ///< Sun ray count; spread over ~2/3 of the screen.
    static constexpr int MOON_RAY_COUNT = 3;     ///< Number of moon rays (very subtle).
    static constexpr int DEW_SPARKLE_COUNT = 4;  ///< Number of dew sparkles.
    /// unused. Nothing in SkyRenderer.cpp reads this; ray length is derived
    /// from the screen height (see @ref LightRay), so changing it has no effect.
    static constexpr float MAX_RAY_LENGTH = 1200.0f;
    /**
     * @brief Ray-width multiplier, applied on top of a fixed 50 px base in RenderSunRays
     *        (`50 + LightRay::width * RAY_WIDTH`).
     *
     * Moon rays use their own inline 70.0f instead of this constant.
     */
    static constexpr float RAY_WIDTH = 80.0f;
    static constexpr float SUN_RAY_SPREAD =
        120.0f;  ///< Total fan spread angle in degrees (~2/3 screen).
    static constexpr float SUN_BAND_WIDTH =
        0.35f;  ///< Width of sun origin band (fraction of screen width).

    /**
     * @name World parallax (full world anchoring)
     * @brief Per-element fraction of camera movement applied to sky positions.
     *
     * 0.0 = locked to screen, 1.0 = locked to world. Sky elements are now
     * anchored to world coordinates so they cover the entire map and walk
     * past as the player moves, instead of feeling like a screen overlay.
     * Stars and shooting stars use a wrap-around field (see RenderStars)
     * so they always remain visible in the upper sky regardless of where
     * the camera is.
     *
     * @warning Only SKY_PARALLAX_SUN and SKY_PARALLAX_MOON are read, and only
     *          by @ref GetLightSourcePosition. The star and aurora constants
     *          are dead: those passes subtract the full camera position
     *          directly, so editing them changes nothing.
     * @{
     */
    static constexpr float SKY_PARALLAX_STARS_BG = 1.0f;  ///< Unused; see the warning above.
    static constexpr float SKY_PARALLAX_STARS_FG = 1.0f;  ///< Unused; see the warning above.
    static constexpr float SKY_PARALLAX_SUN = 1.0f;       ///< Sun + sun rays (world).
    static constexpr float SKY_PARALLAX_MOON = 1.0f;      ///< Moon + moon rays (world).
    static constexpr float SKY_PARALLAX_AURORA = 1.0f;    ///< Unused; see the warning above.

    /**
     * @brief Width of the star-field tile, in viewports.
     *
     * Stars wrap around the camera modulo this period so the whole map gets
     * stars without a per-tile generation pass.
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

    bool m_Initialized;  ///< True after Initialize() completes successfully.
    std::mt19937 m_Rng;  ///< Shared RNG for all procedural generation.
};
