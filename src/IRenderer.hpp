#pragma once

#include "PostFXParams.hpp"
#include "RenderModes.hpp"
#include "Texture.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <string>
#include <vector>

/**
 * @struct RendererInfo
 * @brief Backend identity snapshot returned by `IRenderer::GetBackendInfo`.
 * @ingroup Rendering
 *
 * Populated at the end of `Init()` by each backend; consumed by the
 * developer-console `renderer.info` command. Kept POD-shaped (strings + int)
 * so it can be returned by value cheaply.
 */
struct RendererInfo
{
    std::string backendName;    ///< "OpenGL" or "Vulkan".
    std::string apiVersion;     ///< Driver-reported API version string.
    std::string vendor;         ///< GPU vendor (e.g. "NVIDIA Corporation").
    std::string device;         ///< GPU device / renderer string.
    std::string driverVersion;  ///< Driver version string (Vulkan); empty for GL.
    int maxTextureSize = 0;     ///< Largest 2D texture dimension supported.
};

/**
 * @class IRenderer
 * @brief Abstract interface for 2D rendering operations.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * IRenderer defines the contract that all rendering backends must implement.
 * This abstraction allows the game to run on both OpenGL and Vulkan without
 * modification to the game logic.
 *
 * @par Design pattern
 * Implements the **Strategy Pattern** for runtime graphics API selection.
 *
 * @warning Game owns the renderer by `std::unique_ptr`, and the `renderer.set`
 * console command destroys the renderer **and** the GLFW window and constructs
 * replacements. An `IRenderer*` or `IRenderer&` cached across frames therefore
 * dangles; take the renderer by reference per call instead. Every GPU resource is
 * re-uploaded after a switch, so texture handles captured before it are stale.
 *
 * @htmlonly
 * <pre class="mermaid">
 * classDiagram
 * class IRenderer:::abstract {
 *     <<interface>>
 *     +Init()
 *     +Shutdown()
 *     +BeginFrame()
 *     +EndFrame()
 *     +DrawSprite()
 *     +DrawText()
 * }
 * class OpenGLRenderer:::opengl {
 *     +Init()
 *     +DrawSprite()
 * }
 * class VulkanRenderer:::vulkan {
 *     +Init()
 *     +DrawSprite()
 * }
 * IRenderer <|-- OpenGLRenderer
 * IRenderer <|-- VulkanRenderer
 * style IRenderer fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * style OpenGLRenderer fill:#134e3a,stroke:#10b981,color:#e2e8f0
 * style VulkanRenderer fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 * </pre>
 * @endhtmlonly
 *
 * @par Coordinate systems
 * Every 2D primitive on this interface consumes whichever ortho matrix
 * @ref SetProjection installed last, and the caller subtracts the camera itself.
 * A frame installs two of them: the scene ortho (view space) and, after post-FX,
 * the screen-pixel UI ortho. @ref DrawQuad3D is the single exception - it uses
 * @ref SetViewProjection and takes untranslated scene coordinates.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * W["World Space"]:::space
 * V["View Space
 * (world pixels)"]:::space
 * U["UI Space
 * (screen pixels)"]:::space
 * N["Normalized Device
 * Coordinates"]:::space
 * F["Framebuffer
 * pixels"]:::space
 * classDef space fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * W -->|subtract camera| V
 * V -->|scene ortho| N
 * U -->|UI ortho| N
 * N -->|viewport, x PIXEL_SCALE| F
 * </pre>
 * @endhtmlonly
 *
 * |  Space | Origin               | Range                                             |
 * |--------|----------------------|---------------------------------------------------|
 * |  World | Top-left of map      | @f$ (0,0) @f$ to @f$ (tileW*mapW, tileH*mapH) @f$ |
 * |   View | Top-left of viewport | @f$ (0,0) @f$ to @ref GetViewSize                 |
 * |     UI | Top-left of viewport | @f$ (0,0) @f$ to @f$ (screenW, screenH) @f$       |
 * |    NDC | Center               | @f$ (-1,-1) @f$ to @f$ (+1,+1) @f$                |
 *
 * Tile dimensions are per-map data (`Tilemap::GetTileWidth` / `GetTileHeight`),
 * not a constant, and width and height are independent.
 *
 * @par Rendering pipeline
 * @ref BeginScene redirects the scene into an offscreen target that
 * @ref EndSceneApplyPostFX grades and composites. Draws issued after that call go
 * straight to the swapchain ungraded, which is where the UI belongs. @ref Clear
 * runs after the offscreen target is bound, not before it.
 *
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * BF["BeginFrame()"]:::stage
 * subgraph SCENE["offscreen scene FBO on OpenGL, swapchain on Vulkan"]
 * BS["BeginScene()"]:::stage
 * CL["Clear()"]:::stage
 * SP["SetProjection
 * (scene ortho)"]:::stage
 * D2["2D scene draws"]:::stage
 * D3["SetViewProjection
 * + DrawQuad3D"]:::stage
 * BS --> CL --> SP --> D2 --> D3
 * end
 * PX["EndSceneApplyPostFX()"]:::stage
 * UI["SetProjection (UI ortho)
 * + UI draws
 * swapchain, ungraded"]:::stage
 * EF["EndFrame()"]:::stage
 * classDef stage fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 * BF --> BS
 * D3 --> PX --> UI --> EF
 * </pre>
 * @endhtmlonly
 *
 * @par Orthographic projection
 * Parallel lines stay parallel (no depth foreshortening). The scene ortho is not
 * built from screen pixels: its extent is the zoomed world view,
 * `screenPixels / PIXEL_SCALE / zoom`, which is also what @ref SetViewSize records.
 * The UI ortho installed after post-FX is the one measured in screen pixels.
 *
 * @par Matrix parameters
 * | Symbol |   Meaning   | 2D Value |
 * |--------|-------------|----------|
 * |      l |  Left edge  | 0        |
 * |      r | Right edge  | viewW    |
 * |      t |  Top edge   | 0        |
 * |      b | Bottom edge | viewH    |
 * |      n |  Near plane | -1       |
 * |      f |  Far plane  | +1       |
 *
 * `viewW` / `viewH` are the zoomed world-view extent for the scene ortho, and
 * `screenW` / `screenH` for the UI ortho.
 *
 * @par The orthographic matrix
 * @f[
 * M_{ortho} = \begin{bmatrix}
 *   \frac{2}{r-l} & 0 & 0 & -\frac{r+l}{r-l} \\
 *   0 & \frac{2}{t-b} & 0 & -\frac{t+b}{t-b} \\
 *   0 & 0 & -\frac{2}{f-n} & -\frac{f+n}{f-n} \\
 *   0 & 0 & 0 & 1
 * \end{bmatrix}
 * @f]
 *
 * @par What each row does
 * - **Row 1 (X)**: Scales X from [l, r] to [-1, +1] and centers it
 * - **Row 2 (Y)**: Scales Y from [t, b] to [-1, +1] and centers it
 * - **Row 3 (Z)**: Maps depth [n, f] to [-1, +1] (unused in 2D)
 * - **Row 4**: Homogeneous coordinate (always 1 for orthographic)
 *
 * @par Example transformation
 * A 1280x720 window at `PIXEL_SCALE` 5 and zoom 1.0 gives a 256x144 view extent:
 * - @f$ l=0, \; r=256, \; t=0, \; b=144 @f$
 * - @f$ (128, 72) \rightarrow (0, 0) @f$ (center)
 * - @f$ (0, 0) \rightarrow (-1, +1) @f$ (top-left)
 * - @f$ (256, 144) \rightarrow (+1, -1) @f$ (bottom-right)
 *
 * The UI ortho for the same window uses @f$ r=1280, \; b=720 @f$ instead.
 *
 * @par Texture coordinates
 * UV coordinates map pixel positions to the 0-1 range the GPU expects:
 * @f[
 * u = \frac{pixelX}{textureWidth}, \quad v = \frac{pixelY}{textureHeight}
 * @f]
 *
 * @par Example
 * For a 256x256 texture, pixel (128, 64) becomes UV (0.5, 0.25).
 *
 * @par Backend override sets
 * Adding, removing or re-signing a pure virtual here requires the matching edit in
 * @c RIFT_DECLARE_COMMON_RENDERER_METHODS. That macro is what keeps both backends'
 * override sets byte-identical, so the two files are always edited together.
 *
 * @see OpenGLRenderer, VulkanRenderer, Texture, RendererMacros.hpp
 */
