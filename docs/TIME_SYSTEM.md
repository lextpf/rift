# Time System

Rift features a complete day/night cycle system that drives ambient lighting, sky colors, celestial bodies, and atmospheric effects.

## Overview

\htmlonly
<pre class="mermaid">
graph LR
    classDef manager fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
    classDef renderer fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
    classDef output fill:#134e3a,stroke:#10b981,color:#e2e8f0

    TM["TimeManager"]:::manager
    SR["SkyRenderer"]:::renderer

    subgraph Outputs["Visual Outputs"]
        Ambient["Ambient light color<br/>(sprite tint)"]:::output
        Sky["Sky clear color"]:::output
        Lights["World light pools"]:::output
        Post["PostFX grading"]:::output
        Stars["Stars + shooting stars"]:::output
        Rays["Sun / moon rays"]:::output
        Atmos["Dawn glow + aurora"]:::output
    end

    TM --> |time queries| SR
    TM --> Ambient
    TM --> Sky
    TM --> Lights
    TM --> Post
    SR --> Stars
    SR --> Rays
    SR --> Atmos
</pre>
\endhtmlonly

`TimeManager` owns the clock and every resolved lighting value; `SkyRenderer` owns the sky
artwork and reads the manager once per frame. There is no sun or moon body sprite - the sun and
moon exist only as their ray fans.

## Time Model

Time is represented as a floating-point value from 0.0 to 24.0 (hours):

```
 0.0 ------- 6.0 --------- 13.0 -------- 20.0 ------ 24.0
Midnight   Sunrise      Solar noon      Sunset     Midnight
```

Sunrise and sunset are `SUNRISE_TIME` 6:00 and `SUNSET_TIME` 20:00, a 14-hour day. Solar noon is
the midpoint of that arc, 13:00 - not clock noon.

### Time Periods

The day is divided into 8 distinct periods:

\htmlonly
<pre class="mermaid">
graph LR
    classDef night fill:#1a1a2e,stroke:#4a4a6a,color:#e2e8f0
    classDef dawn fill:#614385,stroke:#9b59b6,color:#e2e8f0
    classDef day fill:#f39c12,stroke:#e67e22,color:#1a1a2e
    classDef dusk fill:#c0392b,stroke:#e74c3c,color:#e2e8f0

    subgraph Cycle["24-Hour Cycle"]
        N["Night<br/>22:00-04:00<br/>Stars + Moon"]:::night
        LN["Late Night<br/>04:00-05:00<br/>Pre-dawn"]:::night
        D["Dawn<br/>05:00-07:00<br/>Sun rising"]:::dawn
        M["Morning<br/>07:00-10:00<br/>Golden hour"]:::day
        MD["Midday<br/>10:00-16:00<br/>Full sun"]:::day
        A["Afternoon<br/>16:00-18:00<br/>Warm light"]:::day
        DU["Dusk<br/>18:00-20:00<br/>Sun setting"]:::dusk
        E["Evening<br/>20:00-22:00<br/>Moon rising"]:::night
    end

    N --> LN --> D --> M --> MD --> A --> DU --> E --> N
</pre>
\endhtmlonly

| Period     | Hours       | Characteristics                    |
|------------|-------------|------------------------------------|
| Night      | 22:00-04:00 | Dark, full starfield, moon visible |
| Late Night | 04:00-05:00 | Darkest hour before dawn           |
| Dawn       | 05:00-07:00 | Orange/pink sky, stars fading      |
| Morning    | 07:00-10:00 | Bright, golden hour fading         |
| Midday     | 10:00-16:00 | Full daylight                      |
| Afternoon  | 16:00-18:00 | Warm light, lengthening shadows    |
| Dusk       | 18:00-20:00 | Orange/purple sky, stars appearing |
| Evening    | 20:00-22:00 | Deep blue, moon rising             |

### Time Scale

Real-time to game-time conversion:

$$
gameHours = \frac{realSeconds \times 24 \times timeScale}{dayDuration}
$$

Default configuration:
- `TimeManager` default: `dayDuration = 24` (24 real seconds = 1 game day; the intended pace -
  `Game::Initialize()` no longer overrides it)
- `timeScale = 1.0` (normal speed)

At the default setting, 1 real second = 1 game hour.

