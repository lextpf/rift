#include "TimeManager.hpp"

#include "AmbienceConfig.hpp"

#include <algorithm>
#include <cmath>

TimeManager::TimeManager()
    : m_CurrentTime(12.0f),
      m_DayCount(0),
      m_TimeScale(1.0f),
      m_DayDuration(24.0f),
      m_Weather(WeatherState::Clear)
{
}

void TimeManager::Initialize()
{
    m_CurrentTime = 12.0f;
    m_DayCount = 0;
    m_TimeScale = 1.0f;
    m_DayDuration = 24.0f;
    m_Weather = WeatherState::Clear;
    m_WeatherIntensity = 1.0f;
    m_Paused = false;
    ClearWeatherBlend();
}

void TimeManager::SetWeatherIntensity(float value)
{
    m_WeatherIntensity = std::clamp(value, 0.0f, 1.0f);
}

void TimeManager::Update(float deltaTime)
{
    if (m_Paused)
        return;

    if (m_DayDuration <= 0.0f)
        return;

    // Convert real time to game time
    // DayDuration = real seconds for 24 game hours
    float hoursPerSecond = 24.0f / std::max(0.001f, m_DayDuration);
    float timeAdvance = deltaTime * hoursPerSecond * m_TimeScale;

    m_CurrentTime += timeAdvance;

    // Handle day rollover
    while (m_CurrentTime >= 24.0f)
    {
        m_CurrentTime -= 24.0f;
        m_DayCount++;
    }
    while (m_CurrentTime < 0.0f)
    {
        m_CurrentTime += 24.0f;
        m_DayCount--;
    }
}

TimePeriod TimeManager::GetTimePeriod() const
{
    float t = m_CurrentTime;

    if (t >= DAWN_START && t < DAWN_END)
        return TimePeriod::Dawn;
    if (t >= DAWN_END && t < MORNING_END)
        return TimePeriod::Morning;
    if (t >= MORNING_END && t < MIDDAY_END)
        return TimePeriod::Midday;
    if (t >= MIDDAY_END && t < AFTERNOON_END)
        return TimePeriod::Afternoon;
    if (t >= AFTERNOON_END && t < DUSK_END)
        return TimePeriod::Dusk;
    if (t >= DUSK_END && t < EVENING_END)
        return TimePeriod::Evening;
    if (t >= EVENING_END || t < NIGHT_END)
        return TimePeriod::Night;
    // 4:00 - 5:00
    return TimePeriod::LateNight;
}

float TimeManager::GetSunArc() const
{
    // Sun visible from SUNRISE_TIME (6:00) to SUNSET_TIME (20:00)
    if (m_CurrentTime < SUNRISE_TIME || m_CurrentTime > SUNSET_TIME)
        return -1.0f;  // Sun below horizon

    // Map sunrise-sunset to 0-1 arc
    float dayLength = SUNSET_TIME - SUNRISE_TIME;
    return (m_CurrentTime - SUNRISE_TIME) / dayLength;
}

float TimeManager::GetMoonArc() const
{
    // Moon visible from MOONRISE_TIME (19:00) to MOONSET_TIME (7:00 next day)
    float t = m_CurrentTime;

    // Moon is up from 19:00 to 7:00 (12 hours)
    if (t >= MOONRISE_TIME)
    {
        // Evening portion: 19:00 to 24:00 (5 hours = 0 to 0.417)
        return (t - MOONRISE_TIME) / 12.0f;
    }
    else if (t <= MOONSET_TIME)
    {
        // Morning portion: 0:00 to 7:00 (7 hours = 0.417 to 1.0)
        return (t + (24.0f - MOONRISE_TIME)) / 12.0f;
    }

    return -1.0f;  // Moon below horizon
}

int TimeManager::GetMoonPhase() const
{
    // 8 phases over MOON_CYCLE_DAYS
    int phase = m_DayCount % MOON_CYCLE_DAYS;
    if (phase < 0)
        phase += MOON_CYCLE_DAYS;
    return phase;
}

