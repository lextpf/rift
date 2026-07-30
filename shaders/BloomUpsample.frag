#version 450

// -----------------------------------------------------------------------------
// Bloom Upsample (9-tap tent filter)
//
// Doubles resolution each pass. Uses a 3x3 tent filter (a.k.a. "fat pixel")
// which is the standard companion to the 13-tap downsample for bloom chains:
//
//     1  2  1
//     2  4  2     all divided by 16
//     1  2  1
//
// The combine happens outside this shader: the renderer draws mip[i] into
// mip[i-1] with glBlendFunc(GL_ONE, GL_ONE). Unlike the downsample loop, the
// upsample loop deliberately does NOT clear its destination - that mip already
// holds what the downsample pass wrote there, and the additive blend is what
// accumulates every coarser level onto it. Adding a defensive glClear here would
// silently flatten the multi-scale bloom. The final result lands in mip[0].
//
// OpenGL-only: not in CMake's SHADER_SOURCES, never compiled to SPIR-V, and the
// default-block uniform below is illegal in Vulkan GLSL.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInput;

// Texel size of the SOURCE (the smaller mip being upsampled FROM).
uniform vec2 uSrcTexelSize;

void main()
{
    vec2 t = uSrcTexelSize;

    // 3x3 tent kernel - corners = 1/16, edges = 2/16, center = 4/16. Sum = 16/16.
    vec3 sum = texture(uInput, vUV + vec2(-1.0, -1.0) * t).rgb * 1.0;
    sum += texture(uInput, vUV + vec2(+0.0, -1.0) * t).rgb * 2.0;
    sum += texture(uInput, vUV + vec2(+1.0, -1.0) * t).rgb * 1.0;
    sum += texture(uInput, vUV + vec2(-1.0, +0.0) * t).rgb * 2.0;
    sum += texture(uInput, vUV + vec2(+0.0, +0.0) * t).rgb * 4.0;
    sum += texture(uInput, vUV + vec2(+1.0, +0.0) * t).rgb * 2.0;
    sum += texture(uInput, vUV + vec2(-1.0, +1.0) * t).rgb * 1.0;
    sum += texture(uInput, vUV + vec2(+0.0, +1.0) * t).rgb * 2.0;
    sum += texture(uInput, vUV + vec2(+1.0, +1.0) * t).rgb * 1.0;
    sum *= (1.0 / 16.0);

    FragColor = vec4(sum, 1.0);
}
