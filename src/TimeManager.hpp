#pragma once

#include "WeatherDefinitions.hpp"

#include <glm/glm.hpp>

/**
 * @enum TimePeriod
 * @brief Discrete time periods within a 24-hour day cycle.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Each period has distinct ambient lighting, sky colors, and gameplay effects.
 *
 * | Period     | Hours       | Characteristics                    |
 * |------------|-------------|------------------------------------|
 * | Dawn       | 05:00-07:00 | Orange/pink sky, stars fading      |
 * | Morning    | 07:00-10:00 | Bright, golden hour fading         |
 * | Midday     | 10:00-16:00 | Full daylight, harsh shadows       |
 * | Afternoon  | 16:00-18:00 | Warm light, lengthening shadows    |
 * | Dusk       | 18:00-20:00 | Orange/purple sky, stars appearing |
 * | Evening    | 20:00-22:00 | Deep blue, moon rising             |
 * | Night      | 22:00-04:00 | Dark, full starfield, moon visible |
 * | LateNight  | 04:00-05:00 | Darkest hour before dawn           |
 */
enum class TimePeriod
{
    Dawn,       ///< 05:00-07:00 - Sunrise transition
    Morning,    ///< 07:00-10:00 - Early day, golden hour
    Midday,     ///< 10:00-16:00 - Full daylight
    Afternoon,  ///< 16:00-18:00 - Late day warmth
    Dusk,       ///< 18:00-20:00 - Sunset transition
    Evening,    ///< 20:00-22:00 - Early night
    Night,      ///< 22:00-04:00 - Deep night
    LateNight   ///< 04:00-05:00 - Pre-dawn darkness
};

/**
 * @struct ResolvedWeatherChannels
 * @brief Captured getter outputs used as an exact from-endpoint during a
 * mid-transition retarget (WeatherDirector).
 *
 * Snapshotting resolved values sidesteps the sentinel formulas entirely -
 * no inversion of the intensity/day-night math, exact at any intensity.
 */
struct ResolvedWeatherChannels
{
    glm::vec3 ambient{1.0f};     ///< GetAmbientColor output at capture time.
    glm::vec3 sky{0.0f};         ///< GetSkyColor output at capture time.
    float starVisibility{0.0f};  ///< GetStarVisibility output at capture time.
};

