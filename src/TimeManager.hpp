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
    Dawn,       ///< 05:00-07:00 - Sunrise transition.
    Morning,    ///< 07:00-10:00 - Early day, golden hour.
    Midday,     ///< 10:00-16:00 - Full daylight.
    Afternoon,  ///< 16:00-18:00 - Late day warmth.
    Dusk,       ///< 18:00-20:00 - Sunset transition.
    Evening,    ///< 20:00-22:00 - Early night.
    Night,      ///< 22:00-04:00 - Deep night.
    LateNight   ///< 04:00-05:00 - Pre-dawn darkness.
};

/**
 * @struct ResolvedWeatherChannels
 * @brief Captured getter outputs used as an exact from-endpoint during a
 * mid-transition retarget (WeatherDirector).
 * @ingroup Effects
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
 * @par Time model
 * Time is represented as a float from 0.0 to 24.0 (hours):
 * @code
 *     0.0 ------- 6.0 --------- 13.0 -------- 20.0 ------ 24.0
 *  Midnight     Sunrise      Solar noon      Sunset     Midnight
 * @endcode
 * Solar noon is the sun-arc midpoint (13:00), halfway between SUNRISE_TIME 6
 * and SUNSET_TIME 20. It is not clock noon.
 *
 * @par Day/night cycle
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
 * @par Sun arc calculation
 * The sun position is normalized to a 0-1 arc:
 * @f[
 * sunArc = \frac{time - sunrise}{sunset - sunrise}
 * @f]
 * Where sunrise=6:00 and sunset=20:00 (14-hour day).
 * Returns -1 when sun is below the horizon.
 *
 * @par Moon arc calculation
 * Similar to sun but offset by ~12 hours:
 * - Moonrise: 19:00
 * - Moonset: 07:00
 * - Crosses midnight, so calculation handles wrap-around
 *
 * @par Moon phases
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
 * @par Day timeline
 * The period/sun/moon boundaries below are named constants on this class (the
 * dawn-effect ones are literals in GetDawnIntensity); this strip is the single
 * place they line up against each other. Hours run left to right.
 * @verbatim
 * hour    0     2     4     6     8     10    12    14    16    18    20    22    24
 *         |-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|
 * period  |   Night   |LN| Dawn|Morning |      Midday     | Aft |Dusk | Eve |Night
 * sun                       ^rise 6:00                                ^set 20:00
 * moon                         ^set 7:00                           ^rise 19:00
 * stars   ===== 1.0 =====\____ ............ 0.0 ................ ____/==== 1.0 ====
 * dawnFx               ////^^^\\\\\
 * @endverbatim
 * - period: LN = LateNight (04:00-05:00). Boundaries are NIGHT_END 4,
 *   DAWN_START 5, DAWN_END 7, MORNING_END 10, MIDDAY_END 16, AFTERNOON_END 18,
 *   DUSK_END 20, EVENING_END 22.
 * - sun: SUNRISE_TIME 6 / SUNSET_TIME 20. GetSunArc returns -1 outside that
 *   window; 0 at sunrise, 0.5 at solar noon, 1 at sunset.
 * - moon: MOONRISE_TIME 19 / MOONSET_TIME 7, so the moon is up across
 *   midnight and GetMoonArc is -1 only in the 07:00-19:00 gap.
 * - stars: NaturalStarVisibility ramps 0->1 over AFTERNOON_END..DUSK_END,
 *   holds 1 through the night, and falls 1->0 over DAWN_START..DAWN_END.
 * - dawnFx: GetDawnIntensity ramps 04:30-05:30, holds 05:30-06:30, falls
 *   06:30-08:00. It is independent of the period boundaries above.
 *
 * @par Ambient color transitions
 * Colors linearly interpolate between these anchors (the values in
 * TimeManager::ComputeAmbientColor). Daylight anchors sit deliberately below
 * white: ambient is an unclamped multiply on albedo with no scene tonemap, so
 * a white midday made noon as bright as the source art allowed; ~7% of
 * headroom takes the eye-strain off daytime while staying the peak.
 * | Time  | Ambient Color      | Description                     |
 * |-------|--------------------|---------------------------------|
 * | 00:00 | (0.30, 0.30, 0.45) | Deep night blue                 |
 * | 04:00 | (0.35, 0.35, 0.50) | Late-night pre-dawn             |
 * | 05:00 | (0.85, 0.75, 0.70) | Dawn warm light starts          |
 * | 07:00 | (0.90, 0.88, 0.85) | Morning warm white (toned down) |
 * | 10:00 | (0.93, 0.93, 0.91) | Midday peak, just below white   |
 * | 18:00 | (0.90, 0.85, 0.78) | Afternoon warm yellow           |
 * | 20:00 | (0.75, 0.60, 0.55) | Dusk muted orange               |
 * | 22:00 | (0.50, 0.50, 0.65) | Evening blue                    |
 *
 * The midday anchor holds flat from MORNING_END to MIDDAY_END. These are the
 * time-of-day values only; @ref GetAmbientColor folds weather on top.
 *
 * @par Time scale
 * Real-time to game-time conversion:
 * @f[
 * gameHours = \frac{realSeconds \times 24 \times timeScale}{dayDuration}
 * @f]
 * Rift ships the class default: 24 real seconds = 1 game day, i.e. 1 game hour
 * per real second. Nothing in the game overrides it - @ref SetDayDuration
 * exists but currently has no production call site (only tests/ calls it), and
 * every @ref Initialize resets @c m_DayDuration back to 24 s anyway.
 *
 * @par Usage example
 * @code
 * TimeManager time;
 * time.Initialize();  // 24 s per day - the shipped setting
 * time.SetTime(6.0f); // Start at sunrise
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
     * Nothing in the game overrides the day duration afterwards; 24 s per game
     * day is the shipped cadence.
     */
    TimeManager();

    /// @}

    /// @name Initialization and update
    /// @{

    /**
     * @brief Initialize the time manager.
     *
     * Resets to the class defaults - including @c m_DayDuration back to 24 s,
     * which is why any earlier @ref SetDayDuration is discarded. Also clears
     * the weather blend and the manual overlay. Called on boot and at every
     * world load (title, New Game, Continue).
     */
    void Initialize();

    /**
     * @brief Advance time based on elapsed real time.
     *
     * Call once per frame. Calls @ref UpdateWeatherEffects first, then advances
     * the clock and handles day rollover unless paused. Because the overlay
     * fade runs before the pause check, it keeps easing while time is frozen.
     * Do not also call @ref UpdateWeatherEffects in the same frame - the fade
     * would advance twice. The day count this maintains feeds
     * @ref GetMoonPhase, which is derived on demand rather than stored.
     *
     * @param deltaTime Real time elapsed since last frame (seconds).
     */
    void Update(float deltaTime);

    /**
     * @brief Advance real-time weather effects without advancing the game clock.
     *
     * Used by cosmetic-only scenes such as the title screen, where the hour
     * stays fixed but a manually selected weather overlay must still fade in
     * and out.
     *
     * @param deltaTime Real elapsed time in seconds.
     */
    void UpdateWeatherEffects(float deltaTime);

    /// @}

    /// @name Time queries
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

    /// @name Celestial positions
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
     * - 0.5 = arc midpoint, 01:00, the moon's highest point (not clock midnight)
     * - 1.0 = moonset (07:00)
     *
     * @pre MOONRISE_TIME and MOONSET_TIME stay exactly 12 hours apart. The
     *      implementation divides by a literal 12.0 rather than deriving the
     *      window, so moving either constant pushes the result outside [0, 1].
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

    /**
     * @name Lighting colors
     * @brief Fully resolved colors for world and sky rendering.
     *
     * These are not plain time-of-day lookups. The full pipeline is, in order:
     * the time-of-day anchor, the active weather (tint or override, scaled by
     * @ref GetWeatherIntensity), any WeatherDirector blend published through
     * @ref SetWeatherBlend (optionally using a captured from-endpoint, see
     * @ref SetWeatherBlendResolvedFrom), the manual sky overlay weighted by
     * @ref GetOverlayBlend, and finally the "imposed night" pull described
     * below. Each channel skips a different stage, so read the matrix rather
     * than assuming the whole chain applies. Consumers that must follow the
     * clock rather than the storm use @ref GetNaturalStarVisibility instead -
     * there is no ambient/sky equivalent, so a caller cannot recover the raw
     * time-of-day color from this class.
     *
     * @verbatim
     * stage                | ambient      | sky          | stars       | celestFade | auroraFade
     * ---------------------|--------------|--------------|-------------|------------|-----------
     * time-of-day anchor   | anchor       | natural      | natural     | --         | --
     * weather x intensity  | tint mix     | override mix | override    | bool       | bool
     * director blend (t)   | lerp         | lerp         | lerp        | published  | published
     * manual overlay       | w = b * int  | --           | w = b       | --         | w = b
     * imposed night        | mix -> night | mix -> night | (its input) | x (1 - n)  | --
     * @endverbatim
     * b = @ref GetOverlayBlend, int = @ref GetWeatherIntensity, n = imposed
     * night. Re-verify against GetAmbientColor, GetSkyColor,
     * GetStarVisibility, GetCelestialFade and GetAuroraFade in TimeManager.cpp.
     * @ref GetSunColor is absent on purpose: it folds no stage at all.
     *
     * Imposed night is `clamp(GetStarVisibility() - natural star visibility,
     * 0, 1)`: a weather that explicitly raises star visibility above the
     * natural hour (for example MeteorShower, as base weather or as overlay)
     * drags the ambient toward the night anchor (0.30, 0.30, 0.45) and the
     * sky toward the night sky (0.04, 0.04, 0.12) by that amount, and fades
     * the celestial bodies out through @ref GetCelestialFade. Aurora has no
     * star override and therefore never imposes night.
     * @{
     */

    /**
     * @brief Get the ambient light color multiplier.
     *
     * Applied to all world sprites. Starts from the time-of-day anchor table
     * in the class docs, multiplies by the weather's ambient tint (mixed in by
     * intensity), blends the transition endpoints, folds the overlay tint, and
     * finally mixes toward the night ambient by the imposed-night amount.
     *
     * @return RGB color multiplier (typically 0.0-1.0 per channel).
     */
    glm::vec3 GetAmbientColor() const;

    /**
     * @brief Get the sky background color.
     *
     * Deep blue (night) to light blue (day) with orange/pink transitions, then
     * replaced by the weather's sky override where one exists (mixed in by
     * intensity and ramped across dawn/dusk), blended across transition
     * endpoints, and mixed toward the night sky by the imposed-night amount.
     * The manual overlay contributes no sky color of its own - it reaches the
     * sky only through imposed night.
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
     * Pure time-of-day: unlike the other members of this group, it folds no
     * weather tint, no blend endpoints, no overlay and no imposed night.
     *
     * @return RGB sun color, or (0, 0, 0) while the sun is below the horizon
     *         (see @ref GetSunArc). Callers that multiply by this color must
     *         gate on the arc first.
     */
    glm::vec3 GetSunColor() const;

    /**
     * @brief Get the resolved star visibility factor.
     *
     * The natural fade (in over 18:00-20:00, out over 05:00-07:00) is only the
     * starting point: a weather with a non-negative @c starVisibilityOverride
     * pulls it toward that override by @ref GetWeatherIntensity, transition
     * endpoints are lerped, and the manual overlay's own resolved value is
     * folded in by @ref GetOverlayBlend. So this can read 1.0 at noon
     * (MeteorShower) or 0.0 at midnight (heavy precipitation).
     *
     * Use @ref GetNaturalStarVisibility for consumers that must track the
     * clock rather than the weather.
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

    /// @name Weather control
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
     * The "how dark is the night" scalar for consumers that must follow the
     * clock, not the storm. Its one production caller is the particle
     * splash/impact scene-night factor, which uses
     * `max(natural, GetStarVisibility())` so a night storm that forces star
     * visibility to 0 still reads as night. World lights and PostFX grading
     * deliberately use @ref GetStarVisibility instead, so a storm dims them.
     *
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
     * @par Effective-only mode
     * Passing null for both endpoints is a supported publication, not a
     * mistake: @ref HasWeatherBlend then reports false, so the getters fall
     * back to the single-definition path keyed by @ref GetWeather, while
     * @ref GetEffectiveWeatherDefinition still serves @p effective. The
     * director uses exactly that - `SetWeatherBlend(nullptr, nullptr, 1.0f,
     * &amp;effective)` - during post-transition fog decay, where only the fog
     * multiplier still differs from the table. Mixed null/non-null endpoints
     * are not a defined mode.
     *
     * @param from Outgoing weather definition, or null for effective-only mode.
     *             Non-owning; must outlive use (the director publishes member
     *             storage that persists across the frame).
     * @param to Incoming weather definition, or null (see above). Non-owning,
     *           same lifetime contract.
     * @param t Blend weight in [0, 1] (clamped); 0 = fully @p from, 1 = fully @p to.
     *          Ignored while the endpoints are null.
     * @param effective Director-owned blended definition served by
     *                  @ref GetEffectiveWeatherDefinition. Non-owning, same
     *                  lifetime contract. Null reverts that getter to the table.
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
     * @ref GetEffectiveWeatherDefinition's @c showCelestialBodies flag. That
     * base is then scaled by imposed night: `fade = base * (1 -
     * ImposedNightAmount())`. A weather that raises star visibility above the
     * natural hour therefore fades the daytime sun/moon out even when the
     * published fade is 1.0; at night imposed night is 0 and the real moon is
     * untouched. Unlike @ref GetAuroraFade, this getter ignores the manual
     * overlay except through that imposed-night term.
     *
     * @return Fade factor in [0, 1].
     */
    float GetCelestialFade() const;

    /**
     * @brief Get the aurora visibility fade.
     *
     * Returns the published fade set by @ref SetWeatherFades if one is
     * active, otherwise derives a binary value from
     * @ref GetEffectiveWeatherDefinition's @c showAurora flag. The manual
     * overlay is then folded in by @ref GetOverlayBlend, so an aurora overlay
     * raises the fade even when the base weather has @c showAurora false -
     * which is what turns the SkyRenderer aurora pass on. Unlike
     * @ref GetCelestialFade, this getter is not scaled by imposed night.
     *
     * @return Fade factor in [0, 1].
     */
    float GetAuroraFade() const;

    /**
     * @brief Activate the manual sky overlay.
     *
     * The overlay contributes only sky fields (star visibility, aurora fade,
     * meteor rate, ambient tint) folded onto the base weather's resolved
     * value in @ref GetStarVisibility, @ref GetAmbientColor,
     * @ref GetAuroraFade, and @ref GetEffectiveMeteorRate. @ref GetOverlayBlend
     * eases toward 1 in @ref Update or @ref UpdateWeatherEffects rather than
     * snapping.
     *
     * @param state Overlay weather state.
     */
    void SetWeatherOverlay(WeatherState state);

    /**
     * @brief Deactivate the manual sky overlay.
     *
     * @ref GetOverlayBlend eases back to 0 in @ref Update or
     * @ref UpdateWeatherEffects (fade-out); the overlay state itself is kept
     * so the fade-out resolves to the correct weather while blend > 0.
     */
    void ClearWeatherOverlay();

    /**
     * @brief Check whether the manual sky overlay is active.
     * @return true if @ref SetWeatherOverlay was called more recently than
     *         @ref ClearWeatherOverlay (blend target = 1).
     */
    bool HasWeatherOverlay() const { return m_OverlayActive; }

    /**
     * @brief Get the current (or fading-out) overlay weather state.
     * @return The last state passed to @ref SetWeatherOverlay.
     */
    WeatherState GetWeatherOverlay() const { return m_OverlayWeather; }

    /**
     * @brief Get the eased overlay fade scalar.
     * @return 0 (no overlay contribution) to 1 (overlay fully folded in).
     */
    float GetOverlayBlend() const { return m_OverlayBlend; }

    /**
     * @brief Get the base weather's meteor rate folded with the overlay's.
     * @return Meteor spawn-rate multiplier (1.0 = default cadence).
     */
    float GetEffectiveMeteorRate() const;

    /// @}

    /// @name Time control
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
     * @note No production code calls this; only tests/ exercises it. The game
     *       runs on the 24 s class default, and @ref Initialize would reset any
     *       value set here, so a caller must apply it after initialization.
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

    /// @brief Toggle pause state.
    void TogglePause() { m_Paused = !m_Paused; }

    /// @}

