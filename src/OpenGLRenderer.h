#pragma once

#include "IRenderer.h"
#include "PostFXParams.h"
#include "RendererMacros.h"

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
 * @section gl_features OpenGL Features Used
 * | Feature              | Version | Usage                          |
 * |----------------------|---------|--------------------------------|
 * | Core Profile         | 4.6     | Modern pipeline                |
 * | VAO/VBO              | 3.0+    | Geometry storage               |
 * | Shaders              | 4.5     | GLSL 450 core                  |
 * | Texture Atlas        | 2.0+    | Font glyph packing             |
 * | Alpha Blending       | 1.0+    | Transparency and particles     |
 *
 * @section gl_batching Sprite Batching System
 * To minimize draw calls, sprites are accumulated in a vertex buffer
 * and flushed when the texture changes or the buffer fills. Each draw
 * call has GPU overhead (state changes, driver validation), so batching
 * many sprites into a single draw call dramatically improves performance.
 *
 * @par Why Batching Matters
 * - Many sprites without batching: one draw call per sprite, high
 * driver overhead.
 * - Many sprites with batching: one draw call per texture run, lower CPU
 * overhead.
 *
 * @par Batch Flow
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
 * D --> E["Bind new texture"]:::add
 * E --> C
 * C --> F["DrawSprite(texA)"]:::add
 * F --> B
 * G["EndFrame()"]:::flush --> H["FlushBatch()"]:::flush
 * H --> I["Present to screen"]
 * </pre>
 * @endhtmlonly
 *
 * @par Batch Accumulation
 * Each sprite adds 6 vertices (2 triangles) to the batch buffer:
 * @code
 * // Sprite quad as two triangles (6 vertices)
 * // TL--TR      TL, BL, BR (triangle 1)
 * // |  / |  =>  TL, BR, TR (triangle 2)
 * // BL--BR
 *
 * DrawSprite(texA, pos1)  // vertices 0-5   -> batch size: 6
 * DrawSprite(texA, pos2)  // vertices 6-11  -> batch size: 12
 * DrawSprite(texA, pos3)  // vertices 12-17 -> batch size: 18
 * DrawSprite(texB, pos4)  // FLUSH! draw 18 vertices, then start new batch
 * @endcode
 *
 * @par Flush Triggers
 * Flush triggers include texture changes, a full batch buffer, frame end,
 * and
 * switches between sprite, rect, particle, or text rendering paths.
 *
 * @par Batch Types
 * | Batch Type  | Buffer                  | Trigger            |
 * |-------------|-------------------------|--------------------|
 * | Sprites     | m_BatchVertices         | Texture change     |
 * | Rects       | m_RectBatchVertices     | Blend mode change  |
 * | Particles   | m_ParticleBatchVertices | Texture/blend      |
 * | Text        | m_TextBatchVertices     | Per DrawText call  |
 *
 * @section gl_shaders Shader Architecture
 * Uses a single unified shader program for all 2D rendering:
 * - Vertex: Transform quad corners, pass UV coordinates
 * - Fragment: Sample texture, apply color tint and ambient light
 *
 * @par Uniform Locations (cached at init)
 * | Uniform      | Type  | Purpose                    |
 * |--------------|-------|----------------------------|
 * | model        | mat4  | Per-sprite transform       |
 * | projection   | mat4  | Orthographic projection    |
 * | color        | vec3  | Color tint                 |
 * | alpha        | float | Transparency               |
 * | ambientColor | vec3  | Day/night lighting         |
 *
 * @section gl_font Font Rendering
 * Text is rendered using FreeType for glyph rasterization and a
 * single texture atlas for efficient batched rendering.
 *
 * @par Atlas Layout
 * All ASCII glyphs (32-127) are packed into a single texture at
 * initialization. UV coordinates for each character are cached
 * in the m_Characters map.
 *
 * @see IRenderer Base interface with method documentation
 * @see VulkanRenderer Alternative
 * Vulkan implementation
 */
