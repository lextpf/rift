#pragma once

#include "../src/IRenderer.hpp"
#include "../src/RendererMacros.hpp"

#include <array>
#include <string>
#include <vector>

/**
 * @class MockRenderer
 * @brief Context-free IRenderer used by tests that must not touch the GPU.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * The test binary links GL and Vulkan but never creates a context, so any test
 * that reaches a render path needs a renderer that only accepts calls. Every
 * override here discards its arguments except two:
 * - DrawQuad3D appends a @ref Quad3D record to `quads3D`.
 * - SetViewProjection overwrites `viewProjection`.
 *
 * The remaining observable results are fixed, not recorded: RequiresYFlip is
 * always true, GetDrawCallCount is always 0, and GetBackendInfo returns a
 * default-constructed RendererInfo. Asserting on those three proves nothing.
 */
class MockRenderer : public IRenderer
{
public:
    RIFT_DECLARE_COMMON_RENDERER_METHODS;

    void SetFontCandidates(const std::vector<std::string>& /*fontCandidates*/) override {}
    bool RequiresYFlip() const override { return true; }
    void SetAmbientColor(const glm::vec3& /*color*/) override {}
    int GetDrawCallCount() const override { return 0; }

    /**
     * @brief One recorded @ref IRenderer::DrawQuad3D submission.
     *
     * World geometry is built entirely on the CPU as scene-space corners, so
     * recording the submissions is what makes tile placement, billboard
     * orientation and pass assignment testable without a graphics context.
     */
    struct Quad3D
    {
        std::array<glm::vec3, 4> corners{};
        glm::vec2 texCoord{0.0f};
        glm::vec2 texSize{0.0f};
        glm::vec4 color{1.0f};
        renderModes::BlendMode blend = renderModes::BlendMode::Alpha;
        renderModes::DepthMode depth = renderModes::DepthMode::TestAndWrite;
    };

    std::vector<Quad3D> quads3D;     ///< Every DrawQuad3D call, in submission order.
    glm::mat4 viewProjection{1.0f};  ///< Last matrix given to SetViewProjection.

    /**
     * @brief Drop the recorded DrawQuad3D submissions (call between assertions).
     *
     * `viewProjection` is deliberately left at its last value, so a test that
     * needs a fresh matrix must set one itself.
     */
    void ClearRecorded() { quads3D.clear(); }
};

inline bool MockRenderer::Init()
{
    return true;
}
inline void MockRenderer::Shutdown() {}
inline void MockRenderer::BeginFrame() {}
inline void MockRenderer::EndFrame() {}
inline void MockRenderer::BeginScene() {}
inline void MockRenderer::EndSceneApplyPostFX(const PostFXParams& /*params*/) {}

inline void MockRenderer::DrawSprite(const Texture& /*texture*/,
                                     glm::vec2 /*position*/,
                                     glm::vec2 /*size*/,
                                     float /*rotation*/,
                                     glm::vec3 /*color*/)
{
}

inline void MockRenderer::DrawSpriteRegion(const Texture& /*texture*/,
                                           glm::vec2 /*position*/,
                                           glm::vec2 /*size*/,
                                           glm::vec2 /*texCoord*/,
                                           glm::vec2 /*texSize*/,
                                           float /*rotation*/,
                                           glm::vec3 /*color*/,
                                           bool /*flipY*/,
                                           bool /*tileFlipX*/,
                                           bool /*tileFlipY*/)
{
}

inline void MockRenderer::DrawSpriteAlpha(const Texture& /*texture*/,
                                          glm::vec2 /*position*/,
                                          glm::vec2 /*size*/,
                                          float /*rotation*/,
                                          glm::vec4 /*color*/,
                                          bool /*additive*/)
{
}

inline void MockRenderer::DrawSpriteAtlas(const Texture& /*texture*/,
                                          glm::vec2 /*position*/,
                                          glm::vec2 /*size*/,
                                          glm::vec2 /*uvMin*/,
                                          glm::vec2 /*uvMax*/,
                                          float /*rotation*/,
                                          glm::vec4 /*color*/,
                                          bool /*additive*/)
{
}

inline void MockRenderer::DrawColoredRect(glm::vec2 /*position*/,
                                          glm::vec2 /*size*/,
                                          glm::vec4 /*color*/,
                                          bool /*additive*/)
{
}

inline void MockRenderer::DrawQuad3D(const Texture& /*texture*/,
                                     const glm::vec3 corners[4],
                                     glm::vec2 texCoord,
                                     glm::vec2 texSize,
                                     glm::vec4 color,
                                     renderModes::BlendMode blend,
                                     renderModes::DepthMode depth,
                                     bool /*flipY*/,
                                     bool /*tileFlipX*/,
                                     bool /*tileFlipY*/)
{
    Quad3D record;
    record.corners = {corners[0], corners[1], corners[2], corners[3]};
    record.texCoord = texCoord;
    record.texSize = texSize;
    record.color = color;
    record.blend = blend;
    record.depth = depth;
    quads3D.push_back(record);
}

inline void MockRenderer::SetViewProjection(const glm::mat4& matrix)
{
    viewProjection = matrix;
}

inline void MockRenderer::SetProjection(const glm::mat4& /*projection*/) {}
inline void MockRenderer::SetViewport(int /*x*/, int /*y*/, int /*width*/, int /*height*/) {}
inline void MockRenderer::Clear(float /*r*/, float /*g*/, float /*b*/, float /*a*/) {}
inline void MockRenderer::UploadTexture(const Texture& /*texture*/) {}

inline void MockRenderer::DrawText(const std::string& /*text*/,
                                   glm::vec2 /*position*/,
                                   float /*scale*/,
                                   glm::vec3 /*color*/,
                                   float /*outlineSize*/,
                                   float /*alpha*/)
{
}

inline float MockRenderer::GetTextAscent(float /*scale*/) const
{
    return 0.0f;
}

inline float MockRenderer::GetTextWidth(const std::string& /*text*/, float /*scale*/) const
{
    return 0.0f;
}

inline RendererInfo MockRenderer::GetBackendInfo() const
{
    return {};
}