class IRenderer
{
public:
    /// @brief Virtual destructor for proper polymorphic cleanup.
    virtual ~IRenderer() = default;

    /**
     * @brief Configure preferred font paths before Init().
     *
     * Renderers try these project-specific candidates before their built-in
     * fallback fonts. Passing an empty vector restores backend defaults.
     *
     * @param fontCandidates Ordered list of TTF font paths.
     */
    virtual void SetFontCandidates(const std::vector<std::string>& fontCandidates) = 0;

    /**
     * @brief Initialize the renderer.
     *
     * Creates GPU resources, compiles shaders, and sets up rendering state.
     * Must be called after window creation but before any rendering.
     *
     * @par OpenGL initialization
     * - Compile sprite shader program
     * - Create VAO/VBO for quad rendering
     * - Enable blending for transparency
     *
     * @par Vulkan initialization
     * - Create instance, device, swapchain
     * - Create render pass, pipeline
     * - Allocate command buffers
     * - Create descriptor sets
     *
     * @return true on success, false if any setup step failed. On false
     * the renderer is not safe to draw with; the caller should Shutdown()
     * and destroy it rather than ship a silently black frame.
     */
    [[nodiscard]] virtual bool Init() = 0;

    /**
     * @brief Shutdown and release all GPU resources.
     *
     * Destroys all graphics resources created during Init().
     * Must be called before window destruction.
     */
    virtual void Shutdown() = 0;

