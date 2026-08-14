#pragma once

#include <glm/glm.hpp>

/**
 * @brief Centralised tuning constants for subtle cozy ambience effects.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * All ambience polish pulls magic numbers from this header. Tuning the world's
 * feel post-launch should require touching only this file.
 *
 * @par Calibration philosophy
 * Saturation pumps chroma (luma-preserving, adds zero brightness),
 * chromatic aberration gives radial color fringe at frame edges, and a
 * saturation-thresholded chroma bloom spreads color outward from saturated
 * sources without lifting luminance (so red lanterns bleed red, blue water
 * bleeds blue, white surfaces stay matte).
 */
namespace ambience
{

/**
 * @name Post-FX pipeline
 * @brief Composite-shader tuning, in the order PostFXComposite.frag applies it.
 *
 * @code
 *   1. Sample scene (with chromatic aberration per-channel offset)
 *   2. Add chroma-only bloom b - vec3(dot(b, LUMA)) - zero net luminance
 *   3. LGG grading split (shadows / midtones / highlights)
 *   4. Vignette + edge desaturation
 *   5. Grain (luminance-modulated, slight chroma)
 *   6. Tonemap (soft-shoulder)
 * @endcode
 *
 * The bloom feeder gates on HSV saturation, not luma so only colored
 * pixels enter the mip chain.
 * @{
 */

/// Vignette darkening intensity (0 = none, 1 = corners fully black).
constexpr float VIGNETTE_INTENSITY = 0.15f;

/// Vignette inner-radius start (fraction of half-diagonal).
constexpr float VIGNETTE_INNER_R = 0.70f;

/// Vignette outer radius (fraction of half-diagonal).
constexpr float VIGNETTE_OUTER_R = 1.00f;

/**
 * @brief Vignette aspect-Y multiplier.
 *
 * Values >1.0 elongate the ellipse vertically, so darkening ramps faster going
 * up/down than left/right and reads as a horizontal lens.
 */
constexpr float VIGNETTE_ASPECT_Y_SCALE = 1.15f;

/// Edge desaturation at full vignette (0 = no desat, 1 = pure grayscale at corners).
constexpr float VIGNETTE_EDGE_DESAT = 0.12f;

/// Film grain intensity (0 = none, 1 = swap to noise).
constexpr float GRAIN_INTENSITY = 0.025f;

/// Per-channel chroma share of the grain (0 = pure luma, 1 = pure RGB confetti).
constexpr float GRAIN_CHROMA_MIX = 0.30f;

/**
 * @brief Bloom HSV-saturation threshold.
 *
 * Pixels with HSV-S above this contribute to bloom; below pass through.
 *
 * See BloomPrefilter.frag and the Karis-style soft filter in
 * PostFXParams::KarisBloomChromaWeight.
 */
constexpr float BLOOM_SATURATION_THRESHOLD = 0.30f;

/// Bloom intensity scalar applied during composite.
constexpr float BLOOM_INTENSITY = 0.22f;

/// Number of mip levels in the bloom downsample/upsample chain.
constexpr int BLOOM_MIP_LEVELS = 5;

/// Global color saturation multiplier, applied after grading and before vignette.
constexpr float COLOR_SATURATION = 1.15f;

/**
 * @brief Color grading warm/cool RGB swing (+/-).
 *
 * A per-time-of-day blend drives the inline directional weights in
 * PostFXParams::ComputeGradingParams; this is for per-time-of-day swing magnitude.
 */
constexpr float GRADING_TINT_AMPLITUDE = 0.06f;

/**
 * @brief Chromatic aberration strength, in UV space.
 *
 * The R/B channels are sampled at radial offsets that grow with distance from
 * screen center, so the center stays sharp while edges feather with a soft
 * color fringe.
 */
constexpr float CA_STRENGTH = 0.007f;

/**
 * @brief Tonemap knee point.
 *
 * Below this value, output = input (LDR content passes through unchanged, which
 * preserves contrast). Above it, a soft shoulder rolls off asymptotically toward
 * 1.0 so HDR highlights (lights/sun/bloom) don't clip.
 */
constexpr float TONEMAP_KNEE = 0.85f;
/// @}

/// Total active ambient particles globally (cap across all types combined).
constexpr int AMBIENT_PARTICLE_TOTAL_CAP = 100;

/// Per-second spawn rate for drifting leaves (active during daylight).
constexpr float AMBIENT_LEAF_SPAWN_PER_SEC = 2.5f;

/// Per-second spawn rate for dust motes (visible in sunbeams: dawn/midday).
constexpr float AMBIENT_DUST_SPAWN_PER_SEC = 3.5f;

/// Per-second spawn rate for pollen (golden hour only).
constexpr float AMBIENT_POLLEN_SPAWN_PER_SEC = 2.5f;

/// Maximum alpha for ambient particles.
constexpr float AMBIENT_PARTICLE_ALPHA_CAP = 0.7f;

/// Margin in world pixels around camera rect for spawning (pre-spawned just off-screen).
constexpr float AMBIENT_PARTICLE_SPAWN_MARGIN = 64.0f;

/// Per-character alpha-in duration in seconds. Char fades from 0 to 1 over this.
constexpr float DIALOGUE_CHAR_FADE_DURATION_S = 0.1f;

/// Extra pause time after punctuation marks (.,!?) in seconds.
constexpr float DIALOGUE_PUNCTUATION_PAUSE_S = 0.20f;

/// Box scale-in endpoints. A smaller start/end gap makes the pop-in gentler.
constexpr float DIALOGUE_BOX_SCALE_START = 1.05f;  ///< Scale at the start of the pop-in.
constexpr float DIALOGUE_BOX_SCALE_END = 1.00f;    ///< Scale once the pop-in completes.

/// Option arrow pulse: alpha = base + amp * sin(2*pi * hz * t), t = boxFadeTimer seconds.
constexpr float DIALOGUE_ARROW_PULSE_BASE = 0.85f;       ///< Alpha at the pulse midpoint.
constexpr float DIALOGUE_ARROW_PULSE_AMPLITUDE = 0.15f;  ///< Peak swing around the base.
constexpr float DIALOGUE_ARROW_PULSE_HZ = 1.5f;          ///< Pulse rate, cycles per second.

/**
 * @brief Panel fill RGB - cool dark slate (#121822).
 *
 * Paired with DIALOGUE_PANEL_FILL_ALPHA below to produce a translucent overlay
 * that blends with whatever is behind it (grass, sky, deep night), so the panel
 * never clashes with the world's palette regardless of time of day.
 */
constexpr glm::vec3 DIALOGUE_PANEL_FILL_RGB{0.071f, 0.094f, 0.137f};

/**
 * @brief Panel fill alpha.
 *
 * 0.85 is visible-but-not-solid: world bleed-through is ~15% (subtle) and text
 * contrast stays high. Multiplied by fadeAlpha at draw time so the panel itself
 * fades in with the scale-in animation.
 */
constexpr float DIALOGUE_PANEL_FILL_ALPHA = 0.85f;

/// Panel border color (#3a4555). 1px lighter slate around the rect for
/// definition; full opacity (modulated only by fadeAlpha during scale-in).
constexpr glm::vec3 DIALOGUE_PANEL_BORDER{0.227f, 0.271f, 0.333f};

/// Inner padding from panel edge to content (ribbon / text / continue prompt).
constexpr float DIALOGUE_PANEL_PADDING = 6.0f;

/**
 * @brief Body text fill color: warm white, \#fff5e0.
 *
 * The renderer always strokes a black outline behind text; pairing that with a
 * *light* fill produces the classic outlined-text look.
 */
constexpr glm::vec3 DIALOGUE_BODY_TEXT_COLOR{1.000f, 0.960f, 0.880f};

/// Speaker ribbon text fill color. Same rationale as body text: light fill +
/// black outline reads on any accent background without a luminance flip.
constexpr glm::vec3 DIALOGUE_RIBBON_TEXT_COLOR{1.000f, 0.960f, 0.880f};

/// Speaker ribbon height (in virtual px).
constexpr float DIALOGUE_RIBBON_HEIGHT = 10.0f;

/// Speaker ribbon horizontal padding around text (each side).
constexpr float DIALOGUE_RIBBON_PADDING_X = 4.0f;

/// Selected-option triangle dimensions (in virtual px).
constexpr float DIALOGUE_SELECTION_TRIANGLE_W = 4.0f;  ///< Triangle width.
constexpr float DIALOGUE_SELECTION_TRIANGLE_H = 6.0f;  ///< Triangle height.

/// Fallback accent color when SampleDominantNonSkinColor finds nothing usable.
/// Matches the speaker-color gold, so a monochrome NPC still reads as on-theme.
constexpr glm::vec3 DIALOGUE_ACCENT_FALLBACK{0.85f, 0.75f, 0.40f};

/**
 * @name Weather director (transition) tuning
 * @brief Timing and envelope constants for weather cross-fades.
 *
 * Transitions blend two WeatherDefinition endpoints over real seconds.
 * @{
 */

/**
 * @brief Default weather cross-fade duration, in real seconds.
 *
 * Applies to `time.weather <name>`, `weather.next` and `weather.random` when no
 * seconds argument is given, to forecast reconciliation for front changes (night
 * events use WEATHER_EVENT_TRANSITION_SECONDS instead), and as the ease rate of the
 * manual sky overlay in TimeManager::UpdateWeatherEffects.
 */
constexpr float WEATHER_TRANSITION_SECONDS = 3.0f;

/// Frequency-space cutoff for blended lightning (Hz). Blended frequencies
/// below this map to interval 0 ("off") instead of a near-infinite interval.
constexpr float WEATHER_LIGHTNING_FREQ_EPS = 0.005f;

/**
 * @brief Decay time (real seconds) for a held fogAlphaMultiplier.
 *
 * After a fog -> no-fog transition completes, the held multiplier eases to the
 * destination value over this window. It matches the max weather fog puff
 * lifetime, so surviving puffs die before the multiplier moves far.
 */
constexpr float WEATHER_FOG_HOLD_DECAY_SECONDS = 18.0f;

/// Half-width (game hours) of the dawn/dusk ramp applied to the sky-color
/// override day/night factor, so the factor eases across dawn instead of stepping.
constexpr float WEATHER_SKY_DAYNIGHT_RAMP_HOURS = 0.5f;

/// Gust envelope amplitude: peak strength swing as a fraction of base
constexpr float WEATHER_GUST_AMP = 0.35f;

/// Primary (slow surge) gust sine period, real seconds.
constexpr float WEATHER_GUST_PERIOD_PRIMARY_S = 7.0f;

/// Secondary (flutter) gust sine period, real seconds.
constexpr float WEATHER_GUST_PERIOD_SECONDARY_S = 1.7f;

/// Base wind direction the gust system wanders around (normalized on use).
constexpr glm::vec2 WEATHER_WIND_BASE_DIR{-1.0f, 0.0f};

/// Max wind-direction wander around WEATHER_WIND_BASE_DIR, degrees.
constexpr float WEATHER_WIND_WANDER_DEG = 15.0f;

/// Direction wander sine period, real seconds (slow, so drift reads as
/// weather shifting, not jitter).
constexpr float WEATHER_WIND_WANDER_PERIOD_S = 23.0f;
/// @}

/**
 * @name Weather director (forecast) tuning
 * @brief Day-seeded forecast math (fronts + night events).
 *
 * Consumed by ForecastForDay / ForecastFrontIndex in WeatherBlend.hpp.
 * @{
 */

/// Real-seconds duration for night-event transitions (night itself is only
/// ~9 real seconds at the 24 s day, so events ramp faster than fronts).
constexpr float WEATHER_EVENT_TRANSITION_SECONDS = 3.0f;

/// Nominal weather-front length in in-game days (~96 real seconds at the
/// 24 s day). Boundaries jitter by +/-1 day per front.
constexpr int WEATHER_FRONT_LENGTH_DAYS = 4;

/// Probability that any given night hosts a special event (aurora / meteor
/// shower / fireflies), before moon-phase weighting picks which.
constexpr float WEATHER_EVENT_NIGHT_CHANCE = 0.25f;
/// @}

}  // namespace ambience
