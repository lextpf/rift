#pragma once

#include "IRenderer.hpp"
#include "PostFXParams.hpp"
#include "RendererMacros.hpp"

#include <glad/glad.h>
#include <map>
#include <string>
#include <vector>

#ifdef USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

/**
 * @class OpenGLRenderer
 * @brief OpenGL 4.6 implementation of the IRenderer interface.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Provides hardware-accelerated 2D rendering using modern OpenGL with
 * batching optimizations for high-performance sprite and text rendering.
 *
 * @par OpenGL features used
 * | Feature              | Version | Usage                          |
 * |----------------------|---------|--------------------------------|
 * | Core Profile         | 4.6     | Modern pipeline                |
 * | VAO/VBO              | 3.0+    | Geometry storage               |
 * | Shaders              | 4.5     | GLSL 450 core                  |
 * | Texture Atlas        | 2.0+    | Font glyph packing             |
 * | Alpha Blending       | 1.0+    | Transparency and particles     |
 *
 * @par Sprite batching system
 * To minimize draw calls, sprites are accumulated in a vertex buffer
 * and flushed when the texture changes or the buffer fills. Each draw
 * call has GPU overhead (state changes, driver validation), so batching
 * many sprites into a single draw call dramatically improves performance.
 *
 * @par Why batching matters
 * - Many sprites without batching: one draw call per sprite, high driver
 *   overhead.
 * - Many sprites with batching: one draw call per texture run, lower CPU
 *   overhead.
 *
 * @par Batch flow
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 * classDef add fill:#134e3a,stroke:#10b981,color:#e2e8f0
 * classDef flush fill:#7f1d1d,stroke:#ef4444,color:#e2e8f0
 * classDef check fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *
 * A["DrawSprite(texA)"]:::add --> B{Same texture?}:::check
 * B -->|Yes| C["Add vertices to batch"]:::add
 * B -->|No| D["FlushBatch()"]:::flush
 * D --> E["Record texture (bound at next flush)"]:::add
 * E --> C
 * C --> J{Batch full?}:::check
 * J -->|Yes| D
 * J -->|No| F["DrawSprite(texA)"]:::add
 * F --> B
 * G["EndFrame()"]:::flush --> H["FlushBatch()"]:::flush
 * H --> I["Caller swaps buffers"]
 * </pre>
 * @endhtmlonly
 *
 * @par Batch accumulation
 * Each sprite adds 6 vertices (2 triangles) to the batch buffer:
 * @code
 * // Sprite quad as two triangles (6 vertices)
 * // TL--TR      TL, BR, BL (triangle 1)
 * // | \  |  =>  TL, TR, BR (triangle 2)
 * // BL--BR
 *
 * DrawSprite(texA, pos1)  // vertices 0-5   -> batch size: 6
 * DrawSprite(texA, pos2)  // vertices 6-11  -> batch size: 12
 * DrawSprite(texA, pos3)  // vertices 12-17 -> batch size: 18
 * DrawSprite(texB, pos4)  // FLUSH! draw 18 vertices, then start new batch
 * @endcode
 *
 * The text batch emits the opposite corner order (TL, BL, BR / TL, BR, TR).
 * Nothing culls faces on the 2D paths, so the winding difference is harmless.
 *
 * @par Flush triggers
 * Flush triggers include texture changes, a full batch buffer, frame end, and
 * switches between sprite, rect, particle, or text rendering paths.
 *
 * @par Batch types
 * "Flush trigger" below is what ends a batch early. Cross-type drains are
 * asymmetric, so the matrix that follows is the authority on painter order.
 * | Batch Type  | Buffer                  | Extra flush trigger        |
 * |-------------|-------------------------|----------------------------|
 * | Sprites     | m_BatchVertices         | Texture change, batch full |
 * | Rects       | m_RectBatchVertices     | Blend mode change          |
 * | Particles   | m_ParticleBatchVertices | Texture/blend              |
 * | Text        | m_TextBatchVertices     | Atlas change, quad budget  |
 *
 * Text does not flush per DrawText call: consecutive DrawText calls that use the
 * same atlas accumulate into one draw (see @ref MAX_TEXT_QUADS).
 *
 * @par Drain matrix
 * Which trigger drains which batch. The world-space 3D batch is included because
 * it shares the frame's painter order even though it has its own program.
 * | Trigger                       | Sprite | Rect | Particle | Text | 3D |
 * |-------------------------------|--------|------|----------|------|----|
 * | DrawSprite / DrawSpriteRegion |        | x    |          |      |    |
 * | DrawSpriteAtlas / ...Alpha    | x      | x    |          |      |    |
 * | DrawColoredRect               | x      |      |          |      |    |
 * | DrawQuad3D                    | x      | x    | x        |      |    |
 * | DrawText / DrawTextLarge      | x      | x    | x        |      |    |
 * | SetAmbientColor               | x      |      |          |      |    |
 * | SetViewProjection             |        |      |          |      | x  |
 * | SetProjection                 | x      | x    | x        | x    | x  |
 * | BeginScene / EndSceneApplyPFX | x      | x    | x        |      | x  |
 * | EndFrame                      | x      | x    | x        | x    | x  |
 * | FlushBatch/Rect/Particle      |        |      |          | x*   |    |
 *
 * `x*`: those three flushes drain text only when their own batch is non-empty -
 * the drain sits after the empty check, so an empty flush is a no-op. An empty
 * cell means that pair does not preserve call-site order. Text in particular is
 * never drained by BeginScene or EndSceneApplyPostFX, so text queued inside the
 * scene pass reaches the swapchain after the composite, ungraded.
 *
 * @par Shader architecture
 * All four 2D batch types - sprites, rects, particles and text - share
 * m_ShaderProgram and differ only in the `useColorOnly` uniform:
 * - Vertex: applies projection * model; model is identity for every batched path,
 *   because the corners are already transformed on the CPU
 * - Fragment: sample texture, apply color tint and ambient light
 *
 * Modes 0 (sprites) and 3 (particles and text) discard any fragment whose sampled
 * texture alpha is below 0.1 (`ALPHA_CUTOFF` in Geometry.frag), so faint texels
 * never reach the blend stage; vertex alpha is not tested. Modes 1 and 2 do not
 * cut out.
 *
 * The only separate programs are Geometry3D (the world-space path) and the four
 * post-FX programs: bloom prefilter, downsample, upsample, and composite.
 *
 * @par Uniform locations (cached at init)
 * Names are the GLSL identifiers looked up by glGetUniformLocation - the member
 * that caches each one is named after its role, not after the uniform.
 * | GLSL name    | Type  | Cached in         | Purpose                 |
 * |--------------|-------|-------------------|-------------------------|
 * | model        | mat4  | m_ModelLoc        | Per-sprite transform    |
 * | projection   | mat4  | m_ProjectionLoc   | Orthographic projection |
 * | spriteColor  | vec3  | m_ColorLoc        | Color tint              |
 * | spriteAlpha  | float | m_AlphaLoc        | Transparency            |
 * | ambientColor | vec3  | m_AmbientColorLoc | Day/night lighting      |
 * | useColorOnly | int   | m_UseColorOnlyLoc | Color-source selector   |
 *
 * @par Font rendering
 * Text is rendered using FreeType for glyph rasterization and a
 * single texture atlas for efficient batched rendering.
 *
 * @par Atlas layout
 * ASCII code points 0-127 are packed into a single RGBA texture at
 * initialization (RGB white, alpha = glyph coverage), with 8 texels of padding
 * so mip minification cannot bleed neighbors together. UV coordinates for each
 * character are cached in the m_Characters map. Two atlases are built from one
 * face: body and headline.
 *
 * @see IRenderer Base interface with method documentation
 * @see VulkanRenderer Alternative Vulkan implementation
 */
