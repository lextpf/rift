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
 * publishes the blend through the TimeManager facade. Deliberately NOT an
 * ECS system: no entity data is involved.
 *
 * Phase 1-2 delivered manual transitions (console), fog hold/decay, and wind
 * gusts. Phase 3 adds autonomy: each Update() reconciles the published
 * weather against the deterministic forecast (WeatherBlend's
 * ForecastForDay/ForecastFrontIndex) and starts a transition on its own when
 * they disagree. A console RequestWeather() arms a manual hold that
 * suspends reconciliation for the rest of the current forecast front; see
 * docs/superpowers/specs/2026-07-02-weather-director-design.md.
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
     */
    void Update(float deltaTime, TimeManager& time);

    /**
     * @brief Request a weather change from the manual/console path.
     *
     * Public wrapper over StartWeatherChange() that also arms the manual hold (records
     * the forecast front so reconciliation won't stomp it for that front), but only when
     * the director is enabled. Semantics:
     *  - durationSeconds <= 0, or disabled: hard SetWeather, no blend.
     *  - Already targeting @p target: no-op.
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

    /// Spawn-stream endpoints for ParticleSystem's dual-stream transition
    /// spawning. outgoing is null when no transition is active.
    struct SpawnStreams
    {
        const WeatherDefinition* outgoing{nullptr};  ///< Null when no transition.
        const WeatherDefinition* incoming{nullptr};  ///< Table def of the target.
        float weight{0.0f};                          ///< Eased blend weight [0, 1].
    };

    SpawnStreams GetSpawnStreams() const;

    /// Gusted wind direction (normalized). Updated every Update() call; frozen
    /// while Paused (no Update calls).
    glm::vec2 GetWindDirection() const { return m_WindDir; }

    /// Gusted wind strength for the current frame, derived from the effective
    /// weather def's windIntensity through the Task 1 gust envelope.
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
    /// by GetTransition(), GetSpawnStreams(), and Publish() (handoff b).
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

    /// Retarget capture: exact resolved from-endpoint for the whole retargeted
    /// transition. Publish re-applies it every frame (SetWeatherBlend
    /// invalidates captures on each publication).
    bool m_UseResolvedFrom{false};
    ResolvedWeatherChannels m_ResolvedFrom{};

    /// Fade values at transition start (fresh: from-def bools as 0/1;
    /// retarget: the currently published fades, so a mid-lerp fade continues
    /// instead of snapping to the old destination's bool).
    float m_FromCelestialFade{1.0f};
    float m_FromAuroraFade{0.0f};

    double m_Clock{0.0};  ///< Real-seconds accumulator for the gust envelope.
    glm::vec2 m_WindDir{
        glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR)};  ///< Gusted wind direction (normalized).
    float m_WindStrength{0.5f};  ///< Gusted strength; 0.5 = engine-wide base.

    /// Forecast seed for ForecastFrontIndex()/ForecastForDay() rolls (front
    /// weather + night events). SetForecastSeed() overrides the default and
    /// recomputes m_GustPhases to match.
    uint64_t m_ForecastSeed{0x9E3779B97F4A7C15ULL};
    bool m_AutoWeather{true};  ///< false = manual sticky (see SetAutoWeather).

    /// Manual hold: armed by RequestWeather() (only when m_Enabled), cleared when the
    /// forecast front changes or by Reset()/SetAutoWeather(true). Suspends reconciliation
    /// for the rest of the front the console override was made on.
    bool m_ManualHoldSet{false};
    int64_t m_ManualHoldFront{0};

    /// Session-constant gust phase offsets from m_ForecastSeed (recomputed in
    /// SetForecastSeed()); deriving them per-day instead re-rolled the wind at midnight.
    glm::vec3 m_GustPhases{GustPhases(SplitMix64(0x9E3779B97F4A7C15ULL))};
};
