#version 450

// -----------------------------------------------------------------------------
// Bloom Downsample (13-tap weighted box, COD / Siggraph 2014 reference)
//
// Halves resolution each pass. The 13-tap pattern combines:
//   - 4 inner samples (a 2x2 quad at +/-0.5 source-texel offsets): 0.5 weight total
//   - 9 outer samples (3x3 grid at +/-1 source-texel offsets):     0.5 weight total
// Sum of all weights = 1.0.
//
// The split-weighted pattern preserves more high-frequency detail than a naive
// 4-tap box filter while still fitting in 13 texture fetches per fragment.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInput;

/// Texel size of the SOURCE (input) texture - we sample at offsets in source units.
uniform vec2 uSrcTexelSize;

void main()
{
    vec2 t = uSrcTexelSize;

    // Inner 2x2 quad: each sample weight 0.125. Total contribution: 0.5.
    vec3 inner = texture(uInput, vUV + vec2(-0.5, -0.5) * t).rgb;
    inner += texture(uInput, vUV + vec2(+0.5, -0.5) * t).rgb;
    inner += texture(uInput, vUV + vec2(-0.5, +0.5) * t).rgb;
    inner += texture(uInput, vUV + vec2(+0.5, +0.5) * t).rgb;
    inner *= 0.125;

    // Outer 3x3: each sample weight ~ 0.0556 (= 0.5 / 9). Total contribution: 0.5.
    vec3 outer = texture(uInput, vUV + vec2(-1.0, -1.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+0.0, -1.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+1.0, -1.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(-1.0, +0.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+0.0, +0.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+1.0, +0.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(-1.0, +1.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+0.0, +1.0) * t).rgb;
    outer += texture(uInput, vUV + vec2(+1.0, +1.0) * t).rgb;
    outer *= 0.5 / 9.0;

    FragColor = vec4(inner + outer, 1.0);
}