/**
 * @class TimeManager
 * @brief Controls game time, day/night cycle, and time-based visual effects.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * The TimeManager is the central authority for all time-related calculations
 * in the game. It drives the day/night cycle, provides sun/moon positions
 * for sky rendering, and calculates ambient lighting colors.
 *
 * @par Time Model
 * Time is represented as a float from 0.0 to 24.0 (hours):
 * @code
 *     0.0 ---------- 6.0 ---------- 12.0 ---------- 18.0 ---------- 24.0
 *   Midnight       Sunrise          Noon           Sunset          Midnight
 * @endcode
 *
 * @par Day/Night Cycle
 * The cycle uses continuous linear transitions between periods:
 *
 * @htmlonly
 * <pre class="mermaid">
 * graph LR
 *     classDef night fill:#1a1a2e,stroke:#4a4a6a,color:#e2e8f0
 *     classDef dawn fill:#614385,stroke:#9b59b6,color:#e2e8f0
 *     classDef day fill:#f39c12,stroke:#e67e22,color:#1a1a2e
 *     classDef dusk fill:#c0392b,stroke:#e74c3c,color:#e2e8f0
 *
 *     subgraph "24-Hour Cycle"
 *         N["Night<br/>22:00-04:00<br/>Stars + Moon"]:::night
 *         LN["Late Night<br/>04:00-05:00<br/>Pre-dawn"]:::night
 *         D["Dawn<br/>05:00-07:00<br/>Sun rising"]:::dawn
 *         M["Morning<br/>07:00-10:00<br/>Golden hour"]:::day
 *         MD["Midday<br/>10:00-16:00<br/>Full sun"]:::day
 *         A["Afternoon<br/>16:00-18:00<br/>Warm light"]:::day
 *         DU["Dusk<br/>18:00-20:00<br/>Sun setting"]:::dusk
 *         E["Evening<br/>20:00-22:00<br/>Moon rising"]:::night
 *     end
 *
 *     N --> LN --> D --> M --> MD --> A --> DU --> E --> N
 * </pre>
 * @endhtmlonly
 *
 * @par Sun Arc Calculation
 * The sun position is normalized to a 0-1 arc:
 * @f[
 * sunArc = \frac{time - sunrise}{sunset - sunrise}
 * @f]
 * Where sunrise=6:00 and sunset=20:00 (14-hour day).
 * Returns -1 when sun is below the horizon.
 *
 * @par Moon Arc Calculation
 * Similar to sun but offset by ~12 hours:
 * - Moonrise: 19:00
 * - Moonset: 07:00
 * - Crosses midnight, so calculation handles wrap-around
 *
 * @par Moon Phases
 * An 8-day lunar cycle provides visual variety:
 * @code
 *   Phase 0: New Moon         (invisible)
 *   Phase 1: Waxing Crescent
 *   Phase 2: First Quarter
 *   Phase 3: Waxing Gibbous
 *   Phase 4: Full Moon        (brightest)
 *   Phase 5: Waning Gibbous
 *   Phase 6: Last Quarter
 *   Phase 7: Waning Crescent
 * @endcode
 *
 * @par Ambient Color Transitions
 * Colors linearly interpolate between key times:
 * | Time  | Ambient Color       | Description |
 * |-------|---------------------|--------------------------|
 * | 00:00 | (0.30, 0.30, 0.45) | Deep night blue          |
 * | 04:00 | (0.35, 0.35, 0.50) | Late-night pre-dawn      |
 * | 05:00 | (0.85, 0.75, 0.70) | Dawn warm light starts   |
 * | 07:00 | (0.95, 0.93, 0.90) | Morning warm white       |
 * | 10:00 | (1.00, 1.00, 0.98) | Midday neutral daylight  |
 * | 18:00 | (0.95, 0.90, 0.82) | Afternoon to dusk warmth |
 * | 20:00 | (0.75, 0.60, 0.55) | Dusk muted orange        |
 * | 22:00 | (0.50, 0.50, 0.65) | Evening blue             |
 *
 * @par Time Scale
 * Real-time to game-time conversion:
 * @f[
 * gameHours = \frac{realSeconds \times 24 \times timeScale}{dayDuration}
 * @f]
 * Class default: 24 real seconds = 1 game day. Rift startup overrides this
 * to 1200 seconds per
 * day in Game::Initialize().
 *
 * @par Usage Example
 * @code
 * TimeManager time;
 * time.Initialize();  // Class default: 24s per day (fast cycle)
 * time.SetTime(6.0f); // Start at
 * sunrise
 *
 * // In game loop:
 * time.Update(deltaTime);
 *
 * // For rendering:
 * glm::vec3 ambient = time.GetAmbientColor();
 * float sunArc = time.GetSunArc();
 * float starVis = time.GetStarVisibility();
 *
 * // For gameplay:
 * if (time.IsNight()) {
 *     SpawnNightCreatures();
 * }
 * @endcode
 *
 * @see SkyRenderer for visual effects driven by TimeManager
 */
class TimeManager
{
public:
    /// @name Constructor
    /// @{

    /**
     * @brief Construct TimeManager with default values.
     *
     * Initial state: time=12:00, dayDuration=24s, timeScale=1.0, Clear weather.
     * Game startup
     * changes dayDuration to 1200s after initialization.
     */
    TimeManager();

    /// @}

    /// @name Initialization and Update
    /// @{

    /**
     * @brief Initialize the time manager.
     *
     * Resets to class default starting values. Game::Initialize() applies
     * Rift's
     * 1200s-per-day startup setting after this call.
     */
    void Initialize();

    /**
     * @brief Advance time based on elapsed real time.
     *
     * Call every frame. Handles day rollover and moon phase updates.
     *
     * @param deltaTime Real time elapsed since last frame (seconds).
     */
    void Update(float deltaTime);

    /// @}

    /// @name Time Queries
    /// @{