private:
    /// @name Helper methods
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
     * Returns 0.0 before start, 1.0 after end, and a linear ramp between:
     * @f[
     * f(t; a, b) =
     * \begin{cases}
     * 0, & b \le a \\
     * \operatorname{clamp}\left(\frac{t-a}{b-a}, 0, 1\right), & b > a
     * \end{cases}
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

    /**
     * @brief Extra night the active weather config imposes beyond the natural
     *        time-of-day: `clamp(GetStarVisibility() - NaturalStarVisibility(), 0, 1)`.
     *
     * Non-zero when a weather explicitly raises star visibility above the natural hour
     * (for example MeteorShower) as the base or overlay. Aurora deliberately has no
     * star override and therefore never imposes night.
     */
    float ImposedNightAmount() const;

    /// @}

    /// @name State
    /// @{
    float m_CurrentTime;             ///< Current time in hours (0.0-24.0).
    int m_DayCount;                  ///< Days elapsed (for moon phases).
    float m_TimeScale;               ///< Time progression multiplier (1.0 = normal).
    float m_DayDuration;             ///< Real seconds per game day (24 s default: 1 game hour/s).
    WeatherState m_Weather;          ///< Current weather condition.
    float m_WeatherIntensity{1.0f};  ///< Particle/effect density 0-1.
    bool m_Paused{false};            ///< Whether time progression is paused.
    WeatherState m_OverlayWeather{
        WeatherState::Clear};     ///< Manual sky overlay (valid while blend > 0).
    bool m_OverlayActive{false};  ///< Overlay set (blend target = 1).
    float m_OverlayBlend{0.0f};   ///< Eased 0-1 fade of the whole overlay.
    /// @}

    /**
     * @name Weather blend state
     * @brief Non-owning pointers published by WeatherDirector.
     *
     * Cleared by @ref ClearWeatherBlend and never dereferenced when null; the director
     * is responsible for publishing storage that outlives the frame.
     * @{
     */
    const WeatherDefinition* m_BlendFrom{nullptr};       ///< Outgoing endpoint (null = no blend).
    const WeatherDefinition* m_BlendTo{nullptr};         ///< Incoming endpoint.
    const WeatherDefinition* m_BlendEffective{nullptr};  ///< Director-owned blended def.
    float m_BlendT{0.0f};                                ///< Eased blend weight [0, 1].
    bool m_HasResolvedFrom{false};  ///< Use m_ResolvedFrom instead of m_BlendFrom formulas.
    ResolvedWeatherChannels m_ResolvedFrom{};  ///< Captured retarget from-values.
    float m_CelestialFade{-1.0f};              ///< Published fade; < 0 = derive from def bool.
    float m_AuroraFade{-1.0f};                 ///< Published fade; < 0 = derive from def bool.
    /// @}

    /// @name Time period boundaries
    /// @brief Hour values defining period transitions.
    /// @{
    static constexpr float DAWN_START = 5.0f;      ///< Dawn begins.
    static constexpr float DAWN_END = 7.0f;        ///< Dawn ends, morning begins.
    static constexpr float MORNING_END = 10.0f;    ///< Morning ends, midday begins.
    static constexpr float MIDDAY_END = 16.0f;     ///< Midday ends, afternoon begins.
    static constexpr float AFTERNOON_END = 18.0f;  ///< Afternoon ends, dusk begins.
    static constexpr float DUSK_END = 20.0f;       ///< Dusk ends, evening begins.
    static constexpr float EVENING_END = 22.0f;    ///< Evening ends, night begins.
    static constexpr float NIGHT_END = 4.0f;       ///< Night ends (wraps), late night begins.
    /// @}

    /// @name Celestial boundaries
    /// @brief Hour values for sun/moon rise and set times.
    /// @{
    static constexpr float SUNRISE_TIME = 6.0f;    ///< Sun rises above horizon.
    static constexpr float SUNSET_TIME = 20.0f;    ///< Sun sets below horizon.
    static constexpr float MOONRISE_TIME = 19.0f;  ///< Moon rises above horizon.
    static constexpr float MOONSET_TIME = 7.0f;    ///< Moon sets below horizon.
    /// @}

    /// @name Moon phase constants
    /// @{
    static constexpr int MOON_CYCLE_DAYS = 8;  ///< Days for complete lunar cycle.
    /// @}
};
