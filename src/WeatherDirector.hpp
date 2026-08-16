#pragma once

#include "AmbienceConfig.hpp"
#include "TimeManager.hpp"
#include "WeatherBlend.hpp"
#include "WeatherDefinitions.hpp"

/**
 * @class WeatherDirector
 * @brief Owns weather choreography: smooth transitions between weather states.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Game-owned subsystem (by value, like TimeManager). Each frame it advances
 * the active transition, blends the endpoint definitions into m_Effective
 * (stable member storage - consumers hold the pointer across the frame), and
 * publishes the blend through the TimeManager facade. Deliberately not an
 * ECS system: no entity data is involved.
 *
 * Phase 1-2 delivered manual transitions (console), fog hold/decay, and wind
 * gusts. Phase 3 adds autonomy: each Update() reconciles the published
 * weather against the deterministic forecast (WeatherBlend's
 * ForecastForDay/ForecastFrontIndex) and starts a transition on its own when
 * they disagree. A console RequestWeather() arms a manual hold that
 * suspends reconciliation for the rest of the current forecast front;
 *
 * @par Publication state machine
 * The director has exactly three publication states. Each edge is labelled
 * with the TimeManager call it makes. The two publishing states (Transition and
 * FogDecay) re-run Publish() once per frame while they last, so SetWeatherBlend
 * is re-issued every frame (which is why a retarget capture must be re-applied
 * every frame - see TimeManager::SetWeatherBlend).
 *
 * @htmlonly
 * <pre class="mermaid">
 * stateDiagram-v2
 *     [*] --> Idle
 *     Idle --> Transition: StartWeatherChange<br/>SetWeatherBlend + SetWeatherFades
 *     Transition --> Transition: retarget<br/>+ SetWeatherBlendResolvedFrom
 *     Transition --> Idle: elapsed >= duration, no fog hold<br/>ClearWeatherBlend
 *     Transition --> FogDecay: elapsed >= duration, fog hold engaged<br/>SetWeatherBlend null,null
 *     FogDecay --> Idle: decay done<br/>ClearWeatherBlend
 *     FogDecay --> Transition: StartWeatherChange<br/>seeds from-def with live fog alpha
 *     Transition --> Idle: hard cut or Reset<br/>SetWeather + ClearWeatherBlend
 *     FogDecay --> Idle: hard cut or Reset<br/>ClearWeatherBlend
 * </pre>
 * @endhtmlonly
 *
 * - Idle: nothing published. The TimeManager getters resolve the single
 *   definition for TimeManager::GetWeather.
 * - Transition: `m_Active`. Publish blends m_FromDef into the destination
 *   table def, stores it in m_Effective, and publishes both endpoints plus
 *   lerped celestial/aurora fades. A retarget (StartWeatherChange while
 *   active) snapshots the resolved getters as the new from-endpoint and
 *   re-enters this state rather than restarting from the table.
 * - FogDecay: entered only when the transition engaged the fog hold, i.e.
 *   the outgoing weather spawned Fog-type particles and the incoming one does
 *   not. It publishes `SetWeatherBlend(nullptr, nullptr, 1.0f, &m_Effective)`
 *   - effective-only mode - so the getters run the plain single-definition
 *   path while the fog multiplier eases from the held value back to the
 *   destination's over ambience::WEATHER_FOG_HOLD_DECAY_SECONDS.
 *
 * The hard-cut edge fires whenever the director is disabled or the requested
 * duration is <= 0; it also collapses m_FromState/m_ToState onto the target so
 * GetTransition never reports a stale pair.
 */
class WeatherDirector
{
public:
    /// Snapshot of the active transition for debug UI / console status.
    struct Transition
    {
        WeatherState from{WeatherState::Clear};
        WeatherState to{WeatherState::Clear};
        float progress{1.0f};  ///< Eased blend weight; 1.0 when idle.
        bool active{false};
    };

