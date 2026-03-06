# Time System

The Wild Game Engine features a complete day/night cycle system that drives ambient lighting, sky colors, celestial bodies, and atmospheric effects.

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
        Ambient["Ambient Light Color"]:::output
        Sky["Sky Background"]:::output
        Sun["Sun Position/Color"]:::output
        Moon["Moon Position/Phase"]:::output
        Stars["Star Visibility"]:::output
        Rays["Light Rays"]:::output
    end

    TM --> |time queries| SR
    TM --> Ambient
    SR --> Sky
    SR --> Sun
    SR --> Moon
    SR --> Stars
    SR --> Rays
</pre>
\endhtmlonly

## Time Model

Time is represented as a floating-point value from 0.0 to 24.0 (hours):

```
 0.0 -------- 6.0 -------- 12.0 -------- 18.0 -------- 24.0
Midnight    Sunrise       Noon         Sunset       Midnight
```

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
- `dayDuration = 600` (10 real minutes = 1 game day)
- `timeScale = 1.0` (normal speed)

At default settings, 1 real second = 0.04 game hours = 2.4 game minutes.

## Celestial Bodies

### Sun Arc

The sun's position along its arc (0 at sunrise, 0.5 at noon, 1 at sunset):

$$
sunArc = \frac{time - sunrise}{sunset - sunrise} = \frac{time - 6.0}{20.0 - 6.0}
$$

Returns -1 when the sun is below the horizon (before 6:00 or after 20:00).

**Sun Position Calculation:**

The sun moves along a parabolic arc across the top of the screen:

$$
x_{sun} = screenWidth \times (1 - sunArc)
$$
$$
y_{sun} = baseY - arcHeight \times (1 - (2 \times sunArc - 1)^2)
$$

This creates a smooth arc: low at sunrise/sunset, highest at noon.

### Moon Arc

Similar to the sun but offset by approximately 12 hours:

- Moonrise: 19:00
- Moonset: 07:00
- Crosses midnight (requires wrap-around handling)

$$
moonArc = \begin{cases}
\frac{time - 19.0}{24.0 - 19.0 + 7.0} & \text{if } time \geq 19.0 \\\\
\frac{time + 24.0 - 19.0}{12.0} & \text{if } time < 7.0 \\\\
-1 & \text{otherwise}
\end{cases}
$$

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

| Phase | Name            | Brightness Factor |
|-------|-----------------|-------------------|
| 0     | New Moon        | 0.0 (invisible)   |
| 1     | Waxing Crescent | 0.25              |
| 2     | First Quarter   | 0.5               |
| 3     | Waxing Gibbous  | 0.75              |
| 4     | Full Moon       | 1.0 (brightest)   |
| 5     | Waning Gibbous  | 0.75              |
| 6     | Last Quarter    | 0.5               |
| 7     | Waning Crescent | 0.25              |

The phase affects:
- Moon texture rendering
- Moonlight intensity
- Moon ray brightness

## Ambient Lighting

### Color Transitions

Ambient light color smoothly interpolates between key times:

| Time  | RGB                | Description      |
|-------|--------------------|------------------|
| 00:00 | (0.15, 0.15, 0.25) | Deep night blue  |
| 05:30 | (0.6, 0.4, 0.5)    | Pre-dawn purple  |
| 06:30 | (1.0, 0.8, 0.6)    | Golden sunrise   |
| 08:00 | (1.0, 1.0, 1.0)    | Full daylight    |
| 17:00 | (1.0, 0.95, 0.9)   | Afternoon warmth |
| 19:00 | (1.0, 0.6, 0.4)    | Sunset orange    |
| 20:30 | (0.3, 0.3, 0.5)    | Twilight blue    |

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

The ambient color multiplies all sprite colors:

```glsl
// In fragment shader
FragColor = texColor * spriteColor * vec4(ambientColor, 1.0);
```

This tints the entire world based on time of day.

## Sky Rendering

### Star Visibility

Stars fade in at dusk and fade out at dawn:

$$
starVisibility = \begin{cases}
1.0 & \text{if } time < 5.0 \text{ or } time \geq 22.0 \\\\
1.0 - \frac{time - 5.0}{2.0} & \text{if } 5.0 \leq time < 7.0 \\\\
\frac{time - 20.0}{2.0} & \text{if } 20.0 \leq time < 22.0 \\\\
0.0 & \text{otherwise}
\end{cases}
$$

\htmlonly
<pre class="mermaid">
graph LR
    subgraph Visibility["Star Visibility over Time"]
        T0["0:00<br/>100%"] --> T5["5:00<br/>100%"]
        T5 --> T7["7:00<br/>0%"]
        T7 --> T20["20:00<br/>0%"]
        T20 --> T22["22:00<br/>100%"]
        T22 --> T24["24:00<br/>100%"]
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

Sun and moon emit volumetric light rays:

\htmlonly
<pre class="mermaid">
graph LR
    classDef source fill:#f39c12,stroke:#e67e22,color:#1a1a2e
    classDef ray fill:#f9e076,stroke:#f4d03f,color:#1a1a2e

    Sun["Sun/Moon"]:::source

    R1["Ray 1"]:::ray
    R2["Ray 2"]:::ray
    R3["Ray 3"]:::ray
    R4["Ray 4"]:::ray
    R5["Ray 5"]:::ray

    Sun --> R1
    Sun --> R2
    Sun --> R3
    Sun --> R4
    Sun --> R5
</pre>
\endhtmlonly

**Ray Properties:**
- Originate from the sun/moon position
- Fan out at different angles (spread pattern)
- Animate with fade in/out cycles
- Intensity varies with light source position

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
- Only appear when `starVisibility > 0.5`
- Random spawn position along top/sides of screen
- Travel at random angles (mostly downward)
- Leave a fading trail

## Weather System

Weather modifies time-of-day lighting:

| Weather  | Sun/Moon | Stars           | Ambient |
|----------|----------|-----------------|---------|
| Clear    | Visible  | Visible (night) | Normal  |
| Overcast | Hidden   | Hidden          | Dimmed  |

**Overcast Ambient Modifier:**

$$
ambientColor_{overcast} = ambientColor \times 0.7
$$

## Usage Examples

### Basic Setup

```cpp
TimeManager time;
time.Initialize();
time.SetDayDuration(600.0f);  // 10 minutes per day
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

| Key | Action                    |
|-----|---------------------------|
| F5  | Cycle to next time period |
| F6  | Advance time by 0.5 hours |

## Integration with Other Systems

### SkyRenderer

`SkyRenderer` queries `TimeManager` every frame for:
- Sun/moon arc positions
- Star visibility factor
- Dawn intensity
- Ambient colors

### Particle System

Certain particle effects (fireflies) only spawn during specific time periods:
- Fireflies: Evening, Night, Late Night

### NPC Behavior

NPCs can have time-based schedules:
- Shopkeepers work during day
- Guards patrol at night

## See Also

- [Architecture](ARCHITECTURE.md) - How TimeManager fits in the system
- [Rendering Pipeline](RENDERING.md) - How ambient colors are applied