glm::vec3 TimeManager::ComputeAmbientColor(const WeatherDefinition& def) const
{
    float t = m_CurrentTime;

    // Define ambient colors for different times - subtle, not too bright
    // Dawn: soft muted orange/pink
    glm::vec3 dawnColor(0.85f, 0.75f, 0.7f);
    // Morning: warm white (toned below white for daytime exposure headroom)
    glm::vec3 morningColor(0.90f, 0.88f, 0.85f);
    // Midday: just below white for daytime exposure headroom (slight warm).
    // Ambient is an unclamped multiply on albedo with no scene tonemap, so a
    // white midday made noon as bright as the source art allowed; ~7% of
    // headroom here takes the eye-strain off daytime while staying the peak.
    glm::vec3 middayColor(0.93f, 0.93f, 0.91f);
    // Afternoon: warm yellow (toned for daytime exposure headroom)
    glm::vec3 afternoonColor(0.90f, 0.85f, 0.78f);
    // Dusk: muted orange/purple (less saturated)
    glm::vec3 duskColor(0.75f, 0.6f, 0.55f);
    // Evening: deep blue (dim)
    glm::vec3 eveningColor(0.5f, 0.5f, 0.65f);
    // Night: dark blue (very dim)
    glm::vec3 nightColor(0.3f, 0.3f, 0.45f);
    // Late night: slightly brighter blue transitioning to dawn
    glm::vec3 lateNightColor(0.35f, 0.35f, 0.5f);

    glm::vec3 result;

    // Smooth transitions between periods
    if (t >= NIGHT_END && t < DAWN_START)
    {
        // Late night to dawn transition (4:00 - 5:00)
        float factor = GetTransitionFactor(t, NIGHT_END, DAWN_START);
        result = LerpColor(lateNightColor, dawnColor, factor);
    }
    else if (t >= DAWN_START && t < DAWN_END)
    {
        // Dawn (5:00 - 7:00)
        float factor = GetTransitionFactor(t, DAWN_START, DAWN_END);
        result = LerpColor(dawnColor, morningColor, factor);
    }
    else if (t >= DAWN_END && t < MORNING_END)
    {
        // Morning (7:00 - 10:00)
        float factor = GetTransitionFactor(t, DAWN_END, MORNING_END);
        result = LerpColor(morningColor, middayColor, factor);
    }
    else if (t >= MORNING_END && t < MIDDAY_END)
    {
        // Midday (10:00 - 16:00)
        result = middayColor;
    }
    else if (t >= MIDDAY_END && t < AFTERNOON_END)
    {
        // Afternoon (16:00 - 18:00)
        float factor = GetTransitionFactor(t, MIDDAY_END, AFTERNOON_END);
        result = LerpColor(middayColor, afternoonColor, factor);
    }
    else if (t >= AFTERNOON_END && t < DUSK_END)
    {
        // Dusk (18:00 - 20:00)
        float factor = GetTransitionFactor(t, AFTERNOON_END, DUSK_END);
        result = LerpColor(afternoonColor, duskColor, factor);
    }
    else if (t >= DUSK_END && t < EVENING_END)
    {
        // Evening (20:00 - 22:00)
        float factor = GetTransitionFactor(t, DUSK_END, EVENING_END);
        result = LerpColor(duskColor, eveningColor, factor);
    }
    else if (t >= EVENING_END)
    {
        // Night start (22:00 - 24:00)
        float factor = GetTransitionFactor(t, EVENING_END, 24.0f);
        result = LerpColor(eveningColor, nightColor, factor);
    }
    else
    {
        // Deep night (0:00 - 4:00)
        float factor = GetTransitionFactor(t, 0.0f, NIGHT_END);
        result = LerpColor(nightColor, lateNightColor, factor);
    }

    // Weather modulates ambient color. Intensity scales the effect:
    // intensity 0 -> no weather tint; intensity 1 -> full tint.
    glm::vec3 weatherTint =
        glm::mix(glm::vec3(1.0f), def.ambientTintMultiplier, m_WeatherIntensity);
    return result * weatherTint;
}

