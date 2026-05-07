#version 450

// -----------------------------------------------------------------------------
// Bloom Threshold (chroma-pass) Fragment Shader - Karis soft saturation filter
//
// Reads the scene texture and outputs only pixels with HSV saturation above a
// threshold, scaled by a Karis-style soft weight `over / (1 + over)`. This is
// the gating mechanism for the arcade-neon look: only colored pixels enter the
// bloom mip chain, so the downstream chroma-only composite in PostFXComposite.frag bleeds
// hue outward from saturated sources without lifting any luminance.
//
// V-relative saturation (max-min)/max reads dim colored pixels as fully
// saturated - a dim red lantern glows the same as a bright one, because the
// arcade-neon eye reads "this pixel is colored," not "this pixel is intense."
// White / off-white / grey pixels return saturation 0 and are excluded.
//
// Output feeds into the bloom mip chain (BloomDownsample.frag -> BloomUpsample.frag).
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uScene;
uniform float uSatThreshold;

void main()
{
    vec3 col = texture(uScene, vUV).rgb;

    // HSV saturation: (max - min) / max. Mirror of PostFXParams::HsvSaturation
    // on the CPU side so the math is testable without a GL context.
    float maxC = max(max(col.r, col.g), col.b);
    float minC = min(min(col.r, col.g), col.b);
    float sat = (maxC > 1e-4) ? (maxC - minC) / maxC : 0.0;

    // Karis soft filter on saturation: continuous (no kink at threshold), zero
    // at/below threshold, ramps smoothly above. Matches the C++
    // KarisBloomChromaWeight helper in PostFXParams.h.
    float over = max(sat - uSatThreshold, 0.0);
    float weight = over / (1.0 + over);

    FragColor = vec4(col * weight, 1.0);
}
