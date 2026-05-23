#include "OpenGLRenderer.hpp"

#include "DrawTracer.hpp"
#include "Logger.hpp"
#include "PerspectiveTransform.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>
#include <vector>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Render";

// Per-draw-call debug visualization state.
bool s_DebugDrawSleep = false;
GLFWwindow* s_DebugWindow = nullptr;
int s_DebugDrawCallIndex = 0;
}  // namespace

void SetDebugDrawSleep(GLFWwindow* window, bool enabled)
{
    s_DebugWindow = window;
    s_DebugDrawSleep = enabled;
}

void ResetDebugDrawCallIndex()
{
    s_DebugDrawCallIndex = 0;
}

bool IsDebugDrawSleepEnabled()
{
    return s_DebugDrawSleep;
}

static void DebugAfterDraw(const char* label, int count)
{
    if (s_DebugDrawSleep && s_DebugWindow)
    {
        Logger::DebugF(
            LOG_SUBSYSTEM, "[DRAW {}] {} ({} vertices)", s_DebugDrawCallIndex++, label, count);
        glFinish();
        glfwSwapBuffers(s_DebugWindow);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

unsigned int OpenGLRenderer::EnsureTextureReady(const Texture& texture)
{
    unsigned int texID = texture.GetID();
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.GetOpenGLContextGeneration() != currentGen || texID == 0)
    {
        texture.RecreateOpenGLTexture();
        texID = texture.GetID();
    }
    return texID;
}

OpenGLRenderer::OpenGLRenderer()
    : m_VAO(0),
      m_VBO(0),
      m_EBO(0),
      m_TextVAO(0),
      m_TextVBO(0),
      m_ShaderProgram(0),
      m_WhiteTexture(0),
      m_ModelLoc(-1),
      m_ProjectionLoc(-1),
      m_ColorLoc(-1),
      m_AlphaLoc(-1),
      m_AmbientColorLoc(-1),
      m_AmbientColor(1.0f, 1.0f, 1.0f),  // White = full bright.
      m_BatchVAO(0),
      m_BatchVBO(0),
      m_CurrentBatchTexture(0),
      m_RectBatchVAO(0),
      m_RectBatchVBO(0),
      m_RectBatchAdditive(false),
      m_CurrentParticleTexture(0),
      m_ParticleBatchAdditive(false),
      m_FontAtlasTexture(0),
      m_FontAtlasWidth(0),
      m_FontAtlasHeight(0)
#ifdef USE_FREETYPE
      ,
      m_FreeType(nullptr),
      m_Face(nullptr)
#endif
{
    m_BatchVertices.reserve(MAX_BATCH_SPRITES * VERTICES_PER_SPRITE);
    m_RectBatchVertices.reserve(MAX_BATCH_SPRITES * VERTICES_PER_SPRITE);
    m_ParticleBatchVertices.reserve(MAX_BATCH_SPRITES * VERTICES_PER_SPRITE);
}

OpenGLRenderer::~OpenGLRenderer()
{
    Shutdown();
}

void OpenGLRenderer::SetFontCandidates(const std::vector<std::string>& fontCandidates)
{
    m_FontCandidates = fontCandidates;
}

void OpenGLRenderer::Shutdown()
{
    if (m_FontAtlasTexture != 0)
    {
        glDeleteTextures(1, &m_FontAtlasTexture);
        m_FontAtlasTexture = 0;
    }
    m_Characters.clear();
    if (m_HeadlineFontAtlasTexture != 0)
    {
        glDeleteTextures(1, &m_HeadlineFontAtlasTexture);
        m_HeadlineFontAtlasTexture = 0;
    }
    m_HeadlineCharacters.clear();

#ifdef USE_FREETYPE
    if (m_Face)
    {
        FT_Done_Face(m_Face);
        m_Face = nullptr;
    }
    if (m_FreeType)
    {
        FT_Done_FreeType(m_FreeType);
        m_FreeType = nullptr;
    }
#endif

    // Reset handles to 0 to prevent double-deletion.
    if (m_VAO != 0)
    {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
    if (m_VBO != 0)
    {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    if (m_EBO != 0)
    {
        glDeleteBuffers(1, &m_EBO);
        m_EBO = 0;
    }
    if (m_TextVAO != 0)
    {
        glDeleteVertexArrays(1, &m_TextVAO);
        m_TextVAO = 0;
    }
    if (m_TextVBO != 0)
    {
        glDeleteBuffers(1, &m_TextVBO);
        m_TextVBO = 0;
    }
    if (m_BatchVAO != 0)
    {
        glDeleteVertexArrays(1, &m_BatchVAO);
        m_BatchVAO = 0;
    }
    if (m_BatchVBO != 0)
    {
        glDeleteBuffers(1, &m_BatchVBO);
        m_BatchVBO = 0;
    }
    if (m_RectBatchVAO != 0)
    {
        glDeleteVertexArrays(1, &m_RectBatchVAO);
        m_RectBatchVAO = 0;
    }
    if (m_RectBatchVBO != 0)
    {
        glDeleteBuffers(1, &m_RectBatchVBO);
        m_RectBatchVBO = 0;
    }
    if (m_ShaderProgram != 0)
    {
        glDeleteProgram(m_ShaderProgram);
        m_ShaderProgram = 0;
    }
    if (m_WhiteTexture != 0)
    {
        glDeleteTextures(1, &m_WhiteTexture);
        m_WhiteTexture = 0;
    }

    DestroySceneFramebuffer();
    DestroyBloomFramebuffers();
    if (m_PostVAO != 0)
    {
        glDeleteVertexArrays(1, &m_PostVAO);
        m_PostVAO = 0;
    }
    if (m_PostProgram != 0)
    {
        glDeleteProgram(m_PostProgram);
        m_PostProgram = 0;
    }
    if (m_BloomThresholdProgram != 0)
    {
        glDeleteProgram(m_BloomThresholdProgram);
        m_BloomThresholdProgram = 0;
    }
    if (m_BloomDownProgram != 0)
    {
        glDeleteProgram(m_BloomDownProgram);
        m_BloomDownProgram = 0;
    }
    if (m_BloomUpProgram != 0)
    {
        glDeleteProgram(m_BloomUpProgram);
        m_BloomUpProgram = 0;
    }
    m_SceneBound = false;

    m_BatchVertices.clear();
    m_RectBatchVertices.clear();
}

std::string OpenGLRenderer::LoadShaderFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        // Fall back to parent directory.
        std::string parentPath = "../" + filepath;
        file.open(parentPath);
        if (!file.is_open())
        {
            Logger::ErrorF(
                LOG_SUBSYSTEM, "Could not open shader file: {} or {}", filepath, parentPath);
            return "";
        }
    }

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return source;
}

unsigned int OpenGLRenderer::CompileShaderProgram(const std::string& vertSrc,
                                                  const std::string& fragSrc,
                                                  const char* debugLabel)
{
    if (vertSrc.empty() || fragSrc.empty())
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Shader source empty for {}", debugLabel);
        return 0;
    }

    int success = 0;
    char infoLog[512] = {};

    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    const char* vsPtr = vertSrc.c_str();
    glShaderSource(vs, 1, &vsPtr, nullptr);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vs, 512, nullptr, infoLog);
        Logger::ErrorF(LOG_SUBSYSTEM, "Vertex shader compile failed ({}): {}", debugLabel, infoLog);
        glDeleteShader(vs);
        return 0;
    }

    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fsPtr = fragSrc.c_str();
    glShaderSource(fs, 1, &fsPtr, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fs, 512, nullptr, infoLog);
        Logger::ErrorF(
            LOG_SUBSYSTEM, "Fragment shader compile failed ({}): {}", debugLabel, infoLog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(prog, 512, nullptr, infoLog);
        Logger::ErrorF(LOG_SUBSYSTEM, "Shader program link failed ({}): {}", debugLabel, infoLog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(prog);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

bool OpenGLRenderer::InitPostFXShaders()
{
    std::string postVert = LoadShaderFromFile("shaders/FullscreenTriangle.vert");
    std::string postFrag = LoadShaderFromFile("shaders/PostFXComposite.frag");
    std::string thresholdFrag = LoadShaderFromFile("shaders/BloomPrefilter.frag");
    std::string downFrag = LoadShaderFromFile("shaders/BloomDownsample.frag");
    std::string upFrag = LoadShaderFromFile("shaders/BloomUpsample.frag");

    m_PostProgram = CompileShaderProgram(postVert, postFrag, "PostFXComposite");
    m_BloomThresholdProgram = CompileShaderProgram(postVert, thresholdFrag, "BloomPrefilter");
    m_BloomDownProgram = CompileShaderProgram(postVert, downFrag, "BloomDownsample");
    m_BloomUpProgram = CompileShaderProgram(postVert, upFrag, "BloomUpsample");

    if (m_PostProgram == 0 || m_BloomThresholdProgram == 0 || m_BloomDownProgram == 0 ||
        m_BloomUpProgram == 0)
    {
        return false;
    }

    // Composite (PostFXComposite.frag) uniforms.
    m_PostULoc_Scene = glGetUniformLocation(m_PostProgram, "uScene");
    m_PostULoc_Bloom = glGetUniformLocation(m_PostProgram, "uBloom");
    m_PostULoc_BloomIntensity = glGetUniformLocation(m_PostProgram, "uBloomIntensity");
    m_PostULoc_Lift = glGetUniformLocation(m_PostProgram, "uLift");
    m_PostULoc_Gamma = glGetUniformLocation(m_PostProgram, "uGamma");
    m_PostULoc_Gain = glGetUniformLocation(m_PostProgram, "uGain");
    m_PostULoc_Saturation = glGetUniformLocation(m_PostProgram, "uSaturation");
    m_PostULoc_CAStrength = glGetUniformLocation(m_PostProgram, "uCAStrength");
    m_PostULoc_VignetteIntensity = glGetUniformLocation(m_PostProgram, "uVignetteIntensity");
    m_PostULoc_VignetteInnerR = glGetUniformLocation(m_PostProgram, "uVignetteInnerR");
    m_PostULoc_VignetteOuterR = glGetUniformLocation(m_PostProgram, "uVignetteOuterR");
    m_PostULoc_VignetteAspectY = glGetUniformLocation(m_PostProgram, "uVignetteAspectY");
    m_PostULoc_EdgeDesat = glGetUniformLocation(m_PostProgram, "uEdgeDesat");
    m_PostULoc_GrainIntensity = glGetUniformLocation(m_PostProgram, "uGrainIntensity");
    m_PostULoc_GrainChromaMix = glGetUniformLocation(m_PostProgram, "uGrainChromaMix");
    m_PostULoc_Time = glGetUniformLocation(m_PostProgram, "uTime");
    m_PostULoc_TonemapKnee = glGetUniformLocation(m_PostProgram, "uTonemapKnee");
    m_PostULoc_Enabled = glGetUniformLocation(m_PostProgram, "uPostFXEnabled");

    // Bloom prep uniforms.
    m_BloomThresholdULoc_Scene = glGetUniformLocation(m_BloomThresholdProgram, "uScene");
    m_BloomThresholdULoc_SatThreshold =
        glGetUniformLocation(m_BloomThresholdProgram, "uSatThreshold");

    m_BloomDownULoc_Input = glGetUniformLocation(m_BloomDownProgram, "uInput");
    m_BloomDownULoc_SrcTexelSize = glGetUniformLocation(m_BloomDownProgram, "uSrcTexelSize");

    m_BloomUpULoc_Input = glGetUniformLocation(m_BloomUpProgram, "uInput");
    m_BloomUpULoc_SrcTexelSize = glGetUniformLocation(m_BloomUpProgram, "uSrcTexelSize");

    glGenVertexArrays(1, &m_PostVAO);
    return true;
}

void OpenGLRenderer::EnsureSceneFramebuffer(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    if (m_SceneFBO != 0 && m_SceneFBOWidth == width && m_SceneFBOHeight == height)
    {
        return;
    }

    DestroySceneFramebuffer();

    glGenFramebuffers(1, &m_SceneFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFBO);

    glGenTextures(1, &m_SceneColorTex);
    glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneColorTex, 0);

    glGenRenderbuffers(1, &m_SceneDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_SceneDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_SceneDepthRBO);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Scene FBO incomplete: 0x{:x}", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_SceneFBOWidth = width;
    m_SceneFBOHeight = height;
}

void OpenGLRenderer::EnsureBloomFramebuffers(int width, int height)
{
    // Mip 0 is half scene resolution; each subsequent mip is half the previous.
    int mip0W = std::max(1, width / 2);
    int mip0H = std::max(1, height / 2);

    if (m_BloomMipFBO[0] != 0 && m_BloomMipWidth[0] == mip0W && m_BloomMipHeight[0] == mip0H)
    {
        return;
    }

    DestroyBloomFramebuffers();

    glGenFramebuffers(kBloomMipLevels, m_BloomMipFBO);
    glGenTextures(kBloomMipLevels, m_BloomMipTex);

    int w = mip0W;
    int h = mip0H;
    for (int i = 0; i < kBloomMipLevels; ++i)
    {
        m_BloomMipWidth[i] = w;
        m_BloomMipHeight[i] = h;

        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMipFBO[i]);
        glBindTexture(GL_TEXTURE_2D, m_BloomMipTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, nullptr);
        // Linear filtering is essential - the downsample/upsample shaders sample
        // at fractional offsets to combine multiple texels per fetch.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_BloomMipTex[i], 0);

        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void OpenGLRenderer::DestroySceneFramebuffer()
{
    if (m_SceneFBO != 0)
    {
        glDeleteFramebuffers(1, &m_SceneFBO);
        m_SceneFBO = 0;
    }
    if (m_SceneColorTex != 0)
    {
        glDeleteTextures(1, &m_SceneColorTex);
        m_SceneColorTex = 0;
    }
    if (m_SceneDepthRBO != 0)
    {
        glDeleteRenderbuffers(1, &m_SceneDepthRBO);
        m_SceneDepthRBO = 0;
    }
    m_SceneFBOWidth = 0;
    m_SceneFBOHeight = 0;
}

void OpenGLRenderer::DestroyBloomFramebuffers()
{
    if (m_BloomMipFBO[0] != 0)
    {
        glDeleteFramebuffers(kBloomMipLevels, m_BloomMipFBO);
    }
    if (m_BloomMipTex[0] != 0)
    {
        glDeleteTextures(kBloomMipLevels, m_BloomMipTex);
    }
    for (int i = 0; i < kBloomMipLevels; ++i)
    {
        m_BloomMipFBO[i] = 0;
        m_BloomMipTex[i] = 0;
        m_BloomMipWidth[i] = 0;
        m_BloomMipHeight[i] = 0;
    }
}

void OpenGLRenderer::BeginScene()
{
    if (m_ViewportWidth <= 0 || m_ViewportHeight <= 0)
    {
        // Game::Render hasn't called SetViewport yet - bail and keep rendering to
        // the swapchain. EndSceneApplyPostFX will short-circuit on m_SceneBound.
        return;
    }

    EnsureSceneFramebuffer(m_ViewportWidth, m_ViewportHeight);
    EnsureBloomFramebuffers(m_ViewportWidth, m_ViewportHeight);

    if (m_SceneFBO == 0)
    {
        return;
    }

    // Flush straggling batch state from the previous frame's UI before swapping FBOs.
    PrepFlushReason("BeginScene");
    FlushBatch();
    PrepFlushReason("BeginScene");
    FlushRectBatch();
    PrepFlushReason("BeginScene");
    FlushParticleBatch();

    glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFBO);
    glViewport(0, 0, m_SceneFBOWidth, m_SceneFBOHeight);
    m_SceneBound = true;
}

void OpenGLRenderer::RunBloomPrep()
{
    // Mip-chain bloom (COD/Siggraph 2014 architecture):
    //   1. Threshold pass: scene -> mip[0] (Karis soft filter)
    //   2. Downsample chain: mip[0] -> mip[1] -> ... -> mip[N-1]
    //   3. Upsample chain (additive): mip[N-1] += into mip[N-2] += into ... += into mip[0]
    //
    // The result is mip[0] with smooth multi-scale bloom: small mips contribute
    // tight bright halos near sources, large mips contribute wide soft halos.

    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_PostVAO);
    glActiveTexture(GL_TEXTURE0);

    // Step 1: threshold pass into mip[0].
    glDisable(GL_BLEND);
    glViewport(0, 0, m_BloomMipWidth[0], m_BloomMipHeight[0]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMipFBO[0]);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_BloomThresholdProgram);
    glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
    glUniform1i(m_BloomThresholdULoc_Scene, 0);
    glUniform1f(m_BloomThresholdULoc_SatThreshold, ambience::BLOOM_SATURATION_THRESHOLD);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Step 2: downsample chain. Each pass reads from the previous mip.
    glUseProgram(m_BloomDownProgram);
    glUniform1i(m_BloomDownULoc_Input, 0);
    for (int i = 1; i < kBloomMipLevels; ++i)
    {
        glViewport(0, 0, m_BloomMipWidth[i], m_BloomMipHeight[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMipFBO[i]);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, m_BloomMipTex[i - 1]);
        // Texel size of the SOURCE mip (the one we're sampling FROM).
        glUniform2f(m_BloomDownULoc_SrcTexelSize,
                    1.0f / static_cast<float>(m_BloomMipWidth[i - 1]),
                    1.0f / static_cast<float>(m_BloomMipHeight[i - 1]));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Step 3: upsample chain. Additively combine each mip into the next-finer one.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(m_BloomUpProgram);
    glUniform1i(m_BloomUpULoc_Input, 0);
    for (int i = kBloomMipLevels - 1; i > 0; --i)
    {
        glViewport(0, 0, m_BloomMipWidth[i - 1], m_BloomMipHeight[i - 1]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMipFBO[i - 1]);
        glBindTexture(GL_TEXTURE_2D, m_BloomMipTex[i]);
        glUniform2f(m_BloomUpULoc_SrcTexelSize,
                    1.0f / static_cast<float>(m_BloomMipWidth[i]),
                    1.0f / static_cast<float>(m_BloomMipHeight[i]));
        // Additive draw - BloomUpsample.frag's output is GL_ONE * src + GL_ONE * dst.
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Final bloom is in mip[0]. Restore non-additive blending.
    glDisable(GL_BLEND);
}

void OpenGLRenderer::EndSceneApplyPostFX(const PostFXParams& params)
{
    if (!m_SceneBound)
    {
        return;
    }

    // Ensure all world rendering inside the scene FBO has flushed before we
    // start sampling from the color attachment.
    PrepFlushReason("EndScene");
    FlushBatch();
    PrepFlushReason("EndScene");
    FlushRectBatch();
    PrepFlushReason("EndScene");
    FlushParticleBatch();

    RunBloomPrep();

    // Composite to the default framebuffer (swapchain).
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(m_PostVAO);
    glUseProgram(m_PostProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_SceneColorTex);
    glUniform1i(m_PostULoc_Scene, 0);

    // Final bloom result is in mip[0] after the upsample chain.
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_BloomMipTex[0]);
    glUniform1i(m_PostULoc_Bloom, 1);

    // Bloom + grading.
    glUniform1f(m_PostULoc_BloomIntensity, params.bloomIntensity);
    glUniform3f(m_PostULoc_Lift,
                params.gradingParams.lift.r,
                params.gradingParams.lift.g,
                params.gradingParams.lift.b);
    glUniform3f(m_PostULoc_Gamma,
                params.gradingParams.gamma.r,
                params.gradingParams.gamma.g,
                params.gradingParams.gamma.b);
    glUniform3f(m_PostULoc_Gain,
                params.gradingParams.gain.r,
                params.gradingParams.gain.g,
                params.gradingParams.gain.b);
    glUniform1f(m_PostULoc_Saturation, params.saturation);

    // Lens character.
    glUniform1f(m_PostULoc_CAStrength, ambience::CA_STRENGTH);
    glUniform1f(m_PostULoc_VignetteIntensity, params.vignetteIntensity);
    glUniform1f(m_PostULoc_VignetteInnerR, ambience::VIGNETTE_INNER_R);
    glUniform1f(m_PostULoc_VignetteOuterR, ambience::VIGNETTE_OUTER_R);
    glUniform1f(m_PostULoc_VignetteAspectY, ambience::VIGNETTE_ASPECT_Y_SCALE);
    glUniform1f(m_PostULoc_EdgeDesat, ambience::VIGNETTE_EDGE_DESAT);

    // Grain.
    glUniform1f(m_PostULoc_GrainIntensity, params.grainIntensity);
    glUniform1f(m_PostULoc_GrainChromaMix, ambience::GRAIN_CHROMA_MIX);
    glUniform1f(m_PostULoc_Time, params.time);

    // Tonemap.
    glUniform1f(m_PostULoc_TonemapKnee, ambience::TONEMAP_KNEE);

    // Master gate. When false the shader early-returns the raw scene.
    glUniform1i(m_PostULoc_Enabled, params.postFXEnabled ? 1 : 0);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Restore default state for subsequent UI rendering.
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(0);
    glUseProgram(0);

    m_SceneBound = false;
}