    /**
     * @brief Advance the transition and publish blend state to @p time.
     *
     * Call once per frame, only in GameMode::Playing, immediately after
     * TimeManager::Update. Not calling it freezes the transition (Paused).
     *
     * @param deltaTime Real frame seconds. Unscaled by `time.scale` and unaffected by
     *                  `time.freeze`: the transition, fog-decay and gust clocks all run
     *                  in real seconds.
     * @param time      Facade the blend is published through, and the source of the
     *                  game-scaled day and hour used by forecast reconciliation.
     */
    void Update(float deltaTime, TimeManager& time);

    /**
     * @brief Request a weather change from the manual/console path.
     *
     * Public wrapper over StartWeatherChange() that also arms the manual hold (records
     * the forecast front so reconciliation won't stomp it for that front), but only when
     * the director is enabled. Semantics:
     *  - durationSeconds <= 0, or disabled: hard SetWeather, no blend.
     *  - Already targeting @p target: no transition is started or retargeted, but the
     *    manual hold is still armed (when enabled), so reconciliation stays suspended
     *    for the rest of the current front.
     *  - Mid-transition: retarget from the blended state (resolved channels captured for
     *    a seamless switch); TimeManager::GetWeather() reports @p target immediately.
     */
    void RequestWeather(TimeManager& time, WeatherState target, float durationSeconds);

    /**
     * @brief Clear all transition/hold state and the published blend.
     *
     * Call alongside every TimeManager::Initialize() so no stale blend, fog hold, or
     * manual forecast hold survives into the next world. Leaves SetAutoWeather()'s
     * setting and the gust clock untouched (gusts must not re-sync on world loads).
     * The published gust outputs are still snapped back to calm defaults (base
     * direction, strength 0.5), so a quit-to-title does not freeze the last gust
     * instant into the title backdrop; only the clock and phases survive.
     */
    void Reset(TimeManager& time);

    /// Enable/disable transitions (Game: true on entering gameplay worlds,
    /// false on the title world). Disabled requests degrade to hard sets.
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

    bool IsTransitioning() const { return m_Active; }
    Transition GetTransition() const;

    /**
     * @brief Set the deterministic forecast seed (front + night-event rolls).
     *
     * Also recomputes the session-constant gust phases from the new seed so wind
     * strength stays continuous across midnight instead of stepping per day.
     *
     * @param seed World seed passed to ForecastFrontIndex/ForecastForDay.
     */
    void SetForecastSeed(uint64_t seed);

    /// The active forecast seed (default: a fixed constant).
    uint64_t GetForecastSeed() const { return m_ForecastSeed; }

    /**
     * @brief Enable/disable forecast-driven autonomy.
     *
     * Default true. false = manual sticky: reconciliation never fires (forecast fully
     * suspended, not just held for one front) until re-enabled; re-enabling also clears
     * any manual hold so the forecast takes over immediately.
     *
     * @param enabled true to let reconciliation drive the weather forward.
     */
    void SetAutoWeather(bool enabled);
    bool IsAutoWeather() const { return m_AutoWeather; }

    /// True while a manual RequestWeather() is holding reconciliation back
    /// for the rest of the current forecast front (console `weather.status`).
    bool IsManualHold() const { return m_ManualHoldSet; }

    /**
     * @brief The forecast for a day relative to @p time's current day.
     * @param time Source of the current day count.
     * @param dayOffset Days from today; 0 = today, negative = past.
     * @return The forecast entry for that day under the active seed.
     */
    ForecastEntry GetForecast(const TimeManager& time, int64_t dayOffset) const;

    /**
     * @brief Spawn-stream endpoints for ParticleSystem's four-stream transition
     *        spawning (outgoing primary/secondary plus incoming primary/secondary).
     *
     * Both pointers are null when no transition is active; a caller must check both,
     * as ParticleSystem does, before taking the transition path.
     */
    struct SpawnStreams
    {
        const WeatherDefinition* outgoing{nullptr};  ///< Outgoing endpoint; null when idle.
        const WeatherDefinition* incoming{nullptr};  ///< Target's table def; null when idle.
        float weight{0.0f};                          ///< Eased blend weight [0, 1].
    };

    SpawnStreams GetSpawnStreams() const;

    /// Gusted wind direction (normalized). Updated every Update() call; frozen
    /// while Paused (no Update calls).
    glm::vec2 GetWindDirection() const { return m_WindDir; }

