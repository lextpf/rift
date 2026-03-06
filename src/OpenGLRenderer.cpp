#include "OpenGLRenderer.h"

#include <iostream>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <fstream>
#include <filesystem>
#include <vector>
#include <thread>
#include <chrono>
#include <GLFW/glfw3.h>

// Debug: Sleep after each draw call to visualize render order
bool g_DebugDrawSleep = false;
GLFWwindow *g_DebugWindow = nullptr;
static int g_DebugDrawCallIndex = 0;

void SetDebugDrawSleep(GLFWwindow *window, bool enabled)
{
    g_DebugWindow = window;
    g_DebugDrawSleep = enabled;
}

void ResetDebugDrawCallIndex()
{
    g_DebugDrawCallIndex = 0;
}

static void DebugAfterDraw(const char *label, int count)
{
    if (g_DebugDrawSleep && g_DebugWindow)
    {
        std::cout << "[DRAW " << g_DebugDrawCallIndex++ << "] " << label << " (" << count << " vertices)" << std::endl;
        glFinish();
        glfwSwapBuffers(g_DebugWindow);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

OpenGLRenderer::OpenGLRenderer()
    // Core geometry buffers
    : m_VAO(0)                                                     // Vertex array object for unit quad
    , m_VBO(0)                                                     // Vertex buffer for quad vertices
    , m_EBO(0)                                                     // Element buffer for quad indices
    , m_TextVAO(0)                                                 // VAO for text rendering
    , m_TextVBO(0)                                                 // VBO for text quads
    , m_ShaderProgram(0)                                           // Unified sprite/text shader
    , m_WhiteTexture(0)                                            // 1x1 white texture for colored rects
    // Shader uniform locations
    , m_ModelLoc(-1)                                               // Per-sprite transform matrix
    , m_ProjectionLoc(-1)                                          // Orthographic projection matrix
    , m_ColorLoc(-1)                                               // RGB color tint
    , m_AlphaLoc(-1)                                               // Transparency multiplier
    , m_AmbientColorLoc(-1)                                        // Day/night ambient light
    , m_AmbientColor(1.0f, 1.0f, 1.0f)                             // Current ambient (white = full bright)
    // Sprite batching
    , m_BatchVAO(0)                                                // VAO for batched sprites
    , m_BatchVBO(0)                                                // VBO for batched sprite vertices
    , m_CurrentBatchTexture(0)                                     // Active texture for current batch
    // Colored rectangle batching
    , m_RectBatchVAO(0)                                            // VAO for colored rectangles
    , m_RectBatchVBO(0)                                            // VBO for rectangle vertices
    , m_RectBatchAdditive(false)                                   // Current blend mode for rects
    // Particle batching
    , m_CurrentParticleTexture(0)                                  // Active particle texture
    , m_ParticleBatchAdditive(false)                               // Current blend mode for particles
    // Font rendering
    , m_FontAtlasTexture(0)                                        // Packed glyph texture atlas
    , m_FontAtlasWidth(0)                                          // Atlas width in pixels
    , m_FontAtlasHeight(0)                                         // Atlas height in pixels
#ifdef USE_FREETYPE
    , m_FreeType(nullptr)                                          // FreeType library handle
    , m_Face(nullptr)                                              // Loaded font face
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

void OpenGLRenderer::Shutdown()
{
    // Delete font atlas texture
    if (m_FontAtlasTexture != 0)
    {
        glDeleteTextures(1, &m_FontAtlasTexture);
        m_FontAtlasTexture = 0;
    }
    m_Characters.clear();

#ifdef USE_FREETYPE
    // Cleanup FreeType
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

    // Delete all GL resources and reset handles to 0 to prevent double-deletion
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

    m_BatchVertices.clear();
    m_RectBatchVertices.clear();
}

std::string OpenGLRenderer::LoadShaderFromFile(const std::string &filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        // Try parent directory
        std::string parentPath = "../" + filepath;
        file.open(parentPath);
        if (!file.is_open())
        {
            std::cerr << "ERROR: Could not open shader file: " << filepath << " or " << parentPath << std::endl;
            return "";
        }
    }

    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    return source;
}

void OpenGLRenderer::Init()
{
    // Initialize all OpenGL resources needed for rendering.
    // Order matters geometry buffers first, then textures, then shaders.
    SetupQuad();
    CreateWhiteTexture();

#ifdef USE_FREETYPE
    // Try to load font from project assets, fall back to system fonts if needed
    const std::vector<std::string> fontCandidates = {
        "assets/fonts/c8ab67e0-519a-49b5-b693-e8fc86d08efa.ttf",
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf", // Fallback
        "C:/Windows/Fonts/arial.ttf",   // Fallback
#endif
    };

    bool fontLoaded = false;
    for (const auto &fontPath : fontCandidates)
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
        std::cerr << "WARNING: No font could be loaded. Text rendering disabled." << std::endl;
    }
#else
    std::cerr << "WARNING: FreeType not available. Text rendering disabled." << std::endl;
#endif
    // Load and compile shaders from files
    std::string vertexShaderSource = LoadShaderFromFile("shaders/sprite.vert");
    std::string fragmentShaderSource = LoadShaderFromFile("shaders/sprite.frag");

    if (vertexShaderSource.empty() || fragmentShaderSource.empty())
    {
        std::cerr << "ERROR: Failed to load shader files!" << std::endl;
        return;
    }

    // Compile vertex shader
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    const char *vertexSourcePtr = vertexShaderSource.c_str();
    glShaderSource(vertexShader, 1, &vertexSourcePtr, nullptr);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
        std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
    }

    // Compile fragment shader
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    const char *fragmentSourcePtr = fragmentShaderSource.c_str();
    glShaderSource(fragmentShader, 1, &fragmentSourcePtr, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
        std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
    }

    // Link shader program
    m_ShaderProgram = glCreateProgram();
    glAttachShader(m_ShaderProgram, vertexShader);
    glAttachShader(m_ShaderProgram, fragmentShader);
    glLinkProgram(m_ShaderProgram);

    glGetProgramiv(m_ShaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(m_ShaderProgram, 512, nullptr, infoLog);
        std::cerr << "Shader program linking failed: " << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Cache uniform locations for performance
    m_ModelLoc = glGetUniformLocation(m_ShaderProgram, "model");
    m_ProjectionLoc = glGetUniformLocation(m_ShaderProgram, "projection");
    m_ColorLoc = glGetUniformLocation(m_ShaderProgram, "spriteColor");
    m_AlphaLoc = glGetUniformLocation(m_ShaderProgram, "spriteAlpha");
    m_AmbientColorLoc = glGetUniformLocation(m_ShaderProgram, "ambientColor");
}