`SetDayDuration()` has no production call site (only tests use it), and `Initialize()` resets
`m_DayDuration` back to 24 s, so any caller must apply it **after** initialization.

## Celestial Bodies

### Sun Arc

The sun's position along its arc (0 at sunrise, 0.5 at solar noon, 1 at sunset):

$$
sunArc = \frac{time - sunrise}{sunset - sunrise} = \frac{time - 6.0}{20.0 - 6.0}
$$

Returns -1 when the sun is below the horizon (before 6:00 or after 20:00).

**Sun Position Calculation:**

`SkyRenderer::GetLightSourcePosition` places the sun and the moon on the same parabolic arc.
X travels across a band three viewports wide, anchored to the camera's current band so the body
is always reachable but still world-anchored; Y is measured from the camera top and does not
descend with vertical camera motion:

$$
x_{world} = bandLeft + (1 - arc) \times 3 W, \quad
bandLeft = \left\lfloor \frac{camera_x}{3W} \right\rfloor 3W - \tfrac{3W}{2}
$$
$$
y_{world} = camera_y + 20 - 40 \times \left(1 - (2 \times arc - 1)^2\right)
$$

Where $W$ is the viewport width in world pixels. Both components then subtract
$camera \times parallax$; `SKY_PARALLAX_SUN` and `SKY_PARALLAX_MOON` are both 1.0, so the result
is simply the camera-relative position. The arc term is 0 at rise and set and 1 at the midpoint,
so the body is lowest at the horizon and highest at solar noon. X decreases as `arc` grows: the
body travels right to left.

### Moon Arc

Similar to the sun but offset by 12 hours:

- Moonrise: 19:00
- Moonset: 07:00
- Crosses midnight (requires wrap-around handling)

$$
moonArc = \begin{cases}
\frac{time - 19.0}{12.0} & \text{if } time \geq 19.0 \\\\
\frac{time + 24.0 - 19.0}{12.0} & \text{if } time \leq 7.0 \\\\
-1 & \text{otherwise}
\end{cases}
$$

The divisor is a literal `12.0`, not `(24 - MOONRISE_TIME) + MOONSET_TIME`. The documented
$[0, 1]$ range therefore holds only while `MOONRISE_TIME` and `MOONSET_TIME` stay exactly 12
hours apart; moving either constant pushes the result out of range. Arc 0.5 is 01:00, the moon's
highest point - not clock midnight.

### Moon Phases

An 8-day lunar cycle provides visual variety:

\htmlonly
<pre class="mermaid">
graph LR
    classDef new fill:#1a1a2e,stroke:#4a4a6a,color:#e2e8f0
    classDef wax fill:#2e4a62,stroke:#5a8ac6,color:#e2e8f0
    classDef full fill:#f4f4dc,stroke:#d4d4bc,color:#1a1a2e
    classDef wan fill:#4a3a5e,stroke:#8a6ab6,color:#e2e8f0

    P0["Phase 0<br/>New Moon"]:::new
    P1["Phase 1<br/>Waxing Crescent"]:::wax
    P2["Phase 2<br/>First Quarter"]:::wax
    P3["Phase 3<br/>Waxing Gibbous"]:::wax
    P4["Phase 4<br/>Full Moon"]:::full
    P5["Phase 5<br/>Waning Gibbous"]:::wan
    P6["Phase 6<br/>Last Quarter"]:::wan
    P7["Phase 7<br/>Waning Crescent"]:::wan

    P0 --> P1 --> P2 --> P3 --> P4 --> P5 --> P6 --> P7 --> P0
</pre>
\endhtmlonly

$$
moonPhase = dayCount \mod 8
$$

`SkyRenderer::RenderMoonRays` turns the phase index into a brightness factor, floored at 0.3 so
a new moon still reads as present rather than vanishing:

$$
phaseFactor = \max\left(0.3,\ 1 - \frac{|phase - 4|}{4}\right)
$$

| Phase | Name            | Raw factor | Applied factor |
|-------|-----------------|------------|----------------|
| 0     | New Moon        | 0.0        | 0.3 (floored)  |
| 1     | Waxing Crescent | 0.25       | 0.3 (floored)  |
| 2     | First Quarter   | 0.5        | 0.5            |
| 3     | Waxing Gibbous  | 0.75       | 0.75           |
| 4     | Full Moon       | 1.0        | 1.0 (brightest)|
| 5     | Waning Gibbous  | 0.75       | 0.75           |
| 6     | Last Quarter    | 0.5        | 0.5            |
| 7     | Waning Crescent | 0.25       | 0.3 (floored)  |