    /**
     * @brief Get the current time of day.
     * @return Time in hours (0.0 to 24.0, wraps at midnight).
     */
    float GetTimeOfDay() const { return m_CurrentTime; }

    /**
     * @brief Get the current discrete time period.
     * @return TimePeriod enum value based on current hour.
     */
    TimePeriod GetTimePeriod() const;

    /**
     * @brief Get the number of days elapsed.
     *
     * Used for moon phase calculation. Increments at midnight.
     *
     * @return Day count starting from 0.
     */
    int GetDayCount() const { return m_DayCount; }

    /**
     * @brief Check if sun is above the horizon.
     * @return true if time is between sunrise (6:00) and sunset (20:00).
     */
    bool IsDay() const;

    /**
     * @brief Check if sun is below the horizon.
     * @return true if time is outside 6:00-20:00 range.
     */
    bool IsNight() const;

    /// @}

    /// @name Celestial Positions
    /// @brief Sun and moon arc positions for sky rendering.
    /// @{

    /**
     * @brief Get the sun's position along its arc.
     *
     * The arc represents the sun's path across the sky:
     * - 0.0 = sunrise (eastern horizon)
     * - 0.5 = solar noon (highest point)
     * - 1.0 = sunset (western horizon)
     *
     * @return Normalized arc position (0.0-1.0), or -1.0 if below horizon.
     */
    float GetSunArc() const;

    /**
     * @brief Get the moon's position along its arc.
     *
     * Similar to sun arc but for nighttime:
     * - 0.0 = moonrise (19:00)
     * - 0.5 = lunar midnight (highest point)
     * - 1.0 = moonset (07:00)
     *
     * @return Normalized arc position (0.0-1.0), or -1.0 if below horizon.
     */
    float GetMoonArc() const;

    /**
     * @brief Get the current moon phase.
     *
     * 8-phase lunar cycle based on day count:
     * - 0 = New Moon (invisible)
     * - 2 = First Quarter (half)
     * - 4 = Full Moon (brightest)
     * - 6 = Last Quarter (half)
     *
     * @return Phase index in range [0, 7].
     */
    int GetMoonPhase() const;

    /// @}

    /// @name Lighting Colors
    /// @brief Time-based colors for world and sky rendering.
    /// @{

    /**
     * @brief Get the ambient light color multiplier.
     *
     * Applied to all world sprites to simulate time-of-day lighting.
     * Transitions linearly between predefined colors at key times.
     *
     * @return RGB color multiplier (typically 0.0-1.0 per channel).
     */
    glm::vec3 GetAmbientColor() const;

    /**
     * @brief Get the sky background color.
     *
     * Base color for sky gradient, varies from deep blue (night)
     * to light blue (day) with orange/pink during transitions.
     *
     * @return RGB sky color.
     */
    glm::vec3 GetSkyColor() const;

    /**
     * @brief Get the sun's rendered color.
     *
     * Varies throughout the day:
     * - Sunrise/sunset: Orange/red
     * - Midday: Pale yellow/white
     *
     * @return RGB sun color.
     */
    glm::vec3 GetSunColor() const;

    /**
     * @brief Get star visibility factor.
     *
     * Stars fade in at dusk and fade out at dawn:
     * - 0.0 = completely invisible (daytime)
     * - 1.0 = fully visible (deep night)
     *
     * @return Visibility factor (0.0-1.0).
     */
    float GetStarVisibility() const;

    /**
     * @brief Get dawn effect intensity.
     *
     * Used for special dawn visual effects (horizon glow, etc.):
     * - Ramps up 4:30-5:30
     * - Peaks 5:30-6:30
     * - Fades out 6:30-8:00
     *
     * @return Intensity factor (0.0-1.0).
     */
    float GetDawnIntensity() const;

    /// @}

    /// @name Weather Control
    /// @{

    /**
     * @brief Get the current weather state.
     * @return WeatherState enum value.
     */
    WeatherState GetWeather() const { return m_Weather; }

    /**
     * @brief Set the weather state.
     *
     * Weather affects:
     * - Celestial visibility (sun/moon/stars)
     * - Ambient lighting intensity
     * - Sky colors
     * - Weather particle spawning (rain, snow, ash, etc.)
     *
     * @param weather New weather state.
     */
    void SetWeather(WeatherState weather) { m_Weather = weather; }

