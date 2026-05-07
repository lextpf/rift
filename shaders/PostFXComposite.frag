#version 450

// -----------------------------------------------------------------------------
// Post-process Composite Fragment Shader
//
// Reads scene + bloom textures and applies the arcade-neon post chain.
// Composition order is significant - see comments in main().
//
//   1. Sample scene with chromatic aberration per-channel offset (3 fetches)
//   2. Add chroma-only bloom (zero net luminance contribution)
//   3. LGG grading split (shadows / midtones / highlights)
//   4. Vignette + edge desaturation
//   5. Grain (luminance-modulated, slight chroma)
//   6. Tonemap (soft-shoulder)
//
// Each effect is implemented as a helper function with a single intensity
// parameter; passing 0.0 to any of them makes the shader self-cancel that
// effect cleanly. Uniform layout matches PostFXParams.h on the C++ side.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uScene;
layout(binding = 1) uniform sampler2D uBloom;

// Bloom + grading
uniform float uBloomIntensity;
uniform vec3 uLift;
uniform vec3 uGamma;
uniform vec3 uGain;
uniform float uSaturation;

// Lens character
uniform float uCAStrength;
uniform float uVignetteIntensity;
uniform float uVignetteInnerR;
uniform float uVignetteOuterR;
uniform float uVignetteAspectY;
uniform float uEdgeDesat;

// Grain
uniform float uGrainIntensity;
uniform float uGrainChromaMix;
uniform float uTime;

// Tonemap
uniform float uTonemapKnee;

// Master gate. When 0, main() early-returns the raw scene texel - no CA, no
// bloom add, no grading, no saturation, no vignette, no grain, no tonemap.
// Toggled from the developer console via `postfx [on|off|toggle]`
// (aliases: `fx`, `pfx`).
uniform int uPostFXEnabled;

const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);