    /**
     * @brief Begin a new rendering frame.
     *
     * Prepares the GPU for drawing. Must be called before any draw calls.
     *
     * @par OpenGL
     * - Reset batch state (vertices, textures, draw call count)
     * - Buffer clearing is handled separately via Clear()
     *
     * @par Vulkan
     * - Acquire swapchain image
     * - Begin command buffer recording
     * - Begin render pass
     */
    virtual void BeginFrame() = 0;

    /**
     * @brief End the current frame and present to screen.
     *
     * Finalizes rendering and displays the result.
     *
     * @par OpenGL
     * - Flush rendering commands
     * - Swap buffers handled by GLFW
     *
     * @par Vulkan
     * - End render pass
     * - Submit command buffer
     * - Present swapchain image
     */
    virtual void EndFrame() = 0;

    /**
     * @brief Redirect subsequent draws to an offscreen scene framebuffer.
     *
     * Called from Game::Render() after BeginFrame() and before clearing.
     * Subsequent draws (world, sky, lights) accumulate into the scene
     * target instead of the swapchain. UI passes draw directly to the
     * swapchain after EndSceneApplyPostFX() unbinds the offscreen target.
     *
     * @par OpenGL
     * - Bind offscreen FBO with color texture + depth renderbuffer
     * - FBO is recreated on resize via the existing window-refresh path
     *
     * @par Vulkan
     * - Currently a no-op (Vulkan post-FX is a later phase). Scene renders
     *   directly to swapchain; renderer.set still works without bloom/grading.
     */
    virtual void BeginScene() = 0;

    /**
     * @brief Composite the offscreen scene through the post-FX chain into
     *        the swapchain.
     *
     * Backend-specific operation:
     * - OpenGL runs an HSV-saturation bright-pass over the scene texture, builds
     *   and additively upsamples a bloom mip chain, then composites via a
     *   full-screen triangle.
     * - Vulkan is currently a no-op post-FX path; the scene has already rendered
     *   to the swapchain, so this returns without bloom or color grading. Every
     *   field of @p params is therefore ignored on the Vulkan backend.
     *
     * The OpenGL composite applies, in this order (see PostFXComposite.frag):
     * 1. Scene sample with radial chromatic aberration (3 fetches).
     * 2. Chroma-only bloom add (luma-orthogonal, so it tints without brightening).
     * 3. Lift/gamma/gain grading (per-time-of-day RGB split).
     * 4. Saturation pump.
     * 5. Vignette + edge desaturation (elliptical smoothstep from screen center).
     * 6. Film grain (luminance-modulated, 2x2 pixel tiles).
     * 7. Soft-shoulder tonemap.
     *
     * After this returns, subsequent draws go to the swapchain unmodified
     * (so dialogue, editor, debug overlays stay sharp and ungrained).
     *
     * @param params Frame-specific intensities + time-of-day blend factors.
     *               Ignored entirely by the Vulkan backend.
     */
    virtual void EndSceneApplyPostFX(const PostFXParams& params) = 0;