Moon-ray brightness is the phase's only visual consumer: there is no moon body sprite and no
separate moonlight term. `time.status` also prints the phase index.

## Ambient Lighting

### Color Transitions

Ambient light color smoothly interpolates between key times:

| Time  | RGB                | Description                     |
|-------|--------------------|---------------------------------|
| 00:00 | (0.30, 0.30, 0.45) | Deep night blue                 |
| 04:00 | (0.35, 0.35, 0.50) | Late-night pre-dawn             |
| 05:00 | (0.85, 0.75, 0.70) | Dawn warm light starts          |
| 07:00 | (0.90, 0.88, 0.85) | Morning warm white              |
| 10:00 | (0.93, 0.93, 0.91) | Midday peak, just below white   |
| 18:00 | (0.90, 0.85, 0.78) | Afternoon warm yellow           |
| 20:00 | (0.75, 0.60, 0.55) | Dusk muted orange               |
| 22:00 | (0.50, 0.50, 0.65) | Evening blue                    |

The midday anchor holds flat from 10:00 to 16:00; the ramp toward the 18:00 afternoon anchor only
starts at 16:00. Daylight anchors sit deliberately below white: ambient is an unclamped multiply
on albedo with no scene tonemap, so a white midday made noon as bright as the source art allowed.

These are the time-of-day values only. `GetAmbientColor()` folds the weather tint, any active
weather transition, the manual overlay, and imposed night on top - see
[Weather System](#weather-system).

**Interpolation:**

Between keyframes, colors are linearly interpolated:

$$
color = color_a + (color_b - color_a) \times t
$$

Where $t$ is the normalized time between keyframes:

$$
t = \frac{currentTime - time_a}{time_b - time_a}
$$

### Application to Sprites

`IRenderer::SetAmbientColor` publishes the color; the sprite path of the fragment shader
multiplies it into every sampled texel:

```glsl
// shaders/Geometry.frag, textured sprite mode (useColorOnly == 0)
FragColor = vec4(spriteColor * ambientColor * texColor.rgb, spriteAlpha * texColor.a);
```

Only that mode applies the tint. The per-vertex-color modes used by the OpenGL rect, particle and
text batches ignore `ambientColor`, and both the sky pass and the UI passes reset the ambient to
white so they are not tinted by the time of day.

## Sky Rendering

### Star Visibility

Two values exist and they are not interchangeable.

`GetNaturalStarVisibility()` is the clock-only fade - in at dusk, out at dawn:

$$
natural = \begin{cases}
\frac{time - 18.0}{2.0} & \text{if } 18.0 \leq time < 20.0 \\\\
1.0 & \text{if } time \geq 20.0 \text{ or } time < 5.0 \\\\
1.0 - \frac{time - 5.0}{2.0} & \text{if } 5.0 \leq time < 7.0 \\\\
0.0 & \text{otherwise}
\end{cases}
$$

`GetStarVisibility()` is the resolved value every renderer gate actually reads. It starts from
`natural`, then blends toward the weather's `starVisibilityOverride` (when that field is
non-negative) by the weather intensity, lerps the endpoints of any active weather transition, and
folds in the manual overlay's own resolved value by the overlay blend. It can therefore read 1.0
at noon under a meteor shower, or 0.0 at midnight under heavy precipitation.

Use the natural value only where a consumer must follow the clock rather than the storm; its one
production caller is the particle scene-night factor, which takes
`max(natural, GetStarVisibility())`.

\htmlonly
<pre class="mermaid">
graph LR
    subgraph Visibility["Natural star visibility over time (weather excluded)"]
        T0["0:00<br/>100%"] --> T5["5:00<br/>100%"]
        T5 --> T7["7:00<br/>0%"]
        T7 --> T18["18:00<br/>0%"]
        T18 --> T20["20:00<br/>100%"]
        T20 --> T24["24:00<br/>100%"]
    end
</pre>
\endhtmlonly

### Dawn Intensity

Special effects during dawn (horizon glow, warm rays):

