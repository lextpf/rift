#include "WeatherDirector.hpp"

#include "AmbienceConfig.hpp"
#include "WeatherBlend.hpp"

#include <algorithm>
#include <cmath>

namespace
{
// True for the three night-event states (AuroraNight/MeteorShower/
// FireflySwarm). Used by reconciliation to detect a release edge (event ->
// front) so the release also gets the event transition duration, matching
// the engage edge - both feel like the event's boundary, not a slow front
// crossfade.
bool IsNightEventState(WeatherState state)
{
    return state == WeatherState::AuroraNight || state == WeatherState::MeteorShower ||
           state == WeatherState::FireflySwarm;
}
}  // namespace

void WeatherDirector::Update(float deltaTime, TimeManager& time)
{
    // Gust envelope: session-constant phases (from the forecast seed),
    // continuous across transitions AND day rollovers (only the base blends;
    // the phases and clock never reset - see m_GustPhases, handoff a).
    m_Clock += deltaTime;
    m_WindStrength =
        GustWindStrength(time.GetEffectiveWeatherDefinition().windIntensity, m_Clock, m_GustPhases);
    m_WindDir = GustWindDirection(m_Clock, m_GustPhases);

    // Forecast reconciliation (level-based): compute what the forecast wants
    // RIGHT NOW and request it when it differs from the current target. One
    // rule covers boundary crossings, time.set jumps in both directions, and
    // event/front precedence (the event wins during its dusk->dawn window).
    if (m_Enabled && m_AutoWeather)
    {
        const int64_t day = time.GetDayCount();
        const int64_t frontIndex = ForecastFrontIndex(m_ForecastSeed, day);
        if (m_ManualHoldSet && frontIndex != m_ManualHoldFront)
        {
            m_ManualHoldSet = false;  // new front: the world takes over again
        }
        if (!m_ManualHoldSet)
        {
            const float hour = time.GetTimeOfDay();
            const ForecastEntry today = ForecastForDay(m_ForecastSeed, day);
            const ForecastEntry yesterday = ForecastForDay(m_ForecastSeed, day - 1);
            WeatherState desired = today.front;
            bool eventWindow = false;
            if (today.hasNightEvent && hour >= 20.0f)
            {
                desired = today.nightEvent;
                eventWindow = true;
            }
            else if (yesterday.hasNightEvent && hour < 5.0f)
            {
                desired = yesterday.nightEvent;  // overlay spans midnight
                eventWindow = true;
            }
            const WeatherState target = m_Active ? m_ToState : time.GetWeather();
            if (desired != target)
            {
                // Release (event -> front) is also an event edge: without
                // this, leaving the window would fall through to the slow
                // front duration while entering used the fast one.
                const bool eventEdge = eventWindow || IsNightEventState(target);
                StartWeatherChange(time,
                                   desired,
                                   eventEdge ? ambience::WEATHER_EVENT_TRANSITION_SECONDS
                                             : ambience::WEATHER_TRANSITION_SECONDS);
            }
        }
    }

    if (m_Active)
    {
        m_Elapsed += deltaTime;
        if (m_Elapsed >= m_Duration)
        {
            // Transition complete. If a fog hold is engaged, keep publishing
            // an effective def whose fog multiplier decays over the residual
            // puff lifetime; otherwise revert to passthrough now.
            m_Active = false;
            if (m_FogHoldActive)
            {
                m_FogDecayActive = true;
                m_FogDecayElapsed = 0.0f;
            }
            else
            {
                time.ClearWeatherBlend();
                return;
            }
        }
        Publish(time);
        return;
    }

    if (m_FogDecayActive)
    {
        m_FogDecayElapsed += deltaTime;
        if (m_FogDecayElapsed >= ambience::WEATHER_FOG_HOLD_DECAY_SECONDS)
        {
            m_FogDecayActive = false;
            m_FogHoldActive = false;
            time.ClearWeatherBlend();
            return;
        }
        Publish(time);
    }
}

void WeatherDirector::RequestWeather(TimeManager& time, WeatherState target, float durationSeconds)
{
    if (m_Enabled)
    {
        // Arm the manual hold on the front active right now, so
        // reconciliation does not stomp this request until the front
        // actually rolls over. A disabled/title hard-set (Enabled false)
        // must not arm a gameplay hold that would outlive the title world.
        m_ManualHoldFront = ForecastFrontIndex(m_ForecastSeed, time.GetDayCount());
        m_ManualHoldSet = true;
    }
    StartWeatherChange(time, target, durationSeconds);
}