    /**
     * @brief Draw a sprite using the backend's default texture region.
     *
     * Position is the **top-left corner** of the sprite. DrawSprite samples the
     * full texture extent. Use DrawSpriteRegion for pixel-coordinate subregions
     * or DrawSpriteAtlas for normalized UV control.
     *
     * @par Region coordinate conventions
     * DrawSpriteRegion uses pixel units:
     * @f[
     * \vec{uv}_{min} = \frac{\vec{texCoord}}{\vec{textureSize}}
     * @f]
     * @f[
     * \vec{uv}_{max} = \frac{\vec{texCoord} + \vec{texSize}}{\vec{textureSize}}
     * @f]
     * DrawSpriteAtlas uses normalized UVs directly in @f$[0,1]@f$.
     *
     * @par Transformation order
     * 1. Scale to size
     * 2. Rotate around center
     * 3. Translate to position
     *
     * @param texture  Texture to draw.
     * @param position Top-left corner in the current SetProjection space (camera pre-subtracted).
     * @param size     Sprite dimensions in pixels (default: 32x32).
     * @param rotation Rotation in degrees, clockwise (default: 0).
     * @param color    Color tint/modulation (default: white = no tint).
     */
    virtual void DrawSprite(const Texture& texture,
                            glm::vec2 position,
                            glm::vec2 size = glm::vec2(32.0f, 32.0f),
                            float rotation = 0.0f,
                            glm::vec3 color = glm::vec3(1.0f)) = 0;

    /**
     * @brief Draw a sprite with alpha tinting and optional additive blending.
     *
     * Similar to DrawSprite but supports alpha modulation and additive blending
     * for effects like particles and glows.
     *
     * @param texture  Texture to render.
     * @param position Top-left corner in the current SetProjection space (camera pre-subtracted).
     * @param size     Output size in pixels.
     * @param rotation Rotation in degrees, clockwise (default: 0).
     * @param color    RGBA color tint/modulation.
     * @param additive Use additive blending for glow effects (default: false).
     *                 Backend support varies; Vulkan currently ignores this flag.
     */
    virtual void DrawSpriteAlpha(const Texture& texture,
                                 glm::vec2 position,
                                 glm::vec2 size,
                                 float rotation,
                                 glm::vec4 color,
                                 bool additive = false) = 0;

    /**
     * @brief Draw a region of a texture (sprite sheet).
     *
     * Renders a rectangular portion of the texture, useful for sprite
     * sheets and tile atlases. The region is specified in **pixel coordinates**.
     *
     * @param texture   Texture containing the sprite sheet.
     * @param position  Top-left corner in the current SetProjection space (camera
     *                  pre-subtracted).
     * @param size      Output size in pixels.
     * @param texCoord  Top-left corner of region in **pixels**.
     * @param texSize   Size of region in **pixels**.
     * @param rotation  Rotation in degrees (default: 0).
     * @param color     Color tint (default: white).
     * @param flipY     Flip vertical UV for renderer-origin convention
     *                  (default: true for OpenGL). Distinct from per-tile mirroring.
     * @param tileFlipX Mirror the sampled source region horizontally before rotation.
     * @param tileFlipY Mirror the sampled source region vertically before rotation.
     */
    virtual void DrawSpriteRegion(const Texture& texture,
                                  glm::vec2 position,
                                  glm::vec2 size,
                                  glm::vec2 texCoord,
                                  glm::vec2 texSize,
                                  float rotation = 0.0f,
                                  glm::vec3 color = glm::vec3(1.0f),
                                  bool flipY = true,
                                  bool tileFlipX = false,
                                  bool tileFlipY = false) = 0;

