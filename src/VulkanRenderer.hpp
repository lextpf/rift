#pragma once

#include "IRenderer.hpp"
#include "PostFXParams.hpp"
#include "RendererMacros.hpp"

#include <vulkan/vulkan.h>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
 * Provides hardware-accelerated 2D rendering using the Vulkan graphics API.
 * Sprite draws currently submit one quad at a time with cached descriptor sets
 * and persistent per-frame vertex buffers; this differs from OpenGL's
 * texture-run sprite batching.
 *
 * @par Vulkan features used
 * | Feature              | Version | Usage                          |
 * |----------------------|---------|--------------------------------|
 * | Core API             | 1.0     | Explicit GPU control           |
 * | Swapchain            | KHR     | Presentation images            |
 * | Descriptor Sets      | 1.0     | Texture binding                |
 * | Push Constants       | 1.0     | Per-draw uniforms              |
 * | Memory Mapping       | 1.0     | Persistent vertex buffers      |
 * | Negative viewport    | 1.1     | Y-flip to match OpenGL         |
 *
 * @warning The negative-height viewport is legal only with VK_KHR_maintenance1 or an
 * effective device version of 1.1+, and the backend declares neither: the instance
 * requests apiVersion 1.0 and the device enables only VK_KHR_SWAPCHAIN. The Y-flip
 * therefore relies on de-facto driver behavior and trips VUID-VkViewport-height-01773
 * under the validation layers on a strict 1.0 device.
 *
 * @warning The 2D push-constant block is 176 bytes, above Vulkan's guaranteed
 * `maxPushConstantsSize` of 128. The backend requires a device advertising at least 176
 * and never reads the limit, so on a minimum-limit device `vkCreatePipelineLayout` fails
 * during Init with a bare VK_CHECK throw.
 *
 * @par Frame lifecycle
 * @htmlonly
 * <pre class="mermaid">
 * flowchart LR
 *     classDef acquire fill:#1e3a5f,stroke:#3b82f6,color:#e2e8f0
 *     classDef record  fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef draw    fill:#4a3520,stroke:#f59e0b,color:#e2e8f0
 *     classDef submit  fill:#7f1d1d,stroke:#ef4444,color:#e2e8f0
 *     classDef present fill:#2e1f5e,stroke:#8b5cf6,color:#e2e8f0
 *
 *     A[BeginFrame]:::acquire --> B[Wait fence + acquire image]:::acquire
 *     B --> C[Begin command buffer]:::record
 *     C --> D[Begin render pass]:::record
 *     D --> E[DrawSprite / DrawText appends 6 verts]:::draw
 *     E --> F[SubmitQuad records vkCmdDraw immediately]:::draw
 *     F --> E
 *     E --> G[EndFrame]:::submit
 *     G --> H[End render pass + command buffer]:::submit
 *     H --> I[Submit + present]:::present
 * </pre>
 * @endhtmlonly
 *
 * @par Core objects
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
 * Two frames in flight, each with its own image-available semaphore, render-finished
 * semaphore and in-flight fence; `m_ImagesInFlight` adds a second wait that keeps two
 * frames off the same swapchain image. Every early return after a successful acquire
 * must recreate the image-available semaphore, and a return after `vkResetFences` must
 * also recreate the fence as SIGNALED. Skipping either repair deadlocks the next
 * `vkWaitForFences` or trips VUID-vkAcquireNextImageKHR-semaphore-01779. Repair paths do
 * not advance `m_CurrentFrame`: the same slot is retried next frame.
 * @htmlonly
 * <pre class="mermaid">
 * flowchart TD
 *     classDef ok  fill:#134e3a,stroke:#10b981,color:#e2e8f0
 *     classDef fix fill:#7f1d1d,stroke:#ef4444,color:#e2e8f0
 *
 *     A[Wait m_InFlightFences frame]:::ok --> B[Acquire swapchain image]:::ok
 *     B --> C[Wait m_ImagesInFlight image]:::ok
 *     C --> D[Record: command buffer, render pass, draws]:::ok
 *     D --> E[EndFrame: end render pass + command buffer]:::ok
 *     E --> F[vkResetFences]:::ok
 *     F --> G[Submit: wait ImageAvailable, signal RenderFinished + fence]:::ok
 *     G --> H[Present: wait RenderFinished]:::ok
 *     H --> I[frame = frame + 1 mod 2]:::ok
 *     B -- OUT_OF_DATE --> W[RecreateImageAvailableSemaphore + RecreateSwapchain]:::fix
 *     B -- acquire error --> R[RecreateImageAvailableSemaphore]:::fix
 *     E -- bounds or vkEndCommandBuffer failure --> R
 *     F -- reset failed --> R
 *     G -- submit failed --> S[Destroy + recreate fence SIGNALED]:::fix
 *     S --> R
 * </pre>
 * @endhtmlonly
 *
 * @par Sprite batching
 * Despite the name, there is currently no batching: every draw path funnels into
 * @c SubmitQuad, which appends 6 vertices to the persistent mapped vertex buffer
 * and records its own @c vkCmdDraw plus a fresh push-constant block right there.
 * One quad = one draw call, and the day/night ambient tint is baked per quad, so
 * the ordering hazard OpenGL guards against with lazy flushes cannot arise here.
 * Per-frame buffers avoid write hazards:
 *
 * @par Buffer strategy
 * @code
 *   Frame 0: Write to m_VertexBuffers[0], GPU reads m_VertexBuffers[1]
 *   Frame 1: Write to m_VertexBuffers[1], GPU reads m_VertexBuffers[0]
 * @endcode
 *
 * @par Texture management
 * UploadTexture() creates a host-visible staging buffer, copies pixel data,
 * records the transfer to a device-local VkImage, transitions it to
 * SHADER_READ_ONLY, then creates the VkImageView.
 *
 * Descriptor sets are not created there. They are allocated lazily per VkImageView on
 * first draw and cached in @c m_DescriptorSetCache, so a re-upload that yields a new
 * view strands the old cache entry. UploadTexture is also not noexcept: it lets
 * `std::runtime_error` from the transfer escape to the caller.
 *
 * Draw calls do not upload texture data mid-frame. A texture without a live
 * Vulkan image view renders with the white fallback texture or is skipped,
 * depending on the draw path.
 *
 * @par Current limitations
 * - One 2D sprite pipeline plus six Geometry3D pipelines ([DepthMode][BlendMode]); no
 * pipeline cache
 * - No compute shaders
 * - No dynamic descriptor indexing
 * - Descriptor pool starts at a fixed size; overflow pools are allocated on demand
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

    void SetFontCandidates(const std::vector<std::string>& fontCandidates) override;

    /// @brief Vulkan uses same Y-flip convention as OpenGL for UV compatibility.
    bool RequiresYFlip() const override { return true; }

    /**
     * @brief Set the global ambient (day/night) tint.
     *
     * On this backend the tint is not deferred: @c SubmitQuad copies
     * @c m_AmbientColor into that quad's push constants and issues its
     * @c vkCmdDraw immediately, so every already-submitted quad keeps the ambient
     * it was drawn with and a later change cannot retroactively recolor it.
     *
     * The @c FlushSpriteBatch() call below is the structural mirror of
     * OpenGLRenderer::SetAmbientColor, where the batch really is lazy and the
     * drain is load-bearing (otherwise, e.g., the sky pass setting ambient to
     * white would flash still-queued night tiles to "day"). Keeping the call
     * costs nothing and preserves the shape if real batching is added back.
     */
    void SetAmbientColor(const glm::vec3& color) override
    {
        if (color == m_AmbientColor)
            return;
        FlushSpriteBatch();
        m_AmbientColor = color;
    }

    int GetDrawCallCount() const override { return m_DrawCallCount; }