$$
dawnIntensity = \begin{cases}
\frac{time - 4.5}{1.0} & \text{if } 4.5 \leq time < 5.5 \\\\
1.0 & \text{if } 5.5 \leq time < 6.5 \\\\
1.0 - \frac{time - 6.5}{1.5} & \text{if } 6.5 \leq time < 8.0 \\\\
0.0 & \text{otherwise}
\end{cases}
$$

### Light Rays (God Rays)

The sun and the moon each emit a fan of `SUN_RAY_COUNT` / `MOON_RAY_COUNT` rays (3 each):

\htmlonly
<pre class="mermaid">
graph LR
    classDef source fill:#f39c12,stroke:#e67e22,color:#1a1a2e
    classDef ray fill:#f9e076,stroke:#f4d03f,color:#1a1a2e

    Sun["Sun / Moon"]:::source

    R1["Ray 1"]:::ray
    R2["Ray 2"]:::ray
    R3["Ray 3"]:::ray

    Sun --> R1
    Sun --> R2
    Sun --> R3
</pre>
\endhtmlonly

**Ray Properties:**
- Originate from the sun/moon position returned by `GetLightSourcePosition`
- Fan out across roughly two thirds of the screen, mostly straight down
- Animate on staggered per-ray cycles of $base + 3.5\phi$ seconds, with $base$ 15 for sun rays and
  20 for moon rays and $\phi \in [0, 2\pi)$ drawn per ray
- Sun-ray intensity peaks at the golden hour and fades to zero below `sunArc` 0.1 and above 0.9
- Both fans are gated by `GetCelestialFade()`, so weather can hide them; moon rays additionally
  require star visibility above 0.3

**Sun Ray Color:**

$$
rayColor = \begin{cases}
(1.0, 0.75, 0.45) & \text{if } sunArc < 0.15 \text{ (golden hour)} \\\\
sunColor \times (1.0, 0.97, 0.92) & \text{otherwise}
\end{cases}
$$

### Shooting Stars

Random shooting star events during night:

\htmlonly
<pre class="mermaid">
stateDiagram-v2
    [*] --> Waiting
    Waiting --> Spawning: Random interval
    Spawning --> Streaking: Initialize
    Streaking --> Fading: Traveled distance
    Fading --> [*]: Alpha = 0
</pre>
\endhtmlonly

**Properties:**
- Spawn and update only while the resolved `GetStarVisibility()` exceeds 0.3. The gate is
  darkness, not a specific weather - any weather that raises star visibility opens it.
- The base spawn interval drifts slowly between 2 s and 6 s and is divided by
  `GetEffectiveMeteorRate()`, so a meteor shower (multiplier 12) collapses it to about 0.33 s and
  raises the concurrent cap from 2 to $1.5 \times multiplier$.
- Spawn position is inside the star-field tile (3 viewports wide, 2 tall): 60% near its top edge,
  otherwise near its right edge, so streaks do not all leave from the same screen edge.
- Travel diagonally down and to the left, leaving a fading trail. A meteor shower lengthens,
  brightens and speeds up each streak.

## Weather System

Weather modifies time-of-day lighting through a `WeatherDefinition` table, one row per
`WeatherState` (17 states: `Clear`, precipitation, atmosphere, floral and event weathers). There
is no separate "overcast" state - a dim, starless sky is what a heavy precipitation row produces.

The fields that touch lighting:

| Field                    | Effect                                                                 |
|--------------------------|------------------------------------------------------------------------|
| `ambientTintMultiplier`  | Multiplied into the time-of-day ambient, mixed in by weather intensity  |
| `skyColorOverride`       | Replaces the natural sky when its x component is >= 0 (a negative x is the "no override" sentinel), scaled by a day/night ramp and mixed in by intensity |
| `starVisibilityOverride` | When >= 0, pulls star visibility toward it by intensity                 |
| `showCelestialBodies`    | Gates the sun/moon ray fans (via `GetCelestialFade()`)                 |
| `showAurora`             | Gates the aurora bands (via `GetAuroraFade()`)                         |
| `meteorRateMultiplier`   | Scales shooting-star spawn rate                                        |

Examples: `HeavyRain` tints ambient to (0.65, 0.68, 0.80), overrides the sky, forces star
visibility to 0 and hides the celestial bodies. `MeteorShower` forces star visibility to **1.0**
and multiplies the meteor rate by 12. `Aurora` overrides nothing at all - it only sets
`showAurora`, so the clock stays authoritative and the effect works at any hour.