void OpenGLRenderer::SetAmbientColor(const glm::vec3 &color)
{
    m_AmbientColor = color;
}

void OpenGLRenderer::BeginFrame()
{
    // Reset batch state at start of frame
    m_BatchVertices.clear();
    m_CurrentBatchTexture = 0;
    m_RectBatchVertices.clear();
    m_ParticleBatchVertices.clear();
    m_CurrentParticleTexture = 0;
    m_DrawCallCount = 0;
}

void OpenGLRenderer::EndFrame()
{
    // Flush any remaining batched sprites, rects, and particles
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
}

void OpenGLRenderer::SetProjection(glm::mat4 projection)
{
    // Flush any pending batches before changing projection
    // This prevents world-space sprites from being drawn with UI projection (or vice versa)
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
    m_Projection = projection;
}

void OpenGLRenderer::SetViewport(int x, int y, int width, int height)
{
    glViewport(x, y, width, height);
}

void OpenGLRenderer::SetVanishingPointPerspective(bool enabled, float horizonY, float horizonScale,
                                                  float viewWidth, float viewHeight)
{
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
    IRenderer::SetVanishingPointPerspective(enabled, horizonY, horizonScale, viewWidth, viewHeight);
}

void OpenGLRenderer::SetGlobePerspective(bool enabled, float sphereRadius,
                                         float viewWidth, float viewHeight)
{
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
    IRenderer::SetGlobePerspective(enabled, sphereRadius, viewWidth, viewHeight);
}

void OpenGLRenderer::SetFisheyePerspective(bool enabled, float sphereRadius,
                                           float horizonY, float horizonScale,
                                           float viewWidth, float viewHeight)
{
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
    IRenderer::SetFisheyePerspective(enabled, sphereRadius, horizonY, horizonScale, viewWidth, viewHeight);
}

void OpenGLRenderer::SuspendPerspective(bool suspend)
{
    FlushBatch();
    FlushRectBatch();
    FlushParticleBatch();
    IRenderer::SuspendPerspective(suspend);
}

void OpenGLRenderer::Clear(float r, float g, float b, float a)
{
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void OpenGLRenderer::UploadTexture(const Texture &texture)
{
    // Recreate only if the texture does not belong to the active GL context generation.
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.GetID() == 0 || texture.m_OpenGLContextGeneration != currentGen)
    {
        const_cast<Texture &>(texture).RecreateOpenGLTexture();
    }
}