glm::vec3 TimeManager::NaturalSkyColor() const
{
    float t = m_CurrentTime;

    // Define sky colors for different times - more muted transitions
    glm::vec3 dawnSky(0.7f, 0.5f, 0.4f);          // Muted orange-pink
    glm::vec3 morningSky(0.45f, 0.6f, 0.85f);     // Light blue
    glm::vec3 middaySky(0.4f, 0.55f, 0.8f);       // Blue
    glm::vec3 afternoonSky(0.45f, 0.55f, 0.75f);  // Blue with warmth
    glm::vec3 duskSky(0.6f, 0.4f, 0.35f);         // Muted orange
    glm::vec3 eveningSky(0.12f, 0.12f, 0.28f);    // Dark blue
    glm::vec3 nightSky(0.04f, 0.04f, 0.12f);      // Near black

    // Smooth transitions
    if (t >= NIGHT_END && t < DAWN_START)
    {
        float factor = GetTransitionFactor(t, NIGHT_END, DAWN_START);
        return LerpColor(nightSky, dawnSky, factor);
    }
    else if (t >= DAWN_START && t < DAWN_END)
    {
        float factor = GetTransitionFactor(t, DAWN_START, DAWN_END);
        return LerpColor(dawnSky, morningSky, factor);
    }
    else if (t >= DAWN_END && t < MORNING_END)
    {
        float factor = GetTransitionFactor(t, DAWN_END, MORNING_END);
        return LerpColor(morningSky, middaySky, factor);
    }
    else if (t >= MORNING_END && t < MIDDAY_END)
    {
        return middaySky;
    }
    else if (t >= MIDDAY_END && t < AFTERNOON_END)
    {
        float factor = GetTransitionFactor(t, MIDDAY_END, AFTERNOON_END);
        return LerpColor(middaySky, afternoonSky, factor);
    }
    else if (t >= AFTERNOON_END && t < DUSK_END)
    {
        float factor = GetTransitionFactor(t, AFTERNOON_END, DUSK_END);
        return LerpColor(afternoonSky, duskSky, factor);
    }
    else if (t >= DUSK_END && t < EVENING_END)
    {
        float factor = GetTransitionFactor(t, DUSK_END, EVENING_END);
        return LerpColor(duskSky, eveningSky, factor);
    }
    else if (t >= EVENING_END)
    {
        float factor = GetTransitionFactor(t, EVENING_END, 24.0f);
        return LerpColor(eveningSky, nightSky, factor);
    }
    else
    {
        return nightSky;
    }
}

float TimeManager::SkyDayNightFactor() const
{
    // Ramped day/night factor for the sky override (was a binary IsDay()
    // step; at 24-second days that stepped 3.3x in one frame).
    const float ramp = ambience::WEATHER_SKY_DAYNIGHT_RAMP_HOURS;
    float dayness =
        GetTransitionFactor(m_CurrentTime, SUNRISE_TIME - ramp, SUNRISE_TIME + ramp) *
        (1.0f - GetTransitionFactor(m_CurrentTime, SUNSET_TIME - ramp, SUNSET_TIME + ramp));
    return 0.3f + 0.7f * dayness;
}

glm::vec3 TimeManager::ComputeSkyColor(const WeatherDefinition& def) const
{
    // Weather override (skyColorOverride.x < 0 means "no override").
    if (def.skyColorOverride.x >= 0.0f)
    {
        glm::vec3 overrideSky = def.skyColorOverride * SkyDayNightFactor();
        // Blend toward override by intensity so a low-intensity weather (e.g.
        // light HeavyRain) doesn't fully wash out the natural sky.
        return glm::mix(NaturalSkyColor(), overrideSky, m_WeatherIntensity);
    }
    return NaturalSkyColor();
}

