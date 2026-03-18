#pragma once

#include "IRenderer.h"
#include "RendererMacros.h"

#include <vulkan/vulkan.h>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

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
 * - Clear color arguments are currently ignored (`Clear()` is handled in `BeginFrame()` with a
 * fixed value)
 * - Additive blending flags are currently ignored in sprite/atlas/rect draw paths
 *
 * @see IRenderer Base interface with method documentation
 * @see OpenGLRenderer Alternative OpenGL implementation
 * @see Texture CPU-side texture data management
 */
class VulkanRenderer : public IRenderer
{
public:
    explicit VulkanRenderer(GLFWwindow* window);
    ~VulkanRenderer() override;

    RIFT_DECLARE_COMMON_RENDERER_METHODS;

    /// @brief Vulkan uses same Y-flip convention as OpenGL for UV compatibility.
    bool RequiresYFlip() const override { return true; }

    void SetAmbientColor(const glm::vec3& color) override { m_AmbientColor = color; }

    int GetDrawCallCount() const override { return m_DrawCallCount; }

private:
    /// @name Sprite Helpers
    /// @{

    /**
     * @struct SpriteVertex
     * @brief Per-vertex data for batched sprite rendering.
     */
    struct SpriteVertex
    {
        float pos[2];  ///< Screen-space position (x, y).
        float tex[2];  ///< Texture coordinates (u, v).
    };

    /// @brief Build 6 vertices (2 triangles) from 4 screen-space corners and UV coords.
    /// @param outVertices Output array of 6 vertices.
    /// @param corners Screen-space quad corners [TL, TR, BR, BL].
    /// @param texCoords UV coordinates matching each corner.
    static void BuildQuadVertices(SpriteVertex outVertices[6],
                                  const glm::vec2 corners[4],
                                  const glm::vec2 texCoords[4]);

    /// @brief Write a quad into the vertex buffer and flush if texture changes.
    /// @param descriptorSet Descriptor set binding the quad's texture.
    /// @param vertices Pre-built 6-vertex quad.
    /// @param spriteColor RGB color tint.
    /// @param spriteAlpha Transparency multiplier.
    /// @param useColorOnly True to render solid color instead of texture.
    /// @param colorOnly RGBA color when useColorOnly is true.
    /// @return True if the quad was successfully submitted.
    bool SubmitQuad(VkDescriptorSet descriptorSet,
                    const SpriteVertex vertices[6],
                    glm::vec3 spriteColor,
                    float spriteAlpha,
                    bool useColorOnly = false,
                    glm::vec4 colorOnly = glm::vec4(0.0f));
    /// @}

    /// @name Performance Metrics
    /// @{
    int m_DrawCallCount = 0;         ///< Draw calls this frame.
    glm::vec3 m_AmbientColor{1.0f};  ///< Current ambient light color.
    /// @}

    /// @name Text Rendering (FreeType)
    /// @{

    /**
     * @struct Glyph
     * @brief Per-character Vulkan texture and metrics for text rendering.
     */
    struct Glyph
    {
        VkImage image{VK_NULL_HANDLE};          ///< Vulkan image for this glyph.
        VkDeviceMemory memory{VK_NULL_HANDLE};  ///< Device memory backing the image.
        VkImageView imageView{VK_NULL_HANDLE};  ///< Image view for shader sampling.
        glm::ivec2 size{0, 0};                  ///< Glyph dimensions in pixels.
        glm::ivec2 bearing{0, 0};               ///< Offset from baseline to top-left.
        unsigned int advance{0};                ///< Horizontal advance to next character.
    };

    /// @brief Load TTF font and create per-glyph Vulkan textures.
    void LoadFont();

    /// @brief Create a Vulkan image from RGBA pixel data for a single glyph.
    /// @param width Glyph width in pixels.
    /// @param height Glyph height in pixels.
    /// @param rgbaData RGBA pixel data.
    /// @param outGlyph Output glyph with populated Vulkan handles.
    void CreateGlyphTexture(int width,
                            int height,
                            const std::vector<unsigned char>& rgbaData,
                            Glyph& outGlyph);

    std::map<char, Glyph> m_Glyphs;  ///< Cached glyph textures.

#ifdef USE_FREETYPE
    FT_Library m_FreeType{nullptr};
    FT_Face m_Face{nullptr};
#endif