void OpenGLRenderer::SetupQuad()
{
    // Unit quad vertices a 1x1 quad from (0,0) to (1,1)
    // Each vertex has 4 floats position (x,y) and texture coords (u,v)
    // This quad is used for immediate-mode sprite rendering (non-batched)
    float vertices[] = {
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,  // Bottom-left
        1.0f, 0.0f, 1.0f, 0.0f,  // Top-right
        0.0f, 0.0f, 0.0f, 0.0f,  // Top-left
        0.0f, 1.0f, 0.0f, 1.0f,  // Bottom-left (second triangle)
        1.0f, 1.0f, 1.0f, 1.0f,  // Bottom-right
        1.0f, 0.0f, 1.0f, 0.0f}; // Top-right

    // Two triangles forming a quad (indices into vertex array)
    unsigned int indices[] = {
        0, 1, 2,  // First triangle
        3, 4, 5}; // Second triangle

    // Generate OpenGL objects for the unit quad
    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    // Upload vertex data (static since unit quad never changes)
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Configure vertex attributes layout matches shader inputs
    // Location 0: position (2 floats at offset 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // Location 1: texture coords (2 floats at offset 8 bytes)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Location 2: color disabled here, used only by colored rect/particle batches
    glDisableVertexAttribArray(2);

    glBindVertexArray(0);

    // Text uses dynamic batching all characters in a DrawText call are
    // uploaded at once and drawn with two draw calls (outline + main text)
    glGenVertexArrays(1, &m_TextVAO);
    glGenBuffers(1, &m_TextVBO);

    glBindVertexArray(m_TextVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);

    // Pre-allocate buffer for text quads (6 vertices per character, dynamic for frequent updates)
    glBufferData(GL_ARRAY_BUFFER, MAX_TEXT_QUADS * 6 * sizeof(TextVertex), nullptr, GL_DYNAMIC_DRAW);

    // Same vertex layout as sprites position + texcoord
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(TextVertex), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDisableVertexAttribArray(2); // Text uses uniform color, not per-vertex

    glBindVertexArray(0);
    m_TextBatchVertices.reserve(MAX_TEXT_QUADS * 6);

    // Sprites are batched by texture all sprites using the same texture are
    // collected and drawn in a single draw call to minimize state changes
    glGenVertexArrays(1, &m_BatchVAO);
    glGenBuffers(1, &m_BatchVBO);

    glBindVertexArray(m_BatchVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_BatchVBO);

    // Pre-allocate for max batch size, dynamic draw since vertices change every frame
    size_t batchBufferSize = MAX_BATCH_SPRITES * VERTICES_PER_SPRITE * sizeof(BatchVertex);
    glBufferData(GL_ARRAY_BUFFER, batchBufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Vertex layout position (xy) + texcoord (uv), no color
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(BatchVertex), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glDisableVertexAttribArray(2); // Sprites use uniform color

    glBindVertexArray(0);

    // Used for rectangles and particles that need per-vertex color/alpha
    // (e.g., gradients, fading particles, lighting effects)
    glGenVertexArrays(1, &m_RectBatchVAO);
    glGenBuffers(1, &m_RectBatchVBO);

    glBindVertexArray(m_RectBatchVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);

    size_t rectBatchBufferSize = MAX_BATCH_SPRITES * VERTICES_PER_SPRITE * sizeof(ColoredVertex);
    glBufferData(GL_ARRAY_BUFFER, rectBatchBufferSize, nullptr, GL_DYNAMIC_DRAW);

    // Extended vertex layout position (xy) + texcoord (uv) + color (rgba)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ColoredVertex), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

void OpenGLRenderer::DrawSprite(const Texture &texture, glm::vec2 position, glm::vec2 size,
                                float rotation, glm::vec3 color)
{
    DrawSpriteRegion(texture, position, size, glm::vec2(0.0f), glm::vec2(1.0f), rotation, color, true);
}

void OpenGLRenderer::DrawSpriteRegion(const Texture &texture, glm::vec2 position, glm::vec2 size,
                                      glm::vec2 texCoord, glm::vec2 texSize, float rotation,
                                      glm::vec3 color, bool flipY)
{
    // Batching requires all sprites in a batch to share the same texture.
    // When switching between batch types (rect vs sprite) or textures, flush first.
    if (!m_RectBatchVertices.empty())
    {
        FlushRectBatch();
    }

    unsigned int texID = texture.GetID();
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.m_OpenGLContextGeneration != currentGen || texID == 0)
    {
        const_cast<Texture &>(texture).RecreateOpenGLTexture();
        texID = texture.GetID();
        if (texID == 0)
            return;
    }

    // Texture change forces a flush, sprites with different textures can't be batched.
    // Use batch-content check instead of m_CurrentBatchTexture!=0 so an invalid
    // texture ID (0) cannot contaminate subsequent sprites in the same batch.
    if (!m_BatchVertices.empty() && m_CurrentBatchTexture != texID)
    {
        FlushBatch();
    }

    // Batch full, flush and start new batch
    if (m_BatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        FlushBatch();
    }

    m_CurrentBatchTexture = texID;

    // Convert pixel coordinates to normalized UV coordinates (0-1 range)
    float texX = texCoord.x / texture.GetWidth();
    float texY = texCoord.y / texture.GetHeight();
    float texW = texSize.x / texture.GetWidth();
    float texH = texSize.y / texture.GetHeight();

    // Handle Y-axis flip for OpenGL texture coordinate convention
    // OpenGL has origin at bottom-left, but image data typically has origin at top-left
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

    // Final UV bounds, no texel offset needed for GL_NEAREST filtering with pixel art
    float u0 = texX;
    float u1 = texX + texW;
    float vTop = finalTexYTop;
    float vBottom = finalTexYBottom;

    // Build quad corners in local space, origin at top-left of sprite
    // Vertices are pre-transformed on CPU to allow batching sprites with different transforms
    glm::vec2 corners[4] = {
        {0.0f, 0.0f},     // Top-left
        {size.x, 0.0f},   // Top-right
        {size.x, size.y}, // Bottom-right
        {0.0f, size.y}    // Bottom-left
    };

    RotateCorners(corners, size, rotation);

    // Move sprite to world position
    for (int i = 0; i < 4; i++)
    {
        corners[i] += position;
    }

    ApplyPerspective(corners);

    // Map UV coordinates to each corner (V is flipped for OpenGL convention)
    glm::vec2 uvs[4] = {
        {u0, vBottom}, // Top-left corner uses bottom V
        {u1, vBottom}, // Top-right
        {u1, vTop},    // Bottom-right corner uses top V
        {u0, vTop}     // Bottom-left
    };

    // Assemble quad as two triangles (6 vertices total)
    // Using counter-clockwise winding for front-facing
    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y}); // TL
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y}); // BR
    m_BatchVertices.push_back({corners[3].x, corners[3].y, uvs[3].x, uvs[3].y}); // BL

    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y}); // TL
    m_BatchVertices.push_back({corners[1].x, corners[1].y, uvs[1].x, uvs[1].y}); // TR
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y}); // BR
}