private:
    RendererInfo m_Info;  ///< Cached at end of Init(); returned by GetBackendInfo().

    /**
     * @name Sprite helpers
     * @{
     */

    /**
     * @struct SpriteVertex
     * @brief Per-vertex data for batched sprite rendering.
     */
    struct SpriteVertex
    {
        float pos[2];  ///< Screen-space position (x, y).
        float tex[2];  ///< Texture coordinates (u, v).
    };

    /**
     * @brief Build 6 vertices (2 triangles) from 4 screen-space corners and UV coords.
     * @param outVertices Output array of 6 vertices.
     * @param corners Screen-space quad corners [TL, TR, BR, BL].
     * @param texCoords UV coordinates matching each corner.
     */
    static void BuildQuadVertices(SpriteVertex outVertices[6],
                                  const glm::vec2 corners[4],
                                  const glm::vec2 texCoords[4]);

    /**
     * @brief Write a quad into the vertex buffer and flush if texture changes.
     * @param descriptorSet Descriptor set binding the quad's texture.
     * @param vertices Pre-built 6-vertex quad.
     * @param spriteColor RGB color tint.
     * @param spriteAlpha Transparency multiplier.
     * @param useColorOnly True to render solid color instead of texture.
     * @param colorOnly RGBA color when useColorOnly is true.
     * @param applyAmbient True to multiply by the day/night ambient tint (lit
     *        surfaces); false for self-lit sprites (particles, sky) that must
     *        keep their own color, matching the OpenGL particle batch.
     * @return True if the quad was successfully submitted.
     */
    bool SubmitQuad(VkDescriptorSet descriptorSet,
                    const SpriteVertex vertices[6],
                    glm::vec3 spriteColor,
                    float spriteAlpha,
                    bool useColorOnly = false,
                    glm::vec4 colorOnly = glm::vec4(0.0f),
                    bool applyAmbient = true);
    /// @}

    /**
     * @name Performance metrics
     * @{
     */
    int m_DrawCallCount = 0;         ///< Draw calls this frame.
    glm::vec3 m_AmbientColor{1.0f};  ///< Current ambient light color.
    /// @}

    /**
     * @name Text rendering (FreeType)
     * @{
     */

    /**
     * @struct Glyph
     * @brief Per-character Vulkan texture and metrics for text rendering.
     */
    struct Glyph
    {
        VkImage image{VK_NULL_HANDLE};          ///< Vulkan image for this glyph.
        VkDeviceMemory memory{VK_NULL_HANDLE};  ///< Device memory backing the image.

        /**
         * @brief Image view sampled for this glyph.
         *
         * Non-owning when it aliases @c m_WhiteTextureImageView: @ref CreateGlyphTexture
         * substitutes the shared white view for zero-sized bitmaps (space and control
         * characters). Compare against @c m_WhiteTextureImageView before destroying, or
         * teardown double-frees the renderer's white texture.
         */
        VkImageView imageView{VK_NULL_HANDLE};

        glm::ivec2 size{0, 0};     ///< Glyph dimensions in pixels.
        glm::ivec2 bearing{0, 0};  ///< Offset from baseline to top-left.
        unsigned int advance{0};   ///< Horizontal advance to next character.
    };

    /// @brief Load TTF font and create per-glyph Vulkan textures.
    void LoadFont();

    /**
     * @brief Create a Vulkan image from RGBA pixel data for a single glyph.
     * @param width Glyph width in pixels.
     * @param height Glyph height in pixels.
     * @param rgbaData RGBA pixel data.
     * @param outGlyph Output glyph with populated Vulkan handles.
     */
    void CreateGlyphTexture(int width,
                            int height,
                            const std::vector<unsigned char>& rgbaData,
                            Glyph& outGlyph);

    std::map<char, Glyph> m_Glyphs;             ///< Cached glyph textures.
    std::vector<std::string> m_FontCandidates;  ///< Project-specific font candidates.