bool OpenGLRenderer::Init()
{
    // Order: geometry buffers, textures, then shaders.
    SetupQuad();
    CreateWhiteTexture();

#ifdef USE_FREETYPE
    // Project-configured fonts first, then defaults.
    std::vector<std::string> fontCandidates = m_FontCandidates;
    if (fontCandidates.empty())
    {
        fontCandidates.push_back("assets/fonts/c8ab67e0-519a-49b5-b693-e8fc86d08efa.ttf");
    }
#ifdef _WIN32
    fontCandidates.push_back("C:/Windows/Fonts/segoeui.ttf");  // Fallback
    fontCandidates.push_back("C:/Windows/Fonts/arial.ttf");    // Fallback
#endif

    bool fontLoaded = false;
    for (const auto& fontPath : fontCandidates)
    {
        if (!std::filesystem::exists(fontPath))
        {
            continue;
        }

        size_t beforeCount = m_Characters.size();
        LoadFont(fontPath);
        if (m_Characters.size() > beforeCount)
        {
            fontLoaded = true;
            break;
        }
    }

    if (!fontLoaded)
    {
        Logger::Warn(LOG_SUBSYSTEM, "No font could be loaded. Text rendering disabled.");
    }
#else
    Logger::Warn(LOG_SUBSYSTEM, "FreeType not available. Text rendering disabled.");
#endif

    std::string vertexShaderSource = LoadShaderFromFile("shaders/Geometry.vert");
    std::string fragmentShaderSource = LoadShaderFromFile("shaders/Geometry.frag");

    if (vertexShaderSource.empty() || fragmentShaderSource.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "Failed to load shader files!");
        return false;
    }

    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char* vertexSourcePtr = vertexShaderSource.c_str();
    glShaderSource(vertexShader, 1, &vertexSourcePtr, nullptr);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        Logger::ErrorF(LOG_SUBSYSTEM, "Vertex shader compilation failed: {}", infoLog);
        glDeleteShader(vertexShader);
        return false;
    }

    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fragmentSourcePtr = fragmentShaderSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSourcePtr, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        Logger::ErrorF(LOG_SUBSYSTEM, "Fragment shader compilation failed: {}", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    m_ShaderProgram = glCreateProgram();
    glAttachShader(m_ShaderProgram, vertexShader);
    glAttachShader(m_ShaderProgram, fragmentShader);
    glLinkProgram(m_ShaderProgram);

    glGetProgramiv(m_ShaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(m_ShaderProgram, 512, nullptr, infoLog);
        Logger::ErrorF(LOG_SUBSYSTEM, "Shader program linking failed: {}", infoLog);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        glDeleteProgram(m_ShaderProgram);
        m_ShaderProgram = 0;
        return false;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Cache uniform locations.
    m_ModelLoc = glGetUniformLocation(m_ShaderProgram, "model");
    m_ProjectionLoc = glGetUniformLocation(m_ShaderProgram, "projection");
    m_ColorLoc = glGetUniformLocation(m_ShaderProgram, "spriteColor");
    m_AlphaLoc = glGetUniformLocation(m_ShaderProgram, "spriteAlpha");
    m_AmbientColorLoc = glGetUniformLocation(m_ShaderProgram, "ambientColor");
    m_UseColorOnlyLoc = glGetUniformLocation(m_ShaderProgram, "useColorOnly");

    // Perspective state - all read in the vertex shader; gated on perspEnabled.
    m_PerspEnabledLoc = glGetUniformLocation(m_ShaderProgram, "perspEnabled");
    m_PerspHasGlobeLoc = glGetUniformLocation(m_ShaderProgram, "perspHasGlobe");
    m_PerspHasVanishingLoc = glGetUniformLocation(m_ShaderProgram, "perspHasVanishing");
    m_PerspHorizonYLoc = glGetUniformLocation(m_ShaderProgram, "perspHorizonY");
    m_PerspHorizonScaleLoc = glGetUniformLocation(m_ShaderProgram, "perspHorizonScale");
    m_PerspViewSizeLoc = glGetUniformLocation(m_ShaderProgram, "perspViewSize");
    m_PerspSphereRadiusLoc = glGetUniformLocation(m_ShaderProgram, "perspSphereRadius");

    // Program is bound; push initial perspective state (off by default).
    glUseProgram(m_ShaderProgram);
    PushPerspectiveUniforms();

    if (!InitPostFXShaders())
    {
        Logger::Warn(
            LOG_SUBSYSTEM,
            "Post-FX shaders failed to compile - rendering will skip the post-process pass");
    }

    // Populate RendererInfo for GetBackendInfo. Uses the caller's current GL
    // context; safe at end of Init.
    m_Info.backendName = "OpenGL";
    if (auto* s = reinterpret_cast<const char*>(glGetString(GL_VERSION)); s != nullptr)
    {
        m_Info.apiVersion = s;
    }
    if (auto* s = reinterpret_cast<const char*>(glGetString(GL_VENDOR)); s != nullptr)
    {
        m_Info.vendor = s;
    }
    if (auto* s = reinterpret_cast<const char*>(glGetString(GL_RENDERER)); s != nullptr)
    {
        m_Info.device = s;
    }
    GLint maxTex = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    m_Info.maxTextureSize = static_cast<int>(maxTex);

    m_Initialized = true;
    return true;
}

RendererInfo OpenGLRenderer::GetBackendInfo() const
{
    return m_Info;
}

void OpenGLRenderer::SetAmbientColor(const glm::vec3& color)
{
    m_AmbientColor = color;
}

void OpenGLRenderer::BeginFrame()
{
    // Suspension depth must net to zero across frames: any unbalanced
    // SuspendPerspective call inside the frame would leak depth and
    // gradually disable perspective entirely.
    assert(m_SuspensionDepth == 0 && "SuspendPerspective leaked depth across BeginFrame");

    m_BatchVertices.clear();
    m_CurrentBatchTexture = 0;
    m_RectBatchVertices.clear();
    m_ParticleBatchVertices.clear();
    m_CurrentParticleTexture = 0;
    m_TextBatchVertices.clear();
    m_CurrentTextAtlas = 0;
    m_DrawCallCount = 0;

    // Roll the draw-call trace: stash the just-finished frame in "last frame"
    // (renderer.trace dumps from there) and clear the live buffer.
    DrawTracer::BeginFrame();
}

void OpenGLRenderer::EndFrame()
{
    // Flush any remaining batched sprites, rects, particles, and text.
    PrepFlushReason("EndFrame");
    FlushBatch();
    PrepFlushReason("EndFrame");
    FlushRectBatch();
    PrepFlushReason("EndFrame");
    FlushParticleBatch();
    PrepFlushReason("EndFrame");
    FlushTextBatch();
}

void OpenGLRenderer::SetProjection(const glm::mat4& projection)
{
    // Flush pending batches first so world-space sprites don't get drawn with
    // UI projection (or vice versa). Text matters too: a HUD string queued
    // under one projection must not render with another.
    PrepFlushReason("SetProjection");
    FlushBatch();
    PrepFlushReason("SetProjection");
    FlushRectBatch();
    PrepFlushReason("SetProjection");
    FlushParticleBatch();
    PrepFlushReason("SetProjection");
    FlushTextBatch();
    m_Projection = projection;
}

void OpenGLRenderer::SetViewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
    // Track the size so BeginScene() can match it. Deferred so we don't
    // recreate FBO GL resources mid-frame.
    m_ViewportWidth = width;
    m_ViewportHeight = height;
}