void OpenGLRenderer::DrawSpriteAlpha(const Texture &texture, glm::vec2 position, glm::vec2 size,
                                     float rotation, glm::vec4 color, bool additive)
{
    DrawSpriteAtlas(texture, position, size, glm::vec2(0.0f), glm::vec2(1.0f), rotation, color, additive);
}

void OpenGLRenderer::DrawSpriteAtlas(const Texture &texture, glm::vec2 position, glm::vec2 size,
                                     glm::vec2 uvMin, glm::vec2 uvMax, float rotation,
                                     glm::vec4 color, bool additive)
{
    // Atlas version of DrawSpriteAlpha - uses custom UV coordinates instead of full texture

    // Must flush other batch types before adding to particle batch
    if (!m_BatchVertices.empty())
        FlushBatch();
    if (!m_RectBatchVertices.empty())
        FlushRectBatch();

    unsigned int texID = texture.GetID();
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.m_OpenGLContextGeneration != currentGen || texID == 0)
    {
        const_cast<Texture &>(texture).RecreateOpenGLTexture();
        texID = texture.GetID();
        if (texID == 0)
            return;
    }

    // Flush particle batch if texture or blend mode changed.
    // Same rule as sprite batch: once vertices exist, texture mismatch must flush
    // even if previous texture ID was 0.
    if (!m_ParticleBatchVertices.empty() &&
        (m_CurrentParticleTexture != texID || m_ParticleBatchAdditive != additive))
    {
        FlushParticleBatch();
    }

    // Check batch capacity
    if (m_ParticleBatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        FlushParticleBatch();
    }

    m_CurrentParticleTexture = texID;
    m_ParticleBatchAdditive = additive;

    // Use provided UV coordinates
    float u0 = uvMin.x, u1 = uvMax.x;
    float v0 = uvMin.y, v1 = uvMax.y;

    // Pre-transform vertices
    glm::vec2 corners[4] = {
        {0.0f, 0.0f},
        {size.x, 0.0f},
        {size.x, size.y},
        {0.0f, size.y}};

    RotateCorners(corners, size, rotation);

    // Translate to world position
    for (int i = 0; i < 4; i++)
    {
        corners[i] += position;
    }

    ApplyPerspective(corners);

    // UV coordinates (OpenGL Y flipped)
    glm::vec2 uvs[4] = {
        {u0, v1}, // Top-left
        {u1, v1}, // Top-right
        {u1, v0}, // Bottom-right
        {u0, v0}  // Bottom-left
    };

    // Add 6 vertices (2 triangles) with per-vertex color to batch
    float r = color.r, g = color.g, b = color.b, a = color.a;
    m_ParticleBatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, r, g, b, a});
    m_ParticleBatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, r, g, b, a});
    m_ParticleBatchVertices.push_back({corners[3].x, corners[3].y, uvs[3].x, uvs[3].y, r, g, b, a});

    m_ParticleBatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y, r, g, b, a});
    m_ParticleBatchVertices.push_back({corners[1].x, corners[1].y, uvs[1].x, uvs[1].y, r, g, b, a});
    m_ParticleBatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y, r, g, b, a});
}

void OpenGLRenderer::FlushBatch()
{
    if (m_BatchVertices.empty())
        return;

    // Texture ID 0 is invalid for sprite sampling in core profile.
    // Drop this batch instead of accidentally sampling a previously bound texture.
    if (m_CurrentBatchTexture == 0)
    {
        m_BatchVertices.clear();
        m_CurrentBatchTexture = 0;
        return;
    }

    glUseProgram(m_ShaderProgram);

    // All sprites in batch share the same transform since vertices are pre-transformed
    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));
    glUniform3f(m_ColorLoc, 1.0f, 1.0f, 1.0f); // No color tint
    glUniform1f(m_AlphaLoc, 1.0f);             // Full opacity
    glUniform3f(m_AmbientColorLoc, m_AmbientColor.r, m_AmbientColor.g, m_AmbientColor.b);

    // Upload vertex data using buffer orphaning technique
    // GL_MAP_INVALIDATE_BUFFER_BIT tells driver we don't need old data,
    // allowing it to allocate new storage and avoid GPU/CPU sync stall
    size_t dataSize = m_BatchVertices.size() * sizeof(BatchVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_BatchVBO);
    void *ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, dataSize,
                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_BatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    // Bind the shared texture for this batch
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_CurrentBatchTexture);

    // Single draw call for all sprites in batch (main performance benefit of batching!)
    glBindVertexArray(m_BatchVAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_BatchVertices.size()));
    DebugAfterDraw("SpriteBatch", static_cast<int>(m_BatchVertices.size()));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    // Reset for next batch, clearing texture forces explicit rebind to prevent stale state
    m_BatchVertices.clear();
    m_CurrentBatchTexture = 0;
}

