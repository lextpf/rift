#version 450

// -----------------------------------------------------------------------------
// Sprite Fragment Shader
// Renders a pixel ("fragment") for a sprite / quad.
// Supports two build paths:
//   1 Vulkan: uses push constants for per-draw data (USE_VULKAN defined)
//   2 OpenGL: uses traditional uniforms
// -----------------------------------------------------------------------------

// Alpha cutoff threshold for hard sprite cutouts.
const float ALPHA_CUTOFF = 0.1;

// ---------------------------
// Fragment shader output
// ---------------------------
// The final RGBA color written to the framebuffer for this fragment.
layout (location = 0) out vec4 FragColor;

// ---------------------------
// Inputs from vertex shader
// ---------------------------
// Interpolated texture coordinates (UVs) for sampling the sprite texture.
// "location = 0" must match the vertex shader output location.
layout (location = 0) in vec2 TexCoord;

// Interpolated per-vertex color (RGBA).
// Used only in one rendering mode (batched colored rects / vertex color mode).
layout (location = 1) in vec4 VertexColor;

// ---------------------------
// Texture sampler
// ---------------------------
// The sprite texture to sample from.
// "binding = 0" indicates which descriptor/texture unit it is expected to be bound to.
layout (binding = 0) uniform sampler2D sprite;

// -----------------------------------------------------------------------------
// Uniform data (two alternatives depending on USE_VULKAN)
// -----------------------------------------------------------------------------
#ifdef USE_VULKAN

// Vulkan path: use push constants (small, fast per-draw data blob).
// NOTE: projection/model are not used in this fragment shader, but may be part of
// a shared push-constant layout used by the vertex shader too.
// The explicit offsets ensure the C++ side and shader side agree on layout.
layout(push_constant) uniform PushConstants {
    layout(offset = 0)   mat4 projection;     // likely used in vertex shader
    layout(offset = 64)  mat4 model;          // likely used in vertex shader
    layout(offset = 128) vec3 spriteColor;    // tint color multiplied with texture RGB
    layout(offset = 140) float useColorOnly;  // mode flag (float for Vulkan packing/alignment)
    layout(offset = 144) vec4 colorOnly;      // solid RGBA color if "color-only" mode is active
    layout(offset = 160) vec3 ambientColor;   // global ambient light color for day/night cycle
    layout(offset = 172) float spriteAlpha;   // alpha multiplier for texture mode (default 1.0)
} pc;

#else

// Non-Vulkan path (e.g. OpenGL):
// spriteColor: tint multiplied with sampled texture.
// useColorOnly controls which output mode is used:
//   0 = use texture
//   1 = use uniform colorOnly
//   2 = use per-vertex color VertexColor
uniform vec3 spriteColor;
uniform int  useColorOnly;
uniform vec4 colorOnly;
uniform float spriteAlpha;  // alpha multiplier for texture mode (default 1.0)
uniform vec3 ambientColor;  // global ambient light color for day/night cycle

#endif

// -----------------------------------------------------------------------------
// Main fragment shader entry point
// -----------------------------------------------------------------------------
void main() {

#ifdef USE_VULKAN

    // -------------------------------------------------------------------------
    // Vulkan mode selection
    // -------------------------------------------------------------------------
    // In this branch, useColorOnly comes in as a float from C++ (often easier for
    // packing / alignment). Treat it like a boolean:
    //   "true" if > 0.5, otherwise "false".
    //
    // NOTE: This Vulkan branch supports only TWO modes:
    //   - Color-only (solid uniform color)
    //   - Textured + tinted
    // It does NOT implement the "vertex color mode" (useColorOnly == 2) like the
    // non-Vulkan branch does.
    // -------------------------------------------------------------------------
    if (pc.useColorOnly > 0.5) {
        // Solid color mode:
        // Ignore texture completely and output the uniform RGBA color.
        FragColor = pc.colorOnly;
    } else {
        // Texture mode:
        // Sample the sprite texture at the interpolated UV coordinates.
        vec4 texColor = texture(sprite, TexCoord);

        // Alpha cutout:
        // If the texture pixel is mostly transparent, drop the fragment entirely.
        // This means:
        //   - No color write
        //   - No depth write
        // Useful for hard cutout sprites / avoiding tiny alpha noise.
        // Tradeoff: can create hard edges if you wanted smooth transparency.
        if (texColor.a < ALPHA_CUTOFF)
            discard;

        // Tint the sampled texture:
        // Multiply texture RGB by spriteColor and ambientColor (for day/night cycle).
        // Multiply alpha by spriteAlpha for transparency control.
        FragColor = vec4(pc.spriteColor * pc.ambientColor * texColor.rgb, pc.spriteAlpha * texColor.a);
    }

#else

    // -------------------------------------------------------------------------
    // Non-Vulkan mode selection (four modes via integer)
    // -------------------------------------------------------------------------
    if (useColorOnly == 3) {
        // Textured particle mode:
        // Sample texture and multiply by per-vertex color (for batched particles).
        vec4 texColor = texture(sprite, TexCoord);
        if (texColor.a < ALPHA_CUTOFF)
            discard;
        FragColor = texColor * VertexColor;

    } else if (useColorOnly == 2) {
        // Per-vertex color mode:
        // Output the interpolated color coming from the vertices.
        // Common use: batched colored rectangles, gradients, debug overlays.
        FragColor = VertexColor;

    } else if (useColorOnly == 1) {
        // Uniform solid color mode:
        // Ignore texture and output a single RGBA color (e.g., for colored quads).
        FragColor = colorOnly;

    } else {
        // Texture mode (useColorOnly == 0):
        // Sample sprite texture using UVs.
        vec4 texColor = texture(sprite, TexCoord);

        // Alpha cutout (same idea as Vulkan branch):
        // Discard fragments with very low alpha to avoid drawing invisible pixels.
        if (texColor.a < ALPHA_CUTOFF)
            discard;

        // Tint the texture with spriteColor and ambientColor (for day/night cycle).
        // Multiply RGB by both colors, multiply alpha by spriteAlpha.
        FragColor = vec4(spriteColor * ambientColor * texColor.rgb, spriteAlpha * texColor.a);
    }

#endif

}
