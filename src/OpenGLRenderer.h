#pragma once

#include "IRenderer.h"

#include <glad/glad.h>
#include <map>
#include <string>

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
 * | Scenario              | Draw Calls | Performance |
 * |-----------------------|------------|-------------|
 * | 1000 sprites, no batch| 1000       | ~15 FPS     |
 * | 1000 sprites, batched | 1-10       | ~500+ FPS   |
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
 * The batch is flushed (submitted to GPU) when:
 * - **Texture changes**: New sprite uses different texture than current batch
 * - **Buffer full**: Batch reaches MAX_BATCH_SPRITES (10000 sprites)
 * - **Frame ends**: EndFrame() flushes any remaining geometry
 * - **State changes**: SetProjection(), blend mode changes, etc.
 *
 * @par Batch Types
 * | Batch Type  | Buffer              | Trigger            |
 * |-------------|---------------------|--------------------|
 * | Sprites     | m_BatchVertices     | Texture change     |
 * | Rects       | m_RectBatchVertices | Blend mode change  |
 * | Particles   | m_ParticleBatchVertices | Texture/blend  |
 * | Text        | m_TextBatchVertices | Per DrawText call  |
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
 * @see VulkanRenderer Alternative Vulkan implementation
 */
class OpenGLRenderer : public IRenderer
{
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    void Init() override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void DrawSprite(const Texture& texture,
                    glm::vec2 position,
                    glm::vec2 size,
                    float rotation,
                    glm::vec3 color) override;
    void DrawSpriteRegion(const Texture& texture,
                          glm::vec2 position,
                          glm::vec2 size,
                          glm::vec2 texCoord,
                          glm::vec2 texSize,
                          float rotation,
                          glm::vec3 color,
                          bool flipY) override;
    void DrawSpriteAlpha(const Texture& texture,
                         glm::vec2 position,
                         glm::vec2 size,
                         float rotation,
                         glm::vec4 color,
                         bool additive) override;
    void DrawSpriteAtlas(const Texture& texture,
                         glm::vec2 position,
                         glm::vec2 size,
                         glm::vec2 uvMin,
                         glm::vec2 uvMax,
                         float rotation,
                         glm::vec4 color,
                         bool additive) override;
    void DrawColoredRect(glm::vec2 position,
                         glm::vec2 size,
                         glm::vec4 color,
                         bool additive) override;
    void DrawWarpedQuad(const Texture& texture,
                        const glm::vec2 corners[4],
                        glm::vec2 texCoord,
                        glm::vec2 texSize,
                        glm::vec3 color,
                        bool flipY) override;

    void SetProjection(glm::mat4 projection) override;
    void SetViewport(int x, int y, int width, int height) override;
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
    void Clear(float r, float g, float b, float a) override;

    void UploadTexture(const Texture& texture) override;

    void DrawText(const std::string& text,
                  glm::vec2 position,
                  float scale,
                  glm::vec3 color,
                  float outlineSize,
                  float alpha) override;
    float GetTextAscent(float scale) const override;
    float GetTextWidth(const std::string& text, float scale) const override;

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

    /// @brief Load a TTF font and build the glyph texture atlas.
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

    std::map<char, Character> m_Characters;   ///< Glyph lookup table.
    unsigned int m_FontAtlasTexture;          ///< OpenGL texture ID for font atlas.
    int m_FontAtlasWidth, m_FontAtlasHeight;  ///< Atlas dimensions.

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

    GLint m_ModelLoc;          ///< Per-sprite model matrix.
    GLint m_ProjectionLoc;     ///< Orthographic projection matrix.
    GLint m_ColorLoc;          ///< RGB color tint.
    GLint m_AlphaLoc;          ///< Transparency multiplier.
    GLint m_AmbientColorLoc;   ///< Day/night ambient light color.
    glm::vec3 m_AmbientColor;  ///< Current ambient light value.

    /// @}

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

    /// @brief Submit accumulated rects to GPU and reset batch.
    void FlushRectBatch();

    /// @brief Create VAO/VBO for colored rectangle batching.
    void SetupRectBatchBuffers();

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

    int m_DrawCallCount = 0;  ///< Number of draw calls this frame (for debug display).

    /// @}

    /// @name Shader Loading
    /// @{

    /**
     * @brief Load shader source code from a file.
     * @param filepath Path to .vert or .frag shader file.
     * @return Shader source as string, or empty string on error.
     */
    std::string LoadShaderFromFile(const std::string& filepath);

    /// @}
};
