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
layout(location = 0) in vec2 aPos;

// Texture coordinates (UVs) for this vertex (0..1 range typically).
layout(location = 1) in vec2 aTexCoord;

// Optional per-vertex color. Useful for:
//  - batching colored rectangles (no texture)
//  - gradients
//  - per-vertex tinting effects
layout(location = 2) in vec4 aColor;  // RGBA

// Per-vertex flag (0.0 or 1.0) telling the shader whether to apply the
// pseudo-3D perspective transform to this vertex. Captured at queue time
// from `IsPerspectiveSuspended()` so suspended and un-suspended geometry
// can share a single batch (avoids CPU-side pre-transformation flushes).
// DrawWarpedQuad and UI text emit 0.0 so their already-projected positions
// pass through unchanged.
layout(location = 3) in float aPerspectiveFlag;

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
// Even if spriteColor/useColorOnly/colorOnly aren't used in THIS vertex shader,
// they may be packed here to share one consistent push-constant layout with the
// fragment shader and/or C++ side.
//
// Offsets are explicit to guarantee binary layout matches the CPU side.
layout(push_constant) uniform PushConstants
{
    layout(offset = 0) mat4 projection;       // camera/projection transform
    layout(offset = 64) mat4 model;           // object/sprite transform (position/scale/rotation)
    layout(offset = 128) vec3 spriteColor;    // (not used here) tint for fragment shader
    layout(offset = 140) float useColorOnly;  // (not used here) mode switch for fragment shader
    layout(offset = 144) vec4 colorOnly;      // (not used here) uniform solid color
}
pc;

// Perspective state lives in its own UBO (set 1, binding 0) - state changes
// only when the player swaps perspective mode, not per draw. Layout matches
// `PerspectiveBlock` packed CPU-side (3x vec4 = 48 bytes, std140-safe).
layout(set = 1, binding = 0) uniform PerspectiveBlock
{
    ivec4 perspFlags;   // (enabled, hasGlobe, hasVanishing, _pad)
    vec4 perspHorizon;  // (horizonY, horizonScale, viewWidth, viewHeight)
    vec4 perspSphere;   // (sphereRadiusX, sphereRadiusY, _pad, _pad)
}
persp;

#define PERSP_ENABLED (persp.perspFlags.x)
#define PERSP_HAS_GLOBE (persp.perspFlags.y)
#define PERSP_HAS_VANISHING (persp.perspFlags.z)
#define PERSP_HORIZON_Y (persp.perspHorizon.x)
#define PERSP_HORIZON_SCALE (persp.perspHorizon.y)
#define PERSP_VIEW_SIZE (persp.perspHorizon.zw)
#define PERSP_SPHERE_RADIUS (persp.perspSphere.xy)

#else

// Non-Vulkan path: classic uniforms for transforms.
// projection: converts world/view coords to clip space (camera).
// model: converts local sprite coords to world/view coords (position/scale/rotation).
uniform mat4 projection;
uniform mat4 model;

// Perspective state mirrors what CPU `m_Persp` holds. Pushed when state
// changes via SetVanishingPointPerspective / SetGlobePerspective /
// SetFisheyePerspective (rare events, already batch-flush points).
uniform int perspEnabled;
uniform int perspHasGlobe;
uniform int perspHasVanishing;
uniform float perspHorizonY;
uniform float perspHorizonScale;
uniform vec2 perspViewSize;  // (viewWidth, viewHeight)
uniform vec2
    perspSphereRadius;  // (rx, ry) - CPU pre-multiplies the kGlobeRadius{X,Y}Scale constants

#define PERSP_ENABLED perspEnabled
#define PERSP_HAS_GLOBE perspHasGlobe
#define PERSP_HAS_VANISHING perspHasVanishing
#define PERSP_HORIZON_Y perspHorizonY
#define PERSP_HORIZON_SCALE perspHorizonScale
#define PERSP_VIEW_SIZE perspViewSize
#define PERSP_SPHERE_RADIUS perspSphereRadius

#endif

// -----------------------------------------------------------------------------
// Perspective transform (ported from src/PerspectiveTransform.hpp::TransformPoint)
// -----------------------------------------------------------------------------
// Two-stage transform: optional globe curvature first, optional vanishing-point
// scaling second. Guards (`dNorm > 0.001`, `denom >= 1e-5`) match the C++
// implementation. Single-precision is sufficient for the magnitudes we hit;
// see tests/PerspectiveTransformFloatTests.cpp for parity bounds.
vec2 applyPerspective(vec2 p)
{
    vec2 center = PERSP_VIEW_SIZE * 0.5;

    if (PERSP_HAS_GLOBE != 0)
    {
        vec2 R = max(PERSP_SPHERE_RADIUS, vec2(1e-5));
        vec2 d = p - center;
        vec2 nd = d / R;
        float dNorm = length(nd);
        if (dNorm > 0.001)
        {
            p = center + d * (sin(dNorm) / dNorm);
        }
    }

    if (PERSP_HAS_VANISHING != 0)
    {
        float denom = PERSP_VIEW_SIZE.y - PERSP_HORIZON_Y;
        if (denom >= 1e-5)
        {
            float t = clamp((p.y - PERSP_HORIZON_Y) / denom, 0.0, 1.0);
            float s = PERSP_HORIZON_SCALE + (1.0 - PERSP_HORIZON_SCALE) * t;
            p.x = center.x + (p.x - center.x) * s;
            p.y = PERSP_HORIZON_Y + (p.y - PERSP_HORIZON_Y) * s;
        }
    }
    return p;
}

// -----------------------------------------------------------------------------
// Main vertex shader entry point
// -----------------------------------------------------------------------------
void main()
{
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
    // Apply pseudo-3D perspective on the GPU when the per-vertex flag opts in
    // and a perspective mode is configured. Suspended geometry (UI, characters,
    // pre-warped quads) emits aPerspectiveFlag = 0.0 and passes through.
    vec2 worldPos = aPos;
    if (PERSP_ENABLED != 0 && aPerspectiveFlag > 0.5)
    {
        worldPos = applyPerspective(worldPos);
    }

#ifdef USE_VULKAN
    gl_Position = pc.projection * pc.model * vec4(worldPos, 0.0, 1.0);
#else
    gl_Position = projection * model * vec4(worldPos, 0.0, 1.0);
#endif

    // -------------------------------------------------------------------------
    // Pass through per-vertex attributes to the fragment shader
    // -------------------------------------------------------------------------
    // These will be interpolated automatically across the triangle surface.
    TexCoord = aTexCoord;
    VertexColor = aColor;
}
