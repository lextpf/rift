#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <vulkan/vulkan.h>

/**
 * @class Texture
 * @brief GPU texture resource with support for OpenGL and Vulkan backends.
 * @author Alex (https://github.com/lextpf)
 *
 * The Texture class provides a unified interface for loading and managing
 * textures across different graphics APIs. It maintains a CPU-side copy of
 * image data to support deferred GPU upload and context recreation.
 *
 * @par Supported Formats
 * Image loading uses stb_image, supporting these formats:
 * | Format | Extensions        | Notes                    |
 * |--------|-------------------|--------------------------|
 * | PNG    | .png              | Recommended for sprites  |
 * | JPEG   | .jpg, .jpeg       | Lossy, no alpha support  |
 * | BMP    | .bmp              | Uncompressed             |
 * | TGA    | .tga              | With/without RLE         |
 * | GIF    | .gif              | First frame only         |
 *
 * @par Memory Model
 * Each texture maintains three potential copies of pixel data:
 * @code
 *   [Image File] --LoadFromFile()--> [CPU Buffer] --Upload--> [GPU Memory]
 *                                         |                        |
 *                                    m_ImageData              m_OpenGLID or
 *                                   (always kept)             m_VulkanImage
 * @endcode
 *
 * The CPU buffer is retained because:
 * - OpenGL textures become invalid after context switches
 * - Vulkan texture creation is deferred (requires device handles)
 * - Enables runtime renderer switching
 *
 * @par Coordinate System
 * OpenGL and Vulkan have different texture coordinate conventions:
 * @code
 *   OpenGL:              Vulkan:
 *   (0,1)-----(1,1)      (0,0)-----(1,0)
 *     |         |          |         |
 *     |  Image  |          |  Image  |
 *     |         |          |         |
 *   (0,0)-----(1,0)      (0,1)-----(1,1)
 * @endcode
 *
 * Images are stored flipped for OpenGL. Vulkan compensates via UV flipping.
 *
 * @par Ownership Semantics
 * Textures are **move-only** (non-copyable) because they own GPU resources.
 * Copying would require duplicating GPU memory, which is expensive and
 * often unintended. Use std::move() or store in containers that support
 * move semantics.
 *
 * @par Lifecycle
 * @code
 * // From file:
 * Texture tex;
 * tex.LoadFromFile("sprites/player.png");  // Loads to CPU; creates OpenGL texture if a GL context
 * is active
 *
 * // From procedural data:
 * std::vector<unsigned char> pixels(64 * 64 * 4);
 * // ... fill pixels ...
 * tex.LoadFromData(pixels.data(), 64, 64, 4, true);
 *
 * // Usage in OpenGL:
 * tex.Bind(0);  // Bind to texture unit 0
 * // ... draw ...
 * tex.Unbind();
 *
 * // After context switch:
 * tex.RecreateOpenGLTexture();  // Recreates from CPU copy
 *
 * // For Vulkan (deferred creation):
 * tex.CreateVulkanTexture(device, physicalDevice, commandPool, queue);
 * // ... use tex.GetVulkanImageView() and tex.GetVulkanSampler() ...
 * tex.DestroyVulkanTexture(device);  // Or let destructor handle it
 * @endcode
 *
 * @par Thread Safety
 * Not thread-safe. All texture operations must occur on the main/render thread.
 *
 * @see IRenderer for texture rendering operations
 */
class Texture
{
public:
    /// @name Constructors and Destructor
    /// @{

    /**
     * @brief Construct an empty texture.
     *
     * Creates an uninitialized texture with no GPU resources.
     * Call LoadFromFile() or LoadFromData() to initialize.
     */
    Texture();

    /**
     * @brief Copy constructor (deleted).
     *
     * Textures cannot be copied because they own GPU resources.
     * Use std::move() to transfer ownership instead.
     */
    Texture(const Texture&) = delete;

    /**
     * @brief Copy assignment (deleted).
     *
     * Textures cannot be copied because they own GPU resources.
     */
    Texture& operator=(const Texture&) = delete;

    /**
     * @brief Move constructor.
     *
     * Transfers ownership of all resources (CPU buffer, GPU handles) from
     * the source texture. The source texture is left in an empty state.
     *
     * @param other Texture to move from (will be emptied).
     */
    Texture(Texture&& other) noexcept;

