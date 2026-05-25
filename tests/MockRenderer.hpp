#pragma once

#include "../src/IRenderer.hpp"
#include "../src/RendererMacros.hpp"

#include <string>
#include <vector>

class MockRenderer : public IRenderer
{
public:
    RIFT_DECLARE_COMMON_RENDERER_METHODS;

    void SetFontCandidates(const std::vector<std::string>& /*fontCandidates*/) override {}
    bool RequiresYFlip() const override { return true; }
    void SetAmbientColor(const glm::vec3& /*color*/) override {}
    int GetDrawCallCount() const override { return 0; }
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

inline void MockRenderer::DrawWarpedQuad(const Texture& /*texture*/,
                                         const glm::vec2 /*corners*/[4],
                                         glm::vec2 /*texCoord*/,
                                         glm::vec2 /*texSize*/,
                                         glm::vec3 /*color*/,
                                         bool /*flipY*/,
                                         bool /*tileFlipX*/,
                                         bool /*tileFlipY*/)
{
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
