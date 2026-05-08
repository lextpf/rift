#pragma once

#include <glm/glm.hpp>

/**
 * @file AmbienceConfig.h
 * @brief Centralised tuning constants for subtle "cozy premium" ambience effects.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Effects
 *
 * Every default targets *"you only notice it when it's gone."* All ambience
 * polish - vignette, grain, bloom, color grading, camera breathing, ambient
 * particles, cloud shadows, chimney smoke, dialogue easing - pulls magic
 * numbers from this header. Tuning the world's feel post-launch should
 * require touching only this file.
 *
 * @par Calibration philosophy
 * Defaults aim for "lowkey arcade neon - color bleed without shine."
 * Saturation pumps chroma (luma-preserving, adds zero brightness),
 * chromatic aberration gives radial color fringe at frame edges, and a
 * saturation-thresholded chroma bloom spreads color outward from saturated
 * sources without lifting luminance (so red lanterns bleed red, blue water
 * bleeds blue, white surfaces stay matte). Saturation +25%, bloom 0.30
 * (chroma-only), vignette ~17%, CA 0.007 (~10px at 1024-wide), color
 * grading +/-6%, grain +/-2 luma steps. Camera breathing/sway remains
 * sub-pixel.
 */
namespace ambience
{

// =============================================================================
// Post-FX pipeline (single offscreen FBO + screen-quad pass)
//
// Composition order in PostFXComposite.frag:
//   1. Sample scene (with chromatic aberration per-channel offset)
//   2. Add chroma-only bloom (b - vec3(dot(b, LUMA))) - zero net luminance
//   3. LGG grading split (shadows / midtones / highlights)
//   4. Vignette + edge desaturation
//   5. Grain (luminance-modulated, slight chroma)
//   6. Tonemap (soft-shoulder)
//
// The bloom feeder (BloomPrefilter.frag) gates on HSV saturation, not luma -
// only colored pixels enter the mip chain. Combined with the chroma-only
// composite this gives "color bleed without shine."
// =============================================================================

/// Vignette darkening intensity (0 = none, 1 = corners fully black). 0.17 reads
/// as a slightly tighter arcade frame without crushing peripheral content.
constexpr float VIGNETTE_INTENSITY = 0.17f;

/// Vignette inner-radius start (fraction of half-diagonal). Inside this -> no darkening.
constexpr float VIGNETTE_INNER_R = 0.70f;

/// Vignette outer radius (fraction of half-diagonal). At/past this -> full darkening.
constexpr float VIGNETTE_OUTER_R = 1.00f;

/// Vignette aspect-Y multiplier (>1.0 elongates ellipse vertically -> darkens
/// faster going up/down than left/right; reads as horizontal lens). 1.15 is
/// felt-but-not-obvious - matches a real anamorphic lens.
constexpr float VIGNETTE_ASPECT_Y_SCALE = 1.15f;

/// Edge desaturation at full vignette (0 = no desat, 1 = pure grayscale at corners).
/// Reads as "lens losing color at the edges" rather than "filter."
constexpr float VIGNETTE_EDGE_DESAT = 0.12f;

/// Film grain intensity (0 = none, 1 = swap to noise). Subtle: ~0.025.
constexpr float GRAIN_INTENSITY = 0.025f;

/// Per-channel chroma share of the grain (0 = pure luma, 1 = pure RGB confetti).
/// 0.30 reads as organic shimmer.
constexpr float GRAIN_CHROMA_MIX = 0.30f;

/// Bloom HSV-saturation threshold. Pixels with HSV-S above this contribute to
/// bloom; below pass through. 0.30 admits visibly colored pixels (saturated
/// foliage, water, lanterns, dialogue accents) and rejects whites / off-whites
/// / lit tile faces - the gating mechanism for "neon glow on colored sources,
/// no glow on bright surfaces." See BloomPrefilter.frag and the Karis-style
/// soft filter in PostFXParams::KarisBloomChromaWeight.
constexpr float BLOOM_SATURATION_THRESHOLD = 0.30f;

/// Bloom intensity scalar applied during composite. 0.30 with the chroma-only
/// composite path in PostFXComposite.frag (`col += b - vec3(dot(b, LUMA))`) yields a
/// pleasantly subtle color bleed without any net luminance lift - the entire
/// bloom contribution projects onto the chroma plane orthogonal to the luma
/// axis. Set to 0.0 to disable bloom contribution while keeping the mip
/// chain available.
constexpr float BLOOM_INTENSITY = 0.30f;

/// Number of mip levels in the bloom downsample/upsample chain.
/// 5 levels covers 1/2 -> 1/32 resolution, enough for multi-scale character on cozy scenes.
constexpr int BLOOM_MIP_LEVELS = 5;

/// Global color saturation multiplier applied after grading, before vignette.
/// 1.0 = identity, >1 pumps chroma, 0 = grayscale. The math is luma-preserving
/// (`mix(vec3(L), c, s)`) so this adds zero brightness - the entire "pop" is
/// chroma deviation, not luma lift. 1.25 reads as a softer arcade-neon pop on
/// foliage / sky / tiles, well below the cartoon threshold (>=1.6). See
/// PostFXComposite.frag applySaturation().
constexpr float COLOR_SATURATION = 1.25f;

/// Color grading warm/cool RGB swing (+/-). Per-time-of-day blend at 0.06
/// reads as distinct mood per anchor (dawn/midday/dusk/night) without
/// crossing into "filtered" territory. Drives the inline directional
/// weights in PostFXParams::ComputeGradingParams; this is the single tuning
/// lever for per-time-of-day swing magnitude.
constexpr float GRADING_TINT_AMPLITUDE = 0.06f;

/// Chromatic aberration strength (UV space). The R/B channels are sampled at radial
/// offsets that grow with distance from screen center, so the center stays sharp
/// while edges feather with a soft color fringe. 0.007 ~ 10px max at 1024-wide -
/// reads as a "lens character" color fringe at frame edges; the center stays
/// pixel-perfect because the radial offset at uv=0.5 is zero.
constexpr float CA_STRENGTH = 0.007f;

/// Tonemap knee point. Below this value, output = input (LDR content passes
/// through unchanged - preserves contrast). Above, a soft shoulder rolls off
/// asymptotically toward 1.0 so HDR highlights (lights/sun/bloom) don't clip.
/// 0.85 lets the top 15% of the range take the gentle filmic rolloff.
constexpr float TONEMAP_KNEE = 0.85f;

// =============================================================================
// Ambient world particles (global spawner, independent of editor zones)
//
// Calibration: decorative ambient - clearly visible during their time-of-day
// windows, similar in presence to fireflies. Tuned to be a feature of the
// world, not invisible texture. Pollen still gates strictly to golden hour;
// leaves spawn during daylight; dust motes peak at dawn and midday.
// =============================================================================

/// Total active ambient particles globally (cap across all types combined).
constexpr int AMBIENT_PARTICLE_TOTAL_CAP = 80;

/// Per-second spawn rate for drifting leaves (active during daylight).
constexpr float AMBIENT_LEAF_SPAWN_PER_SEC = 2.5f;

/// Per-second spawn rate for dust motes (visible in sunbeams: dawn/midday).
constexpr float AMBIENT_DUST_SPAWN_PER_SEC = 3.5f;

/// Per-second spawn rate for pollen (golden hour only - bias formula gates window).
constexpr float AMBIENT_POLLEN_SPAWN_PER_SEC = 2.5f;

/// Maximum alpha for ambient particles. Uniform 0.6-0.75 target across most
/// particle types so they read as glowy decorative features rather than
/// barely-visible filters.
constexpr float AMBIENT_PARTICLE_ALPHA_CAP = 0.7f;

/// Margin in world pixels around camera rect for spawning (pre-spawned just off-screen).
constexpr float AMBIENT_PARTICLE_SPAWN_MARGIN = 64.0f;

// =============================================================================
// Cloud shadows (multiplicative darkening drifting across world)
// =============================================================================

/// Number of large soft-edged cloud shadows visible at once.
constexpr int CLOUD_SHADOW_COUNT = 4;

/// Size of each cloud shadow blob in pixels (covers many tiles).
constexpr float CLOUD_SHADOW_SIZE_PX = 320.0f;

/// Maximum darkening of cloud shadows (0 = none, 1 = black). Subtle: 4-8%.
constexpr float CLOUD_SHADOW_INTENSITY = 0.06f;

/// Wind direction that cloud shadows drift along (normalised on use).
constexpr glm::vec2 CLOUD_SHADOW_WIND_DIR{-1.0f, 0.0f};

/// Drift speed in pixels per second at base zoom.
constexpr float CLOUD_SHADOW_DRIFT_SPEED = 3.0f;

// =============================================================================
// Dialogue easing
// =============================================================================

/// Per-character alpha-in duration in seconds. Char fades from 0 to 1 over this.
constexpr float DIALOGUE_CHAR_FADE_DURATION_S = 0.07f;

/// Extra pause time after punctuation marks (.,!?) in seconds.
constexpr float DIALOGUE_PUNCTUATION_PAUSE_S = 0.20f;

/// Box scale-in from -> to. Smaller is gentler. 1.04 -> 1.00 is barely visible.
constexpr float DIALOGUE_BOX_SCALE_START = 1.04f;
constexpr float DIALOGUE_BOX_SCALE_END = 1.00f;

/// Option arrow pulse parameters: alpha = base + amp * sin(t * freq).
constexpr float DIALOGUE_ARROW_PULSE_BASE = 0.85f;
constexpr float DIALOGUE_ARROW_PULSE_AMPLITUDE = 0.15f;
constexpr float DIALOGUE_ARROW_PULSE_HZ = 1.5f;

// =============================================================================
// Dialogue panel - translucent dark slate (no parchment, no portrait, no wood)
// =============================================================================

/// Panel fill RGB. Cool dark slate (#121822). Paired with PANEL_FILL_ALPHA
/// below to produce a translucent overlay that blends with whatever's behind
/// it (grass, sky, deep night), so the panel never clashes with the world's
/// palette regardless of time of day.
constexpr glm::vec3 DIALOGUE_PANEL_FILL_RGB{0.071f, 0.094f, 0.137f};

/// Panel fill alpha. 0.85 = visible-but-not-solid: world bleed-through is ~15%
/// (subtle), text contrast stays high. Multiplied by fadeAlpha at draw time so
/// the panel itself fades in with the scale-in animation.
constexpr float DIALOGUE_PANEL_FILL_ALPHA = 0.85f;

/// Panel border color (#3a4555). 1px lighter slate around the rect for
/// definition; full opacity (modulated only by fadeAlpha during scale-in).
constexpr glm::vec3 DIALOGUE_PANEL_BORDER{0.227f, 0.271f, 0.333f};

/// Inner padding from panel edge to content (ribbon / text / continue prompt).
constexpr float DIALOGUE_PANEL_PADDING = 6.0f;

/// Body text fill color. The renderer always strokes a black outline behind
/// text; pairing with a *light* fill produces the classic outlined-text look
/// (Stardew, Pokemon). Pure black text + black outline collapses into a
/// black blob - the fill must contrast with the outline.
constexpr glm::vec3 DIALOGUE_BODY_TEXT_COLOR{1.000f, 0.960f, 0.880f};  ///< warm white \#fff5e0

/// Speaker ribbon text fill color. Same rationale as body text: light fill +
/// black outline reads on any accent background without a luminance flip.
constexpr glm::vec3 DIALOGUE_RIBBON_TEXT_COLOR{1.000f, 0.960f, 0.880f};

/// Speaker ribbon height (in virtual px).
constexpr float DIALOGUE_RIBBON_HEIGHT = 10.0f;

/// Speaker ribbon horizontal padding around text (each side).
constexpr float DIALOGUE_RIBBON_PADDING_X = 4.0f;

/// Selected-option triangle dimensions (in virtual px).
constexpr float DIALOGUE_SELECTION_TRIANGLE_W = 4.0f;
constexpr float DIALOGUE_SELECTION_TRIANGLE_H = 6.0f;

/// Fallback accent color when SampleDominantNonSkinColor finds nothing usable.
/// Matches the existing speaker-color gold so monochrome NPCs preserve the old look.
constexpr glm::vec3 DIALOGUE_ACCENT_FALLBACK{0.85f, 0.75f, 0.40f};

}  // namespace ambience