struct GLFWwindow;

/**
 * @name Debug draw visualization
 * @brief Watch a frame assemble one batch at a time.
 * @ingroup Rendering
 *
 * While enabled, every batch flush - sprite, rect, particle, text and the
 * world-space 3D batch - logs its index and vertex count, runs glFinish, presents
 * the half-drawn frame, and then sleeps two seconds.
 * Free functions over file-static state, so they are callable from the console
 * without a renderer reference - but that also means the setting is process-wide
 * and survives a renderer hot-swap.
 *
 * @warning Development only. Two seconds per batch means single-digit seconds per
 * frame; input and the game clock keep running.
 * @{
 */

/**
 * @brief Enable or disable per-draw-call sleep for debugging render order.
 * @param window  Window whose buffers are swapped between draws. Stored, not
 *                owned; must outlive the enabled period.
 * @param enabled True to pause and present after each batch flush.
 */
void SetDebugDrawSleep(GLFWwindow* window, bool enabled);

/// @brief Reset the draw index that labels each step. Call once per frame.
void ResetDebugDrawCallIndex();

/// @brief Check whether debug draw sleep mode is active.
bool IsDebugDrawSleepEnabled();

/// @}

class OpenGLRenderer : public IRenderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    RIFT_DECLARE_COMMON_RENDERER_METHODS;

    /**
     * @brief Draw large title text from the headline atlas.
     *
     * Uses the headline atlas (logical size 96, vs body text's logical 24) for
     * large title-screen text such as the "RIFT" logo.
     */
    void DrawTextLarge(const std::string& text,
                       glm::vec2 position,
                       float scale,
                       glm::vec3 color,
                       float outlineSize,
                       float alpha) override;

    [[nodiscard]] float GetTextWidthLarge(const std::string& text, float scale) const override;

    void SetFontCandidates(const std::vector<std::string>& fontCandidates) override;

    /// @brief OpenGL uses bottom-left texture origin, requires Y-flip.
    bool RequiresYFlip() const override { return true; }

    void SetAmbientColor(const glm::vec3& color) override;

    int GetDrawCallCount() const override { return m_DrawCallCount; }