void OpenGLRenderer::CreateWhiteTexture()
{
    // Create a 1x1 white texture used as a placeholder for colored rectangles.
    // When drawing solid-colored shapes, we bind this texture and let the
    // vertex color or uniform color control the final output.
    glGenTextures(1, &m_WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);

    unsigned char whitePixel[] = {255, 255, 255, 255}; // RGBA white
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);

    // Clamp to edge prevents any filtering artifacts at borders
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void OpenGLRenderer::DrawColoredRect(glm::vec2 position, glm::vec2 size, glm::vec4 color, bool additive)
{
    // If switching from sprite to rect mode, flush sprites first
    if (!m_BatchVertices.empty())
    {
        FlushBatch();
    }

    // If blend mode changed, flush current batch first
    if (!m_RectBatchVertices.empty() && m_RectBatchAdditive != additive)
    {
        FlushRectBatch();
    }
    m_RectBatchAdditive = additive;

    // Check batch capacity
    if (m_RectBatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        FlushRectBatch();
    }

    // Pre-transform vertices (no rotation for rects)
    glm::vec2 corners[4] = {
        position,                                   // Top-left
        {position.x + size.x, position.y},          // Top-right
        {position.x + size.x, position.y + size.y}, // Bottom-right
        {position.x, position.y + size.y}           // Bottom-left
    };

    ApplyPerspective(corners);

    // Add 6 vertices (2 triangles) with per-vertex color
    float r = color.r, g = color.g, b = color.b, a = color.a;
    m_RectBatchVertices.push_back({corners[0].x, corners[0].y, 0.0f, 0.0f, r, g, b, a});
    m_RectBatchVertices.push_back({corners[2].x, corners[2].y, 1.0f, 1.0f, r, g, b, a});
    m_RectBatchVertices.push_back({corners[3].x, corners[3].y, 0.0f, 1.0f, r, g, b, a});

    m_RectBatchVertices.push_back({corners[0].x, corners[0].y, 0.0f, 0.0f, r, g, b, a});
    m_RectBatchVertices.push_back({corners[1].x, corners[1].y, 1.0f, 0.0f, r, g, b, a});
    m_RectBatchVertices.push_back({corners[2].x, corners[2].y, 1.0f, 1.0f, r, g, b, a});
}

void OpenGLRenderer::DrawWarpedQuad(const Texture& texture, const glm::vec2 corners[4],
                                    glm::vec2 texCoord, glm::vec2 texSize,
                                    glm::vec3 color, bool flipY)
{
    // Warped quads are pre-transformed by the caller (sphere projection already applied)
    // We add them directly to the sprite batch without additional perspective transformation

    // Flush other batch types first
    if (!m_RectBatchVertices.empty())
        FlushRectBatch();
    if (!m_ParticleBatchVertices.empty())
        FlushParticleBatch();

    unsigned int texID = texture.GetID();
    const std::uint64_t currentGen = Texture::GetCurrentOpenGLContextGeneration();
    if (texture.m_OpenGLContextGeneration != currentGen || texID == 0)
    {
        const_cast<Texture &>(texture).RecreateOpenGLTexture();
        texID = texture.GetID();
        if (texID == 0)
            return;
    }

    // Flush sprite batch if texture changed.
    // Do not special-case texture 0, or stale/invalid IDs can leak into the batch.
    if (!m_BatchVertices.empty() && m_CurrentBatchTexture != texID)
    {
        FlushBatch();
    }

    // Check batch capacity
    if (m_BatchVertices.size() >= MAX_BATCH_SPRITES * VERTICES_PER_SPRITE)
    {
        FlushBatch();
    }

    m_CurrentBatchTexture = texID;

    // Calculate UV coordinates from pixel coordinates
    float texW = static_cast<float>(texture.GetWidth());
    float texH = static_cast<float>(texture.GetHeight());

    float u0 = texCoord.x / texW;
    float u1 = (texCoord.x + texSize.x) / texW;

    float v0, v1;
    if (flipY)
    {
        // OpenGL: flip Y for textures loaded with stb_image
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

    // Assemble quad as two triangles (6 vertices)
    // Triangle 1: TL, BR, BL
    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y});
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y});
    m_BatchVertices.push_back({corners[3].x, corners[3].y, uvs[3].x, uvs[3].y});

    // Triangle 2: TL, TR, BR
    m_BatchVertices.push_back({corners[0].x, corners[0].y, uvs[0].x, uvs[0].y});
    m_BatchVertices.push_back({corners[1].x, corners[1].y, uvs[1].x, uvs[1].y});
    m_BatchVertices.push_back({corners[2].x, corners[2].y, uvs[2].x, uvs[2].y});
}

