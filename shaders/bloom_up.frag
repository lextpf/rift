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
// The output is added (additively combined) into the next-finer mip's bloom
// target by the renderer - that combine is done outside this shader, via
// a glBlendFunc(GL_ONE, GL_ONE) draw or an explicit add pass.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInput;

/// Texel size of the SOURCE (the smaller mip being upsampled FROM).
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