private:
    RendererInfo m_Info;  ///< Cached at end of Init(); returned by GetBackendInfo().

    /**
     * @name Initialization helpers
     * @{
     */

    /**
     * @brief Ensure texture has a valid OpenGL ID for the current context, recreating if needed.
     * @return Valid texture ID, or 0 if the texture could not be made ready.
     */
    unsigned int EnsureTextureReady(const Texture& texture);

    /**
     * @brief Create every static and dynamic vertex buffer used by the 2D paths.
     *
     * Builds, in order: the unit quad (m_VAO/m_VBO/m_EBO - uploaded but never bound
     * for a draw), the text VAO/VBO sized MAX_TEXT_QUADS * 6, the sprite batch
     * VAO/VBO, and the rect batch VAO/VBO that the particle path also draws
     * through. Also reserves the text vertex vector.
     *
     * All four agree on one attribute layout: 0 = position, 1 = UV, 2 = RGBA,
     * with attribute 2 disabled where the vertex format carries no color.
     */
    void SetupQuad();

    /// @brief Create a 1x1 white texture for colored rectangle rendering.
    void CreateWhiteTexture();

    /**
     * @brief Load a TTF font and build both glyph atlases (body + headline).
     * @param fontPath Path to the .ttf font file.
     */
    void LoadFont(const std::string& fontPath);

    /// @}

    /**
     * @name Font atlas
     * @{
     */

    /**
     * @brief Cached glyph metrics and atlas UV coordinates.
     *
     * Populated during LoadFont() for every code point in 0-127 that FreeType can
     * render; control characters land in the map as zero-size cells. A character
     * absent from the map is silently skipped by DrawText.
     */
    struct Character
    {
        glm::ivec2 Size;     ///< Glyph dimensions in PHYSICAL atlas texels.
        glm::ivec2 Bearing;  ///< Offset from baseline to top-left, in atlas texels.
        /// Horizontal advance in 1/64 px (FreeType's 26.6 fixed point) - callers
        /// must shift right by 6 before scaling.
        unsigned int Advance;
        /// Normalized UV corners of the glyph within the atlas texture.
        float u0, v0, u1, v1;
    };

    /**
     * @brief Body-text glyph atlas, rasterized at @c BODY_FONT_PIXEL_SIZE.
     *
     * Metrics are in physical texels; multiply by @c BODY_METRIC_NORM to get the
     * logical size a caller's `scale` is relative to.
     */
    std::map<char, Character> m_Characters;
    /**
     * @brief RGBA atlas texture; 0 when no font is loaded.
     *
     * RGB is flat white and alpha carries the glyph coverage, so a tint can be
     * applied purely by multiplying. Mipmapped (LINEAR_MIPMAP_LINEAR) because small
     * UI text minifies from the supersampled bake.
     */
    unsigned int m_FontAtlasTexture;
    /// Atlas dimensions in texels; both rounded up to a power of two.
    int m_FontAtlasWidth, m_FontAtlasHeight;

    /**
     * @brief Headline glyph atlas at @c HEADLINE_FONT_PIXEL_SIZE.
     * Used by @ref DrawTextLarge for large title-screen text (the "RIFT" logo).
     */
    std::map<char, Character> m_HeadlineCharacters;
    unsigned int m_HeadlineFontAtlasTexture = 0;
    int m_HeadlineFontAtlasWidth = 0;
    int m_HeadlineFontAtlasHeight = 0;

    /**
     * @name Font sizes: physical bake vs logical
     * @brief FreeType *physical* bake sizes (atlas texel resolution) plus the
     * *logical* sizes that DrawText/DrawTextLarge `scale` is relative to.
     *
     * Per-glyph metrics are normalized by (logical / physical) so on-screen text
     * size is independent of the atlas texel resolution, which allows supersampling
     * the body atlas for crispness without changing any caller's layout.
     * @{
     */
    /// Physical bake size of the body atlas, in px. 4x its logical size, i.e. body
    /// text is supersampled - it does not blur when scaled up a few times.
    static constexpr int BODY_FONT_PIXEL_SIZE = 96;
    /// Logical body size in px: what `DrawText(scale = 1.0)` means on screen.
    static constexpr int BODY_FONT_LOGICAL_PIXEL_SIZE = 24;
    /// Physical bake size of the headline atlas, in px.
    static constexpr int HEADLINE_FONT_PIXEL_SIZE = 96;
    /**
     * @brief Logical headline size in px: what `DrawTextLarge(scale = 1.0)` means.
     *
     * Equal to the physical size, so the headline atlas is 1:1, not supersampled -
     * what it buys is the larger default size, not extra resolution.
     */
    static constexpr int HEADLINE_FONT_LOGICAL_PIXEL_SIZE = 96;
    /**
     * @brief Logical/physical ratio applied to body glyph metrics (0.25 here) so
     *        screen size is independent of bake resolution.
     *
     * Outline offsets deliberately skip this: they are screen-space, not
     * atlas-space.
     */
    static constexpr float BODY_METRIC_NORM =
        static_cast<float>(BODY_FONT_LOGICAL_PIXEL_SIZE) / static_cast<float>(BODY_FONT_PIXEL_SIZE);
    /// Same ratio for the headline atlas; exactly 1.0, so headline metrics pass
    /// through unchanged.
    static constexpr float HEADLINE_METRIC_NORM =
        static_cast<float>(HEADLINE_FONT_LOGICAL_PIXEL_SIZE) /
        static_cast<float>(HEADLINE_FONT_PIXEL_SIZE);
    /// @}

    /**
     * @brief Internal: build one atlas at @p pixelSize into the provided slots.
     *
     * Used by LoadFont to emit both body and headline atlases from one face.
     * Destroys any texture already in @p outTexture first, so it is safe to call
     * again on a font reload. Requires @c m_Face; logs and returns otherwise.
     *
     * @param pixelSize  Physical bake size in px. Also selects the row-wrap limit
     *                   (2048 texels at >= 64 px, otherwise 512).
     * @param outChars   Receives the glyph map (cleared first).
     * @param outTexture Receives the atlas texture name.
     * @param outWidth,outHeight Receive the atlas dimensions, rounded up to a
     *                   power of two.
     */
    void BuildAtlasInto(int pixelSize,
                        std::map<char, Character>& outChars,
                        unsigned int& outTexture,
                        int& outWidth,
                        int& outHeight);

    /**
     * @brief Internal: shared body of DrawText, parameterized over the atlas.
     * @c chars and @c atlasTexture select between body and headline atlases.
     */
    void DrawTextImpl(const std::string& text,
                      glm::vec2 position,
                      float scale,
                      glm::vec3 color,
                      float outlineSize,
                      float alpha,
                      float metricNorm,
                      const std::map<char, Character>& chars,
                      unsigned int atlasTexture);

    /// @brief Internal: shared body of GetTextWidth.
    [[nodiscard]] float GetTextWidthImpl(const std::string& text,
                                         float scale,
                                         float metricNorm,
                                         const std::map<char, Character>& chars) const;

    std::vector<std::string> m_FontCandidates;  ///< Project-specific font candidates.

