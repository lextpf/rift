#pragma once

#include "AmbienceConfig.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

/**
 * @file PostFXParams.h
 * @brief Per-frame parameters threaded into the post-processing pass, plus
 *        pure-math helpers shared between the shader and CPU-side tests.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Constructed by Game::Render() each frame from TimeManager and friends, then
 * handed to IRenderer::EndSceneApplyPostFX() which composites the offscreen
 * scene through the bloom + grading + vignette + grain + tonemap chain.
 *
 * The grading model uses the industry-standard ASC CDL split (lift / gamma /
 * gain) instead of a single scalar tint so we can independently push shadows,
 * midtones, and highlights - required for the per-time-of-day character that a
 * single multiplier can't express.
 */

/**
 * @struct GradingParams
 * @brief Lift / gamma / gain triplet - the ASC CDL model for color grading.
 *
 * Standard formula: `out = pow(in * gain + lift, 1.0 / gamma)`.
 * - `lift`  shifts shadows (additive, fades toward black)
 * - `gamma` curves midtones (per-channel power)
 * - `gain`  scales highlights (multiplicative)
 *
 * Identity (no grading) is `lift = vec3(0)`, `gamma = vec3(1)`, `gain = vec3(1)`.
 */
struct GradingParams
{
    glm::vec3 lift{0.0f};
    glm::vec3 gamma{1.0f};
    glm::vec3 gain{1.0f};
};

/**
 * @brief Pure-math implementation of the ASC CDL formula. Inlined for tests.
 *
 * `pow` of a negative base is undefined, so the intermediate `c * gain + lift`
 * is clamped to zero before the power step.
 */
inline glm::vec3 ApplyLGG(const glm::vec3& c,
                          const glm::vec3& lift,
                          const glm::vec3& gamma,
                          const glm::vec3& gain)
{
    glm::vec3 step = c * gain + lift;
    step = glm::max(step, glm::vec3(0.0f));
    return glm::vec3(std::pow(step.r, 1.0f / gamma.r),
                     std::pow(step.g, 1.0f / gamma.g),
                     std::pow(step.b, 1.0f / gamma.b));
}

/**
 * @brief Karis-style soft threshold weight for bloom bright-pass.
 *
 * Replaces the hard smoothstep cutoff so bloom doesn't pop on/off as scene
 * luminance crosses the threshold. Returns 0 below the threshold, ramping
 * smoothly above. Retained as public API even though no shader path currently
 * feeds it - the chroma-bleed pipeline uses KarisBloomChromaWeight instead.
 */
inline float KarisBloomWeight(float lum, float threshold)
{
    float over = std::max(lum - threshold, 0.0f);
    return over / (1.0f + over);
}

/**
 * @brief HSV saturation - "how colored is this pixel."
 *
 * Returns `(max - min) / max` over the RGB channels, or 0 for all-equal
 * (achromatic) input. Mirrors the GLSL formula in bloom_threshold.frag so the
 * same math is testable on the CPU side. The 1e-4 epsilon guards against a
 * divide-by-zero on near-black pixels (which read as achromatic anyway).
 *
 * V-relative saturation reads dim colored pixels as fully saturated - a dim
 * red `(0.3, 0, 0)` returns 1.0 the same as bright red `(1, 0, 0)`. That
 * matches the arcade-neon intuition: the eye reads "colored," not "intense."
 */
inline float HsvSaturation(const glm::vec3& c)
{
    float maxC = std::max({c.r, c.g, c.b});
    float minC = std::min({c.r, c.g, c.b});
    return (maxC > 1e-4f) ? (maxC - minC) / maxC : 0.0f;
}

/**
 * @brief Karis-style soft saturation weight for the chroma-bloom bright-pass.
 *
 * Returns 0 below the saturation threshold, ramping smoothly above. Same shape
 * as KarisBloomWeight but gated on HSV saturation instead of luma, so only
 * colored pixels enter the bloom mip chain.
 */
inline float KarisBloomChromaWeight(float sat, float threshold)
{
    float over = std::max(sat - threshold, 0.0f);
    return over / (1.0f + over);
}

/**
 * @brief Pure-math saturation pump. Mirrors the GLSL `applySaturation()` in
 *        post.frag so the same formula is testable on the CPU side.
 *
 * `s = 1.0` is identity, `s > 1.0` pumps chroma away from luma, `s = 0.0`
 * collapses to grayscale. LUMA weights match the shader.
 */