    /**
     * @brief Draw a sprite from a texture atlas with per-vertex alpha.
     *
     * Renders a region of a texture atlas using normalized UV coordinates.
     * Supports per-vertex color/alpha and additive blending for particles.
     *
     * @param texture  Atlas texture.
     * @param position Top-left corner in the current SetProjection space (camera pre-subtracted).
     * @param size     Output size in pixels.
     * @param uvMin    Top-left UV coordinates (normalized 0-1).
     * @param uvMax    Bottom-right UV coordinates (normalized 0-1).
     * @param rotation Rotation in degrees, clockwise.
     * @param color    RGBA color tint/modulation.
     * @param additive Use additive blending for glow effects.
     *                 Backend support varies; Vulkan currently ignores this flag.
     */
    virtual void DrawSpriteAtlas(const Texture& texture,
                                 glm::vec2 position,
                                 glm::vec2 size,
                                 glm::vec2 uvMin,
                                 glm::vec2 uvMax,
                                 float rotation,
                                 glm::vec4 color,
                                 bool additive = false) = 0;

    /**
     * @brief Draw a solid colored rectangle.
     *
     * Renders a filled rectangle with the specified RGBA color.
     * Useful for UI elements, debug overlays, and backgrounds.
     *
     * @par Alpha blending variables
     * |          Symbol | Meaning                               |
     * |-----------------|---------------------------------------|
     * | @f$ C_{out} @f$ | Final pixel color written to screen   |
     * | @f$ C_{src} @f$ | Rectangle color                       |
     * | @f$ C_{dst} @f$ | Existing pixel color                  |
     * |  @f$ \alpha @f$ | Opacity (0 = transparent, 1 = opaque) |
     *
     * @par Standard blend (additive=false)
     * @f[
     * C_{out} = C_{src} \times \alpha + C_{dst} \times (1 - \alpha)
     * @f]
     * Linearly interpolates between source and destination.
     * - @f$ \alpha = 0.5 @f$: 50% mix of both colors
     * - @f$ \alpha = 1.0 @f$: Fully opaque, destination hidden
     * - @f$ \alpha = 0.0 @f$: Fully transparent, destination unchanged
     *
     * @par Additive blend (additive=true)
     * @f[
     * C_{out} = C_{src} \times \alpha + C_{dst}
     * @f]
     * Adds source color to destination, making pixels brighter.
     * - @see ParticleSystem uses this for glowing/emissive particles
     *
     * @param position Top-left corner in the current SetProjection space (camera pre-subtracted).
     * @param size     Rectangle dimensions.
     * @param color    RGBA color (0-1 range).
     * @param additive Use additive blending for glow effects (default: false).
     *                 Backend support varies; Vulkan currently ignores this flag.
     */
    virtual void DrawColoredRect(glm::vec2 position,
                                 glm::vec2 size,
                                 glm::vec4 color,
                                 bool additive = false) = 0;

    /**
     * @brief Draw a texture region onto a quad given by 4 world-space corners.
     *
     * The single entry point for every piece of scene geometry: ground tiles,
     * upright billboards, characters, particles and light pools all arrive here.
     * Corners are in scene space (see @c sceneMath) and are transformed by the
     * matrix last given to @ref SetViewProjection, so unlike every other draw
     * method on this interface the caller does not pre-subtract the camera.
     *
     * @par Corner order
     * @code
     * corners[0] (TL)----corners[1] (TR)
     *      |                   |
     * corners[3] (BL)----corners[2] (BR)
     * @endcode
     *
     * @par World geometry vs UI
     * Which method you call decides the space, so it is checked at compile time:
     * scene geometry goes through @ref DrawQuad3D, screen-space UI through the
     * 2D primitives above.
     *
     * @param texture   Texture containing the sprite.
     * @param corners   4 scene-space positions [TL, TR, BR, BL].
     * @param texCoord  Top-left of the texture region, in pixels.
     * @param texSize   Size of the texture region, in pixels.
     * @param color     RGBA tint multiplied into the sampled texel.
     * @param blend     Color combine mode for this quad's pass.
     * @param depth     Depth test/write mode for this quad's pass.
     * @param flipY     Flip vertical UVs (default true, matching the 2D path).
     * @param tileFlipX Mirror the source UV region horizontally.
     * @param tileFlipY Mirror the source UV region vertically.
     */
    virtual void DrawQuad3D(const Texture& texture,
                            const glm::vec3 corners[4],
                            glm::vec2 texCoord,
                            glm::vec2 texSize,
                            glm::vec4 color = glm::vec4(1.0f),
                            renderModes::BlendMode blend = renderModes::BlendMode::Alpha,
                            renderModes::DepthMode depth = renderModes::DepthMode::TestAndWrite,
                            bool flipY = true,
                            bool tileFlipX = false,
                            bool tileFlipY = false) = 0;