void OpenGLRenderer::FlushRectBatch()
{
    if (m_RectBatchVertices.empty())
        return;

    glUseProgram(m_ShaderProgram);

    // Additive blending makes colors add together (used for glow effects)
    // Standard alpha blending: dest = src*alpha + dest*(1-alpha)
    // Additive blending: dest = src*alpha + dest (brighter where overlapping)
    if (m_RectBatchAdditive)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    // Identity transform since vertices are pre-transformed on CPU
    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // Tell shader to use per-vertex color instead of texture sampling
    // useColorOnly modes: 0=texture, 1=uniform color, 2=vertex color, 3=texture*vertex color
    GLint useColorOnlyLoc = glGetUniformLocation(m_ShaderProgram, "useColorOnly");
    if (useColorOnlyLoc >= 0)
        glUniform1i(useColorOnlyLoc, 2);

    // Upload with buffer orphaning to avoid GPU sync stall
    size_t dataSize = m_RectBatchVertices.size() * sizeof(ColoredVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);
    void *ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, dataSize,
                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_RectBatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    // White texture acts as placeholder, shader ignores it in vertex color mode
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);

    // Single draw call for all rectangles
    glBindVertexArray(m_RectBatchVAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_RectBatchVertices.size()));
    DebugAfterDraw("RectBatch", static_cast<int>(m_RectBatchVertices.size()));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    // Restore shader and blend state for next batch
    if (useColorOnlyLoc >= 0)
        glUniform1i(useColorOnlyLoc, 0);
    if (m_RectBatchAdditive)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_RectBatchVertices.clear();
}

void OpenGLRenderer::FlushParticleBatch()
{
    // Particles are batched separately from sprites because they use per-vertex
    // color/alpha for effects like fading, color variation, and glow intensity
    if (m_ParticleBatchVertices.empty())
        return;

    if (m_CurrentParticleTexture == 0)
    {
        m_ParticleBatchVertices.clear();
        m_CurrentParticleTexture = 0;
        return;
    }

    glUseProgram(m_ShaderProgram);

    // Additive blend for glow particles (fire, magic, light effects)
    if (m_ParticleBatchAdditive)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    }

    glm::mat4 identity = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // Mode 3: multiply texture color by per-vertex color
    // This allows particles to be tinted and faded individually while using a shared texture
    GLint useColorOnlyLoc = glGetUniformLocation(m_ShaderProgram, "useColorOnly");
    if (useColorOnlyLoc >= 0)
        glUniform1i(useColorOnlyLoc, 3);

    // Upload particle vertices, reuses rect batch VBO since same vertex layout
    size_t dataSize = m_ParticleBatchVertices.size() * sizeof(ColoredVertex);
    glBindBuffer(GL_ARRAY_BUFFER, m_RectBatchVBO);
    void *ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, dataSize,
                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (ptr)
    {
        memcpy(ptr, m_ParticleBatchVertices.data(), dataSize);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    // All particles in this batch share the same texture (e.g., soft circle for glow)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_CurrentParticleTexture);

    // Single draw call for entire particle batch
    glBindVertexArray(m_RectBatchVAO);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_ParticleBatchVertices.size()));
    DebugAfterDraw("ParticleBatch", static_cast<int>(m_ParticleBatchVertices.size()));
    glBindVertexArray(0);
    ++m_DrawCallCount;

    // Restore state
    if (useColorOnlyLoc >= 0)
        glUniform1i(useColorOnlyLoc, 0);
    if (m_ParticleBatchAdditive)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    m_ParticleBatchVertices.clear();
    m_CurrentParticleTexture = 0;
}