void OpenGLRenderer::PushPerspectiveUniforms()
{
    if (m_PerspEnabledLoc < 0)
    {
        // Shader didn't compile or uniforms were optimized out; nothing to do.
        return;
    }

    const bool hasGlobe =
        (m_Persp.mode == ProjectionMode::Globe || m_Persp.mode == ProjectionMode::Fisheye);
    const bool hasVanishing =
        (m_Persp.mode == ProjectionMode::VanishingPoint || m_Persp.mode == ProjectionMode::Fisheye);

    glUniform1i(m_PerspEnabledLoc, m_Persp.enabled ? 1 : 0);
    glUniform1i(m_PerspHasGlobeLoc, hasGlobe ? 1 : 0);
    glUniform1i(m_PerspHasVanishingLoc, hasVanishing ? 1 : 0);
    glUniform1f(m_PerspHorizonYLoc, m_Persp.horizonY);
    glUniform1f(m_PerspHorizonScaleLoc, m_Persp.horizonScale);
    glUniform2f(m_PerspViewSizeLoc, m_Persp.viewWidth, m_Persp.viewHeight);
    glUniform2f(
        m_PerspSphereRadiusLoc,
        m_Persp.sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusXScale),
        m_Persp.sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusYScale));
}

void OpenGLRenderer::SetVanishingPointPerspective(
    bool enabled, float horizonY, float horizonScale, float viewWidth, float viewHeight)
{
    PrepFlushReason("SetVanishingPointPerspective");
    FlushBatch();
    PrepFlushReason("SetVanishingPointPerspective");
    FlushRectBatch();
    PrepFlushReason("SetVanishingPointPerspective");
    FlushParticleBatch();
    PrepFlushReason("SetVanishingPointPerspective");
    FlushTextBatch();
    IRenderer::SetVanishingPointPerspective(enabled, horizonY, horizonScale, viewWidth, viewHeight);
    // Flushes left the shader program bound, so uniform writes target it.
    glUseProgram(m_ShaderProgram);
    PushPerspectiveUniforms();
}

