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

// 2D position of the vertex (typically in local/object space of the sprite quad).
layout (location = 0) in vec2 aPos;

// Texture coordinates (UVs) for this vertex (0..1 range typically).
layout (location = 1) in vec2 aTexCoord;

// Optional per-vertex color. Useful for:
//  - batching colored rectangles (no texture)
//  - gradients
//  - per-vertex tinting effects
layout (location = 2) in vec4 aColor;  // RGBA

// ---------------------------
// Varyings (outputs to fragment shader)
// ---------------------------
// These are interpolated across the triangle and become "in" variables in the
// fragment shader with matching locations.

// UV coordinates forwarded to the fragment shader for texture sampling.
layout (location = 0) out vec2 TexCoord;

// Per-vertex color forwarded to fragment shader (interpolated across the face).
layout (location = 1) out vec4 VertexColor;

// -----------------------------------------------------------------------------
// Uniform / per-draw data
// -----------------------------------------------------------------------------
#ifdef USE_VULKAN

// Vulkan path: push constants for per-draw transform + extra sprite parameters.
// Even if spriteColor/useColorOnly/colorOnly aren't used in THIS vertex shader,
// they may be packed here to share one consistent push-constant layout with the
// fragment shader and/or C++ side.
//
// Offsets are explicit to guarantee binary layout matches the CPU side.
layout(push_constant) uniform PushConstants {
    layout(offset = 0)   mat4 projection;     // camera/projection transform
    layout(offset = 64)  mat4 model;          // object/sprite transform (position/scale/rotation)
    layout(offset = 128) vec3 spriteColor;    // (not used here) tint for fragment shader
    layout(offset = 140) float useColorOnly;  // (not used here) mode switch for fragment shader
    layout(offset = 144) vec4 colorOnly;      // (not used here) uniform solid color
} pc;

#else

// Non-Vulkan path: classic uniforms for transforms.
// projection: converts world/view coords to clip space (camera).
// model: converts local sprite coords to world/view coords (position/scale/rotation).
uniform mat4 projection;
uniform mat4 model;

#endif

// -----------------------------------------------------------------------------
// Main vertex shader entry point
// -----------------------------------------------------------------------------
void main() {

    // -------------------------------------------------------------------------
    // Compute clip-space position (gl_Position)
    // -------------------------------------------------------------------------
    // aPos is 2D, so we extend it to 4D homogeneous coordinates:
    //   vec4(aPos.x, aPos.y, z, w)
    // We set:
    //   z = 0.0  (sprites lie in a 2D plane)
    //   w = 1.0  (standard homogeneous position)
    //
    // Then we apply transforms:
    //   model      moves/scales/rotates the sprite in the world
    //   projection maps it into clip space (for rasterization)
    //
    // Order matters: projection * model * position
    // (Right-most is applied first.)
    // -------------------------------------------------------------------------
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