glm::vec3 TimeManager::GetSunColor() const
{
    float arc = GetSunArc();
    if (arc < 0.0f)
        return glm::vec3(0.0f);  // Sun not visible

    // Sun color changes through the day
    // Sunrise/sunset: orange
    // Midday: bright white/yellow
    glm::vec3 sunriseColor(1.0f, 0.6f, 0.3f);  // Orange
    glm::vec3 middayColor(1.0f, 0.98f, 0.9f);  // Bright white-yellow
    glm::vec3 sunsetColor(1.0f, 0.5f, 0.2f);   // Deep orange

    // arc: 0 = sunrise, 0.5 = noon, 1 = sunset
    if (arc < 0.15f)
    {
        // Sunrise transition (0 - 0.15)
        float t = arc / 0.15f;
        return LerpColor(sunriseColor, middayColor, t);
    }
    else if (arc > 0.85f)
    {
        // Sunset transition (0.85 - 1.0)
        float t = (arc - 0.85f) / 0.15f;
        return LerpColor(middayColor, sunsetColor, t);
    }
    else
    {
        return middayColor;
    }
}

float TimeManager::NaturalStarVisibility() const
{
    // Stars fade in during dusk (18:00 - 20:00)
    // Stars fully visible at night (20:00 - 5:00)
    // Stars fade out during dawn (5:00 - 7:00)
    float t = m_CurrentTime;
    if (t >= AFTERNOON_END && t < DUSK_END)
    {
        return GetTransitionFactor(t, AFTERNOON_END, DUSK_END);
    }
    if (t >= DUSK_END || t < DAWN_START)
    {
        return 1.0f;
    }
    if (t >= DAWN_START && t < DAWN_END)
    {
        return 1.0f - GetTransitionFactor(t, DAWN_START, DAWN_END);
    }
    return 0.0f;
}

float TimeManager::ComputeStarVisibility(const WeatherDefinition& def) const
{
    float natural = NaturalStarVisibility();
    // Weather override: blend natural visibility toward override by intensity.
    if (def.starVisibilityOverride >= 0.0f)
    {
        return std::clamp(
            std::lerp(natural, def.starVisibilityOverride, m_WeatherIntensity), 0.0f, 1.0f);
    }
    return natural;
}

glm::vec3 TimeManager::GetAmbientColor() const
{
    if (HasWeatherBlend())
    {
        glm::vec3 from =
            m_HasResolvedFrom ? m_ResolvedFrom.ambient : ComputeAmbientColor(*m_BlendFrom);
        return glm::mix(from, ComputeAmbientColor(*m_BlendTo), m_BlendT);
    }
    return ComputeAmbientColor(GetWeatherDefinition(m_Weather));
}

glm::vec3 TimeManager::GetSkyColor() const
{
    if (HasWeatherBlend())
    {
        glm::vec3 from = m_HasResolvedFrom ? m_ResolvedFrom.sky : ComputeSkyColor(*m_BlendFrom);
        return glm::mix(from, ComputeSkyColor(*m_BlendTo), m_BlendT);
    }
    return ComputeSkyColor(GetWeatherDefinition(m_Weather));
}

float TimeManager::GetStarVisibility() const
{
    if (HasWeatherBlend())
    {
        float from =
            m_HasResolvedFrom ? m_ResolvedFrom.starVisibility : ComputeStarVisibility(*m_BlendFrom);
        return std::lerp(from, ComputeStarVisibility(*m_BlendTo), m_BlendT);
    }
    return ComputeStarVisibility(GetWeatherDefinition(m_Weather));
}

float TimeManager::GetNaturalStarVisibility() const
{
    return NaturalStarVisibility();
}