    /// @}

    /// @name Vulkan Instance and Device
    /// @{
    VkInstance m_Instance;              ///< Vulkan API entry point.
    VkPhysicalDevice m_PhysicalDevice;  ///< Selected GPU.
    VkDevice m_Device;                  ///< Logical device for commands.
    VkQueue m_GraphicsQueue;            ///< Queue for draw commands.
    VkQueue m_PresentQueue;             ///< Queue for presentation.
    /// @}

    /// @name Surface and Swapchain
    /// @{
    VkSurfaceKHR m_Surface;                          ///< Window surface.
    VkSwapchainKHR m_Swapchain;                      ///< Presentation swapchain.
    std::vector<VkImage> m_SwapchainImages;          ///< Swapchain images.
    std::vector<VkImageView> m_SwapchainImageViews;  ///< Views into swapchain images.
    std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    VkExtent2D m_SwapchainExtent;     ///< Swapchain dimensions.
    VkFormat m_SwapchainImageFormat;  ///< Pixel format.
    /// @}

    /// @name Render Pass and Pipeline
    /// @{
    VkRenderPass m_RenderPass;          ///< Defines attachment usage.
    VkPipelineLayout m_PipelineLayout;  ///< Descriptor/push constant layout.
    VkPipeline m_GraphicsPipeline;      ///< Compiled shader + state.
    /// @}

    /// @name Command Recording
    /// @{
    VkCommandPool m_CommandPool;                    ///< Command buffer allocator.
    std::vector<VkCommandBuffer> m_CommandBuffers;  ///< Per-frame command buffers.
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
    size_t m_CurrentFrame;         ///< Current frame index (0 or 1).
    uint32_t m_ImageIndex;         ///< Acquired swapchain image index.
    bool m_FrameActive{false};     ///< True after BeginFrame started a render pass.
    GLFWwindow* m_Window;          ///< GLFW window reference.
    glm::mat4 m_Projection{1.0f};  ///< Current orthographic projection.
    /// @}