inline glm::vec3 ApplySaturation(const glm::vec3& c, float s)
{
    const glm::vec3 LUMA{0.2126f, 0.7152f, 0.0722f};
    float lum = glm::dot(c, LUMA);
    return glm::mix(glm::vec3(lum), c, s);
}

/**
 * @brief Compute per-time-of-day LGG grading parameters.
 *
 * Anchors:
 * - Dawn (~6h):   cool lift / slight gamma lift / warm gain
 * - Midday (12h): identity (no grading)
 * - Dusk (~19h):  purple lift / slight gamma lift / warm-orange gain
 * - Night:        navy lift / slight gamma crush / cool muted gain
 *
 * All values capped well below +/-0.08 (matching `GRADING_TINT_AMPLITUDE`).
 * Inline coefficients here are the documentation amplitude scaled by 1.6 -
 * bumping `GRADING_TINT_AMPLITUDE` alone changes the doc anchor; the magnitudes
 * here must be scaled together.
 */
inline GradingParams ComputeGradingParams(float timeOfDay, float nightFactor)
{
    // Warmth ramps at golden hour (dawn 5-7h, dusk 18-20h), zero elsewhere.
    float warmth = 0.0f;
    bool isDusk = false;
    if (timeOfDay >= 5.0f && timeOfDay <= 7.0f)
    {
        warmth = 1.0f - std::abs(timeOfDay - 6.0f);
    }
    else if (timeOfDay >= 18.0f && timeOfDay <= 20.0f)
    {
        warmth = 1.0f - std::abs(timeOfDay - 19.0f);
        isDusk = true;
    }

    GradingParams p;

    // Gain (highlights): warm at dawn, orange at dusk, cool muted at night.
    if (isDusk)
    {
        p.gain = glm::vec3(1.0f + 0.096f * warmth, 1.0f + 0.048f * warmth, 1.0f - 0.064f * warmth);
    }
    else
    {
        p.gain = glm::vec3(1.0f + 0.080f * warmth, 1.0f + 0.032f * warmth, 1.0f - 0.048f * warmth);
    }
    p.gain += glm::vec3(-0.064f, -0.032f, +0.064f) * nightFactor;

    // Lift (shadows): cool at dawn, purple at dusk, navy at night.
    if (isDusk)
    {
        p.lift = glm::vec3(+0.008f, -0.008f, +0.016f) * warmth;
    }
    else
    {
        p.lift = glm::vec3(-0.008f, 0.0f, +0.024f) * warmth;
    }
    p.lift += glm::vec3(-0.016f, -0.008f, +0.032f) * nightFactor;

    // Gamma (midtones): slight lift at golden hour, slight crush at night.
    if (isDusk)
    {
        p.gamma = glm::vec3(1.0f + 0.016f * warmth, 1.0f, 1.0f - 0.016f * warmth);
    }
    else
    {
        p.gamma = glm::vec3(1.0f + 0.032f * warmth, 1.0f + 0.016f * warmth, 1.0f);
    }
    p.gamma += glm::vec3(-0.048f, -0.032f, 0.0f) * nightFactor;

    return p;
}

/**
 * @struct PostFXParams
 * @brief Per-frame post-processing parameters.
 */
struct PostFXParams
{
    /// Time of day in hours [0, 24], driven by TimeManager.
    float timeOfDay{12.0f};

    /// Night factor [0, 1]. 0 = full day, 1 = deep night.
    float nightFactor{0.0f};

    /// Vignette intensity scalar (0 disables vignette this frame).
    float vignetteIntensity{ambience::VIGNETTE_INTENSITY};

    /// Film grain intensity scalar.
    float grainIntensity{ambience::GRAIN_INTENSITY};

    /// Bloom intensity scalar applied during composite.
    float bloomIntensity{ambience::BLOOM_INTENSITY};

    /// Color saturation multiplier. 1.0 = identity, >1 pumps chroma, 0 = grayscale.
    float saturation{ambience::COLOR_SATURATION};

    /// LGG grading parameters (see GradingParams). Default = identity.
    GradingParams gradingParams{};

    /// Time accumulator (seconds) - drives the grain noise seed.
    float time{0.0f};

    /// Master gate. When false the post fragment shader early-returns the raw
    /// scene texel, bypassing every post step (CA, blur, bloom, grading,
    /// saturation, vignette, grain, tonemap). Toggled from the dev console via
    /// `postfx [on|off|toggle]` (aliases: `fx`, `pfx`).
    bool postFXEnabled{true};
};