`SetWeatherIntensity()` scales every mix above (and particle density); it does **not** affect the
boolean flags.

### Imposed Night

A weather that raises star visibility above the natural hour drags the rest of the scene toward
night by the difference:

$$
imposedNight = \operatorname{clamp}\big(GetStarVisibility() - natural,\ 0,\ 1\big)
$$

By that amount, `GetAmbientColor()` mixes toward the night ambient (0.30, 0.30, 0.45),
`GetSkyColor()` mixes toward the night sky (0.04, 0.04, 0.12), and `GetCelestialFade()` is scaled
by $(1 - imposedNight)$ so the daytime sun fades out. This is what lets a midday meteor shower
look like night. At night the term is 0, so the real night moon is untouched. Aurora has no star
override and therefore never imposes night.

## Usage Examples

### Basic Setup

```cpp
TimeManager time;
time.Initialize();            // 24 s per day (1 game hour per real second)
time.SetTime(6.0f);           // Start at sunrise

// In game loop:
time.Update(deltaTime);
```

### Querying Time State

```cpp
// For rendering
glm::vec3 ambient = time.GetAmbientColor();
float sunArc = time.GetSunArc();
float starVis = time.GetStarVisibility();

// For gameplay
if (time.IsNight()) {
    SpawnNightCreatures();
}

TimePeriod period = time.GetTimePeriod();
if (period == TimePeriod::Dawn) {
    PlayRoosterSound();
}
```

### Manual Time Control

```cpp
// Skip to noon
time.SetTime(12.0f);

// Advance 2 hours
time.AdvanceTime(2.0f);

// Speed up time (2x)
time.SetTimeScale(2.0f);

// Pause time
time.SetPaused(true);
```

## Debug Controls

No function key except F12 is bound. Every time control is a developer-console command; open the
console with F12 and run `help` for the full catalog.

| Command                | Aliases      | Action                                            |
|------------------------|--------------|---------------------------------------------------|
| `time.next`            | `tn`         | Advance to the next time-of-day preset            |
| `time.add <hours>`     | -            | Offset the clock by a signed amount, wrapping 0-24 |
| `time.set <hours>`     | `ts`         | Set the clock; any value is wrapped into 0.0-24.0 |
| `time.scale <mult>`    | -            | Scale time progression (1.0 = normal)             |
| `time.freeze`          | `tm.freeze`, `tfz` | Pause and resume the day/night cycle        |
| `time.status`          | -            | Print time, period, weather, day count, moon phase |

## Integration with Other Systems

### SkyRenderer

`SkyRenderer` queries `TimeManager` every frame for:
- `GetSunArc()` / `GetMoonArc()` - ray fan positions
- `GetStarVisibility()` - stars, shooting stars, atmospheric glow and the moon-ray gate
- `GetDawnIntensity()` - dawn gradient and horizon glow
- `GetMoonPhase()` - moon-ray brightness
- `GetSunColor()` / `GetSkyColor()` - ray and aurora tinting
- `GetCelestialFade()`, `GetAuroraFade()`, `GetEffectiveMeteorRate()` and the effective weather
  definition - cached once per frame in `SkyRenderer::Update`

`GetTimePeriod()` is deliberately unused there: every gate is continuous, and a discrete period
would snap.

### Particle System

No particle type is gated on a `TimePeriod`. The coupling is a single scalar,
`SetSceneNightFactor(max(GetNaturalStarVisibility(), GetStarVisibility()))`, which behaviors use
to darken or brighten themselves; the `max` keeps a night storm that forces star visibility to 0
reading as night. Which particles spawn is a weather question, not a time question - it comes
from `GetEffectiveWeatherDefinition()`.

### World Lights and Post-Processing

World light pools are scaled by `ComputeLightIntensity(light.schedule, hour) *
GetStarVisibility()` and skipped entirely below 0.01, so a storm dims them along with the sky.
The post-FX composite receives `PostFXParams::timeOfDay` and `nightFactor` (also
`GetStarVisibility()`), which drive the per-time-of-day lift/gamma/gain grading split.

## See Also

- [Architecture](ARCHITECTURE.md) - How TimeManager fits in the system
- [Rendering Pipeline](RENDERING.md) - How ambient colors are applied