void OpenGLRenderer::SetGlobePerspective(bool enabled,
                                         float sphereRadius,
                                         float viewWidth,
                                         float viewHeight)
{
    PrepFlushReason("SetGlobePerspective");
    FlushBatch();
    PrepFlushReason("SetGlobePerspective");
    FlushRectBatch();
    PrepFlushReason("SetGlobePerspective");
    FlushParticleBatch();
    PrepFlushReason("SetGlobePerspective");
    FlushTextBatch();
    IRenderer::SetGlobePerspective(enabled, sphereRadius, viewWidth, viewHeight);
    glUseProgram(m_ShaderProgram);
    PushPerspectiveUniforms();
}

void OpenGLRenderer::SetFisheyePerspective(bool enabled,
                                           float sphereRadius,
                                           float horizonY,
                                           float horizonScale,
                                           float viewWidth,
                                           float viewHeight)
{
    PrepFlushReason("SetFisheyePerspective");
    FlushBatch();
    PrepFlushReason("SetFisheyePerspective");
    FlushRectBatch();
    PrepFlushReason("SetFisheyePerspective");
    FlushParticleBatch();
    PrepFlushReason("SetFisheyePerspective");
    FlushTextBatch();
    IRenderer::SetFisheyePerspective(
        enabled, sphereRadius, horizonY, horizonScale, viewWidth, viewHeight);
    glUseProgram(m_ShaderProgram);
    PushPerspectiveUniforms();
}

void OpenGLRenderer::Clear(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::UploadTexture(const Texture& texture)
{
    // Recreate only if the texture isn't from the active GL context generation.
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.GetID() == 0 || texture.GetOpenGLContextGeneration() != currentGen)
    {
        texture.RecreateOpenGLTexture();
    }
}

void OpenGLRenderer::SetupQuad()
{
    // Unit quad (0,0)-(1,1) for immediate-mode sprite rendering.
    // Each vertex: 4 floats - position (x,y) then UV (u,v).
    float vertices[] = {                          // pos      // tex
                        0.0f, 1.0f, 0.0f, 1.0f,   // Bottom-left
                        1.0f, 0.0f, 1.0f, 0.0f,   // Top-right
                        0.0f, 0.0f, 0.0f, 0.0f,   // Top-left
                        0.0f, 1.0f, 0.0f, 1.0f,   // Bottom-left (second triangle)
                        1.0f, 1.0f, 1.0f, 1.0f,   // Bottom-right
                        1.0f, 0.0f, 1.0f, 0.0f};  // Top-right

    unsigned int indices[] = {0,
                              1,
                              2,  // First triangle
                              3,
                              4,
                              5};  // Second triangle

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    // Static buffer - unit quad never changes.
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Vertex attribs: 0=pos(2f@0), 1=UV(2f@8), 2=color disabled here (only
    // colored rect / particle batches use it).
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    glBindVertexArray(0);

    // Text uses dynamic batching all characters in a DrawText call are
    // uploaded at once and drawn with two draw calls (outline + main text)
    glGenVertexArrays(1, &m_TextVAO);
    glGenBuffers(1, &m_TextVBO);

    glBindVertexArray(m_TextVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);

    // Pre-allocate buffer for text quads (6 vertices per character, dynamic for frequent updates)
    glBufferData(
        GL_ARRAY_BUFFER, MAX_TEXT_QUADS * 6 * sizeof(TextVertex), nullptr, GL_DYNAMIC_DRAW);

    // Text layout: position + texcoord + per-vertex RGBA + perspective flag.
    // Per-vertex color lets one DrawText call submit outline and foreground in
    // a single draw. Perspective flag is always 0 for text (UI).
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    m_TextBatchVertices.reserve(MAX_TEXT_QUADS * 6);

    // Sprite batch: sprites with the same texture collapse into one draw call.
    glGenVertexArrays(1, &m_BatchVAO);
    glGenBuffers(1, &m_BatchVBO);

    glBindVertexArray(m_BatchVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_BatchVBO);

    // Pre-allocate for max batch; dynamic since vertices change every frame.
    size_t batchBufferSize = MAX_BATCH_SPRITES * VERTICES_PER_SPRITE * sizeof(BatchVertex);
    glBufferData(GL_ARRAY_BUFFER, batchBufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Layout: pos(xy) + uv + perspective flag. Sprites use uniform color
    // (no per-vertex color, so attribute 2 is disabled).
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glVertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    // Rect + particle batch (per-vertex color for gradients, fading particles, lights).
    glGenVertexArrays(1, &m_RectBatchVAO);
    glGenBuffers(1, &m_RectBatchVBO);

    glBindVertexArray(m_RectBatchVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);

    size_t rectBatchBufferSize = MAX_BATCH_SPRITES * VERTICES_PER_SPRITE * sizeof(ColoredVertex);
    glBufferData(GL_ARRAY_BUFFER, rectBatchBufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Layout: pos(xy) + uv + color(rgba) + perspective flag.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        2, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        3, 1, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void*)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
}

void OpenGLRenderer::DrawSprite(
    const Texture& texture, glm::vec2 position, glm::vec2 size, float rotation, glm::vec3 color)
{
    DrawSpriteRegion(
        texture,
        position,
        size,
        glm::vec2(0.0f),
        glm::vec2(static_cast<float>(texture.GetWidth()), static_cast<float>(texture.GetHeight())),
        rotation,
        color,
        true,
        false,
        false);
}

void OpenGLRenderer::DrawSpriteRegion(const Texture& texture,
                                      glm::vec2 position,
                                      glm::vec2 size,
                                      glm::vec2 texCoord,
                                      glm::vec2 texSize,
                                      float rotation,
                                      glm::vec3 color,
                                      bool flipY,
                                      bool tileFlipX,
                                      bool tileFlipY)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[160];
        std::snprintf(buf,
                      sizeof(buf),
                      "draw spriteRegion tex=%dx%d pos=(%.1f,%.1f) size=(%.1f,%.1f) "
                      "uv=(%.0f,%.0f)+%.0fx%.0f rot=%.1f%s%s",
                      texture.GetWidth(),
                      texture.GetHeight(),
                      position.x,
                      position.y,
                      size.x,
                      size.y,
                      texCoord.x,
                      texCoord.y,
                      texSize.x,
                      texSize.y,
                      rotation,
                      tileFlipX ? " flipX" : "",
                      tileFlipY ? " flipY" : "");
        DrawTracer::Mark(buf, m_DrawCallCount);
    }

    // Switching from rect to sprite batch requires a flush first.
    if (!m_RectBatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(sprite<-rect)");
        FlushRectBatch();
    }

    unsigned int texID = EnsureTextureReady(texture);
    if (texID == 0)
        return;

    // Texture change forces a flush. Check batch contents (not
    // m_CurrentBatchTexture != 0) so an invalid texture ID (0) can't
    // contaminate subsequent sprites in the same batch.
    if (!m_BatchVertices.empty() && m_CurrentBatchTexture != texID)
    {
        PrepFlushReason("TextureChange");
        FlushBatch();
    }

    if (m_BatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        PrepFlushReason("BatchFull");
        FlushBatch();
    }

    m_CurrentBatchTexture = texID;

    // Guard division by zero.
    if (texture.GetWidth() == 0 || texture.GetHeight() == 0)
        return;

    // Pixel coords -> normalized UV (0..1).
    float texX = texCoord.x / texture.GetWidth();
    float texY = texCoord.y / texture.GetHeight();
    float texW = texSize.x / texture.GetWidth();
    float texH = texSize.y / texture.GetHeight();

    // Y-flip for OpenGL: GL origin is bottom-left, image data is top-left.
    float finalTexYTop, finalTexYBottom;
    if (flipY)
    {
        finalTexYTop = 1.0f - (texY + texH);
        finalTexYBottom = 1.0f - texY;
    }
    else
    {
        finalTexYTop = texY;
        finalTexYBottom = texY + texH;
    }

    // No texel offset needed for GL_NEAREST + pixel art.
    float u0 = texX;
    float u1 = texX + texW;
    float vTop = finalTexYTop;
    float vBottom = finalTexYBottom;

    // Per-tile mirror: swap UV before rotation so the composition is
    // flip-then-rotate (geometrically correct order for reflections).
    if (tileFlipX)
    {
        std::swap(u0, u1);
    }
    if (tileFlipY)
    {
        std::swap(vTop, vBottom);
    }

    // Local-space corners (origin at sprite top-left). Perspective is applied
    // in the vertex shader gated on `perspectiveFlag`; capture the suspension
    // state once and emit it for all 6 vertices of this quad.
    glm::vec2 corners[4] = {
        {0.0f, 0.0f},      // Top-left
        {size.x, 0.0f},    // Top-right
        {size.x, size.y},  // Bottom-right
        {0.0f, size.y}     // Bottom-left
    };

    RotateCorners(corners, size, rotation);

    for (int i = 0; i < 4; i++)
    {
        corners[i] += position;
    }

    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    // V flipped for OpenGL convention.
    glm::vec2 uvs[4] = {
        {u0, vBottom},  // Top-left corner uses bottom V
        {u1, vBottom},  // Top-right
        {u1, vTop},     // Bottom-right corner uses top V
        {u0, vTop}      // Bottom-left
    };

    // Two triangles, counter-clockwise winding.
    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, perspFlag});  // TL
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, perspFlag});  // BR
    m_BatchVertices.push_back({corners[3].x, corners[3].y, uvs[3].x, uvs[3].y, perspFlag});  // BL

    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, perspFlag});  // TL
    m_BatchVertices.push_back({corners[1].x, corners[1].y, uvs[1].x, uvs[1].y, perspFlag});  // TR
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, perspFlag});  // BR
}

