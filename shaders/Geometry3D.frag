#version 450

// -----------------------------------------------------------------------------
// World Geometry Fragment Shader ("Geometry3D.frag")
// -----------------------------------------------------------------------------
// Deliberately single-mode, unlike Geometry.frag's four-way useColorOnly switch:
// every world draw is "sample the atlas, tint it, cut out the transparent bits".
// Untextured world geometry (debug overlays, light pools) uses a 1x1 white
// texture instead of a separate shader branch.
//
// Rendering is unlit - texture x vertex colour x global ambient. That is the
// correct look here: the Pokemon DS games this is modelled on render their maps
// unlit with vertex colours, and Rift's day/night cycle is already expressed as
// an ambient multiply rather than as real lighting.
// -----------------------------------------------------------------------------

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 VertexColor;

layout(location = 0) out vec4 FragColor;

#ifdef USE_VULKAN

layout(binding = 0) uniform sampler2D sprite;

layout(push_constant) uniform PushConstants
{
    layout(offset = 0) mat4 viewProjection;
    layout(offset = 64) vec3 ambientColor;
    layout(offset = 76) float alphaCutoff;
}
pc;

#define AMBIENT_COLOR (pc.ambientColor)
#define ALPHA_CUTOFF (pc.alphaCutoff)

#else

uniform sampler2D sprite;
uniform vec3 ambientColor;
// Set per pass, not baked in: the depth-writing pass (DepthMode::TestAndWrite)
// passes renderModes::OPAQUE_ALPHA_CUTOFF = 0.5 so discarded fragments write no
// depth, while every other pass passes 1/255 and so drops only empty texels.
// Both backends read the same constant from src/RenderModes.hpp.
uniform float alphaCutoff;

#define AMBIENT_COLOR ambientColor
#define ALPHA_CUTOFF alphaCutoff

#endif

void main()
{
    vec4 texColor = texture(sprite, TexCoord);
    vec4 result = texColor * VertexColor;

    // The cutout is what makes the depth buffer work for sprite artwork: a
    // discarded fragment writes neither colour nor depth, so a tree's
    // transparent corners never occlude what is behind them - and the opaque
    // pass therefore needs no sorting at all.
    if (result.a < ALPHA_CUTOFF)
    {
        discard;
    }

    FragColor = vec4(result.rgb * AMBIENT_COLOR, result.a);
}
