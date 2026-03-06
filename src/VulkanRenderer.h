#pragma once

#include "IRenderer.h"

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <map>

#ifdef USE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

struct GLFWwindow;

/**
 * @class VulkanRenderer
 * @brief Vulkan 1.0 implementation of the IRenderer interface.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Provides hardware-accelerated 2D rendering using the Vulkan graphics API
 * with batching optimizations similar to the OpenGL implementation.
 *
 * @section vk_features Vulkan Features Used
 * | Feature              | Version | Usage                          |
 * |----------------------|---------|--------------------------------|
 * | Core API             | 1.0     | Explicit GPU control           |
 * | Swapchain            | KHR     | Presentation images            |
 * | Descriptor Sets      | 1.0     | Texture binding                |
 * | Push Constants       | 1.0     | Per-draw uniforms              |
 * | Memory Mapping       | 1.0     | Persistent vertex buffers      |
 *
 * @section vk_architecture Architecture Overview
 * Unlike OpenGL's implicit state machine, Vulkan requires explicit management
 * of all GPU resources. The renderer maintains:
 *
 * @par Core Objects
 * | Object               | Purpose                              |
 * |----------------------|--------------------------------------|
 * | VkInstance           | Vulkan API entry point               |
 * | VkDevice             | Logical device for commands          |
 * | VkSwapchain          | Presentation surface images          |
 * | VkRenderPass         | Defines attachment usage             |
 * | VkPipeline           | Shader + fixed-function state        |
 * | VkCommandBuffer      | Recorded GPU commands                |
 *
 * @par Synchronization
 * Uses 2 frames-in-flight with semaphores and fences:
 * @code
 *   Frame N:   [Record Cmds] --> [Submit] ---> [Present]
 *                                   |              |
 *   Semaphore:               ImageAvailable  RenderFinished
 *                                   |              |
 *   Frame N+1: [Wait Fence] --> [Record] ----> [Submit] --> ...
 * @endcode
 *
 * @section vk_batching Sprite Batching
 * Sprites are batched into a persistent mapped vertex buffer to minimize
 * CPU-GPU synchronization. Per-frame buffers avoid write hazards:
 *
 * @par Buffer Strategy
 * @code
 *   Frame 0: Write to m_VertexBuffers[0], GPU reads m_VertexBuffers[1]
 *   Frame 1: Write to m_VertexBuffers[1], GPU reads m_VertexBuffers[0]
 * @endcode
 *
 * @section vk_textures Texture Management
 * Textures are uploaded via staging buffers and cached by Texture pointer:
 * 1. Create staging buffer (host-visible memory)
 * 2. Copy pixel data to staging buffer
 * 3. Record copy command to device-local VkImage
 * 4. Transition image layout to SHADER_READ_ONLY
 * 5. Create VkImageView and cache descriptor set
 *
 * @section vk_limitations Current Limitations
 * - Single graphics pipeline (no compute shaders)
 * - No dynamic descriptor indexing
 * - Fixed descriptor pool size
 * - Synchronous texture uploads
 * - Clear color arguments are currently ignored (`Clear()` is handled in `BeginFrame()` with a fixed value)
 * - Additive blending flags are currently ignored in sprite/atlas/rect draw paths
 *
 * @see IRenderer Base interface with method documentation
 * @see OpenGLRenderer Alternative OpenGL implementation
 * @see Texture CPU-side texture data management
 */
class VulkanRenderer : public IRenderer
{
public:
    VulkanRenderer(GLFWwindow *window);
    ~VulkanRenderer() override;

    void Init() override;
    void Shutdown() override;

    void BeginFrame() override;
    void EndFrame() override;

    void DrawSprite(const Texture &texture, glm::vec2 position, glm::vec2 size = glm::vec2(32.0f, 32.0f),
                    float rotation = 0.0f, glm::vec3 color = glm::vec3(1.0f)) override;
    void DrawSpriteRegion(const Texture &texture, glm::vec2 position, glm::vec2 size,
                          glm::vec2 texCoord, glm::vec2 texSize, float rotation = 0.0f,
                          glm::vec3 color = glm::vec3(1.0f), bool flipY = true) override;
    void DrawSpriteAlpha(const Texture &texture, glm::vec2 position, glm::vec2 size,
                          float rotation, glm::vec4 color, bool additive = false) override;
    void DrawSpriteAtlas(const Texture &texture, glm::vec2 position, glm::vec2 size,
                         glm::vec2 uvMin, glm::vec2 uvMax, float rotation,
                         glm::vec4 color, bool additive = false) override;
    void DrawColoredRect(glm::vec2 position, glm::vec2 size, glm::vec4 color, bool additive = false) override;
    void DrawWarpedQuad(const Texture& texture, const glm::vec2 corners[4],
                        glm::vec2 texCoord, glm::vec2 texSize,
                        glm::vec3 color = glm::vec3(1.0f), bool flipY = true) override;