#ifdef USE_FREETYPE
    FT_Library m_FreeType;  ///< FreeType library handle.
    FT_Face m_Face;         ///< Loaded font face.
#endif

    /// @}

    /**
     * @name Core OpenGL objects
     * @{
     */

    unsigned int m_VAO, m_VBO, m_EBO;  ///< Unit quad geometry.
    /**
     * @brief Unified sprite/text/rect program.
     *
     * All four batch types share it and select behavior through the
     * `useColorOnly` uniform, which is exactly why a draw of one type must flush
     * the others first.
     */
    unsigned int m_ShaderProgram;
    glm::mat4 m_Projection;       ///< Current orthographic projection.
    unsigned int m_WhiteTexture;  ///< Owned 1x1 opaque-white texture backing rect draws.

    /// @}

    /**
     * @name Shader uniform locations
     * @{
     */

    GLint m_ModelLoc;         ///< GLSL `model`; per-sprite model matrix.
    GLint m_ProjectionLoc;    ///< GLSL `projection`; orthographic projection matrix.
    GLint m_ColorLoc;         ///< GLSL `spriteColor`; RGB color tint.
    GLint m_AlphaLoc;         ///< GLSL `spriteAlpha`; transparency multiplier.
    GLint m_AmbientColorLoc;  ///< GLSL `ambientColor`; day/night ambient light color.
    GLint m_UseColorOnlyLoc;  ///< Color mode selector (0=texture, 1=uniform, 2=vertex, 3=tex*vert).
    /// Current ambient tint, multiplied into lit sprites. (1,1,1) = no tint.
    /// Self-lit geometry (particles, sky) is drawn through paths that bypass it.
    glm::vec3 m_AmbientColor;

    /// @}

    /**
     * @name World-space 3D batching
     * @brief The `Geometry3D` program, its buffers, and the pending 3D batch.
     *
     * Deliberately a separate program, VAO and vertex format from the 2D sprite
     * path above rather than an extension of it. The 2D path stays byte-identical
     * so screen-space UI is unaffected and the flat rendering baseline is
     * preserved exactly; world geometry moves to this path instead. The two
     * coexist within a frame - world in passes A (opaque cutout) and B
     * (translucent), UI in pass C (screen space); see @ref renderModes for the
     * three-pass contract.
     * @{
     */

    /**
     * @brief Vertex format for world-space quads.
     *
     * Position is a scene-space 3D point, transformed only by viewProjection.
     * Color is per-vertex so one batch can mix differently tinted tiles and
     * sprites without a uniform change between them.
     */
    struct BatchVertex3D
    {
        float x, y, z;     ///< Scene-space position (see sceneMath).
        float u, v;        ///< Texture coordinates.
        float r, g, b, a;  ///< Per-vertex RGBA tint.
    };

    unsigned int m_Geometry3DProgram = 0;  ///< Linked Geometry3D vert+frag program.
    GLint m_VP3DLoc = -1;                  ///< GLSL `viewProjection`.
    GLint m_Ambient3DLoc = -1;             ///< GLSL `ambientColor`.
    GLint m_AlphaCutoff3DLoc = -1;         ///< GLSL `alphaCutoff`; varies per pass.

    glm::mat4 m_ViewProjection{1.0f};  ///< Last matrix from SetViewProjection.

    std::vector<BatchVertex3D> m_Batch3DVertices;  ///< Accumulated world geometry.
    unsigned int m_Batch3DVAO = 0;
    unsigned int m_Batch3DVBO = 0;
    unsigned int m_CurrentBatch3DTexture = 0;  ///< Active texture for the 3D batch.
    /**
     * @brief Blend/depth state the pending batch was queued under.
     *
     * A draw requesting different state must flush first, exactly like a texture
     * change does - these are pipeline state, not per-vertex data.
     */
    renderModes::BlendMode m_Batch3DBlend = renderModes::BlendMode::Alpha;
    renderModes::DepthMode m_Batch3DDepth = renderModes::DepthMode::TestAndWrite;

    /// @brief Create the VAO/VBO for world-space batching.
    void SetupBatch3DBuffers();

    /// @brief Apply the GL blend and depth state a 3D pass requires.
    void ApplyPass3DState(renderModes::BlendMode blend, renderModes::DepthMode depth);

    /// @brief Submit accumulated world geometry to the GPU and reset the batch.
    void FlushBatch3D();

    /// @}

    /**
     * @name Text batching
     * @{
     */

    /**
     * @brief Capacity of the text VBO, in quads.
     *
     * This is a budget for the whole text batch, which spans every DrawText call
     * sharing the current atlas - not a per-call limit. One glyph costs 5 quads,
     * not 1: DrawTextImpl emits four offset outline passes plus the foreground, so
     * ~409 glyphs fill the buffer. Overflowing is not an error - addCharQuad
     * flushes and re-arms mid-string - but each overflow is an extra draw call.
     */
    static constexpr size_t MAX_TEXT_QUADS = 2048;

    /**
     * @brief Vertex format for text quads.
     *
     * Per-vertex color lets a single DrawText call render outline and
     * foreground in one draw: outline quads carry RGBA(0,0,0,outlineAlpha),
     * foreground quads carry RGBA(color.rgb, alpha). The shader runs in
     * "textured x vertex color" mode (useColorOnly == 3) so the glyph
     * alpha multiplies the per-vertex color directly.
     */
    struct TextVertex
    {
        float x, y;        ///< Screen position.
        float u, v;        ///< Font atlas UV.
        float r, g, b, a;  ///< Per-vertex RGBA tint (multiplied by glyph alpha).
    };

    std::vector<TextVertex> m_TextBatchVertices;  ///< Accumulated text geometry.
    unsigned int m_TextVAO, m_TextVBO;            ///< Text-specific buffers.
    /**
     * @brief Active font atlas for the pending text batch.
     *
     * Zero when no text is pending. Multiple DrawText calls with the same atlas
     * batch into a single draw; changing atlas (e.g., body -> headline) forces a
     * flush first.
     */
    unsigned int m_CurrentTextAtlas = 0;

    /// @brief Submit accumulated text geometry to the GPU and reset the batch.
    void FlushTextBatch();

    /// @}

    /**
     * @name Sprite batching
     * @{
     */

    /**
     * @brief Quad cap shared by every non-text batch: sprite, rect, particle, 3D.
     *
     * It also sizes all four VBOs, at three different vertex strides, so raising it
     * to cut sprite flushes costs GPU memory in each of them. Text has its own
     * budget, @ref MAX_TEXT_QUADS.
     */
    static constexpr size_t MAX_BATCH_SPRITES = 10000;
    static constexpr size_t VERTICES_PER_SPRITE = 6;  ///< Two triangles.

    /// @brief Vertex format for batched sprites (screen-space pre-camera).
    struct BatchVertex
    {
        float x, y;  ///< Screen-space position.
        float u, v;  ///< Texture coordinates.
    };

    std::vector<BatchVertex> m_BatchVertices;  ///< Accumulated sprite geometry.
    unsigned int m_BatchVAO, m_BatchVBO;       ///< Sprite batch buffers.
    unsigned int m_CurrentBatchTexture;        ///< Active texture for batching.

    /// @brief Submit accumulated sprites to GPU and reset batch.
    void FlushBatch();

    /// @}

    /**
     * @name Colored rectangle batching
     * @{
     */

    /// @brief Vertex format with per-vertex RGBA color.
    struct ColoredVertex
    {
        float x, y;        ///< Screen-space position.
        float u, v;        ///< Texture coords (unused for rects).
        float r, g, b, a;  ///< Per-vertex color.
    };

    std::vector<ColoredVertex> m_RectBatchVertices;  ///< Accumulated rect geometry.
    unsigned int m_RectBatchVAO, m_RectBatchVBO;     ///< Rect batch buffers.
    bool m_RectBatchAdditive;                        ///< Current blend mode.

    /// @}

    /**
     * @name Colored rectangle helpers
     * @{
     */

    /// @brief Submit accumulated rects to GPU and reset batch.
    void FlushRectBatch();

    /// @brief Create VAO/VBO for colored rectangle batching.
    void SetupRectBatchBuffers();

    /// @}

    /**
     * @name Particle batching
     * @brief Vertices and per-batch state only; the particle path owns no GPU
     *        buffers of its own.
     *
     * It uploads through m_RectBatchVBO and draws through m_RectBatchVAO, because
     * both use the ColoredVertex layout - so the rect vertex format and buffer size
     * bound particles too, and MAX_BATCH_SPRITES caps both. Each flush orphans and
     * rewrites that shared buffer, so a rect flush and a particle flush must stay
     * strictly sequential. The texture and blend flag below are the only per-type
     * state.
     * @{
     */

    std::vector<ColoredVertex> m_ParticleBatchVertices;  ///< Particle geometry.
    unsigned int m_CurrentParticleTexture;               ///< Active particle texture.
    bool m_ParticleBatchAdditive;                        ///< Particle blend mode.

    /// @brief Submit accumulated particles to GPU and reset batch.
    void FlushParticleBatch();

    /// @}

    /**
     * @name Frame counters and draw-trace plumbing
     * @{
     */

    int m_DrawCallCount = 0;     ///< Number of draw calls this frame (for debug display).
    bool m_Initialized = false;  ///< True after Init() completes successfully.

    /**
     * @brief Reason string for the next batch flush, or nullptr.
     *
     * Set by call sites just before invoking Flush*Batch (see PrepFlushReason
     * helper). Read and cleared by the flush methods when DrawTracer is
     * enabled, so the trace records why each GPU submission happened
     * (texture change, batch full, projection swap, end of pass, etc.)
     * without changing the FlushBatch signature.
     */
    const char* m_PendingFlushReason = nullptr;

    /**
     * @brief Stamp the next flush(es) with a reason. No-op when DrawTracer
     * is disabled, so call sites can sprinkle these freely without
     * per-frame cost when tracing is off.
     */
    inline void PrepFlushReason(const char* reason) { m_PendingFlushReason = reason; }

    /// @}

    /**
     * @name Shader loading
     * @{
     */

    /**
     * @brief Load shader source code from a file.
     * @param filepath Path to .vert or .frag shader file.
     * @return Shader source as string, or empty string on error.
     */
    std::string LoadShaderFromFile(const std::string& filepath);

    /**
     * @brief Compile and link a vertex+fragment program from inline source strings.
     * @return Linked GL program ID, or 0 on compile/link failure.
     */
    unsigned int CompileShaderProgram(const std::string& vertSrc,
                                      const std::string& fragSrc,
                                      const char* debugLabel);

    /// @}

    /**
     * @name Viewport tracking (for post-FX FBO sizing)
     * @{
     */
    int m_ViewportWidth = 0;
    int m_ViewportHeight = 0;
    /// @}

    /**
     * @name Post-FX pipeline
     * @brief Offscreen scene target, bloom mip chain, and the composite pass.
     *
     * **OpenGL only.** VulkanRenderer renders straight to the swapchain and its
     * EndSceneApplyPostFX is a no-op, so nothing below has a Vulkan counterpart
     * and no PostFXParams field has any effect on that backend.
     *
     * BeginScene binds the scene FBO; every world draw lands there.
     * EndSceneApplyPostFX drains the batches, runs the bloom prep, then composites
     * to the swapchain. UI drawn afterwards bypasses the chain entirely.
     *
     * @verbatim
     *  world draws
     *       |
     *       v
     *  [scene FBO  RGB16F]-----------------------------------+   (HDR, values > 1)
     *       |                                                |
     *       v  BloomPrefilter.frag                           |
     *  HSV-saturation bright pass  -->  mip0 (1/2 res)       |
     *       |                                                |
     *       v  BloomDownsample.frag                          |
     *  mip0 -> mip1 -> mip2 -> mip3 -> mip4   (down to 1/32) |
     *       |                                                |
     *       v  BloomUpsample.frag, additive (GL_ONE,GL_ONE)  |
     *  mip4 += -> mip3 += -> mip2 += -> mip1 += -> mip0      |
     *       |                                                |
     *       v                                                v
     *  [bloom = mip0] ------> PostFXComposite.frag <---------+
     *                                 |
     *        1. scene sample w/ chromatic aberration
     *        2. + chroma-only bloom (zero net luminance)
     *        3. lift / gamma / gain grading
     *        4. saturation pump
     *        5. vignette + edge desaturation
     *        6. film grain
     *        7. soft-shoulder tonemap
     *                                 |
     *                                 v
     *                          [swapchain] --> sharp UI drawn after
     * @endverbatim
     *
     * The bloom feeder gates on HSV saturation, not luma, so only colored pixels
     * enter the chain. Step 2 projects the bloom onto the chroma plane, so it
     * shifts hue without brightening. `postFXEnabled == false` early-returns the
     * raw scene texel inside the composite shader - the mip chain still runs.
     * @{
     */

    /**
     * @brief Offscreen scene target.
     *
     * RGB16F color so highlights keep values above 1.0 for the bright pass and
     * tonemap; depth is a renderbuffer (never sampled). Reallocated whenever the
     * viewport size changes.
     */
    unsigned int m_SceneFBO = 0;
    unsigned int m_SceneColorTex = 0;  ///< RGB16F color attachment; sampled by the composite.
    unsigned int m_SceneDepthRBO = 0;  ///< DEPTH_COMPONENT24 renderbuffer; write-only.
    int m_SceneFBOWidth = 0;   ///< Allocated width; compared against the viewport to detect resize.
    int m_SceneFBOHeight = 0;  ///< Allocated height; same role as the width.

    /**
     * @brief Bloom mip chain depth.
     *
     * Mip 0 is half scene resolution and each subsequent mip halves again, so 5
     * levels span 1/2 down to 1/32 of the scene. Sizes every fixed-length array
     * below.
     *
     * This value is duplicated as `ambience::BLOOM_MIP_LEVELS` in
     * AmbienceConfig.hpp (note: namespace `ambience`, not `AmbienceConfig`); the
     * two are not linked, so changing one without the other silently desyncs the
     * documented tuning from the actual chain.
     */
    static constexpr int kBloomMipLevels = 5;
    unsigned int m_BloomMipFBO[kBloomMipLevels] = {};  ///< One FBO per mip level.
    /**
     * @brief RGB16F color attachment per mip, LINEAR-filtered (the down/upsample
     *        shaders sample at fractional offsets) and CLAMP_TO_EDGE.
     *
     * Index 0 doubles as both the bright-pass output and the final bloom the
     * composite reads.
     */
    unsigned int m_BloomMipTex[kBloomMipLevels] = {};
    int m_BloomMipWidth[kBloomMipLevels] = {};   ///< Per-mip width; each >= 1 after halving.
    int m_BloomMipHeight[kBloomMipLevels] = {};  ///< Per-mip height; each >= 1 after halving.

    /// Empty VAO for post-FX full-screen triangle (no vertex buffer needed).
    unsigned int m_PostVAO = 0;

    /// @name Post-FX programs
    /// @{
    unsigned int m_PostProgram = 0;            ///< PostFXComposite.frag; scene+bloom -> swapchain.
    unsigned int m_BloomThresholdProgram = 0;  ///< BloomPrefilter.frag; saturation bright pass.
    unsigned int m_BloomDownProgram = 0;       ///< BloomDownsample.frag; mip i-1 -> mip i.
    unsigned int m_BloomUpProgram = 0;         ///< BloomUpsample.frag; mip i additively into i-1.
    /// @}

    /**
     * @name Cached uniform locations
     * @brief Looked up once in InitPostFXShaders.
     *
     * -1 means the uniform is absent from the linked program (typically optimized
     * out because nothing reads it); glUniform* on location -1 is a documented
     * no-op, so those writes are silently discarded rather than raising a GL error.
     * @{
     */
    GLint m_PostULoc_Scene = -1;
    GLint m_PostULoc_Bloom = -1;
    GLint m_PostULoc_BloomIntensity = -1;
    GLint m_PostULoc_Lift = -1;
    GLint m_PostULoc_Gamma = -1;
    GLint m_PostULoc_Gain = -1;
    GLint m_PostULoc_Saturation = -1;
    GLint m_PostULoc_CAStrength = -1;
    GLint m_PostULoc_VignetteIntensity = -1;
    GLint m_PostULoc_VignetteInnerR = -1;
    GLint m_PostULoc_VignetteOuterR = -1;
    GLint m_PostULoc_VignetteAspectY = -1;
    GLint m_PostULoc_EdgeDesat = -1;
    GLint m_PostULoc_GrainIntensity = -1;
    GLint m_PostULoc_GrainChromaMix = -1;
    GLint m_PostULoc_Time = -1;
    GLint m_PostULoc_TonemapKnee = -1;
    GLint m_PostULoc_Enabled = -1;

    GLint m_BloomThresholdULoc_Scene = -1;
    GLint m_BloomThresholdULoc_SatThreshold = -1;

    GLint m_BloomDownULoc_Input = -1;
    GLint m_BloomDownULoc_SrcTexelSize = -1;

    GLint m_BloomUpULoc_Input = -1;
    /// Texel size of the source mip (1/width, 1/height), not the destination -
    /// the tent filter samples the smaller mip it is reading from.
    GLint m_BloomUpULoc_SrcTexelSize = -1;
    /// @}

    /**
     * @brief True after BeginScene has bound the scene FBO; cleared by
     *        EndSceneApplyPostFX.
     *
     * Stays false when BeginScene bailed (no viewport yet, or FBO creation
     * failed), which is what makes EndSceneApplyPostFX short-circuit and leave the
     * frame un-composited instead of sampling an unbound target.
     */
    bool m_SceneBound = false;

    /// @brief Allocate (or recreate, on size change) the scene FBO + color tex + depth RBO.
    /// No-op for a non-positive size or when the current allocation already matches.
    void EnsureSceneFramebuffer(int width, int height);
    /// @brief Allocate (or recreate, on size change) the bloom mip chain.
    /// @param width,height Scene size; mip 0 is half this, each later mip half again.
    void EnsureBloomFramebuffers(int width, int height);
    /// @brief Destroy scene FBO resources (called on resize and Shutdown).
    void DestroySceneFramebuffer();
    /// @brief Destroy bloom FBO resources (called on resize and Shutdown).
    void DestroyBloomFramebuffers();
    /**
     * @brief Compile post-FX shader programs and cache their uniform locations,
     *        once during Init.
     *
     * On failure it returns before the uniform lookups, so m_PostVAO stays 0, but
     * only the programs that actually failed are 0 - a partial failure leaves the
     * others linked and valid. Init logs a warning and continues.
     *
     * @warning The composite path is not guarded. BeginScene binds the scene FBO on
     * m_SceneFBO alone, and EndSceneApplyPostFX then runs RunBloomPrep and the
     * composite with whatever handles exist, so with a program or the VAO at 0 the
     * frame stays in the scene FBO and nothing reaches the swapchain. Treat valid
     * post-FX programs as a precondition of BeginScene binding the FBO.
     *
     * @return False if any of the four programs failed to compile or link.
     */
    bool InitPostFXShaders();
    /// @brief Run the saturation bright pass and the downsample/upsample mip chain,
    /// leaving the finished bloom in m_BloomMipTex[0]. Leaves GL_BLEND disabled.
    void RunBloomPrep();

    /// @}
};
