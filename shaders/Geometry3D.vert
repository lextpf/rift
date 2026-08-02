#version 450

// -----------------------------------------------------------------------------
// World Geometry Vertex Shader ("Geometry3D.vert")
// -----------------------------------------------------------------------------
// The 3D counterpart to Geometry.vert. Where that shader draws screen-space 2D
// positions under a plain orthographic matrix, this one takes real world-space
// positions and multiplies them by a view-projection matrix.
//
// Geometry.vert is retained for the flat 2D world path and for screen-space UI
// (menus, console, editor HUD, text), which must never be projected.
// -----------------------------------------------------------------------------

// ---------------------------
// Vertex attributes (inputs)
// ---------------------------

// Scene-space position. See src/SceneMath.hpp for the mapping from Rift's
// Y-down world pixels: scene = (worldX, height, worldY), Y-up right-handed.
layout(location = 0) in vec3 aPos;

// Texture coordinates (UVs), already resolved to atlas space on the CPU.
layout(location = 1) in vec2 aTexCoord;

// Per-vertex RGBA tint. Carries the sprite/tile colour and alpha so that a
// single batch can mix differently tinted quads without a uniform change.
layout(location = 2) in vec4 aColor;

// ---------------------------
// Varyings (outputs)
// ---------------------------

layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec4 VertexColor;

// -----------------------------------------------------------------------------
// Uniform / per-draw data
// -----------------------------------------------------------------------------
#ifdef USE_VULKAN

// One combined matrix rather than separate view and projection: the CPU already
// has to build both to extract frustum planes, so multiplying there costs
// nothing and halves the push-constant footprint.
layout(push_constant) uniform PushConstants
{
    layout(offset = 0) mat4 viewProjection;
    layout(offset = 64) vec3 ambientColor;
    layout(offset = 76) float alphaCutoff;
}
pc;

#else

uniform mat4 viewProjection;

#endif

void main()
{
#ifdef USE_VULKAN
    gl_Position = pc.viewProjection * vec4(aPos, 1.0);
#else
    gl_Position = viewProjection * vec4(aPos, 1.0);
#endif

    TexCoord = aTexCoord;
    VertexColor = aColor;
}