void OpenGLRenderer::DrawSpriteAlpha(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    DrawSpriteAtlas(
        texture, position, size, glm::vec2(0.0f), glm::vec2(1.0f), rotation, color, additive);
}

void OpenGLRenderer::DrawSpriteAtlas(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec2 uvMin,
                                     glm::vec2 uvMax,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[176];
        std::snprintf(buf,
                      sizeof(buf),
                      "draw spriteAtlas tex=%dx%d pos=(%.1f,%.1f) size=(%.1f,%.1f) "
                      "uv=[%.2f,%.2f]-[%.2f,%.2f] rgba=(%.2f,%.2f,%.2f,%.2f) %s",
                      texture.GetWidth(),
                      texture.GetHeight(),
                      position.x,
                      position.y,
                      size.x,
                      size.y,
                      uvMin.x,
                      uvMin.y,
                      uvMax.x,
                      uvMax.y,
                      color.r,
                      color.g,
                      color.b,
                      color.a,
                      additive ? "additive" : "alpha");
        DrawTracer::Mark(buf, m_DrawCallCount);
    }

    // Atlas variant of DrawSpriteAlpha - custom UV instead of full texture.

    // Flush other batch types before switching to the particle batch.
    if (!m_BatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(particle<-sprite)");
        FlushBatch();
    }
    if (!m_RectBatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(particle<-rect)");
        FlushRectBatch();
    }

    unsigned int texID = EnsureTextureReady(texture);
    if (texID == 0)
        return;

    // Flush if texture or blend mode changed. Same rule as sprite batch:
    // once vertices exist, a texture mismatch must flush even if the previous
    // texture ID was 0.
    if (!m_ParticleBatchVertices.empty() &&
        (m_CurrentParticleTexture != texID || m_ParticleBatchAdditive != additive))
    {
        PrepFlushReason((m_CurrentParticleTexture != texID) ? "TextureChange" : "BlendChange");
        FlushParticleBatch();
    }

    if (m_ParticleBatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        PrepFlushReason("BatchFull");
        FlushParticleBatch();
    }

    m_CurrentParticleTexture = texID;
    m_ParticleBatchAdditive = additive;

    float u0 = uvMin.x, u1 = uvMax.x;
    float v0 = uvMin.y, v1 = uvMax.y;

    glm::vec2 corners[4] = {{0.0f, 0.0f}, {size.x, 0.0f}, {size.x, size.y}, {0.0f, size.y}};

    RotateCorners(corners, size, rotation);

    for (int i = 0; i < 4; i++)
    {
        corners[i] += position;
    }

    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    // UV with OpenGL Y-flip.
    glm::vec2 uvs[4] = {
        {u0, v1},  // Top-left
        {u1, v1},  // Top-right
        {u1, v0},  // Bottom-right
        {u0, v0}   // Bottom-left
    };

    // Two triangles with per-vertex color.
    float r = color.r, g = color.g, b = color.b, a = color.a;
    m_ParticleBatchVertices.push_back(
        {corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, r, g, b, a, perspFlag});
    m_ParticleBatchVertices.push_back(
        {corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, r, g, b, a, perspFlag});
    m_ParticleBatchVertices.push_back(
        {corners[3].x, corners[3].y, uvs[3].x, uvs[3].y, r, g, b, a, perspFlag});

    m_ParticleBatchVertices.push_back(
        {corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, r, g, b, a, perspFlag});
    m_ParticleBatchVertices.push_back(
        {corners[1].x, corners[1].y, uvs[1].x, uvs[1].y, r, g, b, a, perspFlag});
    m_ParticleBatchVertices.push_back(
        {corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, r, g, b, a, perspFlag});
}

void OpenGLRenderer::FlushBatch()
{
    if (m_BatchVertices.empty())
    {
        return;
    }

    // Drain pending text first so anything queued before the current sprites
    // renders underneath them, preserving the call-site insertion order.
    // (Done after the empty check so DrawText's drain-sprite call doesn't
    // mutually flush the text it is about to accumulate.)
    if (!m_TextBatchVertices.empty())
    {
        const char* savedReason = m_PendingFlushReason;
        FlushTextBatch();
        m_PendingFlushReason = savedReason;
    }

    // Texture ID 0 is invalid for sprite sampling in core profile. Drop the
    // batch rather than accidentally sampling a previously bound texture.
    if (m_CurrentBatchTexture == 0 || !m_Initialized)
    {
        m_BatchVertices.clear();
        m_CurrentBatchTexture = 0;
        return;
    }

    glUseProgram(m_ShaderProgram);

    // Every Flush* owns its GL state; set everything at entry instead of
    // relying on the previous flush to have restored defaults (its error-return
    // paths may have skipped the restore).
    if (m_UseColorOnlyLoc >= 0)
        glUniform1i(m_UseColorOnlyLoc, 0);  // mode 0: sample from bound texture
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Vertices are pre-transformed, so the model matrix is identity.
    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));
    glUniform3f(m_ColorLoc, 1.0f, 1.0f, 1.0f);  // No tint.
    glUniform1f(m_AlphaLoc, 1.0f);              // Full opacity.
    glUniform3f(m_AmbientColorLoc, m_AmbientColor.r, m_AmbientColor.g, m_AmbientColor.b);

    // Buffer orphaning: GL_MAP_INVALIDATE_BUFFER_BIT tells the driver we
    // don't need the old data, so it can allocate new storage and skip the
    // GPU/CPU sync stall.
    size_t dataSize = m_BatchVertices.size() * sizeof(BatchVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_BatchVBO);
    void* ptr = glMapBufferRange(
        GL_ARRAY_BUFFER, 0, dataSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_BatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    else
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "glMapBufferRange returned null in FlushBatch ({} vertices, {} bytes)",
                       m_BatchVertices.size(),
                       dataSize);
        m_BatchVertices.clear();
        m_CurrentBatchTexture = 0;
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_CurrentBatchTexture);

    // Single draw call for all sprites in the batch.
    glBindVertexArray(m_BatchVAO);
    const size_t spriteVertCount = m_BatchVertices.size();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(spriteVertCount));
    DebugAfterDraw("SpriteBatch", static_cast<int>(spriteVertCount));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    if (DrawTracer::IsEnabled())
    {
        const char* reason = m_PendingFlushReason ? m_PendingFlushReason : "explicit";
        char buf[96];
        std::snprintf(buf,
                      sizeof(buf),
                      "flush sprite (%s) %zu verts tex=%u",
                      reason,
                      spriteVertCount,
                      m_CurrentBatchTexture);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    m_PendingFlushReason = nullptr;

    // Clear texture so the next batch must rebind (prevents stale state).
    m_BatchVertices.clear();
    m_CurrentBatchTexture = 0;
}