void WeatherDirector::StartWeatherChange(TimeManager& time,
                                         WeatherState target,
                                         float durationSeconds)
{
    const bool sameTarget =
        (m_Active && target == m_ToState) || (!m_Active && target == time.GetWeather());
    if (sameTarget)
    {
        return;
    }

    if (!m_Enabled || durationSeconds <= 0.0f)
    {
        // Hard cut: debug/console-on-title path. Clears any in-flight state.
        m_Active = false;
        m_FogHoldActive = false;
        m_FogDecayActive = false;
        m_UseResolvedFrom = false;
        m_FromState = target;  // handoff (c): no stale pair after a hard cut
        m_ToState = target;
        time.SetWeather(target);
        time.ClearWeatherBlend();
        return;
    }

    // Retarget: continue from the current blended def, and capture the
    // resolved getter outputs as the exact from-endpoint (sentinel formulas
    // are not invertible at fractional intensity). Captured BEFORE any state
    // changes; Publish re-applies the capture after every publication.
    if (m_Active)
    {
        m_ResolvedFrom.ambient = time.GetAmbientColor();
        m_ResolvedFrom.sky = time.GetSkyColor();
        m_ResolvedFrom.starVisibility = time.GetStarVisibility();
        m_UseResolvedFrom = true;
        m_FromCelestialFade = time.GetCelestialFade();
        m_FromAuroraFade = time.GetAuroraFade();
        m_FromDef = m_Effective;
        m_FromState = m_ToState;
    }
    else
    {
        m_UseResolvedFrom = false;
        m_FromState = time.GetWeather();
        m_FromDef = GetWeatherDefinition(m_FromState);
        if (m_FogDecayActive)
        {
            // A fog decay is still publishing a mid-decay multiplier; seed the
            // new from-endpoint with it so the blend continues from the
            // on-screen value instead of snapping to the table's.
            m_FromDef.fogAlphaMultiplier = m_Effective.fogAlphaMultiplier;
        }
        m_FromCelestialFade = m_FromDef.showCelestialBodies ? 1.0f : 0.0f;
        m_FromAuroraFade = m_FromDef.showAurora ? 1.0f : 0.0f;
    }

    // Fog hold: fog -> no-fog keeps the outgoing multiplier so surviving
    // puffs don't brighten toward the destination's neutral 1.0.
    const WeatherDefinition& toDef = GetWeatherDefinition(target);
    m_FogDecayActive = false;
    m_FogHoldActive = WeatherSpawnsFogType(m_FromDef) && !WeatherSpawnsFogType(toDef);
    if (m_FogHoldActive)
    {
        m_FogHoldValue = m_FromDef.fogAlphaMultiplier;
    }

    m_ToState = target;
    m_Duration = durationSeconds;
    m_Elapsed = 0.0f;
    m_Active = true;
    time.SetWeather(target);  // GetWeather reports the destination from frame 1
    Publish(time);
}

void WeatherDirector::Reset(TimeManager& time)
{
    m_Active = false;
    m_FogHoldActive = false;
    m_FogDecayActive = false;
    m_UseResolvedFrom = false;
    m_Elapsed = 0.0f;
    m_ManualHoldSet = false;  // world reload: the forecast is free to drive again
    time.ClearWeatherBlend();

    // Title pushes wind unconditionally while the director never updates
    // there - restore the calm defaults so a quit-to-title doesn't freeze
    // the last gust instant into the backdrop. The clock deliberately
    // survives (gusts must not re-sync on world loads).
    m_WindDir = glm::normalize(ambience::CLOUD_SHADOW_WIND_DIR);
    m_WindStrength = 0.5f;
}

void WeatherDirector::SetForecastSeed(uint64_t seed)
{
    m_ForecastSeed = seed;
    // Session-constant gust phases derive from the forecast seed, not the
    // day count (handoff a) - recompute so a seed change stays consistent
    // with the freshly-seeded forecast.
    m_GustPhases = GustPhases(SplitMix64(m_ForecastSeed));
}

void WeatherDirector::SetAutoWeather(bool enabled)
{
    m_AutoWeather = enabled;
    if (enabled)
    {
        m_ManualHoldSet = false;  // re-enabling autonomy takes over immediately
    }
}

ForecastEntry WeatherDirector::GetForecast(const TimeManager& time, int64_t dayOffset) const
{
    return ForecastForDay(m_ForecastSeed, static_cast<int64_t>(time.GetDayCount()) + dayOffset);
}

float WeatherDirector::Progress() const
{
    return m_Active ? BlendSmoothstep(m_Elapsed / std::max(m_Duration, 0.001f)) : 1.0f;
}

WeatherDirector::Transition WeatherDirector::GetTransition() const
{
    Transition t;
    t.from = m_FromState;
    t.to = m_ToState;
    t.active = m_Active;
    t.progress = Progress();
    return t;
}

WeatherDirector::SpawnStreams WeatherDirector::GetSpawnStreams() const
{
    SpawnStreams streams;
    if (m_Active)
    {
        streams.outgoing = &m_FromDef;
        streams.incoming = &GetWeatherDefinition(m_ToState);
        streams.weight = Progress();
    }
    return streams;
}

void WeatherDirector::Publish(TimeManager& time)
{
    const WeatherDefinition& fromDef = m_FromDef;
    const WeatherDefinition& toDef = GetWeatherDefinition(m_ToState);

    if (m_Active)
    {
        const float s = Progress();
        m_Effective = BlendWeatherDefinitions(fromDef, toDef, s);
        if (m_FogHoldActive)
        {
            m_Effective.fogAlphaMultiplier = m_FogHoldValue;
        }
        time.SetWeatherBlend(&m_FromDef, &toDef, s, &m_Effective);
        if (m_UseResolvedFrom)
        {
            time.SetWeatherBlendResolvedFrom(m_ResolvedFrom);
        }
        const float celestialFade =
            std::lerp(m_FromCelestialFade, toDef.showCelestialBodies ? 1.0f : 0.0f, s);
        const float auroraFade = std::lerp(m_FromAuroraFade, toDef.showAurora ? 1.0f : 0.0f, s);
        time.SetWeatherFades(celestialFade, auroraFade);
        return;
    }

    // Post-transition fog decay: destination def with an easing multiplier.
    const float decayT =
        std::clamp(m_FogDecayElapsed / ambience::WEATHER_FOG_HOLD_DECAY_SECONDS, 0.0f, 1.0f);
    m_Effective = toDef;
    m_Effective.fogAlphaMultiplier = std::lerp(m_FogHoldValue, toDef.fogAlphaMultiplier, decayT);
    // Getter blending is over (from = null -> passthrough); only the
    // effective def still differs from the table.
    time.SetWeatherBlend(nullptr, nullptr, 1.0f, &m_Effective);
    time.SetWeatherFades(toDef.showCelestialBodies ? 1.0f : 0.0f, toDef.showAurora ? 1.0f : 0.0f);
}