    /// @name Vertex Buffers (Double-Buffered)
    /// @{
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t DESCRIPTOR_POOL_MAX_SETS = 1000;
    VkBuffer m_VertexBuffers[MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_VertexBufferMemories[MAX_FRAMES_IN_FLIGHT];
    void* m_VertexBuffersMapped[MAX_FRAMES_IN_FLIGHT];  ///< Persistent mapping.
    VkBuffer m_IndexBuffer;
    VkDeviceMemory m_IndexBufferMemory;
    VkDeviceSize m_VertexBufferSize;
    uint32_t m_CurrentVertexCount;
    /// @}

    /// @name Sprite Batching
    /// @{
    VkImageView m_BatchImageView;          ///< Current batched texture.
    VkDescriptorSet m_BatchDescriptorSet;  ///< Descriptor for batch.
    uint32_t m_BatchStartVertex;           ///< Batch start in buffer.
    void FlushSpriteBatch();               ///< Submit batch to GPU.
    /// @}

    /// @name Staging Buffer
    /// @{
    VkBuffer m_StagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_StagingBufferMemory{VK_NULL_HANDLE};
    void* m_StagingBufferMapped{nullptr};
    /// @}

    /// @name Descriptors
    /// @{
    VkDescriptorPool m_DescriptorPool;
    VkDescriptorSetLayout m_DescriptorSetLayout;
    VkSampler m_TextureSampler;  ///< Shared texture sampler.
    std::unordered_map<VkImageView, VkDescriptorSet> m_DescriptorSetCache;
    bool m_DescriptorPoolWarned{false};
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

    /**
     * @struct TextureResources
     * @brief Vulkan GPU resources for a single cached texture.
     */
    struct TextureResources
    {
        VkImage image;          ///< Vulkan image handle.
        VkDeviceMemory memory;  ///< Device memory backing the image.
        VkImageView imageView;  ///< Image view for shader sampling.
        bool initialized;       ///< True after successful upload.
    };
    std::unordered_map<const Texture*, TextureResources> m_TextureCache;
    std::vector<const Texture*> m_UploadedTextures;
    /// @}

    /// @name Initialization Helpers
    /// @{
    /// @brief Create VkInstance with validation layers.
    void CreateInstance();
    /// @brief Create window surface via GLFW.
    void CreateSurface();
    /// @brief Select a suitable physical device (GPU).
    void PickPhysicalDevice();
    /// @brief Create logical device and retrieve queue handles.
    void CreateLogicalDevice();
    /// @brief Create the presentation swapchain.
    void CreateSwapchain();
    /// @brief Create image views for swapchain images.
    void CreateImageViews();
    /// @brief Create the render pass with color attachment.
    void CreateRenderPass();
    /// @brief Compile shaders and create the graphics pipeline.
    void CreateGraphicsPipeline();
    /// @brief Create framebuffers for each swapchain image.
    void CreateFramebuffers();
    /// @brief Create the command pool for the graphics queue family.
    void CreateCommandPool();
    /// @brief Allocate per-frame command buffers.
    void CreateCommandBuffers();
    /// @brief Create semaphores and fences for frame synchronization.
    void CreateSyncObjects();
    /// @brief Create vertex and index buffers with persistent mapping.
    void CreateBuffers();
    /// @brief Create the descriptor pool for texture bindings.
    void CreateDescriptorPool();
    /// @brief Create a 1x1 white texture for colored rectangle rendering.
    void CreateWhiteTexture();
    /// @brief Create the shared nearest-neighbor texture sampler.
    void CreateTextureSampler();
    /// @brief Destroy swapchain and dependent resources for recreation.
    void CleanupSwapchain();
    /// @brief Recreate swapchain after window resize.
    void RecreateSwapchain();
    bool m_FramebufferResized{false};  ///< Set by resize callback to trigger swapchain recreation.
    /// @}

    /// @name Texture Helpers
    /// @{
    /// @brief Get cached Vulkan resources for a texture, uploading if needed.
    /// @param texture CPU-side texture to look up or upload.
    TextureResources& GetOrCreateTexture(const Texture& texture);
    /// @brief Get or allocate a descriptor set for an image view.
    /// @param imageView Image view to bind.
    VkDescriptorSet GetOrCreateDescriptorSet(VkImageView imageView);
    /// @brief Compute a model matrix for sprite positioning.
    /// @param position World position.
    /// @param size Sprite dimensions.
    /// @param rotation Rotation in degrees.
    glm::mat4 CalculateModelMatrix(glm::vec2 position, glm::vec2 size, float rotation);
    /// @}

    /// @name Buffer Helpers
    /// @{
    /// @brief Find a suitable memory type index for the given requirements.
    /// @param typeFilter Bit mask of acceptable memory type indices.
    /// @param properties Required memory property flags.
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    /// @brief Create a Vulkan buffer with backing memory.
    /// @param size Buffer size in bytes.
    /// @param usage Buffer usage flags.
    /// @param properties Memory property flags.
    /// @param buffer Output buffer handle.
    /// @param bufferMemory Output memory handle.
    void CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory);
    /// @brief Copy data between two Vulkan buffers using a one-shot command.
    /// @param srcBuffer Source buffer.
    /// @param dstBuffer Destination buffer.
    /// @param size Number of bytes to copy.
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    /// @brief Transition a Vulkan image between layouts.
    /// @param image Image to transition.
    /// @param format Image format.
    /// @param oldLayout Current layout.
    /// @param newLayout Target layout.
    void TransitionImageLayout(VkImage image,
                               VkFormat format,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout);
    /// @brief Copy buffer contents to a Vulkan image.
    /// @param buffer Source staging buffer.
    /// @param image Destination image.
    /// @param width Image width in pixels.
    /// @param height Image height in pixels.
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    /// @brief Upload staging buffer to image with layout transitions.
    /// @param stagingBuffer Source staging buffer.
    /// @param image Destination image.
    /// @param width Image width in pixels.
    /// @param height Image height in pixels.
    void UploadStagingBufferToImage(VkBuffer stagingBuffer,
                                    VkImage image,
                                    uint32_t width,
                                    uint32_t height);
    /// @}

    /// @name Queue Families
    /// @{
    uint32_t m_GraphicsFamily;
    uint32_t m_PresentFamily;
    /// @}

    /// @name Validation and Extensions
    /// @{
    const std::vector<const char*> m_ValidationLayers = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> m_DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();
    /// @}
};