void OpenGLRenderer::CreateWhiteTexture()
{
    // 1x1 white texture for colored rects - bind this, and the per-vertex /
    // uniform color drives the final output.
    glGenTextures(1, &m_WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);

    unsigned char whitePixel[] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);

    // Clamp + nearest to prevent border artifacts.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DrawColoredRect(glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec4 color,
                                     bool additive)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[128];
        std::snprintf(buf,
                      sizeof(buf),
                      "draw rect pos=(%.1f,%.1f) size=(%.1f,%.1f) "
                      "rgba=(%.2f,%.2f,%.2f,%.2f) %s",
                      position.x,
                      position.y,
                      size.x,
                      size.y,
                      color.r,
                      color.g,
                      color.b,
                      color.a,
                      additive ? "additive" : "alpha");
        DrawTracer::Mark(buf, m_DrawCallCount);
    }

    // Switching from sprite to rect mode requires a flush first.
    if (!m_BatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(rect<-sprite)");
        FlushBatch();
    }

    if (!m_RectBatchVertices.empty() && m_RectBatchAdditive != additive)
    {
        PrepFlushReason("BlendChange");
        FlushRectBatch();
    }
    m_RectBatchAdditive = additive;

    if (m_RectBatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        PrepFlushReason("BatchFull");
        FlushRectBatch();
    }

    // No rotation for rects.
    glm::vec2 corners[4] = {
        position,                                    // Top-left
        {position.x + size.x, position.y},           // Top-right
        {position.x + size.x, position.y + size.y},  // Bottom-right
        {position.x, position.y + size.y}            // Bottom-left
    };

    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    // Two triangles with per-vertex color.
    float r = color.r, g = color.g, b = color.b, a = color.a;
    m_RectBatchVertices.push_back({corners[0].x, corners[0].y, 0.0f, 0.0f, r, g, b, a, perspFlag});
    m_RectBatchVertices.push_back({corners[2].x, corners[2].y, 1.0f, 1.0f, r, g, b, a, perspFlag});
    m_RectBatchVertices.push_back({corners[3].x, corners[3].y, 0.0f, 1.0f, r, g, b, a, perspFlag});

    m_RectBatchVertices.push_back({corners[0].x, corners[0].y, 0.0f, 0.0f, r, g, b, a, perspFlag});
    m_RectBatchVertices.push_back({corners[1].x, corners[1].y, 1.0f, 0.0f, r, g, b, a, perspFlag});
    m_RectBatchVertices.push_back({corners[2].x, corners[2].y, 1.0f, 1.0f, r, g, b, a, perspFlag});
}

void OpenGLRenderer::DrawWarpedQuad(const Texture& texture,
                                    const glm::vec2 corners[4],
                                    glm::vec2 texCoord,
                                    glm::vec2 texSize,
                                    glm::vec3 color,
                                    bool flipY,
                                    bool tileFlipX,
                                    bool tileFlipY)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[176];
        std::snprintf(buf,
                      sizeof(buf),
                      "draw warpedQuad tex=%dx%d corners=[(%.1f,%.1f),(%.1f,%.1f),"
                      "(%.1f,%.1f),(%.1f,%.1f)]",
                      texture.GetWidth(),
                      texture.GetHeight(),
                      corners[0].x,
                      corners[0].y,
                      corners[1].x,
                      corners[1].y,
                      corners[2].x,
                      corners[2].y,
                      corners[3].x,
                      corners[3].y);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }

    // Warped quads are pre-transformed by the caller (sphere projection already
    // applied); add directly to the sprite batch with no further perspective.

    if (!m_RectBatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(warpedQuad<-rect)");
        FlushRectBatch();
    }
    if (!m_ParticleBatchVertices.empty())
    {
        PrepFlushReason("BatchTypeChange(warpedQuad<-particle)");
        FlushParticleBatch();
    }

    unsigned int texID = EnsureTextureReady(texture);
    if (texID == 0)
        return;

    // Flush on texture change. Don't special-case texture 0 - stale/invalid
    // IDs would otherwise leak into the batch.
    if (!m_BatchVertices.empty() && m_CurrentBatchTexture != texID)
    {
        PrepFlushReason("TextureChange(warpedQuad)");
        FlushBatch();
    }

    if (m_BatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        PrepFlushReason("BatchFull(warpedQuad)");
        FlushBatch();
    }

    m_CurrentBatchTexture = texID;

    float texW = static_cast<float>(texture.GetWidth());
    float texH = static_cast<float>(texture.GetHeight());

    float u0 = texCoord.x / texW;
    float u1 = (texCoord.x + texSize.x) / texW;

    float v0, v1;
    if (flipY)
    {
        // GL Y-flip for textures loaded with stb_image.
        float finalTexYTop = texH - texCoord.y;
        float finalTexYBottom = texH - (texCoord.y + texSize.y);
        v0 = finalTexYBottom / texH;  // Top vertex uses bottom V
        v1 = finalTexYTop / texH;     // Bottom vertex uses top V
    }
    else
    {
        v0 = texCoord.y / texH;
        v1 = (texCoord.y + texSize.y) / texH;
    }

    // Per-tile content mirror via UV swap (the warped corners[] geometry stays put).
    if (tileFlipX)
    {
        std::swap(u0, u1);
    }
    if (tileFlipY)
    {
        std::swap(v0, v1);
    }

    // Map UV coordinates to corners: [TL, TR, BR, BL]
    // TL (screen top) -> texture top, BR (screen bottom) -> texture bottom
    // With flipY, v0=visual top, v1=visual bottom, so:
    // TL/TR (top of quad) -> v1 (bottom of texture = visual top after flip)
    // BL/BR (bottom of quad) -> v0 (top of texture = visual bottom after flip)
    glm::vec2 uvs[4] = {
        {u0, v1},  // TL - top of quad gets visual top of texture
        {u1, v1},  // TR
        {u1, v0},  // BR - bottom of quad gets visual bottom of texture
        {u0, v0}   // BL
    };

    // Warped quad corners are already projected by the caller (e.g.,
    // Tilemap::ComputeBuildingVertex for sphere-conforming buildings). Emit
    // perspFlag = 0 so the vertex shader leaves them alone.
    constexpr float kWarpedFlag = 0.0f;

    // Two triangles: (TL, BR, BL) and (TL, TR, BR).
    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, kWarpedFlag});
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, kWarpedFlag});
    m_BatchVertices.push_back({corners[3].x, corners[3].y, uvs[3].x, uvs[3].y, kWarpedFlag});

    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, kWarpedFlag});
    m_BatchVertices.push_back({corners[1].x, corners[1].y, uvs[1].x, uvs[1].y, kWarpedFlag});
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, kWarpedFlag});
}

void OpenGLRenderer::FlushRectBatch()
{
    if (m_RectBatchVertices.empty() || !m_Initialized)
    {
        m_RectBatchVertices.clear();
        return;
    }

    // Drain pending text first so anything queued before these rects renders
    // underneath them, preserving the call-site insertion order.
    // (Done after the empty check so DrawText's drain-rect call doesn't
    // mutually flush the text it is about to accumulate.)
    if (!m_TextBatchVertices.empty())
    {
        const char* savedReason = m_PendingFlushReason;
        FlushTextBatch();
        m_PendingFlushReason = savedReason;
    }

    glUseProgram(m_ShaderProgram);

    // Set blend func unconditionally (every Flush* owns its GL state).
    // Additive: dest = src*alpha + dest. Standard: src*alpha + dest*(1-alpha).
    if (m_RectBatchAdditive)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // useColorOnly modes: 0=texture, 1=uniform, 2=per-vertex, 3=texture*per-vertex.
    if (m_UseColorOnlyLoc >= 0)
        glUniform1i(m_UseColorOnlyLoc, 2);

    // Buffer orphaning to avoid GPU sync stall.
    size_t dataSize = m_RectBatchVertices.size() * sizeof(ColoredVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);
    void* ptr = glMapBufferRange(
        GL_ARRAY_BUFFER, 0, dataSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_RectBatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    else
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "glMapBufferRange returned null in FlushRectBatch ({} vertices, {} bytes)",
                       m_RectBatchVertices.size(),
                       dataSize);
        m_RectBatchVertices.clear();
        return;
    }

    // White texture as placeholder; shader ignores it in vertex-color mode.
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);

    glBindVertexArray(m_RectBatchVAO);
    const size_t rectVertCount = m_RectBatchVertices.size();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rectVertCount));
    DebugAfterDraw("RectBatch", static_cast<int>(rectVertCount));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    if (DrawTracer::IsEnabled())
    {
        const char* reason = m_PendingFlushReason ? m_PendingFlushReason : "explicit";
        char buf[80];
        std::snprintf(buf, sizeof(buf), "flush rect (%s) %zu verts", reason, rectVertCount);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    m_PendingFlushReason = nullptr;

    // No state restoration needed; the next Flush* sets what it wants at entry.
    m_RectBatchVertices.clear();
}

void OpenGLRenderer::FlushParticleBatch()
{
    // Particles are batched separately because they use per-vertex color/alpha
    // for fades, color variation, and glow intensity.
    if (m_ParticleBatchVertices.empty())
    {
        return;
    }

    // Drain pending text first so anything queued before these particles
    // renders underneath them, preserving the call-site insertion order.
    // (Done after the empty check so DrawText's drain-particle call doesn't
    // mutually flush the text it is about to accumulate.)
    if (!m_TextBatchVertices.empty())
    {
        const char* savedReason = m_PendingFlushReason;
        FlushTextBatch();
        m_PendingFlushReason = savedReason;
    }

    if (m_CurrentParticleTexture == 0 || !m_Initialized)
    {
        m_ParticleBatchVertices.clear();
        m_CurrentParticleTexture = 0;
        return;
    }

    glUseProgram(m_ShaderProgram);

    // Every Flush* owns its GL state; set blend func unconditionally.
    if (m_ParticleBatchAdditive)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // Mode 3: texture * per-vertex color - particles tint/fade individually
    // while sharing one texture.
    if (m_UseColorOnlyLoc >= 0)
        glUniform1i(m_UseColorOnlyLoc, 3);

    // Reuse the rect batch VBO (same vertex layout).
    size_t dataSize = m_ParticleBatchVertices.size() * sizeof(ColoredVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);
    void* ptr = glMapBufferRange(
        GL_ARRAY_BUFFER, 0, dataSize, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_ParticleBatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    else
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM,
            "glMapBufferRange returned null in FlushParticleBatch ({} vertices, {} bytes)",
            m_ParticleBatchVertices.size(),
            dataSize);
        m_ParticleBatchVertices.clear();
        m_CurrentParticleTexture = 0;
        return;
    }

    // All particles in this batch share one texture (e.g., soft glow circle).
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_CurrentParticleTexture);

    glBindVertexArray(m_RectBatchVAO);
    const size_t particleVertCount = m_ParticleBatchVertices.size();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(particleVertCount));
    DebugAfterDraw("ParticleBatch", static_cast<int>(particleVertCount));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    if (DrawTracer::IsEnabled())
    {
        const char* reason = m_PendingFlushReason ? m_PendingFlushReason : "explicit";
        char buf[112];
        std::snprintf(buf,
                      sizeof(buf),
                      "flush particle (%s) %zu verts tex=%u %s",
                      reason,
                      particleVertCount,
                      m_CurrentParticleTexture,
                      m_ParticleBatchAdditive ? "additive" : "alpha");
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    m_PendingFlushReason = nullptr;

    // No state restoration needed; the next Flush* sets what it wants at entry.
    m_ParticleBatchVertices.clear();
    m_CurrentParticleTexture = 0;
}