    /**
     * @brief Get the current weather intensity scalar.
     * @return Multiplier in [0, 1] applied to weather particle spawn rate
     *         and effect strength.
     */
    float GetWeatherIntensity() const { return m_WeatherIntensity; }

    /**
     * @brief Set the weather intensity scalar.
     *
     * Drives particle density and effect strength independent of which
     * WeatherState is active. Boolean flags (lightning on/off, aurora,
     * celestial-body visibility) are not affected by intensity.
     *
     * @param value Intensity in [0, 1] (clamped).
     */
    void SetWeatherIntensity(float value);

    /**
     * @brief Star visibility from time-of-day alone, ignoring weather overrides.
     *
     * The "how dark is the night" scalar for consumers that should follow the
     * clock, not the storm (WorldLights, PostFX grading - spec section 4.4).
     * @return Visibility in [0, 1].
     */
    float GetNaturalStarVisibility() const;

    /**
     * @brief Publish an active weather blend (WeatherDirector transition).
     *
     * While a blend is active, @ref GetAmbientColor, @ref GetSkyColor, and
     * @ref GetStarVisibility mix the per-endpoint channel values by @p t
     * instead of resolving a single weather definition, and
     * @ref GetEffectiveWeatherDefinition serves @p effective in place of the
     * table lookup for @ref GetWeather.
     *
     * Every publication invalidates any prior @ref SetWeatherBlendResolvedFrom
     * capture - a resolved-from snapshot belongs to one specific blend and a
     * stale capture would silently corrupt the next transition's from-endpoint.
     * Call @ref SetWeatherBlendResolvedFrom again afterward if the new
     * publication should also use a captured from-endpoint.
     *
     * @param from Outgoing weather definition. Non-owning; must outlive use
     *             (the director publishes member storage that persists across
     *             the frame).
     * @param to Incoming weather definition. Non-owning, same lifetime contract.
     * @param t Blend weight in [0, 1] (clamped); 0 = fully @p from, 1 = fully @p to.
     * @param effective Director-owned blended definition served by
     *                  @ref GetEffectiveWeatherDefinition. Non-owning, same
     *                  lifetime contract.
     */
    void SetWeatherBlend(const WeatherDefinition* from,
                         const WeatherDefinition* to,
                         float t,
                         const WeatherDefinition* effective);

    /**
     * @brief Override the blend's from-endpoint with exact captured values.
     *
     * Used when retargeting mid-transition: rather than inverting the
     * intensity/day-night sentinel formulas to find what the previous
     * from-endpoint "should" resolve to, the caller snapshots the getters at
     * the retarget instant and republishes them here as the new from-value.
     * Only meaningful until the next @ref SetWeatherBlend or
     * @ref ClearWeatherBlend call, both of which clear it.
     *
     * @param resolved Captured channel values to use in place of the blend's
     *                 from-endpoint formulas.
     */
    void SetWeatherBlendResolvedFrom(const ResolvedWeatherChannels& resolved);

    /**
     * @brief Clear any active weather blend, resolved-from capture, and
     * published fades.
     *
     * Restores the single-definition getter path keyed by @ref GetWeather.
     * Also called from @ref Initialize() so no stale blend survives a world
     * load even if the director's own reset is missed.
     */
    void ClearWeatherBlend();

    /**
     * @brief Check whether a weather blend is currently published.
     * @return true if both blend endpoints are non-null.
     */
    bool HasWeatherBlend() const { return m_BlendFrom != nullptr && m_BlendTo != nullptr; }

    /**
     * @brief Get the weather definition currently driving weather-dependent
     * effects (particles, etc.).
     *
     * Returns the director-published effective definition while a blend is
     * active (see @ref SetWeatherBlend), otherwise the table lookup for the
     * current @ref GetWeather state.
     *
     * @return Reference to the active weather definition.
     */
    const WeatherDefinition& GetEffectiveWeatherDefinition() const;