    /**
     * @brief Move assignment operator.
     *
     * Releases any existing resources, then transfers ownership from the
     * source texture. The source is left empty.
     *
     * @param other Texture to move from (will be emptied).
     * @return Reference to this texture.
     */
    Texture& operator=(Texture&& other) noexcept;

    /**
     * @brief Destructor releases all resources.
     *
     * Frees CPU buffer and destroys GPU resources if they exist.
     * For OpenGL, checks that a valid context exists before cleanup.
     * For Vulkan, uses the stored device handle.
     */
    ~Texture();

    /// @}

    /// @name Loading Methods
    /// @{

    /**
     * @brief Load texture from an image file.
     *
     * Loads image data using stb_image and stores a CPU copy. If an active
     * OpenGL context exists, an OpenGL texture is created immediately.
     * Vulkan texture creation is deferred.
     *
     * @par Supported Formats
     * PNG, JPEG, BMP, TGA, GIF (first frame), PSD, HDR, PIC, PNM
     *
     * @par Vertical Flip
     * Images are automatically flipped vertically to match OpenGL's
     * bottom-left origin convention.
     *
     * @param path Path to image file (relative or absolute).
     * @return true if loaded successfully, false on error (logged to stderr).
     */
    bool LoadFromFile(const std::string& path);

    /**
     * @brief Load texture from raw pixel data.
     *
     * Creates a texture from an in-memory pixel buffer. Useful for
     * procedurally generated textures (noise, gradients, etc.).
     *
     * @par Pixel Format
     * Data must be packed as consecutive pixels, each with `channels` bytes:
     * - 1 channel: grayscale (R)
     * - 3 channels: RGB
     * - 4 channels: RGBA
     *
     * @param data    Pointer to pixel data (not null).
     * @param width   Image width in pixels (must be > 0).
     * @param height  Image height in pixels (must be > 0).
     * @param channels Number of color channels (must be > 0).
     *                 Supported formats are 1 (R), 3 (RGB), and 4 (RGBA).
     * @param flipY   If true, flip image vertically for OpenGL coordinates.
     * @return true if loaded successfully, false on invalid parameters.
     */
    bool LoadFromData(unsigned char* data, int width, int height, int channels, bool flipY = true);

    /// @}

    /// @name OpenGL Operations
    /// @{

    /**
     * @brief Bind texture to an OpenGL texture unit.
     *
     * Activates the specified texture unit and binds this texture to it.
     * The texture will be sampled by shaders using the corresponding
     * sampler uniform (sampler2D).
     *
     * @param slot Texture unit index (0-15 typically, 0 is default).
     */
    void Bind(unsigned int slot = 0) const;

    /**
     * @brief Unbind texture from current texture unit.
     *
     * Binds texture ID 0 to GL_TEXTURE_2D, clearing the binding.
     */
    void Unbind() const;

    /**
     * @brief Get the OpenGL texture ID.
     *
     * @return OpenGL texture name (0 if not created).
     */
    unsigned int GetID() const { return m_OpenGLID; }

    /**
     * @brief Recreate OpenGL texture after context change.
     *
     * When the OpenGL context is destroyed and recreated (e.g., when
     * switching renderers), all texture IDs become invalid. This method
     * recreates the GPU texture from the stored CPU buffer.
     *
     * This is a logically const operation: it lazily initializes or
     * refreshes the GPU-side cache without changing the texture's
     * pixel data or dimensions.
     *
     * @pre m_ImageData must contain valid pixel data.
     * @post m_OpenGLID contains a new valid texture ID.
     */
    void RecreateOpenGLTexture() const;

    /**
     * @brief Advance global OpenGL context generation after creating a new GL context.
     */
    static void AdvanceOpenGLContextGeneration();

    /**
     * @brief Get active OpenGL context generation.
     */
    static std::uint64_t GetCurrentOpenGLContextGeneration();

    /// @}

    /// @name Vulkan Operations
    /// @{

    /**
     * @brief Get the Vulkan image view for shader sampling.
     *
     * @return VkImageView handle (VK_NULL_HANDLE if not created).
     */
    VkImageView GetVulkanImageView() const { return m_VulkanImageView; }

