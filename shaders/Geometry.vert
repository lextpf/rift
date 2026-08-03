#version 450

// -----------------------------------------------------------------------------
// Sprite Vertex Shader ("vert")
// Runs once per vertex.
// Its job is to:
//   1) Transform the vertex position into clip space (gl_Position)
//   2) Pass per-vertex data (UVs, color) to the fragment shader via "out" varyings
// -----------------------------------------------------------------------------

// ---------------------------
// Vertex attributes (inputs)
// ---------------------------
// These come from your vertex buffer(s). The layout locations must match how you
// configure your vertex input bindings (Vulkan) or vertex attrib pointers (OpenGL).

// 2D position of the vertex, already in screen-space pixels. The CPU builds the
// batch vertices with the camera offset, scale and rotation applied, so no
// object-to-world transform is left for this shader to do.
layout(location = 0) in vec2 aPos;

// Texture coordinates (UVs) for this vertex (0..1 range typically).
layout(location = 1) in vec2 aTexCoord;

// Optional per-vertex color, supplied only by the OpenGL backend: the colored
// rectangle batch, the particle batch and the text batch.
// The OpenGL sprite batch binds no attribute at location 2, so aColor falls back
// to the generic attribute default (0, 0, 0, 1) there. The Vulkan 2D pipeline
// declares only locations 0 and 1, so VertexColor is undefined for every Vulkan
// draw and the Vulkan fragment branch must not read it.
layout(location = 2) in vec4 aColor;  // RGBA

// ---------------------------
// Varyings (outputs to fragment shader)
// ---------------------------
// These are interpolated across the triangle and become "in" variables in the
// fragment shader with matching locations.

// UV coordinates forwarded to the fragment shader for texture sampling.
layout(location = 0) out vec2 TexCoord;

// Per-vertex color forwarded to fragment shader (interpolated across the face).
layout(location = 1) out vec4 VertexColor;

// -----------------------------------------------------------------------------
// Uniform / per-draw data
// -----------------------------------------------------------------------------
#ifdef USE_VULKAN

// Vulkan path: push constants for per-draw transform + extra sprite parameters.
// This stage declares only the leading 160 bytes of the 176-byte range the
// pipeline layout pushes to VERTEX|FRAGMENT. ambientColor (offset 160) and
// spriteAlpha (offset 172) are fragment-only and are deliberately left out here.
// spriteColor/useColorOnly/colorOnly are unused by this stage but must stay
// declared so the shared prefix keeps its offsets.
//
// `CombinedPushConstants` in src/VulkanRenderer.cpp is the single source of truth
// for these offsets; a static_assert there pins the total size at 176 bytes.
layout(push_constant) uniform PushConstants
{
    layout(offset = 0) mat4 projection;       // orthographic screen-to-clip matrix
    layout(offset = 64) mat4 model;           // identity except on the Vulkan glyph path
    layout(offset = 128) vec3 spriteColor;    // (not used here) tint for fragment shader
    layout(offset = 140) float useColorOnly;  // (not used here) mode switch for fragment shader
    layout(offset = 144) vec4 colorOnly;      // (not used here) uniform solid color
}
pc;

#else

// Non-Vulkan path: classic uniforms for transforms.
// projection: the orthographic screen-to-clip matrix set by SetProjection.
// model: identity on every OpenGL batch, because aPos already carries the whole
// transform. Only the Vulkan glyph path ever pushes a non-identity model, so do
// not add motion here - the next flush overwrites it.
uniform mat4 projection;
uniform mat4 model;

#endif

// -----------------------------------------------------------------------------
// Main vertex shader entry point
// -----------------------------------------------------------------------------
void main()
{
    // z = 0 for every 2D sprite: this path binds no depth buffer and relies on
    // submission order for layering. Only the projection is load-bearing here;
    // model is identity outside the Vulkan glyph path.
#ifdef USE_VULKAN
    gl_Position = pc.projection * pc.model * vec4(aPos, 0.0, 1.0);
#else
    gl_Position = projection * model * vec4(aPos, 0.0, 1.0);
#endif

    // -------------------------------------------------------------------------
    // Pass through per-vertex attributes to the fragment shader
    // -------------------------------------------------------------------------
    // These will be interpolated automatically across the triangle surface.
    TexCoord = aTexCoord;
    VertexColor = aColor;
}