    /// Gusted wind strength for the current frame, derived from the effective
    /// weather def's windIntensity through GustWindStrength (WeatherBlend.hpp).
    float GetWindStrength() const { return m_WindStrength; }

private:
    /// Recompute m_Effective and republish blend + fades for the current state.
    void Publish(TimeManager& time);

    /**
     * @brief Start (or retarget) a transition toward @p target.
     *
     * Shared engine behind RequestWeather() (manual, arms the hold) and forecast
     * reconciliation (internal, no hold). The hard-cut branch also sets
     * m_FromState = m_ToState = target so GetTransition() never reports a stale pair.
     */
    void StartWeatherChange(TimeManager& time, WeatherState target, float durationSeconds);

    /// Eased [0, 1] progress of the active transition; 1.0 when idle. Shared
    /// by GetTransition(), GetSpawnStreams(), and Publish().
    float Progress() const;

    bool m_Enabled{false};  ///< Off until a gameplay world loads.
    bool m_Active{false};   ///< A transition is in flight.
    WeatherState m_FromState{WeatherState::Clear};
    WeatherState m_ToState{WeatherState::Clear};
    WeatherDefinition m_FromDef{};    ///< Outgoing endpoint (copy; stable storage).
    WeatherDefinition m_Effective{};  ///< Published blended def (stable storage).
    float m_Elapsed{0.0f};
    float m_Duration{0.0f};

    bool m_FogHoldActive{false};   ///< Holding outgoing fogAlphaMultiplier.
    float m_FogHoldValue{1.0f};    ///< Held multiplier value.
    bool m_FogDecayActive{false};  ///< Post-transition decay in progress.
    float m_FogDecayElapsed{0.0f};

    /**
     * @name Retarget capture
     * @brief Exact resolved from-endpoint for the whole retargeted transition.
     *
     * Publish re-applies it every frame (SetWeatherBlend invalidates captures on each
     * publication).
     * @{
     */
    bool m_UseResolvedFrom{false};             ///< True while a retarget capture is live.
    ResolvedWeatherChannels m_ResolvedFrom{};  ///< Captured ambient, sky and star channels.
    /// @}

    /**
     * @name Fades at transition start
     * @brief Fresh start captures the from-def bools as 0/1; a retarget captures the
     *        currently published fades, so a mid-lerp fade continues instead of
     *        snapping to the old destination's bool.
     * @{
     */
    float m_FromCelestialFade{1.0f};  ///< Sun/moon body fade at transition start.
    float m_FromAuroraFade{0.0f};     ///< Aurora band fade at transition start.
    /// @}

    double m_Clock{0.0};  ///< Real-seconds accumulator for the gust envelope.
    glm::vec2 m_WindDir{
        glm::normalize(ambience::WEATHER_WIND_BASE_DIR)};  ///< Gusted wind direction (normalized).
    float m_WindStrength{0.5f};  ///< Gusted strength; 0.5 = engine-wide base.

    /**
     * @brief Forecast seed for ForecastFrontIndex()/ForecastForDay() rolls
     *        (front weather + night events).
     *
     * SetForecastSeed() overrides the default and recomputes m_GustPhases to match.
     */
    uint64_t m_ForecastSeed{0x9E3779B97F4A7C15ULL};
    bool m_AutoWeather{true};  ///< false = manual sticky (see SetAutoWeather).

    /**
     * @name Manual hold
     * @brief Armed by RequestWeather() (only when m_Enabled), cleared when the forecast
     *        front changes or by Reset()/SetAutoWeather(true).
     *
     * Suspends reconciliation for the rest of the front the console override was made
     * on.
     * @{
     */
    bool m_ManualHoldSet{false};   ///< True while the hold suspends reconciliation.
    int64_t m_ManualHoldFront{0};  ///< Forecast front the console override was made on.
    /// @}

    /// Session-constant gust phase offsets from m_ForecastSeed (recomputed in
    /// SetForecastSeed()); deriving them per-day instead re-rolled the wind at midnight.
    glm::vec3 m_GustPhases{GustPhases(SplitMix64(0x9E3779B97F4A7C15ULL))};
};