    /**
     * @brief Publish explicit fade scalars for celestial bodies and aurora.
     *
     * Weather definitions gate celestial/aurora visibility with hard booleans
     * (@c showCelestialBodies, @c showAurora); a blend transition needs a
     * smooth ramp instead of a snap, so the director computes and publishes
     * fade scalars here. Cleared together with the rest of the blend state by
     * @ref ClearWeatherBlend.
     *
     * @param celestialFade Celestial-body visibility fade in [0, 1] (clamped).
     * @param auroraFade Aurora visibility fade in [0, 1] (clamped).
     */
    void SetWeatherFades(float celestialFade, float auroraFade);

    /**
     * @brief Get the celestial-body visibility fade.
     *
     * Returns the published fade set by @ref SetWeatherFades if one is
     * active, otherwise derives a binary value from
     * @ref GetEffectiveWeatherDefinition's @c showCelestialBodies flag.
     *
     * @return Fade factor in [0, 1].
     */
    float GetCelestialFade() const;

    /**
     * @brief Get the aurora visibility fade.
     *
     * Returns the published fade set by @ref SetWeatherFades if one is
     * active, otherwise derives a binary value from
     * @ref GetEffectiveWeatherDefinition's @c showAurora flag.
     *
     * @return Fade factor in [0, 1].
     */
    float GetAuroraFade() const;

    /// @}

    /// @name Time Control
    /// @brief Methods for controlling time progression.
    /// @{

    /**
     * @brief Set the time progression speed multiplier.
     *
     * @param scale Multiplier (1.0 = normal, 2.0 = 2x speed, 0.5 = half speed).
     */
    void SetTimeScale(float scale) { m_TimeScale = scale; }

    /**
     * @brief Get the current time scale.
     * @return Time progression multiplier.
     */
    float GetTimeScale() const { return m_TimeScale; }

    /**
     * @brief Set the duration of one full day in real seconds.
     *
     * Lower values = faster day/night cycle.
     * - 600s (10 min) = fast for testing
     * - 1800s (30 min) = moderate gameplay
     * - 3600s (1 hour) = realistic feel
     *
     * @param seconds Real-time seconds for one complete day.
     *                Must be > 0. Values <= 0 are clamped to a tiny positive value.
     */
    void SetDayDuration(float seconds) { m_DayDuration = (seconds > 0.0f) ? seconds : 0.001f; }

    /**
     * @brief Get the day duration in real seconds.
     * @return Seconds per game day.
     */
    float GetDayDuration() const { return m_DayDuration; }

    /**
     * @brief Set the current time directly.
     *
     * Automatically wraps values outside 0-24 range.
     * Does not modify the day count.
     *
     * @param hours Time in hours (0.0-24.0).
     */
    void SetTime(float hours);

    /**
     * @brief Advance time by a specified amount.
     *
     * Wraps across midnight and updates day count for both positive and
     * negative advances.
     *
     * @param hours Hours to advance (can be negative to go back).
     */
    void AdvanceTime(float hours);

    /**
     * @brief Pause or resume time progression.
     * @param paused true to pause, false to resume.
     */
    void SetPaused(bool paused) { m_Paused = paused; }

    /**
     * @brief Check if time is paused.
     * @return true if time progression is paused.
     */
    bool IsPaused() const { return m_Paused; }

    /**
     * @brief Toggle pause state.
     */
    void TogglePause() { m_Paused = !m_Paused; }

    /// @}

private:
    /// @name Helper Methods
    /// @{

    /**
     * @brief Linearly interpolate between two colors.
     * @param a Start color.
     * @param b End color.
     * @param t Interpolation factor (0.0-1.0).
     * @return Interpolated RGB color.
     */
    glm::vec3 LerpColor(const glm::vec3& a, const glm::vec3& b, float t) const;

    /**
     * @brief Calculate a clamped linear transition factor between time boundaries.
     *
     *
     * Returns 0.0 before start, 1.0 after end, and a linear ramp between:
     * @f[
     * f(t; a,
     * b) =
     * \begin{cases}
     * 0, & b \le a \\
     *
     * \operatorname{clamp}\left(\frac{t-a}{b-a}, 0, 1\right), & b > a
     * \end{cases}
     *
     * @f]
     *
     * @param time Current time.
     * @param start Transition start time.
     * @param end Transition end time.
     * @return Transition factor (0.0-1.0).
     */
    float GetTransitionFactor(float time, float start, float end) const;