struct GLFWwindow;

/// @name Debug Draw Visualization
/// @{

/// @brief Enable or disable per-draw-call sleep for debugging render order.
void SetDebugDrawSleep(GLFWwindow* window, bool enabled);

/// @brief Reset the debug draw call counter to zero.
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

    /// Draws text from the high-resolution headline atlas; crisp at the
    /// scales used by the title screen ("RIFT" logo) without upscaling
    /// 24-px body glyphs.
    void DrawTextLarge(const std::string& text,
                       glm::vec2 position,
                       float scale,
                       glm::vec3 color,
                       float outlineSize,
                       float alpha) override;

    [[nodiscard]] float GetTextWidthLarge(const std::string& text, float scale) const override;

    void SetFontCandidates(const std::vector<std::string>& fontCandidates) override;

    void SetVanishingPointPerspective(bool enabled,
                                      float horizonY,
                                      float horizonScale,
                                      float viewWidth,
                                      float viewHeight) override;
    void SetGlobePerspective(bool enabled,
                             float sphereRadius,
                             float viewWidth,
                             float viewHeight) override;
    void SetFisheyePerspective(bool enabled,
                               float sphereRadius,
                               float horizonY,
                               float horizonScale,
                               float viewWidth,
                               float viewHeight) override;
    void SuspendPerspective(bool suspend) override;

    /// @brief OpenGL uses bottom-left texture origin, requires Y-flip.
    bool RequiresYFlip() const override { return true; }

    void SetAmbientColor(const glm::vec3& color) override;

    int GetDrawCallCount() const override { return m_DrawCallCount; }

private:
    /// @name Initialization Helpers
    /// @{

    /// @brief Ensure texture has a valid OpenGL ID for the current context, recreating if needed.
    /// @return Valid texture ID, or 0 if the texture could not be made ready.
    unsigned int EnsureTextureReady(const Texture& texture);

    /// @brief Create VAO/VBO for the unit quad used by all sprite rendering.
    void SetupQuad();

    /// @brief Create a 1x1 white texture for colored rectangle rendering.
    void CreateWhiteTexture();

    /// @brief Load a TTF font and build both glyph atlases (body + headline).
    /// @param fontPath Path to the .ttf font file.
    void LoadFont(const std::string& fontPath);

    /// @}

    /// @name Font Atlas
    /// @{

    /**
     * @brief Cached glyph metrics and atlas UV coordinates.
     *
     * Populated during LoadFont() for ASCII characters 32-127.
     */
    struct Character
    {
        glm::ivec2 Size;       ///< Glyph dimensions in pixels.
        glm::ivec2 Bearing;    ///< Offset from baseline to top-left.
        unsigned int Advance;  ///< Horizontal advance to next character.
        float u0, v0, u1, v1;  ///< UV coordinates in the font atlas.
    };

    /// @brief Body-text glyph atlas, rasterized at @c BODY_FONT_PIXEL_SIZE.
    std::map<char, Character> m_Characters;
    unsigned int m_FontAtlasTexture;
    int m_FontAtlasWidth, m_FontAtlasHeight;

    /// @brief Headline glyph atlas at @c HEADLINE_FONT_PIXEL_SIZE.
    /// Used by @ref DrawTextLarge so title-screen text stays crisp without
    /// upscaling the body atlas's 24-px glyphs.
    std::map<char, Character> m_HeadlineCharacters;
    unsigned int m_HeadlineFontAtlasTexture = 0;
    int m_HeadlineFontAtlasWidth = 0;
    int m_HeadlineFontAtlasHeight = 0;

    /// FreeType pixel sizes for the two atlases.
    static constexpr int BODY_FONT_PIXEL_SIZE = 24;
    static constexpr int HEADLINE_FONT_PIXEL_SIZE = 96;

    /// @brief Internal: build one atlas at @p pixelSize into the provided slots.
    /// Used by LoadFont to emit both body and headline atlases from one face.
    void BuildAtlasInto(int pixelSize,
                        std::map<char, Character>& outChars,
                        unsigned int& outTexture,
                        int& outWidth,
                        int& outHeight);

    /// @brief Internal: shared body of DrawText, parameterized over the atlas.
    /// @c chars and @c atlasTexture select between body and headline atlases.
    void DrawTextImpl(const std::string& text,
                      glm::vec2 position,
                      float scale,
                      glm::vec3 color,
                      float outlineSize,
                      float alpha,
                      const std::map<char, Character>& chars,
                      unsigned int atlasTexture);

    /// @brief Internal: shared body of GetTextWidth.
    [[nodiscard]] float GetTextWidthImpl(const std::string& text,
                                         float scale,
                                         const std::map<char, Character>& chars) const;

    std::vector<std::string> m_FontCandidates;  ///< Project-specific font candidates.