#ifdef USE_FREETYPE
    FT_Library m_FreeType{nullptr};
    FT_Face m_Face{nullptr};
#endif

    /// @}

    /**
     * @name Vulkan instance and device
     * @{
     */
    VkInstance m_Instance{VK_NULL_HANDLE};              ///< Vulkan API entry point.
    VkPhysicalDevice m_PhysicalDevice{VK_NULL_HANDLE};  ///< Selected GPU.
    VkDevice m_Device{VK_NULL_HANDLE};                  ///< Logical device for commands.
    VkQueue m_GraphicsQueue{VK_NULL_HANDLE};            ///< Queue for draw commands.
    VkQueue m_PresentQueue{VK_NULL_HANDLE};             ///< Queue for presentation.
    /// @}

    /**
     * @name Surface and swapchain
     * @{
     */
    VkSurfaceKHR m_Surface{VK_NULL_HANDLE};          ///< Window surface.
    VkSwapchainKHR m_Swapchain{VK_NULL_HANDLE};      ///< Presentation swapchain.
    std::vector<VkImage> m_SwapchainImages;          ///< Swapchain images.
    std::vector<VkImageView> m_SwapchainImageViews;  ///< Views into swapchain images.
    std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    VkExtent2D m_SwapchainExtent{};                        ///< Swapchain dimensions.
    VkFormat m_SwapchainImageFormat{VK_FORMAT_UNDEFINED};  ///< Pixel format.
    /// @}

    /**
     * @name Render pass and pipeline
     * @{
     */
    VkRenderPass m_RenderPass{VK_NULL_HANDLE};          ///< Defines attachment usage.
    VkPipelineLayout m_PipelineLayout{VK_NULL_HANDLE};  ///< Descriptor/push constant layout.
    VkPipeline m_GraphicsPipeline{VK_NULL_HANDLE};      ///< Compiled shader + state.
    /// @}

    /**
     * @name Command recording
     * @{
     */
    VkCommandPool m_CommandPool{VK_NULL_HANDLE};  ///< Command buffer allocator.

    /**
     * @brief Primary command buffers, allocated one per swapchain image at Init.
     *
     * Recording indexes this by @c m_CurrentFrame (0..MAX_FRAMES_IN_FLIGHT-1), not by
     * image index, so any surplus entries are never recorded. @ref RecreateSwapchain
     * does not reallocate the vector, so its size stays at the Init-time image count.
     */
    std::vector<VkCommandBuffer> m_CommandBuffers;
    /// @}

    /**
     * @name Synchronization
     * @{
     */
    std::vector<VkSemaphore> m_ImageAvailableSemaphores;  ///< Swapchain image ready.
    std::vector<VkSemaphore> m_RenderFinishedSemaphores;  ///< Rendering complete.
    std::vector<VkFence> m_InFlightFences;                ///< CPU-GPU sync.
    std::vector<VkFence> m_ImagesInFlight;                ///< Per-image fence tracking.
    VkFence m_TransferFence{VK_NULL_HANDLE};  ///< Fence for synchronous transfer operations.
    /// @}

    /**
     * @name Frame state
     * @{
     */
    size_t m_CurrentFrame{0};       ///< Current frame index (0 or 1).
    uint32_t m_ImageIndex{0};       ///< Acquired swapchain image index.
    bool m_FrameActive{false};      ///< True after BeginFrame started a render pass.
    GLFWwindow* m_Window{nullptr};  ///< GLFW window reference.
    glm::mat4 m_Projection{1.0f};   ///< Current orthographic projection.
    /// @}

    /**
     * @name World-space 3D path
     * @brief Depth attachment, the Geometry3D pipelines, and their vertex buffers.
     *
     * Mirrors the OpenGL `Geometry3D` path. The two coexist in one render pass and one
     * command buffer, world geometry drawing with depth and screen-space UI without,
     * and they hand a single pipeline binding back and forth through
     * @ref m_Bound3DPipeline. Pipeline binding is command-buffer state and dies with the
     * command buffer, so every draw path must claim its own pipeline before recording:
     * a new 2D path that forgets to reclaim rasterizes UI with the world's vertex layout
     * and depth state, and a missing BeginFrame reset makes the first 3D draw of a frame
     * inherit the 2D pipeline.
     * @htmlonly
     * <pre class="mermaid">
     * stateDiagram-v2
     *     [*] --> Bound2D: BeginFrame binds m_GraphicsPipeline, clears m_Bound3DPipeline
     *     Bound2D --> Bound3D: DrawQuad3D - bind depth/blend pipeline, re-apply Y-flip
     *     Bound3D --> Bound3D: same depth/blend pair - no rebind; other pair - rebind
     *     Bound3D --> Bound2D: SubmitQuad rebinds 2D, clears m_Bound3DPipeline
     * </pre>
     * @endhtmlonly
     * @{
     */

    /**
     * @struct Vertex3D
     * @brief Vertex format for world-space quads. Must match
     *        `OpenGLRenderer::BatchVertex3D` and `shaders/Geometry3D.vert`.
     */
    struct Vertex3D
    {
        float x, y, z;     ///< Scene-space position (see sceneMath).
        float u, v;        ///< Texture coordinates.
        float r, g, b, a;  ///< Per-vertex RGBA tint.
    };

    /**
     * @struct Push3D
     * @brief Push-constant block for the Geometry3D program (80 bytes).
     *
     * One combined matrix rather than separate view/projection: the CPU builds
     * both anyway to extract frustum planes, so combining costs nothing here.
     */
    struct Push3D
    {
        glm::mat4 viewProjection;  ///< Offset 0, 64 bytes.
        glm::vec3 ambientColor;    ///< Offset 64, 12 bytes.
        float alphaCutoff;         ///< Offset 76, 4 bytes.
    };

    /// Combined view-projection for the 3D path, already corrected for Vulkan's
    /// clip space by @ref SetViewProjection.
    glm::mat4 m_ViewProjection{1.0f};

    VkImage m_DepthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_DepthImageMemory{VK_NULL_HANDLE};
    VkImageView m_DepthImageView{VK_NULL_HANDLE};
    VkFormat m_DepthFormat{VK_FORMAT_UNDEFINED};

    /**
     * @brief Geometry3D pipelines indexed [DepthMode][BlendMode].
     *
     * Vulkan bakes depth and blend state into the pipeline object, so each
     * combination the draw interface can request needs its own - six small
     * objects, created once.
     */
    VkPipeline m_Pipeline3D[renderModes::DEPTH_MODE_COUNT][renderModes::BLEND_MODE_COUNT]{};
    VkPipelineLayout m_Pipeline3DLayout{VK_NULL_HANDLE};
    /// Last pipeline bound this frame, to skip redundant vkCmdBindPipeline calls.
    VkPipeline m_Bound3DPipeline{VK_NULL_HANDLE};

    /// @brief Pick a supported depth format, preferring 32-bit float.
    VkFormat FindDepthFormat() const;
    /// @brief Create the depth image/memory/view for the current swapchain extent.
    void CreateDepthResources();
    /// @brief Destroy the depth image/memory/view (swapchain recreate + shutdown).
    void DestroyDepthResources();
    /// @brief Create the six Geometry3D pipelines and their shared layout.
    void CreatePipeline3D();

    /// @}

    /**
     * @name Vertex buffers (double-buffered)
     * @{
     */
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t DESCRIPTOR_POOL_MAX_SETS = 1000;
    VkBuffer m_VertexBuffers[MAX_FRAMES_IN_FLIGHT]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory m_VertexBufferMemories[MAX_FRAMES_IN_FLIGHT]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    void* m_VertexBuffersMapped[MAX_FRAMES_IN_FLIGHT]{nullptr, nullptr};  ///< Persistent mapping.
    /// Uploaded at init but unused: no path calls vkCmdBindIndexBuffer, so every draw
    /// is a non-indexed vkCmdDraw. Kept as the seam for an indexed quad batch.
    VkBuffer m_IndexBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_IndexBufferMemory{VK_NULL_HANDLE};
    VkDeviceSize m_VertexBufferSize{0};
    uint32_t m_CurrentVertexCount{0};

    /**
     * @brief Per-frame vertex storage for the world-space 3D path.
     *
     * A separate buffer from the 2D one above because Vertex3D has a different
     * stride; declared here so the MAX_FRAMES_IN_FLIGHT bound is already in scope.
     */
    VkBuffer m_Vertex3DBuffers[MAX_FRAMES_IN_FLIGHT]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDeviceMemory m_Vertex3DMemories[MAX_FRAMES_IN_FLIGHT]{VK_NULL_HANDLE, VK_NULL_HANDLE};
    void* m_Vertex3DMapped[MAX_FRAMES_IN_FLIGHT]{nullptr, nullptr};
    VkDeviceSize m_Vertex3DBufferSize{0};
    uint32_t m_Current3DVertexCount{0};
    /// @}

    /**
     * @name Sprite batching (currently inert)
     *
     * Vestigial batch bookkeeping. No draw path ever assigns a real texture or
     * descriptor here - every path calls @ref SubmitQuad, which draws each quad on
     * its own - so these two handles hold VK_NULL_HANDLE at all times and
     * @ref FlushSpriteBatch always returns at its null-handle guard without
     * recording anything. Do not read them as "the texture currently batched".
     * @{
     */
    VkImageView m_BatchImageView{VK_NULL_HANDLE};          ///< Always VK_NULL_HANDLE today.
    VkDescriptorSet m_BatchDescriptorSet{VK_NULL_HANDLE};  ///< Always VK_NULL_HANDLE today.
    /// First vertex of the pending batch range. Only ever advanced by the
    /// unreachable tail of FlushSpriteBatch, so in practice it stays 0.
    uint32_t m_BatchStartVertex{0};
    /**
     * @brief Would submit m_BatchStartVertex..m_CurrentVertexCount as one draw.
     *
     * Called from SetAmbientColor, SetProjection and EndFrame, but bails at the
     * null-handle guard before recording anything.
     */
    void FlushSpriteBatch();
    /// @}

    /**
     * @name Staging buffer (unused)
     *
     * No persistent staging buffer exists today. None of these three handles is ever
     * assigned, mapped, used or destroyed: every upload creates and frees a function-
     * local staging buffer on the spot (see @ref UploadStagingBufferToImage). Kept as
     * the seam for a reusable upload buffer.
     * @{
     */
    VkBuffer m_StagingBuffer{VK_NULL_HANDLE};
    VkDeviceMemory m_StagingBufferMemory{VK_NULL_HANDLE};
    void* m_StagingBufferMapped{nullptr};
    /// @}

    /**
     * @name Descriptors
     * @{
     */
    VkDescriptorPool m_DescriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_DescriptorSetLayout{VK_NULL_HANDLE};
    VkSampler m_TextureSampler{VK_NULL_HANDLE};  ///< Shared texture sampler.
    std::unordered_map<VkImageView, VkDescriptorSet> m_DescriptorSetCache;
    std::vector<VkDescriptorPool> m_OverflowPools;  ///< Additional pools created on overflow.
    bool m_DescriptorPoolWarned{false};
    /// @}

    /**
     * @name White texture (for colored rects)
     * @{
     */
    VkImage m_WhiteTextureImage{VK_NULL_HANDLE};
    VkDeviceMemory m_WhiteTextureImageMemory{VK_NULL_HANDLE};
    VkImageView m_WhiteTextureImageView{VK_NULL_HANDLE};
    /// Created and destroyed but never bound - every descriptor write, the white texture
    /// included, uses the shared m_TextureSampler.
    VkSampler m_WhiteTextureSampler{VK_NULL_HANDLE};
    /// @}

    /**
     * @name Texture cache
     * @{
     */

    /**
     * @struct TextureResources
     * @brief Non-owning aliases of the Vulkan resources for one cached texture.
     *
     * Every handle here is borrowed. On the uploaded path only @c imageView is set and
     * the owning @ref Texture keeps image and memory; on the fallback path all three
     * alias the renderer's 1x1 white texture. Never destroy them - Shutdown clears
     * @c m_TextureCache without releasing anything, and a destroy loop over the cache
     * would double-free the white texture.
     */
    struct TextureResources
    {
        VkImage image;          ///< VK_NULL_HANDLE unless this is a white-texture entry.
        VkDeviceMemory memory;  ///< VK_NULL_HANDLE unless this is a white-texture entry.
        VkImageView imageView;  ///< Image view for shader sampling.
        bool initialized;       ///< True once the entry resolved to a usable view.
    };
    std::unordered_map<const Texture*, TextureResources> m_TextureCache;
    std::vector<const Texture*> m_UploadedTextures;
    std::unordered_set<const Texture*> m_UploadedTextureSet;  ///< O(1) dedup for uploads.
    /// @}

    /**
     * @name Initialization helpers
     * @{
     */
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
    /// @brief Create the single-subpass render pass: color attachment 0, depth 1.
    /// @pre `CreateDepthResources()` has run - the format is read from `m_DepthFormat`.
    void CreateRenderPass();
    /// @brief Compile shaders and create the graphics pipeline.
    void CreateGraphicsPipeline();
    /// @brief Create framebuffers for each swapchain image.
    void CreateFramebuffers();
    /// @brief Create the command pool for the graphics queue family.
    void CreateCommandPool();
    /// @brief Allocate the primary command buffers, one per swapchain image.
    /// @note Only run from Init(); @ref RecreateSwapchain does not reallocate them.
    void CreateCommandBuffers();
    /// @brief Create semaphores and fences for frame synchronization.
    void CreateSyncObjects();
    /**
     * @brief Destroy and recreate the image-available semaphore for a given
     * frame slot. Used after vkAcquireNextImageKHR returns VK_ERROR_OUT_OF_DATE_KHR
     * or after an EndFrame submit-path failure, both of which leave the
     * semaphore in an ambiguous "signaled but never waited" state that would
     * trip a validation error on the next acquire.
     */
    void RecreateImageAvailableSemaphore(size_t frame);
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

    /**
     * @name Texture helpers
     * @{
     */
    /**
     * @brief Get cached Vulkan resources for a texture or a white fallback entry.
     *
     * Currently unreachable: no draw path calls this. Every path resolves
     * `Texture::GetVulkanImageView()` directly with a white fallback, so
     * @c m_TextureCache stays empty. Kept as the seam for renderer-owned resources.
     *
     * @param texture CPU-side texture to look up.
     * @return Reference to the cache entry, valid until the cache is cleared.
     */
    TextureResources& GetOrCreateTexture(const Texture& texture);
    /**
     * @brief Get or allocate a combined-image-sampler descriptor set for an image view.
     *
     * The set is written with the shared @c m_TextureSampler and cached forever - no set
     * is freed before Shutdown.
     *
     * @note On pool exhaustion this allocates and permanently retains an overflow
     *       VkDescriptorPool of DESCRIPTOR_POOL_MAX_SETS sets.
     *
     * @param imageView Image view to bind.
     * @return Cached or newly allocated set, or VK_NULL_HANDLE when @p imageView is
     *         null, the pool is gone, or allocation fails. Callers must check.
     */
    VkDescriptorSet GetOrCreateDescriptorSet(VkImageView imageView);
    /**
     * @brief Compute a model matrix for sprite positioning.
     * @param position World position.
     * @param size Sprite dimensions.
     * @param rotation Rotation in degrees.
     */
    glm::mat4 CalculateModelMatrix(glm::vec2 position, glm::vec2 size, float rotation);
    /// @}

    /**
     * @name Buffer helpers
     * @{
     */
    /**
     * @brief Find a suitable memory type index for the given requirements.
     *
     * @warning Throws `std::runtime_error` when no type matches - there is no sentinel
     *          return. The other helpers in this group likewise throw through VK_CHECK,
     *          so none of them is usable from a `noexcept` or destructor context. Only
     *          `Init()` catches, so a throw reached from `RecreateSwapchain()` or
     *          `LoadFont()` escapes the frame loop.
     *
     * @param typeFilter Bit mask of acceptable memory type indices.
     * @param properties Required memory property flags.
     * @return Index of the first memory type in @p typeFilter carrying all of
     *         @p properties.
     */
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    /**
     * @brief Create a Vulkan buffer with backing memory.
     * @param size Buffer size in bytes.
     * @param usage Buffer usage flags.
     * @param properties Memory property flags.
     * @param buffer Output buffer handle.
     * @param bufferMemory Output memory handle.
     */
    void CreateBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer,
                      VkDeviceMemory& bufferMemory);
    /**
     * @brief Copy data between two Vulkan buffers using a one-shot command.
     * @param srcBuffer Source buffer.
     * @param dstBuffer Destination buffer.
     * @param size Number of bytes to copy.
     */
    void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    /**
     * @brief Transition a Vulkan image between layouts.
     *
     * @pre Called between BeginFrame() and EndFrame(): the barrier is recorded into the
     *      current frame's command buffer.
     * @warning Only UNDEFINED -> TRANSFER_DST_OPTIMAL and TRANSFER_DST_OPTIMAL ->
     *          SHADER_READ_ONLY_OPTIMAL are supported; any other pair throws
     *          `std::runtime_error`.
     *
     * @param image Image to transition.
     * @param format Ignored - every image this renderer transitions is a color image,
     *        so the aspect mask is always COLOR.
     * @param oldLayout Current layout.
     * @param newLayout Target layout.
     */
    void TransitionImageLayout(VkImage image,
                               VkFormat format,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout);
    /**
     * @brief Copy buffer contents to a Vulkan image.
     *
     * @pre Called between BeginFrame() and EndFrame(): the copy is recorded into the
     *      current frame's command buffer.
     * @pre @p image is already in TRANSFER_DST_OPTIMAL.
     *
     * @param buffer Source staging buffer.
     * @param image Destination image.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     */
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    /**
     * @brief Upload staging buffer to image with layout transitions.
     * @param stagingBuffer Source staging buffer.
     * @param image Destination image.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     */
    void UploadStagingBufferToImage(VkBuffer stagingBuffer,
                                    VkImage image,
                                    uint32_t width,
                                    uint32_t height);
    /// @}

    /**
     * @name Queue families
     * @{
     */
    uint32_t m_GraphicsFamily{0};
    uint32_t m_PresentFamily{0};
    /// @}

    /**
     * @name Validation and extensions
     * @{
     */
    const std::vector<const char*> m_ValidationLayers = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> m_DeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();
    /// @}
};