void OpenGLRenderer::LoadFont(const std::string& fontPath)
{
#ifdef USE_FREETYPE
    if (FT_Init_FreeType(&m_FreeType))
    {
        Logger::Error(LOG_SUBSYSTEM, "FREETYPE: Could not init FreeType Library");
        return;
    }

    if (FT_New_Face(m_FreeType, fontPath.c_str(), 0, &m_Face))
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "FREETYPE: Failed to load font: {}", fontPath);
        FT_Done_FreeType(m_FreeType);
        m_FreeType = nullptr;
        return;
    }

    // Two atlases from the same face, both baked at 96 px. The body atlas is
    // normalized to a logical 24-px size (BODY_METRIC_NORM) so small text stays
    // crisp; the headline atlas stays logical 96 for the large title logo.
    BuildAtlasInto(BODY_FONT_PIXEL_SIZE,
                   m_Characters,
                   m_FontAtlasTexture,
                   m_FontAtlasWidth,
                   m_FontAtlasHeight);
    BuildAtlasInto(HEADLINE_FONT_PIXEL_SIZE,
                   m_HeadlineCharacters,
                   m_HeadlineFontAtlasTexture,
                   m_HeadlineFontAtlasWidth,
                   m_HeadlineFontAtlasHeight);

    Logger::InfoF(LOG_SUBSYSTEM,
                  "Loaded font: {} (body atlas {}x{}, headline atlas {}x{})",
                  fontPath,
                  m_FontAtlasWidth,
                  m_FontAtlasHeight,
                  m_HeadlineFontAtlasWidth,
                  m_HeadlineFontAtlasHeight);
#else
    Logger::Error(LOG_SUBSYSTEM, "LoadFont called but FreeType is not available!");
#endif
}

void OpenGLRenderer::BuildAtlasInto(int pixelSize,
                                    std::map<char, Character>& outChars,
                                    unsigned int& outTexture,
                                    int& outWidth,
                                    int& outHeight)
{
#ifdef USE_FREETYPE
    if (!m_Face)
    {
        Logger::Error(LOG_SUBSYSTEM, "BuildAtlasInto: no FreeType face loaded");
        return;
    }

    outChars.clear();
    if (outTexture != 0)
    {
        glDeleteTextures(1, &outTexture);
        outTexture = 0;
    }

    FT_Set_Pixel_Sizes(m_Face, 0, pixelSize);

    // FreeType emits 8-bit grayscale; disable 4-byte row alignment.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Two-pass packing: pass 1 measures so we can allocate the exact atlas size,
    // pass 2 copies pixels. Left-to-right with row wrap.
    int atlasWidth = 0;
    int atlasHeight = 0;
    int rowHeight = 0;
    int currentX = 0;
    // Wider row limit for large atlases (96-px glyphs at 512px wide is ~17 rows).
    const int ATLAS_MAX_WIDTH = (pixelSize >= 64) ? 2048 : 512;
    // Gap between glyphs. Enlarged (was 2) so mipmap minification doesn't bleed
    // neighboring glyphs together at the small on-screen sizes (version footer,
    // debug HUD) that now minify from the supersampled atlas.
    const int PADDING = 8;

    // Cache bitmaps + metrics - FreeType reuses its internal buffer per glyph.
    struct GlyphData
    {
        std::vector<unsigned char> bitmap;
        int width, height;
        int bearingX, bearingY;
        unsigned int advance;
    };
    std::map<char, GlyphData> glyphData;

    // Pass 1: rasterize each ASCII glyph, cache it, and simulate atlas packing
    // to find the final atlas dimensions.
    for (unsigned char c = 0; c < 128; c++)
    {
        if (FT_Load_Char(m_Face, c, FT_LOAD_RENDER))
        {
            continue;  // Skip glyphs that fail to load.
        }

        int w = m_Face->glyph->bitmap.width;
        int h = m_Face->glyph->bitmap.rows;

        // Bearing: cursor -> glyph top-left. Advance: cursor step per glyph.
        GlyphData gd;
        gd.width = w;
        gd.height = h;
        gd.bearingX = m_Face->glyph->bitmap_left;
        gd.bearingY = m_Face->glyph->bitmap_top;
        gd.advance = static_cast<unsigned int>(m_Face->glyph->advance.x);

        // Must copy - FreeType reuses the buffer for the next glyph.
        if (w > 0 && h > 0)
        {
            gd.bitmap.assign(m_Face->glyph->bitmap.buffer, m_Face->glyph->bitmap.buffer + w * h);
        }
        glyphData[c] = gd;

        if (currentX + w + PADDING > ATLAS_MAX_WIDTH)
        {
            atlasHeight += rowHeight + PADDING;
            currentX = 0;
            rowHeight = 0;
        }

        currentX += w + PADDING;
        if (h > rowHeight)
            rowHeight = h;
        if (currentX > atlasWidth)
            atlasWidth = currentX;
    }
    atlasHeight += rowHeight;  // Include the final row.

    // Round up to power of 2 (some drivers require it).
    auto nextPow2 = [](int v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    };
    atlasWidth = nextPow2(atlasWidth);
    atlasHeight = nextPow2(atlasHeight);

    outWidth = atlasWidth;
    outHeight = atlasHeight;

    // RGBA: white color + alpha from glyph grayscale (enables color tint via uniform).
    std::vector<unsigned char> atlasData(atlasWidth * atlasHeight * 4, 0);

    currentX = 0;
    int currentY = 0;
    rowHeight = 0;

    // Pass 2: place each glyph in the atlas (same packing logic as pass 1) and record UVs.
    for (unsigned char c = 0; c < 128; c++)
    {
        auto it = glyphData.find(c);
        if (it == glyphData.end())
            continue;

        GlyphData& gd = it->second;
        int w = gd.width;
        int h = gd.height;

        if (currentX + w + PADDING > ATLAS_MAX_WIDTH)
        {
            currentY += rowHeight + PADDING;
            currentX = 0;
            rowHeight = 0;
        }

        if (!gd.bitmap.empty() && w > 0 && h > 0)
        {
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int atlasIdx = ((currentY + y) * atlasWidth + (currentX + x)) * 4;
                    unsigned char value = gd.bitmap[y * w + x];
                    atlasData[atlasIdx + 0] = 255;    // R (white)
                    atlasData[atlasIdx + 1] = 255;    // G (white)
                    atlasData[atlasIdx + 2] = 255;    // B (white)
                    atlasData[atlasIdx + 3] = value;  // A (glyph coverage)
                }
            }
        }

        float u0 = static_cast<float>(currentX) / atlasWidth;
        float v0 = static_cast<float>(currentY) / atlasHeight;
        float u1 = static_cast<float>(currentX + w) / atlasWidth;
        float v1 = static_cast<float>(currentY + h) / atlasHeight;

        Character character = {
            glm::ivec2(w, h), glm::ivec2(gd.bearingX, gd.bearingY), gd.advance, u0, v0, u1, v1};
        outChars.insert(std::pair<char, Character>(c, character));

        currentX += w + PADDING;
        if (h > rowHeight)
        {
            rowHeight = h;
        }
    }

    glGenTextures(1, &outTexture);
    glBindTexture(GL_TEXTURE_2D, outTexture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 atlasWidth,
                 atlasHeight,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 atlasData.data());

    // Mipmaps so text that minifies from the supersampled atlas (small UI text)
    // stays clean: trilinear on minify, linear on magnify. glGenerateMipmap runs
    // after the base-level glTexImage2D upload above.
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Restore default alignment for subsequent uploads.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#else
    (void)pixelSize;
    (void)outChars;
    (void)outTexture;
    (void)outWidth;
    (void)outHeight;
    Logger::Error(LOG_SUBSYSTEM, "BuildAtlasInto called but FreeType is not available!");
#endif
}

float OpenGLRenderer::GetTextAscent(float scale) const
{
    // Max bearing.y across loaded characters; fall back to font size if empty.
    int maxAscent = 0;
    for (const auto& pair : m_Characters)
    {
        if (pair.second.Bearing.y > maxAscent)
        {
            maxAscent = pair.second.Bearing.y;
        }
    }
    if (maxAscent == 0)
    {
        maxAscent = BODY_FONT_PIXEL_SIZE;  // Physical fallback; normalized below.
    }
    return static_cast<float>(maxAscent) * scale * BODY_METRIC_NORM;
}