    /// Natural (weatherless) sky color for the current hour.
    glm::vec3 NaturalSkyColor() const;
    /// Natural (weatherless) star visibility for the current hour.
    float NaturalStarVisibility() const;
    /// Ramped day/night factor for the sky override (replaces binary IsDay()).
    float SkyDayNightFactor() const;
    /// Ambient color as it would render under @p def.
    glm::vec3 ComputeAmbientColor(const WeatherDefinition& def) const;
    /// Sky color as it would render under @p def.
    glm::vec3 ComputeSkyColor(const WeatherDefinition& def) const;
    /// Star visibility as it would render under @p def.
    float ComputeStarVisibility(const WeatherDefinition& def) const;

    /// @}

    /// @name State
    /// @{
    float m_CurrentTime;             ///< Current time in hours (0.0-24.0)
    int m_DayCount;                  ///< Days elapsed (for moon phases)
    float m_TimeScale;               ///< Time progression multiplier (1.0 = normal)
    float m_DayDuration;             ///< Real seconds per game day (24 s default: 1 game hour/s)
    WeatherState m_Weather;          ///< Current weather condition
    float m_WeatherIntensity{1.0f};  ///< Particle/effect density 0-1.
    bool m_Paused{false};            ///< Whether time progression is paused
    /// @}

    /// @name Weather Blend State
    /// @brief Non-owning pointers published by WeatherDirector. Cleared by
    /// @ref ClearWeatherBlend and never dereferenced when null; the director
    /// is responsible for publishing storage that outlives the frame.
    /// @{
    const WeatherDefinition* m_BlendFrom{nullptr};       ///< Outgoing endpoint (null = no blend).
    const WeatherDefinition* m_BlendTo{nullptr};         ///< Incoming endpoint.
    const WeatherDefinition* m_BlendEffective{nullptr};  ///< Director-owned blended def.
    float m_BlendT{0.0f};                                ///< Eased blend weight [0, 1].
    bool m_HasResolvedFrom{false};  ///< Use m_ResolvedFrom instead of m_BlendFrom formulas.
    ResolvedWeatherChannels m_ResolvedFrom{};  ///< Captured retarget from-values.
    float m_CelestialFade{-1.0f};              ///< Published fade; < 0 = derive from def bool.
    float m_AuroraFade{-1.0f};                 ///< Published fade; < 0 = derive from def bool.
    /// @}

    /// @name Time Period Boundaries
    /// @brief Hour values defining period transitions.
    /// @{
    static constexpr float DAWN_START = 5.0f;      ///< Dawn begins
    static constexpr float DAWN_END = 7.0f;        ///< Dawn ends, morning begins
    static constexpr float MORNING_END = 10.0f;    ///< Morning ends, midday begins
    static constexpr float MIDDAY_END = 16.0f;     ///< Midday ends, afternoon begins
    static constexpr float AFTERNOON_END = 18.0f;  ///< Afternoon ends, dusk begins
    static constexpr float DUSK_END = 20.0f;       ///< Dusk ends, evening begins
    static constexpr float EVENING_END = 22.0f;    ///< Evening ends, night begins
    static constexpr float NIGHT_END = 4.0f;       ///< Night ends (wraps), late night begins
    /// @}

    /// @name Celestial Boundaries
    /// @brief Hour values for sun/moon rise and set times.
    /// @{
    static constexpr float SUNRISE_TIME = 6.0f;    ///< Sun rises above horizon
    static constexpr float SUNSET_TIME = 20.0f;    ///< Sun sets below horizon
    static constexpr float MOONRISE_TIME = 19.0f;  ///< Moon rises above horizon
    static constexpr float MOONSET_TIME = 7.0f;    ///< Moon sets below horizon
    /// @}

    /// @name Moon Phase Constants
    /// @{
    static constexpr int MOON_CYCLE_DAYS = 8;  ///< Days for complete lunar cycle
    /// @}
};
