#pragma once

/**
 * @brief Declares all IRenderer override methods for a concrete renderer class.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Expands to the IRenderer overrides both backends declare identically:
 * Init, Shutdown, BeginFrame, EndFrame, BeginScene, EndSceneApplyPostFX,
 * DrawSprite, DrawSpriteRegion, DrawSpriteAlpha, DrawSpriteAtlas,
 * DrawColoredRect, DrawQuad3D, SetViewProjection,
 * SetProjection, SetViewport, Clear, UploadTexture, DrawText, GetTextAscent,
 * GetTextWidth, and GetBackendInfo.
 *
 * Note the expansion ends without a trailing semicolon, so every use site must
 * write one: `RIFT_DECLARE_COMMON_RENDERER_METHODS;`.
 *
 * @par Overrides that stay outside the macro
 * This is not the whole override set. Both backends declare `SetFontCandidates`,
 * `RequiresYFlip`, `SetAmbientColor` and `GetDrawCallCount` separately, because
 * each is defined inline or with backend-specific documentation; the optional
 * non-pure hooks `DrawTextLarge` and `GetTextWidthLarge` are overridden only by the
 * OpenGL backend. Changing any of those six signatures means editing four files:
 * `IRenderer.hpp`, `OpenGLRenderer.hpp`, `VulkanRenderer.hpp`, and this one only if
 * the method moves into the macro.
 *
 * @par Why this macro exists
 * For the methods it does cover, `OpenGLRenderer` and `VulkanRenderer` must declare
 * byte-identical signatures. Centralising those declarations here makes signature
 * drift impossible: change IRenderer, mirror it in this macro, and both backends
 * recompile against the same source. Do not bypass the macro for one-off overrides;
 * add the method here and to `IRenderer.hpp` together.
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
    void DrawQuad3D(const Texture& texture,                                                  \
                    const glm::vec3 corners[4],                                              \
                    glm::vec2 texCoord,                                                      \
                    glm::vec2 texSize,                                                       \
                    glm::vec4 color,                                                         \
                    renderModes::BlendMode blend,                                            \
                    renderModes::DepthMode depth,                                            \
                    bool flipY,                                                              \
                    bool tileFlipX,                                                          \
                    bool tileFlipY) override;                                                \
    void SetViewProjection(const glm::mat4& viewProjection) override;                        \
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