    /**
     * @brief Get the Vulkan sampler for texture filtering.
     *
     * @return VkSampler handle (VK_NULL_HANDLE if not created).
     */
    VkSampler GetVulkanSampler() const { return m_VulkanSampler; }

    /**
     * @brief Create Vulkan texture resources.
     *
     * Performs the complete Vulkan texture setup:
     * 1. Creates VkImage with OPTIMAL tiling
     * 2. Allocates device-local GPU memory
     * 3. Creates VkImageView for shader access
     * 4. Creates VkSampler with nearest-neighbor filtering
     * 5. Uploads pixel data via staging buffer
     * 6. Transitions image layout to SHADER_READ_ONLY_OPTIMAL
     *
     * This is a logically const operation: it lazily initializes the
     * GPU-side resources without changing the texture's pixel data
     * or dimensions.
     *
     * @param device         Vulkan logical device.
     * @param physicalDevice Vulkan physical device (for memory type queries).
     * @param commandPool    Command pool for transfer commands.
     * @param queue          Queue for executing the upload.
     *
     * @pre m_ImageData must contain valid pixel data.
     * @post All m_Vulkan* handles are valid.
     * @throws std::runtime_error on Vulkan API failures.
     */
    void CreateVulkanTexture(VkDevice device,
                             VkPhysicalDevice physicalDevice,
                             VkCommandPool commandPool,
                             VkQueue queue) const;

    /**
     * @brief Destroy Vulkan texture resources.
     *
     * Frees sampler, image view, image, and device memory in reverse
     * creation order. Safe to call multiple times.
     *
     * This is a logically const operation: it tears down the GPU-side
     * cache without changing the texture's pixel data or dimensions.
     *
     * @param device Vulkan logical device.
     */
    void DestroyVulkanTexture(VkDevice device) const;

    /// @}

    /// @name Accessors
    /// @{

    /**
     * @brief Get texture width.
     * @return Width in pixels (0 if not loaded).
     */
    int GetWidth() const { return m_Width; }

    /**
     * @brief Get texture height.
     * @return Height in pixels (0 if not loaded).
     */
    int GetHeight() const { return m_Height; }

    /**
     * @brief Get number of color channels.
     * @return Channel count: 1 (grayscale), 3 (RGB), or 4 (RGBA).
     */
    int GetChannels() const { return m_Channels; }

    /**
     * @brief Get the OpenGL context generation this texture was created in.
     * @return Context generation counter.
     */
    std::uint64_t GetOpenGLContextGeneration() const { return m_OpenGLContextGeneration; }

    /**
     * @brief Get read-only access to the CPU pixel buffer.
     * @return Const reference to the pixel data vector.
     */
    const std::vector<unsigned char>& GetImageData() const { return m_ImageData; }

    /// @}

private:
    /// @name Internal Methods
    /// @{

    /**
     * @brief Create OpenGL texture from pixel data.
     *
     * Generates a texture ID, uploads pixel data, and configures
     * filtering (NEAREST) and wrapping (CLAMP_TO_EDGE).
     *
     * @param data  Pixel data to upload.
     * @param flipY Unused (flip is handled before this call).
     */
    void CreateOpenGLTexture(const unsigned char* data, bool flipY) const;

    static std::uint64_t s_CurrentOpenGLContextGeneration;  ///< Global context generation counter

    /// @}

    /// @name OpenGL Resources (mutable: GPU upload is a lazy-init/caching operation)
    /// @{
    mutable unsigned int m_OpenGLID{0};
    mutable void* m_OpenGLContextTag{nullptr};
    mutable std::uint64_t m_OpenGLContextGeneration{0};
    /// @}

    /// @name Vulkan Resources (mutable: GPU upload is a lazy-init/caching operation)
    /// @{
    mutable VkImage m_VulkanImage{VK_NULL_HANDLE};
    mutable VkDeviceMemory m_VulkanImageMemory{VK_NULL_HANDLE};
    mutable VkImageView m_VulkanImageView{VK_NULL_HANDLE};
    mutable VkSampler m_VulkanSampler{VK_NULL_HANDLE};
    mutable VkDevice m_VulkanDevice{VK_NULL_HANDLE};
    /// @}

    /// @name Image Data
    /// @{
    int m_Width{0};
    int m_Height{0};
    int m_Channels{0};
    std::vector<unsigned char> m_ImageData;
    /// @}
};