float hash21(vec2 p)
{
    // Cheap 2D->1D hash. Used only for grain noise.
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

// -----------------------------------------------------------------------------
// 1. Scene sample with chromatic aberration.
//
// Each color channel is sampled at a radial offset from screen center so the
// center stays pixel-perfect (offset = 0 at uv=0.5) and edges feather with a
// soft color fringe (R outward, B inward). 3 fetches per fragment - no extra
// blur kernel; the previous sub-pixel box blur was excised because the bloom
// chroma path now handles all the "color bleed" duty and the blur was just
// muddying CA without contributing visible softening.
// -----------------------------------------------------------------------------
vec3 sampleSceneCA(vec2 uv, float caAmount)
{
    vec2 caDir = uv - 0.5;
    vec3 col;
    col.r = texture(uScene, uv + caDir * caAmount).r;
    col.g = texture(uScene, uv).g;
    col.b = texture(uScene, uv - caDir * caAmount).b;
    return col;
}

// -----------------------------------------------------------------------------
// 3. ASC CDL grading: out = pow(in * gain + lift, 1.0 / gamma)
// -----------------------------------------------------------------------------
vec3 applyLGG(vec3 c, vec3 lift, vec3 gamma, vec3 gain)
{
    vec3 step = max(c * gain + lift, vec3(0.0));
    return pow(step, vec3(1.0) / gamma);
}

// -----------------------------------------------------------------------------
// 3.5. Saturation pump (chroma boost). 1.0 = identity, >1 pumps, 0 = grayscale.
//      Placed AFTER grading (so grading's warm/cool tint is the input) and
//      BEFORE vignette (so corner edge-desat operates on saturated colors).
// -----------------------------------------------------------------------------
vec3 applySaturation(vec3 c, float s)
{
    float lum = dot(c, LUMA);
    return mix(vec3(lum), c, s);
}

// -----------------------------------------------------------------------------
// 4a. Elliptical vignette factor (0 at center, 1 at corners).
// -----------------------------------------------------------------------------
float computeVignetteFactor(vec2 uv, float aspectY, float innerR, float outerR)
{
    vec2 uvCentered = (uv - 0.5) * vec2(1.0, aspectY);
    // sqrt(2) so the corner of an unscaled UV space hits r=1.
    float r = length(uvCentered) * 1.41421356;
    return smoothstep(innerR, outerR, r);
}

// -----------------------------------------------------------------------------
// 4b. Apply vignette darkening + edge desaturation.
// -----------------------------------------------------------------------------
vec3 applyVignette(vec3 c, float vig, float intensity, float edgeDesat)
{
    vec3 darkened = c * mix(1.0, 1.0 - intensity, vig);
    float lum = dot(darkened, LUMA);
    return mix(darkened, vec3(lum), vig * edgeDesat);
}

// -----------------------------------------------------------------------------
// 5. Film grain - luminance-modulated, slight chroma, 2px tiles.
// -----------------------------------------------------------------------------
vec3 applyGrain(vec3 c, float time, float intensity, float chromaMix)
{
    float lum = dot(c, LUMA);
    // Parabola peaks at L=0.5, =0 at L=0 and L=1 -> grain visible only in midtones.
    float lumMod = 4.0 * lum * (1.0 - lum);

    // 2x2 pixel tiles (gl_FragCoord is in pixels).
    vec2 grainCoord = floor(gl_FragCoord.xy * 0.5) + time * 60.0;
    vec3 grain;
    grain.r = hash21(grainCoord) - 0.5;
    grain.g = hash21(grainCoord + 17.13) - 0.5;
    grain.b = hash21(grainCoord + 91.41) - 0.5;
    vec3 grainLuma = vec3(dot(grain, vec3(1.0 / 3.0)));
    vec3 grainFinal = mix(grainLuma, grain, chromaMix);

    return c + grainFinal * intensity * lumMod;
}

// -----------------------------------------------------------------------------
// 6. Soft-shoulder tonemap.
// Below the knee point, output = input (LDR content passes through unchanged
// - this preserves contrast). Above, a per-channel soft shoulder rolls off
// asymptotically toward 1.0 so HDR highlights don't clip.
//
// Implementation note: the rolloff branch evaluates to `knee` for c <= knee
// (because `over` is 0), so we use `min(c, above)` to select identity below
// the knee and the compressed value above it. Both agree at c == knee, so
// the function is continuous.
// -----------------------------------------------------------------------------
vec3 softShoulderTonemap(vec3 c, float knee)
{
    vec3 over = max(c - knee, vec3(0.0));
    float headroom = max(1.0 - knee, 1e-4);
    vec3 above = knee + over * headroom / (over + headroom);
    return min(c, above);
}

void main()
{
    // 0. Master gate. When the postfx pipeline is disabled, hand the raw scene
    //    straight through - no CA, bloom add, grading, saturation, vignette,
    //    grain, or tonemap. The bloom mip chain still computes on the renderer
    //    side but its contribution here is bypassed by the early return.
    if (uPostFXEnabled == 0)
    {
        FragColor = vec4(texture(uScene, vUV).rgb, 1.0);
        return;
    }

    // 1. Scene with chromatic aberration. Bloom is sampled normally (no CA)
    //    since CA on already-blurred bloom would look wrong, and the bloom
    //    mip chain is itself a multi-scale blur.
    vec3 col = sampleSceneCA(vUV, uCAStrength);

    // 2. Add chroma-only bloom contribution.
    //    The bloom feeder (BloomPrefilter.frag) gates on HSV saturation, so
    //    only colored pixels reach this point in the pipeline. We then project
    //    the bloom sample onto the chroma plane orthogonal to the luma axis
    //    (b - vec3(dot(b, LUMA))) so dot(bChroma, LUMA) == 0 by construction:
    //    the bloom shifts hue/saturation of the underlying pixel without
    //    changing its luminance. Together with the saturation gate this is
    //    the entire "color bleed without shine" mechanism.
    //
    //    bChroma can have negative components (a pure red bloom contributes
    //    (0.79, -0.21, -0.21) to the off-channels), so we clamp to non-negative
    //    before grading - applyLGG's pow() is undefined on negatives.
    vec3 b = texture(uBloom, vUV).rgb;
    vec3 bChroma = b - vec3(dot(b, LUMA));
    col = max(col + bChroma * uBloomIntensity, vec3(0.0));

    // 3. LGG grading (shadows / midtones / highlights).
    col = applyLGG(col, uLift, uGamma, uGain);

    // 3.5. Saturation pump.
    col = applySaturation(col, uSaturation);

    // 4. Vignette + edge desaturation.
    float vig = computeVignetteFactor(vUV, uVignetteAspectY, uVignetteInnerR, uVignetteOuterR);
    col = applyVignette(col, vig, uVignetteIntensity, uEdgeDesat);

    // 5. Grain - added BEFORE tonemap so it feels camera-captured, not overlaid.
    col = applyGrain(col, uTime, uGrainIntensity, uGrainChromaMix);

    // 6. Tonemap last so the curve sees the fully-composited HDR image.
    col = softShoulderTonemap(col, uTonemapKnee);

    FragColor = vec4(col, 1.0);
}