#ifdef USE_FREETYPE
    FT_Library m_FreeType;  ///< FreeType library handle.
    FT_Face m_Face;         ///< Loaded font face.
#endif

    /// @}

    /// @name Core OpenGL Objects
    /// @{

    unsigned int m_VAO, m_VBO, m_EBO;  ///< Unit quad geometry.
    unsigned int m_ShaderProgram;      ///< Unified sprite/text shader.
    glm::mat4 m_Projection;            ///< Current orthographic projection.
    unsigned int m_WhiteTexture;       ///< 1x1 white texture for rects.

    /// @}

    /// @name Shader Uniform Locations
    /// @{

    GLint m_ModelLoc;         ///< Per-sprite model matrix.
    GLint m_ProjectionLoc;    ///< Orthographic projection matrix.
    GLint m_ColorLoc;         ///< RGB color tint.
    GLint m_AlphaLoc;         ///< Transparency multiplier.
    GLint m_AmbientColorLoc;  ///< Day/night ambient light color.
    GLint m_UseColorOnlyLoc;  ///< Color mode selector (0=texture, 1=uniform, 2=vertex, 3=tex*vert).
    glm::vec3 m_AmbientColor;  ///< Current ambient light value.

    /// @}

    /// @name Text Batching
    /// @{

    /// @brief Maximum characters per DrawText call before flush.
    static constexpr size_t MAX_TEXT_QUADS = 2048;

    /// @brief Vertex format for text quads.
    struct TextVertex
    {
        float x, y;  ///< Screen position.
        float u, v;  ///< Font atlas UV.
    };

    std::vector<TextVertex> m_TextBatchVertices;  ///< Accumulated text geometry.
    unsigned int m_TextVAO, m_TextVBO;            ///< Text-specific buffers.

    /// @}

    /// @name Sprite Batching
    /// @{

    /// @brief Maximum sprites before automatic flush.
    static constexpr size_t MAX_BATCH_SPRITES = 10000;
    static constexpr size_t VERTICES_PER_SPRITE = 6;  ///< Two triangles.
    static constexpr size_t FLOATS_PER_VERTEX = 4;    ///< x, y, u, v.

    /// @brief Vertex format for batched sprites (pre-transformed).
    struct BatchVertex
    {
        float x, y;  ///< Final screen position after perspective.
        float u, v;  ///< Texture coordinates.
    };

    std::vector<BatchVertex> m_BatchVertices;  ///< Accumulated sprite geometry.
    unsigned int m_BatchVAO, m_BatchVBO;       ///< Sprite batch buffers.
    unsigned int m_CurrentBatchTexture;        ///< Active texture for batching.

    /// @brief Submit accumulated sprites to GPU and reset batch.
    void FlushBatch();

    /// @}

    /// @name Colored Rectangle Batching
    /// @{

    /// @brief Vertex format with per-vertex RGBA color.
    struct ColoredVertex
    {
        float x, y;        ///< Screen position.
        float u, v;        ///< Texture coords (unused for rects).
        float r, g, b, a;  ///< Per-vertex color.
    };

    std::vector<ColoredVertex> m_RectBatchVertices;  ///< Accumulated rect geometry.
    unsigned int m_RectBatchVAO, m_RectBatchVBO;     ///< Rect batch buffers.
    bool m_RectBatchAdditive;                        ///< Current blend mode.

    /// @}

    /// @name Colored Rectangle Helpers
    /// @{

    /// @brief Submit accumulated rects to GPU and reset batch.
    void FlushRectBatch();

    /// @brief Create VAO/VBO for colored rectangle batching.
    void SetupRectBatchBuffers();

    /// @}

    /// @name Particle Batching
    /// @{

    std::vector<ColoredVertex> m_ParticleBatchVertices;  ///< Particle geometry.
    unsigned int m_CurrentParticleTexture;               ///< Active particle texture.
    bool m_ParticleBatchAdditive;                        ///< Particle blend mode.

    /// @}

    /// @brief Submit accumulated particles to GPU and reset batch.
    void FlushParticleBatch();

    /// @name Performance Metrics
    /// @{

    int m_DrawCallCount = 0;     ///< Number of draw calls this frame (for debug display).
    bool m_Initialized = false;  ///< True after Init() completes successfully.

    /// @}

    /// @name Shader Loading
    /// @{

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

    /// @name Viewport tracking (for post-FX FBO sizing)
    /// @{
    int m_ViewportWidth = 0;
    int m_ViewportHeight = 0;
    /// @}

    /// @name Post-FX Pipeline
    /// @{

    /// Scene FBO + color texture + depth renderbuffer. Resized on viewport change.
    unsigned int m_SceneFBO = 0;
    unsigned int m_SceneColorTex = 0;
    unsigned int m_SceneDepthRBO = 0;
    int m_SceneFBOWidth = 0;
    int m_SceneFBOHeight = 0;

    /// Bloom mip chain: progressively halved render targets for the
    /// downsample/upsample bloom architecture. Mip 0 is half scene resolution;
    /// each subsequent mip is half the previous. See AmbienceConfig::BLOOM_MIP_LEVELS.
    static constexpr int kBloomMipLevels = 5;
    unsigned int m_BloomMipFBO[kBloomMipLevels] = {};
    unsigned int m_BloomMipTex[kBloomMipLevels] = {};
    int m_BloomMipWidth[kBloomMipLevels] = {};
    int m_BloomMipHeight[kBloomMipLevels] = {};

    /// Empty VAO for post-FX full-screen triangle (no vertex buffer needed).
    unsigned int m_PostVAO = 0;

    /// Post-FX programs and cached uniform locations.
    unsigned int m_PostProgram = 0;
    unsigned int m_BloomThresholdProgram = 0;
    unsigned int m_BloomDownProgram = 0;
    unsigned int m_BloomUpProgram = 0;

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
    GLint m_BloomUpULoc_SrcTexelSize = -1;

    /// True after BeginScene has bound the scene FBO; cleared by EndSceneApplyPostFX.
    bool m_SceneBound = false;

    /// @brief Allocate (or recreate, on size change) the scene FBO + color tex + depth RBO.
    void EnsureSceneFramebuffer(int width, int height);
    /// @brief Allocate (or recreate, on size change) the bloom ping-pong FBOs.
    void EnsureBloomFramebuffers(int width, int height);
    /// @brief Destroy scene FBO resources (called on resize and Shutdown).
    void DestroySceneFramebuffer();
    /// @brief Destroy bloom FBO resources (called on resize and Shutdown).
    void DestroyBloomFramebuffers();
    /// @brief Compile post-FX shader programs (called once during Init).
    bool InitPostFXShaders();
    /// @brief Run the bright-pass + separable Gaussian blur sequence into m_BloomTex[0].
    void RunBloomPrep();

    /// @}
};