void TimeManager::SetWeatherBlend(const WeatherDefinition* from,
                                  const WeatherDefinition* to,
                                  float t,
                                  const WeatherDefinition* effective)
{
    m_BlendFrom = from;
    m_BlendTo = to;
    m_BlendT = std::clamp(t, 0.0f, 1.0f);
    m_BlendEffective = effective;
    // A resolved-from capture belongs to one specific blend publication; any
    // republish invalidates it (the caller re-applies it afterward if wanted).
    // Without this, a capture from an old retarget silently corrupts the next
    // transition's from-endpoint.
    m_HasResolvedFrom = false;
}

void TimeManager::SetWeatherBlendResolvedFrom(const ResolvedWeatherChannels& resolved)
{
    m_ResolvedFrom = resolved;
    m_HasResolvedFrom = true;
}

void TimeManager::ClearWeatherBlend()
{
    m_BlendFrom = nullptr;
    m_BlendTo = nullptr;
    m_BlendEffective = nullptr;
    m_BlendT = 0.0f;
    m_HasResolvedFrom = false;
    m_CelestialFade = -1.0f;
    m_AuroraFade = -1.0f;
}

const WeatherDefinition& TimeManager::GetEffectiveWeatherDefinition() const
{
    if (m_BlendEffective != nullptr)
    {
        return *m_BlendEffective;
    }
    return GetWeatherDefinition(m_Weather);
}

void TimeManager::SetWeatherFades(float celestialFade, float auroraFade)
{
    m_CelestialFade = std::clamp(celestialFade, 0.0f, 1.0f);
    m_AuroraFade = std::clamp(auroraFade, 0.0f, 1.0f);
}

float TimeManager::GetCelestialFade() const
{
    if (m_CelestialFade >= 0.0f)
    {
        return m_CelestialFade;
    }
    return GetEffectiveWeatherDefinition().showCelestialBodies ? 1.0f : 0.0f;
}

float TimeManager::GetAuroraFade() const
{
    if (m_AuroraFade >= 0.0f)
    {
        return m_AuroraFade;
    }
    return GetEffectiveWeatherDefinition().showAurora ? 1.0f : 0.0f;
}

void TimeManager::SetTime(float hours)
{
    m_CurrentTime = std::fmod(hours, 24.0f);
    if (m_CurrentTime < 0.0f)
        m_CurrentTime += 24.0f;
}

void TimeManager::AdvanceTime(float hours)
{
    if (hours == 0.0f)
        return;

    const float totalHours = m_CurrentTime + hours;
    const int dayDelta = static_cast<int>(std::floor(totalHours / 24.0f));

    m_CurrentTime = std::fmod(totalHours, 24.0f);
    if (m_CurrentTime < 0.0f)
        m_CurrentTime += 24.0f;

    m_DayCount += dayDelta;
}

bool TimeManager::IsDay() const
{
    return m_CurrentTime >= SUNRISE_TIME && m_CurrentTime <= SUNSET_TIME;
}

bool TimeManager::IsNight() const
{
    return !IsDay();
}

float TimeManager::GetDawnIntensity() const
{
    float t = m_CurrentTime;

    // Fade in from 4:30 to 5:30
    if (t >= 4.5f && t < 5.5f)
        return (t - 4.5f) / 1.0f;
    // Peak during 5:30 to 6:30
    if (t >= 5.5f && t < 6.5f)
        return 1.0f;
    // Fade out from 6:30 to 8:00
    if (t >= 6.5f && t < 8.0f)
        return 1.0f - (t - 6.5f) / 1.5f;

    return 0.0f;
}

glm::vec3 TimeManager::LerpColor(const glm::vec3& a, const glm::vec3& b, float t) const
{
    t = std::clamp(t, 0.0f, 1.0f);
    return a + (b - a) * t;
}

float TimeManager::GetTransitionFactor(float time, float start, float end) const
{
    if (end <= start)
        return 0.0f;
    return std::clamp((time - start) / (end - start), 0.0f, 1.0f);
}
