#pragma once

#include <glm/glm.hpp>
#include <random>
#include <vector>

#include "IRenderer.h"
#include "Texture.h"

class TimeManager;

/**
 * @struct Star
 * @brief Represents a single star in the night sky with twinkling animation.
 * @author Alex (https://github.com/lextpf)
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
 * @class SkyRenderer
 * @brief Renders atmospheric sky effects synchronized with the day/night cycle.
 * @author Alex (https://github.com/lextpf)
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

    /**
     * @brief Initialize all sky rendering resources.
     *
     * Generates procedural textures and populates star/ray arrays.
     * Must be called before Render().
     *
     * @par Initialization Steps
     * 1. Generate ray texture (soft vertical gradient)
     * 2. Generate star textures (point + glow)
     * 3. Generate shooting star texture (elongated streak)
     * 4. Generate atmospheric glow texture
     * 5. Populate star arrays with random positions/properties
     * 6. Populate light ray arrays for sun and moon
     * 7. Generate dew sparkle positions
     */
    void Initialize();

    /**
     * @brief Re-upload all sky textures to the renderer.
     *
     * Called after switching rendering backends to ensure textures
     * are available in the new renderer's GPU memory.
     *
     * @param renderer The renderer to upload textures to.
     */
    void UploadTextures(IRenderer& renderer);

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
     * Effects automatically fade in/out based on TimeManager state.
     *
     * @param renderer     Renderer interface for draw calls.
     * @param time         TimeManager for time-based visibility.
     * @param screenWidth  Current screen width in pixels.
     * @param screenHeight Current screen height in pixels.
     */
    void Render(IRenderer& renderer, const TimeManager& time, int screenWidth, int screenHeight);

private:
    /// @name Texture Generation
    /// @brief Procedural texture creation for sky effects.
    /// @{

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

    /// @name Object Generation
    /// @brief Populate arrays with randomized sky objects.
    /// @{

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

    /// @name Shooting Star Management
    /// @brief Lifecycle management for meteor effects.
    /// @{

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

    /// @name Render Functions
    /// @brief Individual effect rendering routines.
    /// @{

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
                     int screenWidth,
                     int screenHeight);

    /**
     * @brief Render active shooting stars with trails.
     *
     * @param renderer     Renderer interface.
     * @param time         TimeManager for visibility.
     * @param screenWidth  Screen width.
     * @param screenHeight Screen height.
     */
    void RenderShootingStars(IRenderer& renderer,
                             const TimeManager& time,
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
                        int screenWidth,
                        int screenHeight);

    /// @}

    /// @name Morning/Dawn Effects
    /// @brief Special effects for sunrise period.
    /// @{

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

    /// @name Utility Functions
    /// @{

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
    glm::vec2 GetLightSourcePosition(float arc, int screenWidth, int screenHeight) const;

    /// @}

    /// @name Procedural Textures
    /// @brief GPU textures generated at initialization.
    /// @{
    Texture m_RayTexture;           ///< Vertical gradient for light rays
    Texture m_StarTexture;          ///< Small soft circle for stars
    Texture m_StarGlowTexture;      ///< Larger glow behind bright stars
    Texture m_ShootingStarTexture;  ///< Elongated streak for meteors
    Texture m_GlowTexture;          ///< Large soft glow for atmosphere
    /// @}

    /// @name Sky Object Arrays
    /// @brief Collections of sky elements to render.
    /// @{
    std::vector<Star> m_Stars;                  ///< Foreground stars (bright, prominent)
    std::vector<Star> m_BackgroundStars;        ///< Background stars (dim, distant)
    std::vector<LightRay> m_SunRays;            ///< Sun god ray configurations
    std::vector<LightRay> m_MoonRays;           ///< Moon ray configurations
    std::vector<ShootingStar> m_ShootingStars;  ///< Active shooting stars
    std::vector<DewSparkle> m_DewSparkles;      ///< Morning dew sparkle points
    /// @}

    /// @name Animation State
    /// @brief Time tracking for animations.
    /// @{
    float m_Time;               ///< Accumulated time for twinkle animations (seconds)
    float m_ShootingStarTimer;  ///< Countdown to next shooting star spawn
    float m_LastScreenWidth;    ///< Cached screen width for resize detection
    float m_LastScreenHeight;   ///< Cached screen height for resize detection
    /// @}

    /// @name Texture Size Constants
    /// @brief Dimensions for procedurally generated textures.
    /// @{
    static constexpr int RAY_TEXTURE_WIDTH = 64;        ///< Ray texture width (narrow)
    static constexpr int RAY_TEXTURE_HEIGHT = 512;      ///< Ray texture height (tall for length)
    static constexpr int STAR_TEXTURE_SIZE = 64;        ///< Star point texture size
    static constexpr int STAR_GLOW_TEXTURE_SIZE = 128;  ///< Star glow texture size
    static constexpr int GLOW_TEXTURE_SIZE = 256;       ///< Atmospheric glow texture size
    /// @}

    /// @name Rendering Constants
    /// @brief Configuration for effect counts and sizes.
    /// @{
    static constexpr int STAR_COUNT = 600;             ///< Number of foreground stars
    static constexpr int BACKGROUND_STAR_COUNT = 400;  ///< Number of background stars
    static constexpr int SUN_RAY_COUNT = 3;   ///< Number of sun rays (spread across ~2/3 of screen)
    static constexpr int MOON_RAY_COUNT = 3;  ///< Number of moon rays (very subtle)
    static constexpr int DEW_SPARKLE_COUNT = 4;       ///< Number of dew sparkles
    static constexpr float MAX_RAY_LENGTH = 1200.0f;  ///< Maximum ray length in pixels
    static constexpr float RAY_WIDTH = 80.0f;         ///< Base ray width in pixels
    static constexpr float SUN_RAY_SPREAD =
        120.0f;  ///< Total fan spread angle in degrees (~2/3 screen)
    static constexpr float SUN_BAND_WIDTH =
        0.35f;  ///< Width of sun origin band (fraction of screen width)
    /// @}

    bool m_Initialized;  ///< True after Initialize() completes successfully
    std::mt19937 m_Rng;  ///< Shared RNG for all procedural generation
};