    /**
     * @brief Set the combined view-projection matrix used by @ref DrawQuad3D.
     *
     * Supplied pre-multiplied rather than as separate view and projection
     * matrices because the CPU already builds both to extract frustum planes for
     * culling, so combining there is free and keeps the GPU-side uniform (and the
     * Vulkan push-constant block) to a single mat4.
     *
     * Changing it flushes any pending 3D batch, so call it once per pass.
     *
     * @param viewProjection `projection * view`, from @c cameraRig.
     */
    virtual void SetViewProjection(const glm::mat4& viewProjection) = 0;

    /**
     * @brief Set the projection matrix every 2D primitive draws through.
     *
     * Called several times per frame, not once: the frame switches between the
     * scene ortho and the screen-pixel UI ortho (console, menus, editor panels,
     * tile picker) and back again.
     *
     * Changing it first drains every pending 2D and 3D batch, so it is both a real
     * cost and a hard painter-order barrier: geometry queued before the call is
     * guaranteed to land before anything queued after it. Call it only when the
     * space actually changes.
     *
     * @param projection 4x4 projection matrix.
     */
    virtual void SetProjection(const glm::mat4& projection) = 0;

    /**
     * @brief Set the rendering viewport.
     *
     * Defines the rectangular region of the window to render into.
     * Typically matches the window size.
     *
     * @param x Viewport left edge.
     * @param y Viewport bottom edge.
     * @param width Viewport width in pixels.
     * @param height Viewport height in pixels.
     */
    virtual void SetViewport(int x, int y, int width, int height) = 0;

    /**
     * @brief Record the zoomed world-view extent, in world pixels.
     *
     * @warning This is not @ref SetViewport, which takes screen pixels. This is the
     * world-space extent the current orthographic projection covers, i.e. exactly the
     * width/height @ref CameraController::GetOrthoProjection was built from. Callers
     * set both from the same pair of numbers.
     *
     * Purely a value the renderer carries for consumers that need the view extent at
     * draw time; nothing in the renderer reacts to it.
     *
     * @param size Zoomed world-view width and height in world pixels.
     */
    void SetViewSize(glm::vec2 size) { m_ViewSize = size; }

    /// @brief Zoomed world-view extent in world pixels, as last given to SetViewSize().
    [[nodiscard]] glm::vec2 GetViewSize() const { return m_ViewSize; }

    /**
     * @brief Clear the screen to a solid color.
     *
     * Fills the entire viewport with the specified color.
     * Should be called at the start of each frame.
     *
     * OpenGL uses the provided RGBA values directly. Vulkan currently performs
     * clearing during BeginFrame() with a fixed clear value and ignores these parameters.
     *
     * @param r Red component (0-1).
     * @param g Green component (0-1).
     * @param b Blue component (0-1).
     * @param a Alpha component (0-1).
     */
    virtual void Clear(float r, float g, float b, float a) = 0;

    /**
     * @brief Ensure a texture is uploaded to GPU memory.
     *
     * If the texture hasn't been uploaded yet, this creates the GPU resource.
     * Safe to call multiple times.
     *
     * @par Backend notes
     * CPU-side texture data is loaded separately by Texture. OpenGL can lazily
     * create texture objects during draw setup, while Vulkan callers should
     * upload required textures before the frame that samples them. A Vulkan
     * draw that sees no image view may use a white fallback instead of
     * uploading mid-render-pass.
     *
     * @param texture Texture to upload.
     */
    virtual void UploadTexture(const Texture& texture) = 0;

    /**
     * @brief Draw text at the specified position.
     *
     * Renders a text string using the backend's loaded glyph resources.
     *
     * @par Font rendering
     * Backends may implement text differently (atlas-based or per-glyph
     * textures), but both render each character as textured quads.
     *
     * @param text Text string to render (ASCII fully supported; extended
     *             glyph coverage depends on loaded fonts/backend).
     * @param position Top-left corner of text.
     * @param scale Text scale multiplier (default: 1.0).
     * @param color Text color (default: white).
     * @param outlineSize Outline/shadow thickness multiplier (default: 1.0).
     * @param alpha Text transparency 0.0-1.0 (default: 0.85).
     */
    virtual void DrawText(const std::string& text,
                          glm::vec2 position,
                          float scale = 1.0f,
                          glm::vec3 color = glm::vec3(1.0f),
                          float outlineSize = 1.0f,
                          float alpha = 0.85f) = 0;