    void SetProjection(glm::mat4 projection) override { m_Projection = projection; }
    void SetViewport(int x, int y, int width, int height) override;
    void Clear(float r, float g, float b, float a) override;

    void UploadTexture(const Texture &texture) override;

    void DrawText(const std::string &text, glm::vec2 position, float scale = 1.0f,
                  glm::vec3 color = glm::vec3(1.0f), float outlineSize = 1.0f,
                  float alpha = 0.85f) override;

    float GetTextAscent(float scale = 1.0f) const override;
    float GetTextWidth(const std::string& text, float scale = 1.0f) const override;

    /// @brief Vulkan uses same Y-flip convention as OpenGL for UV compatibility.
    bool RequiresYFlip() const override { return true; }

    void SetAmbientColor(const glm::vec3& color) override { m_AmbientColor = color; }

    int GetDrawCallCount() const override { return m_DrawCallCount; }

private:
    /// @name Sprite Helpers
    /// @{
    struct SpriteVertex
    {
        float pos[2];
        float tex[2];
    };

    static void BuildQuadVertices(SpriteVertex outVertices[6],
                                  const glm::vec2 corners[4],
                                  const glm::vec2 texCoords[4]);

    bool SubmitQuad(VkDescriptorSet descriptorSet,
                    const SpriteVertex vertices[6],
                    glm::vec3 spriteColor, float spriteAlpha,
                    bool useColorOnly = false,
                    glm::vec4 colorOnly = glm::vec4(0.0f));
    /// @}

    /// @name Performance Metrics
    /// @{
    int m_DrawCallCount = 0;             ///< Draw calls this frame.
    glm::vec3 m_AmbientColor{1.0f};      ///< Current ambient light color.
    /// @}

    /// @name Text Rendering (FreeType)
    /// @{

    struct Glyph
    {
        VkImage image{VK_NULL_HANDLE};
        VkDeviceMemory memory{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        glm::ivec2 size{0, 0};
        glm::ivec2 bearing{0, 0};
        unsigned int advance{0};
    };

    void LoadFont();
    void CreateGlyphTexture(int width, int height, const std::vector<unsigned char> &rgbaData, Glyph &outGlyph);

    std::map<char, Glyph> m_Glyphs;      ///< Cached glyph textures.

#ifdef USE_FREETYPE
    FT_Library m_FreeType{nullptr};
    FT_Face m_Face{nullptr};
#endif

    /// @}

    /// @name Vulkan Instance and Device
    /// @{
    VkInstance m_Instance;               ///< Vulkan API entry point.
    VkPhysicalDevice m_PhysicalDevice;   ///< Selected GPU.
    VkDevice m_Device;                   ///< Logical device for commands.
    VkQueue m_GraphicsQueue;             ///< Queue for draw commands.
    VkQueue m_PresentQueue;              ///< Queue for presentation.
    /// @}

    /// @name Surface and Swapchain
    /// @{
    VkSurfaceKHR m_Surface;                           ///< Window surface.
    VkSwapchainKHR m_Swapchain;                       ///< Presentation swapchain.
    std::vector<VkImage> m_SwapchainImages;           ///< Swapchain images.
    std::vector<VkImageView> m_SwapchainImageViews;   ///< Views into swapchain images.
    std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    VkExtent2D m_SwapchainExtent;                     ///< Swapchain dimensions.
    VkFormat m_SwapchainImageFormat;                  ///< Pixel format.
    /// @}

    /// @name Render Pass and Pipeline
    /// @{
    VkRenderPass m_RenderPass;           ///< Defines attachment usage.
    VkPipelineLayout m_PipelineLayout;   ///< Descriptor/push constant layout.
    VkPipeline m_GraphicsPipeline;       ///< Compiled shader + state.
    /// @}

