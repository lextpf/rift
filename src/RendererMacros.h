#pragma once

/**
 * @brief Declares all IRenderer override methods for a concrete renderer class.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Expands to the full set of overridden virtual method declarations:
 * Init, Shutdown, BeginFrame, EndFrame, BeginScene, EndSceneApplyPostFX,
 * DrawSprite, DrawSpriteRegion, DrawSpriteAlpha, DrawSpriteAtlas,
 * DrawColoredRect, DrawWarpedQuad, SetProjection, SetViewport, Clear,
 * UploadTexture, DrawText, GetTextAscent, and GetTextWidth.
 *
 * @par Why this macro exists
 * Both `OpenGLRenderer` and `VulkanRenderer` must declare every IRenderer
 * override with byte-identical signatures. Centralising the declarations here
 * makes signature drift impossible: any change to IRenderer's pure-virtual set
 * must be mirrored in this macro, after which both backends recompile against
 * the same source. Do not bypass this macro for one-off overrides; add the
 * method here and to `IRenderer.h` together.
 *
 * @see IRenderer for documentation of each method.
 * @see OpenGLRenderer, VulkanRenderer for the backends that consume this macro.
 */
#define RIFT_DECLARE_COMMON_RENDERER_METHODS                                                 \
    [[nodiscard]] bool Init() override;                                                      \
    void Shutdown() override;                                                                \
    void BeginFrame() override;                                                              \
    void EndFrame() override;                                                                \
    void BeginScene() override;                                                              \
    void EndSceneApplyPostFX(const PostFXParams& params) override;                           \
    void DrawSprite(const Texture& texture,                                                  \
                    glm::vec2 position,                                                      \
                    glm::vec2 size,                                                          \
                    float rotation,                                                          \
                    glm::vec3 color) override;                                               \
    void DrawSpriteRegion(const Texture& texture,                                            \
                          glm::vec2 position,                                                \
                          glm::vec2 size,                                                    \
                          glm::vec2 texCoord,                                                \
                          glm::vec2 texSize,                                                 \
                          float rotation,                                                    \
                          glm::vec3 color,                                                   \
                          bool flipY,                                                        \
                          bool tileFlipX,                                                    \
                          bool tileFlipY) override;                                          \
    void DrawSpriteAlpha(const Texture& texture,                                             \
                         glm::vec2 position,                                                 \
                         glm::vec2 size,                                                     \
                         float rotation,                                                     \
                         glm::vec4 color,                                                    \
                         bool additive) override;                                            \
    void DrawSpriteAtlas(const Texture& texture,                                             \
                         glm::vec2 position,                                                 \
                         glm::vec2 size,                                                     \
                         glm::vec2 uvMin,                                                    \
                         glm::vec2 uvMax,                                                    \
                         float rotation,                                                     \
                         glm::vec4 color,                                                    \
                         bool additive) override;                                            \
    void DrawColoredRect(glm::vec2 position, glm::vec2 size, glm::vec4 color, bool additive) \
        override;                                                                            \
    void DrawWarpedQuad(const Texture& texture,                                              \
                        const glm::vec2 corners[4],                                          \
                        glm::vec2 texCoord,                                                  \
                        glm::vec2 texSize,                                                   \
                        glm::vec3 color,                                                     \
                        bool flipY,                                                          \
                        bool tileFlipX,                                                      \
                        bool tileFlipY) override;                                            \
    void SetProjection(const glm::mat4& projection) override;                                \
    void SetViewport(int x, int y, int width, int height) override;                          \
    void Clear(float r, float g, float b, float a) override;                                 \
    void UploadTexture(const Texture& texture) override;                                     \
    void DrawText(const std::string& text,                                                   \
                  glm::vec2 position,                                                        \
                  float scale,                                                               \
                  glm::vec3 color,                                                           \
                  float outlineSize,                                                         \
                  float alpha) override;                                                     \
    float GetTextAscent(float scale) const override;                                         \
    float GetTextWidth(const std::string& text, float scale) const override;                 \
    [[nodiscard]] RendererInfo GetBackendInfo() const override