    /**
     * @brief Get text line height metrics for alignment calculations.
     *
     * Returns the ascent (distance from baseline to top of tallest glyph)
     * scaled by the given scale factor. Use this to align UI elements
     * with rendered text.
     *
     * @param scale Text scale multiplier.
     * @return Scaled ascent in pixels.
     */
    virtual float GetTextAscent(float scale = 1.0f) const = 0;

    /**
     * @brief Measure the width of a text string.
     *
     * Returns the width in pixels that the text would occupy when rendered
     * at the given scale. Uses actual glyph advance values for accuracy.
     *
     * @param text Text string to measure.
     * @param scale Text scale multiplier.
     * @return Width in pixels.
     */
    virtual float GetTextWidth(const std::string& text, float scale = 1.0f) const = 0;

    /**
     * @brief Draw text using a large (headline) glyph atlas.
     *
     * A backend's body and headline atlases differ in their logical size - the
     * size a `scale` of 1.0 means - not necessarily in bake resolution. The
     * OpenGL backend bakes both at 96 px and normalizes the body atlas's glyph
     * metrics down to a logical 24 px, so body text is supersampled 4x rather
     * than blurry; what a headline atlas buys is a logical size of 96 px, i.e.
     * large text without multiplying `scale`.
     *
     * Default fallback: scales the body atlas by an internal multiplier so
     * existing call sites still work. Backends whose body atlas is baked at its
     * logical size get visibly soft headlines from that path.
     *
     * Parameters mirror @ref DrawText. @p scale is interpreted relative to
     * the headline atlas's logical size (96 px in the OpenGL backend).
     */
    virtual void DrawTextLarge(const std::string& text,
                               glm::vec2 position,
                               float scale = 1.0f,
                               glm::vec3 color = glm::vec3(1.0f),
                               float outlineSize = 1.0f,
                               float alpha = 0.85f);

    /**
     * @brief Measure text width using the headline atlas.
     *
     * Default fallback uses the body atlas scaled up by the headline ratio,
     * so layout calculations match @ref DrawTextLarge for both real and
     * fallback implementations.
     */
    [[nodiscard]] virtual float GetTextWidthLarge(const std::string& text,
                                                  float scale = 1.0f) const;

    /**
     * @brief Check if this renderer requires Y-axis flipping for textures.
     *
     * Both OpenGL and Vulkan backends return true to maintain consistent
     * UV conventions. Tilesets are pre-flipped during loading and require
     * flipY=true when sampling.
     *
     * @return true for both OpenGL and Vulkan (both use Y-flip)
     */
    virtual bool RequiresYFlip() const = 0;

    /**
     * @brief Set the global ambient light color for day & night cycle.
     *
     * This color is multiplied with all textured sprites to create
     * the effect of changing light throughout the day.
     *
     * @param color RGB ambient color (1,1,1 = full brightness, no tint).
     */
    virtual void SetAmbientColor(const glm::vec3& color) = 0;

    /**
     * @brief Get the number of draw calls made this frame.
     *
     * Returns the count of GPU draw calls (batch flushes) since the last
     * BeginFrame(). Useful for performance debugging and optimization.
     *
     * @return Number of draw calls this frame.
     */
    virtual int GetDrawCallCount() const = 0;

    /**
     * @brief Backend identity snapshot for the developer-console
     *        `renderer.info` command.
     *
     * Populated at the end of `Init()`; reading before Init() returns a
     * default-constructed `RendererInfo` (empty strings, 0 max texture size).
     */
    [[nodiscard]] virtual RendererInfo GetBackendInfo() const = 0;

protected:
    /// Zoomed world-view extent in world pixels; see SetViewSize().
    glm::vec2 m_ViewSize{0.0f};

    /**
     * @brief Rotate four quad corners around the sprite center.
     * @param corners Array of 4 corner positions [TL, TR, BR, BL] (modified in-place).
     * @param size Sprite dimensions for center calculation.
     * @param rotation Rotation angle in degrees.
     */
    static void RotateCorners(glm::vec2 corners[4], glm::vec2 size, float rotation);
};
