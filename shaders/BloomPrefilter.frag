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
// V-relative saturation (max-min)/max is brightness-independent, so a dim red
// lantern passes the gate exactly as a bright one does: the arcade-neon eye reads
// "this pixel is colored," not "this pixel is intense." The gate only decides
// whether a pixel bleeds - the emitted value is still `col * weight`, so a dim
// lantern feeds a proportionally dim bloom.
// White / off-white / grey pixels return saturation 0 and are excluded.
//
// Output feeds the bloom mip chain, which ping-pongs over one mip pyramid:
//
//   scene (RGB16F) --BloomPrefilter--> mip0        mip0 = half scene resolution
//
//   downsample (BloomDownsample.frag), destination CLEARED each pass:
//     mip0 --> mip1 --> mip2 --> ... --> mipN-1    each level halves again
//
//   upsample (BloomUpsample.frag), GL_ONE/GL_ONE additive, destination NOT
//   cleared - it already holds what the downsample wrote there:
//     mipN-1 --+--> mipN-2 --+--> ... --+--> mip0
//
//   mip0 --> uBloom in PostFXComposite.frag
//
// uSrcTexelSize is always 1/size of the mip being SAMPLED, so it shrinks going
// down the chain and grows coming back up. Adding a clear to the upsample loop
// silently destroys the multi-scale accumulation.
//
// OpenGL-only: not in CMake's SHADER_SOURCES, never compiled to SPIR-V, and the
// default-block uniform below is illegal in Vulkan GLSL.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uScene;

// Fed from the compile-time constant `ambience::BLOOM_SATURATION_THRESHOLD`
// (src/AmbienceConfig.hpp), not from PostFXParams: the gate cannot vary per frame.
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
    // KarisBloomChromaWeight helper in src/PostFXParams.hpp.
    float over = max(sat - uSatThreshold, 0.0);
    float weight = over / (1.0 + over);

    FragColor = vec4(col * weight, 1.0);
}