float OpenGLRenderer::GetTextWidthImpl(const std::string& text,
                                       float scale,
                                       float metricNorm,
                                       const std::map<char, Character>& chars) const
{
    if (chars.empty() || text.empty())
    {
        return 0.0f;
    }

    float width = 0.0f;
    for (char c : text)
    {
        auto it = chars.find(c);
        if (it != chars.end())
        {
            // Advance is in 1/64th pixels (FreeType convention)
            width += (it->second.Advance >> 6) * scale * metricNorm;
        }
    }
    return width;
}

float OpenGLRenderer::GetTextWidth(const std::string& text, float scale) const
{
    return GetTextWidthImpl(text, scale, BODY_METRIC_NORM, m_Characters);
}

float OpenGLRenderer::GetTextWidthLarge(const std::string& text, float scale) const
{
    return GetTextWidthImpl(text, scale, HEADLINE_METRIC_NORM, m_HeadlineCharacters);
}

void OpenGLRenderer::DrawText(const std::string& text,
                              glm::vec2 position,
                              float scale,
                              glm::vec3 color,
                              float outlineSize,
                              float alpha)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[160];
        const std::string preview = text.size() > 32 ? text.substr(0, 32) + "..." : text;
        std::snprintf(buf,
                      sizeof(buf),
                      "draw text \"%s\" pos=(%.1f,%.1f) scale=%.2f outline=%.1f alpha=%.2f",
                      preview.c_str(),
                      position.x,
                      position.y,
                      scale,
                      outlineSize,
                      alpha);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    DrawTextImpl(text,
                 position,
                 scale,
                 color,
                 outlineSize,
                 alpha,
                 BODY_METRIC_NORM,
                 m_Characters,
                 m_FontAtlasTexture);
}

void OpenGLRenderer::DrawTextLarge(const std::string& text,
                                   glm::vec2 position,
                                   float scale,
                                   glm::vec3 color,
                                   float outlineSize,
                                   float alpha)
{
    if (DrawTracer::IsEnabled())
    {
        char buf[160];
        const std::string preview = text.size() > 32 ? text.substr(0, 32) + "..." : text;
        std::snprintf(buf,
                      sizeof(buf),
                      "draw textLarge \"%s\" pos=(%.1f,%.1f) scale=%.2f outline=%.1f",
                      preview.c_str(),
                      position.x,
                      position.y,
                      scale,
                      outlineSize);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    if (m_HeadlineCharacters.empty() || m_HeadlineFontAtlasTexture == 0)
    {
        // No headline atlas - fall back to scaled body atlas (blurry but visible).
        IRenderer::DrawTextLarge(text, position, scale, color, outlineSize, alpha);
        return;
    }
    DrawTextImpl(text,
                 position,
                 scale,
                 color,
                 outlineSize,
                 alpha,
                 HEADLINE_METRIC_NORM,
                 m_HeadlineCharacters,
                 m_HeadlineFontAtlasTexture);
}

void OpenGLRenderer::DrawTextImpl(const std::string& text,
                                  glm::vec2 position,
                                  float scale,
                                  glm::vec3 color,
                                  float outlineSize,
                                  float alpha,
                                  float metricNorm,
                                  const std::map<char, Character>& chars,
                                  unsigned int atlasTexture)
{
    if (chars.empty() || atlasTexture == 0)
    {
        Logger::Error(LOG_SUBSYSTEM, "DrawText: No font atlas loaded!");
        return;
    }

    if (text.empty())
    {
        return;
    }

    // Different atlas (body vs headline) than the currently batched text -
    // flush before switching, since FlushTextBatch binds one atlas per draw.
    if (!m_TextBatchVertices.empty() && m_CurrentTextAtlas != atlasTexture)
    {
        PrepFlushReason("AtlasChange");
        FlushTextBatch();
    }

    // Sprite/rect/particle batches use the same shader program with different
    // uniforms (modes 0/1/2) than text (mode 3). Drain them before the text
    // pass so their geometry renders with sprite state, not text state.
    PrepFlushReason("DrawText");
    FlushBatch();
    PrepFlushReason("DrawText");
    FlushRectBatch();
    PrepFlushReason("DrawText");
    FlushParticleBatch();

    m_CurrentTextAtlas = atlasTexture;

    // Take line height from the first printable character.
    float lineHeight = 24.0f;
    for (auto c : text)
    {
        if (c != '\n')
        {
            auto it = chars.find(c);
            if (it != chars.end())
            {
                lineHeight = static_cast<float>(it->second.Size.y);
                break;
            }
        }
    }

    float outlineOffset = 2.0f * scale * outlineSize;

    // Glyph geometry is in physical atlas texels; normalize so on-screen size is
    // independent of the bake resolution. (Outline offset above stays raw `scale`:
    // it is a screen-space offset, so it must not change with atlas resolution.)
    const float gscale = scale * metricNorm;

    const size_t maxTextVertices = MAX_TEXT_QUADS * 6;

    auto addCharQuad = [this, maxTextVertices](float xpos,
                                               float ypos,
                                               float w,
                                               float h,
                                               float u0,
                                               float v0,
                                               float u1,
                                               float v1,
                                               float r,
                                               float g,
                                               float b,
                                               float a)
    {
        // Don't overflow the pre-allocated text VBO.
        if (m_TextBatchVertices.size() + 6 > maxTextVertices)
            return;

        // Two triangles per glyph; per-vertex color is uniform across the quad.
        // Text never wants perspective (UI overlay), so emit perspFlag = 0.
        m_TextBatchVertices.push_back({xpos, ypos, u0, v0, r, g, b, a, 0.0f});          // TL
        m_TextBatchVertices.push_back({xpos, ypos + h, u0, v1, r, g, b, a, 0.0f});      // BL
        m_TextBatchVertices.push_back({xpos + w, ypos + h, u1, v1, r, g, b, a, 0.0f});  // BR
        m_TextBatchVertices.push_back({xpos, ypos, u0, v0, r, g, b, a, 0.0f});          // TL
        m_TextBatchVertices.push_back({xpos + w, ypos + h, u1, v1, r, g, b, a, 0.0f});  // BR
        m_TextBatchVertices.push_back({xpos + w, ypos, u1, v0, r, g, b, a, 0.0f});      // TR
    };

    auto buildTextVertices = [&](float offsetX, float offsetY, float r, float g, float b, float a)
    {
        float x = position.x + offsetX;
        float y = position.y + offsetY;

        for (auto c : text)
        {
            if (c == '\n')
            {
                x = position.x + offsetX;  // CR
                y += lineHeight * gscale;  // LF
                continue;
            }

            auto it = chars.find(c);
            if (it == chars.end())
                continue;
            const Character& ch = it->second;

            // Bearing offsets the glyph from the cursor to its top-left.
            float xpos = x + ch.Bearing.x * gscale;
            float ypos = y - ch.Bearing.y * gscale;
            float w = ch.Size.x * gscale;
            float h = ch.Size.y * gscale;

            addCharQuad(xpos, ypos, w, h, ch.u0, ch.v0, ch.u1, ch.v1, r, g, b, a);

            // Advance is in 1/64 px (shift right 6 bits).
            x += (ch.Advance >> 6) * gscale;
        }
    };

    // Outline first: four stroke passes offset by (+/-outlineOffset). Each
    // vertex carries (0,0,0,alpha) so the shader (mode 3) outputs a black
    // halo where the glyph is non-transparent.
    static const float outlineDirections[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int dir = 0; dir < 4; dir++)
    {
        buildTextVertices(outlineDirections[dir][0] * outlineOffset,
                          outlineDirections[dir][1] * outlineOffset,
                          0.0f,
                          0.0f,
                          0.0f,
                          alpha);
    }

    // Main text on top, carrying the requested color.
    buildTextVertices(0.0f, 0.0f, color.x, color.y, color.z, alpha);

    // Vertices accumulate in m_TextBatchVertices. The actual draw is deferred
    // to FlushTextBatch, which is triggered by EndFrame or any non-text draw
    // (sprite/rect/particle) that needs to reset shader state.
}

void OpenGLRenderer::FlushTextBatch()
{
    if (m_TextBatchVertices.empty())
    {
        m_CurrentTextAtlas = 0;
        return;
    }

    if (m_CurrentTextAtlas == 0 || !m_Initialized)
    {
        m_TextBatchVertices.clear();
        m_CurrentTextAtlas = 0;
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_ShaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // Mode 3: FragColor = texture(font, uv) * VertexColor. Per-quad color
    // (outline vs foreground) is baked into the vertex stream.
    if (m_UseColorOnlyLoc >= 0)
        glUniform1i(m_UseColorOnlyLoc, 3);

    const size_t totalVertexCount = m_TextBatchVertices.size();
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, totalVertexCount * sizeof(TextVertex), m_TextBatchVertices.data());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_CurrentTextAtlas);

    glBindVertexArray(m_TextVAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(totalVertexCount));
    DebugAfterDraw("Text", static_cast<int>(totalVertexCount));
    ++m_DrawCallCount;

    if (DrawTracer::IsEnabled())
    {
        const char* reason = m_PendingFlushReason ? m_PendingFlushReason : "explicit";
        char buf[96];
        std::snprintf(buf,
                      sizeof(buf),
                      "flush text (%s) %zu verts tex=%u",
                      reason,
                      totalVertexCount,
                      m_CurrentTextAtlas);
        DrawTracer::Mark(buf, m_DrawCallCount);
    }
    m_PendingFlushReason = nullptr;

    // Reset state so the next caller doesn't inherit mode 3.
    if (m_UseColorOnlyLoc >= 0)
        glUniform1i(m_UseColorOnlyLoc, 0);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_TextBatchVertices.clear();
    m_CurrentTextAtlas = 0;
}