void OpenGLRenderer::LoadFont(const std::string &fontPath)
{
#ifdef USE_FREETYPE
    // FreeType is a library for rendering TrueType/OpenType fonts to bitmaps.
    // We use it to generate a texture atlas containing all ASCII glyphs.
    if (FT_Init_FreeType(&m_FreeType))
    {
        std::cerr << "ERROR::FREETYPE: Could not init FreeType Library" << std::endl;
        return;
    }

    // Load the font file (TTF/OTF) and select the first face (index 0)
    if (FT_New_Face(m_FreeType, fontPath.c_str(), 0, &m_Face))
    {
        std::cerr << "ERROR::FREETYPE: Failed to load font: " << fontPath << std::endl;
        FT_Done_FreeType(m_FreeType);
        m_FreeType = nullptr;
        return;
    }

    // Set glyph size in pixels (height only; width is proportional)
    FT_Set_Pixel_Sizes(m_Face, 0, 24);

    // FreeType renders 8-bit grayscale bitmaps; disable 4-byte alignment requirement
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Glyphs are packed left-to-right, wrapping to new rows when needed.
    // This two-pass approach lets us allocate the exact atlas size required.
    int atlasWidth = 0;
    int atlasHeight = 0;
    int rowHeight = 0;
    int currentX = 0;
    const int ATLAS_MAX_WIDTH = 512; // Row width limit before wrapping
    const int PADDING = 2;           // Gap between glyphs to prevent bleeding

    // Cache glyph bitmaps and metrics from first pass (FreeType reuses internal buffer)
    struct GlyphData
    {
        unsigned char *bitmap;
        int width, height;
        int bearingX, bearingY;
        unsigned int advance;
    };
    std::map<char, GlyphData> glyphData;

    // Render each ASCII character and measure it
    for (unsigned char c = 0; c < 128; c++)
    {
        // FT_LOAD_RENDER: rasterize glyph to bitmap immediately
        if (FT_Load_Char(m_Face, c, FT_LOAD_RENDER))
        {
            continue; // Skip characters that fail to load
        }

        int w = m_Face->glyph->bitmap.width;
        int h = m_Face->glyph->bitmap.rows;

        // Extract glyph metrics for text layout:
        // - Bearing: offset from cursor to top-left of glyph
        // - Advance: horizontal distance to move cursor after this glyph
        GlyphData gd;
        gd.width = w;
        gd.height = h;
        gd.bearingX = m_Face->glyph->bitmap_left;
        gd.bearingY = m_Face->glyph->bitmap_top;
        gd.advance = static_cast<unsigned int>(m_Face->glyph->advance.x);

        // Copy bitmap since FreeType reuses buffer for next character
        if (w > 0 && h > 0)
        {
            gd.bitmap = new unsigned char[w * h];
            memcpy(gd.bitmap, m_Face->glyph->bitmap.buffer, w * h);
        }
        else
        {
            gd.bitmap = nullptr; // Some characters (like space) have no pixels
        }
        glyphData[c] = gd;

        // Simulate atlas packing to determine final dimensions
        if (currentX + w + PADDING > ATLAS_MAX_WIDTH)
        {
            // Wrap to next row
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
    atlasHeight += rowHeight; // Include final row

    // Round up to power of 2 for GPU compatibility (some drivers require this)
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

    m_FontAtlasWidth = atlasWidth;
    m_FontAtlasHeight = atlasHeight;

    // RGBA format with white color and alpha from glyph grayscale
    std::vector<unsigned char> atlasData(atlasWidth * atlasHeight * 4, 0);

    currentX = 0;
    int currentY = 0;
    rowHeight = 0;

    // Place each glyph in the atlas and record its UV coordinates
    for (unsigned char c = 0; c < 128; c++)
    {
        auto it = glyphData.find(c);
        if (it == glyphData.end())
            continue;

        GlyphData &gd = it->second;
        int w = gd.width;
        int h = gd.height;

        // Same packing logic as pass 1 to get consistent positions
        if (currentX + w + PADDING > ATLAS_MAX_WIDTH)
        {
            currentY += rowHeight + PADDING;
            currentX = 0;
            rowHeight = 0;
        }

        // Copy glyph pixels into atlas, converting grayscale to RGBA
        // White color with alpha = glyph intensity enables color tinting via uniform
        if (gd.bitmap && w > 0 && h > 0)
        {
            for (int y = 0; y < h; y++)
            {
                for (int x = 0; x < w; x++)
                {
                    int atlasIdx = ((currentY + y) * atlasWidth + (currentX + x)) * 4;
                    unsigned char value = gd.bitmap[y * w + x];
                    atlasData[atlasIdx + 0] = 255;   // R (white)
                    atlasData[atlasIdx + 1] = 255;   // G (white)
                    atlasData[atlasIdx + 2] = 255;   // B (white)
                    atlasData[atlasIdx + 3] = value; // A (glyph coverage)
                }
            }
        }

        // Calculate normalized UV coordinates for this glyph's position in atlas
        float u0 = static_cast<float>(currentX) / atlasWidth;
        float v0 = static_cast<float>(currentY) / atlasHeight;
        float u1 = static_cast<float>(currentX + w) / atlasWidth;
        float v1 = static_cast<float>(currentY + h) / atlasHeight;

        // Store character info for text rendering
        Character character = {
            glm::ivec2(w, h),
            glm::ivec2(gd.bearingX, gd.bearingY),
            gd.advance,
            u0, v0, u1, v1};
        m_Characters.insert(std::pair<char, Character>(c, character));

        currentX += w + PADDING;
        if (h > rowHeight)
            rowHeight = h;

        delete[] gd.bitmap;
    }

    // Upload atlas to GPU
    glGenTextures(1, &m_FontAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_FontAtlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasWidth, atlasHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlasData.data());

    // Linear filtering for smooth text at various scales
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Restore default alignment for other texture uploads
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    std::cout << "Loaded font: " << fontPath << " (atlas " << atlasWidth << "x" << atlasHeight
              << ", " << m_Characters.size() << " characters)" << std::endl;
#else
    std::cerr << "ERROR: LoadFont called but FreeType is not available!" << std::endl;
#endif
}

float OpenGLRenderer::GetTextAscent(float scale) const
{
    // Find the maximum bearing.y (ascent) across all loaded characters
    int maxAscent = 0;
    for (const auto &pair : m_Characters)
    {
        if (pair.second.Bearing.y > maxAscent)
        {
            maxAscent = pair.second.Bearing.y;
        }
    }
    // If no characters loaded, use font size as fallback
    if (maxAscent == 0)
    {
        maxAscent = 24; // Default font size
    }
    return static_cast<float>(maxAscent) * scale;
}

float OpenGLRenderer::GetTextWidth(const std::string &text, float scale) const
{
    if (m_Characters.empty() || text.empty())
    {
        return 0.0f;
    }

    float width = 0.0f;
    for (char c : text)
    {
        auto it = m_Characters.find(c);
        if (it != m_Characters.end())
        {
            // Advance is in 1/64th pixels (FreeType convention)
            width += (it->second.Advance >> 6) * scale;
        }
    }
    return width;
}

void OpenGLRenderer::DrawText(const std::string &text, glm::vec2 position, float scale, glm::vec3 color,
                              float outlineSize, float alpha)
{
    // Text uses different render state, so flush other batches first
    FlushBatch();
    FlushRectBatch();

    if (m_Characters.empty() || m_FontAtlasTexture == 0)
    {
        std::cerr << "DrawText: No font atlas loaded!" << std::endl;
        return;
    }

    if (text.empty())
    {
        return;
    }

    m_TextBatchVertices.clear();

    // Determine line height from first printable character
    float lineHeight = 24.0f;
    for (auto c : text)
    {
        if (c != '\n')
        {
            auto it = m_Characters.find(c);
            if (it != m_Characters.end())
            {
                lineHeight = static_cast<float>(it->second.Size.y);
                break;
            }
        }
    }

    float outlineOffset = 2.0f * scale * outlineSize;

    // Helper add a quad for one character to the vertex batch
    auto addCharQuad = [this](float xpos, float ypos, float w, float h,
                              float u0, float v0, float u1, float v1)
    {
        // Two triangles per character (6 vertices)
        m_TextBatchVertices.push_back({xpos, ypos, u0, v0});         // TL
        m_TextBatchVertices.push_back({xpos, ypos + h, u0, v1});     // BL
        m_TextBatchVertices.push_back({xpos + w, ypos + h, u1, v1}); // BR
        m_TextBatchVertices.push_back({xpos, ypos, u0, v0});         // TL
        m_TextBatchVertices.push_back({xpos + w, ypos + h, u1, v1}); // BR
        m_TextBatchVertices.push_back({xpos + w, ypos, u1, v0});     // TR
    };

    // Helper generate vertices for entire text string at given offset
    auto buildTextVertices = [&](float offsetX, float offsetY)
    {
        float x = position.x + offsetX;
        float y = position.y + offsetY;

        for (auto c : text)
        {
            if (c == '\n')
            {
                x = position.x + offsetX; // Carriage return
                y += lineHeight * scale;  // Line feed
                continue;
            }

            auto it = m_Characters.find(c);
            if (it == m_Characters.end())
                continue;
            const Character &ch = it->second;

            // Position glyph using its bearing (offset from cursor to top-left)
            float xpos = x + ch.Bearing.x * scale;
            float ypos = y - ch.Bearing.y * scale;
            float w = ch.Size.x * scale;
            float h = ch.Size.y * scale;

            addCharQuad(xpos, ypos, w, h, ch.u0, ch.v0, ch.u1, ch.v1);

            // Advance cursor (value is in 1/64 pixels, so shift right 6 bits)
            x += (ch.Advance >> 6) * scale;
        }
    };

    // Create outline by rendering text 4 times with offsets (creates a stroke effect)
    static const float outlineDirections[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int dir = 0; dir < 4; dir++)
    {
        buildTextVertices(outlineDirections[dir][0] * outlineOffset,
                          outlineDirections[dir][1] * outlineOffset);
    }

    size_t outlineVertexCount = m_TextBatchVertices.size();

    // Add main text vertices (drawn on top of outline)
    buildTextVertices(0, 0);

    size_t totalVertexCount = m_TextBatchVertices.size();

    if (totalVertexCount == 0)
    {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_ShaderProgram);

    glm::mat4 model = glm::mat4(1.0f);
    glUniformMatrix4fv(m_ModelLoc, 1, GL_FALSE, glm::value_ptr(model));
    glUniformMatrix4fv(m_ProjectionLoc, 1, GL_FALSE, glm::value_ptr(m_Projection));

    // Use texture mode (mode 0) color uniform tints the white glyphs
    GLint useColorOnlyLoc = glGetUniformLocation(m_ShaderProgram, "useColorOnly");
    if (useColorOnlyLoc >= 0)
        glUniform1i(useColorOnlyLoc, 0);
    glUniform1f(m_AlphaLoc, alpha);
    glUniform3f(m_AmbientColorLoc, m_AmbientColor.r, m_AmbientColor.g, m_AmbientColor.b);

    // Upload all text vertices in one buffer update
    glBindBuffer(GL_ARRAY_BUFFER, m_TextVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, totalVertexCount * sizeof(TextVertex), m_TextBatchVertices.data());

    // Bind font atlas (contains all glyphs in one texture)
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_FontAtlasTexture);

    glBindVertexArray(m_TextVAO);

    // Draw outline first (black, behind main text)
    if (outlineVertexCount > 0)
    {
        glUniform3f(m_ColorLoc, 0.0f, 0.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(outlineVertexCount));
        DebugAfterDraw("TextOutline", static_cast<int>(outlineVertexCount));
    }

    // Draw main text on top (user-specified color)
    size_t mainVertexCount = totalVertexCount - outlineVertexCount;
    if (mainVertexCount > 0)
    {
        glUniform3f(m_ColorLoc, color.x, color.y, color.z);
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(outlineVertexCount), static_cast<GLsizei>(mainVertexCount));
        DebugAfterDraw("TextMain", static_cast<int>(mainVertexCount));
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