    /// @name Command Recording
    /// @{
    VkCommandPool m_CommandPool;                   ///< Command buffer allocator.
    std::vector<VkCommandBuffer> m_CommandBuffers; ///< Per-frame command buffers.
    /// @}

    /// @name Synchronization
    /// @{
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;  ///< Swapchain image ready.
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;  ///< Rendering complete.
    std::vector<VkFence> m_InFlightFences;                ///< CPU-GPU sync.
    std::vector<VkFence> m_ImagesInFlight;                ///< Per-image fence tracking.
    /// @}

    /// @name Frame State
    /// @{
    size_t m_CurrentFrame;    ///< Current frame index (0 or 1).
    uint32_t m_ImageIndex;    ///< Acquired swapchain image index.
    bool m_FrameActive{false};///< True after BeginFrame started a render pass.
    GLFWwindow *m_Window;     ///< GLFW window reference.
    glm::mat4 m_Projection;   ///< Current orthographic projection.
    /// @}


    /// @name Vertex Buffers (Double-Buffered)
    /// @{
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    VkBuffer m_VertexBuffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_VertexBufferMemories[MAX_FRAMES_IN_FLIGHT];
    void *m_VertexBuffersMapped[MAX_FRAMES_IN_FLIGHT];  ///< Persistent mapping.
    VkBuffer m_IndexBuffer;
    VkDeviceMemory m_IndexBufferMemory;
    VkDeviceSize m_VertexBufferSize;
    uint32_t m_CurrentVertexCount;
    /// @}

    /// @name Sprite Batching
    /// @{
    VkImageView m_BatchImageView;           ///< Current batched texture.
    VkDescriptorSet m_BatchDescriptorSet;   ///< Descriptor for batch.
    uint32_t m_BatchStartVertex;            ///< Batch start in buffer.
    void FlushSpriteBatch();                ///< Submit batch to GPU.
    /// @}

    /// @name Staging Buffer
    /// @{
    VkBuffer m_StagingBuffer;
    VkDeviceMemory m_StagingBufferMemory;
    void *m_StagingBufferMapped;
    /// @}

    /// @name Descriptors
    /// @{
    VkDescriptorPool m_DescriptorPool;
    VkDescriptorSetLayout m_DescriptorSetLayout;
    VkSampler m_TextureSampler;              ///< Shared texture sampler.
    std::unordered_map<VkImageView, VkDescriptorSet> m_DescriptorSetCache;
    /// @}

    /// @name White Texture (for colored rects)
    /// @{
    VkImage m_WhiteTextureImage;
    VkDeviceMemory m_WhiteTextureImageMemory;
    VkImageView m_WhiteTextureImageView;
    VkSampler m_WhiteTextureSampler;
    /// @}

    /// @name Texture Cache
    /// @{
    struct TextureResources
    {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView imageView;
        bool initialized;
    };
    std::unordered_map<const Texture*, TextureResources> m_TextureCache;
    std::vector<Texture*> m_UploadedTextures;
    /// @}

    /// @name Initialization Helpers
    /// @{
    void CreateInstance();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapchain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateGraphicsPipeline();
    void CreateFramebuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void CreateBuffers();
    void CreateDescriptorPool();
    void CreateWhiteTexture();
    void CreateTextureSampler();
    void CleanupSwapchain();
    void RecreateSwapchain();
    bool m_FramebufferResized{false};
    /// @}

    /// @name Texture Helpers
    /// @{
    TextureResources &GetOrCreateTexture(const Texture &texture);
    VkDescriptorSet GetOrCreateDescriptorSet(VkImageView imageView);
    glm::mat4 CalculateModelMatrix(glm::vec2 position, glm::vec2 size, float rotation);
    /// @}

    /// @name Buffer Helpers
    /// @{
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer &buffer, VkDeviceMemory &bufferMemory);
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    /// @}

    /// @name Queue Families
    /// @{
    uint32_t m_GraphicsFamily;
    uint32_t m_PresentFamily;
    /// @}

    /// @name Validation and Extensions
    /// @{
    const std::vector<const char *> m_ValidationLayers = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char *> m_DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    bool CheckValidationLayerSupport();
    std::vector<const char *> GetRequiredExtensions();
    /// @}
};
