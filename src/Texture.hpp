#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/glad.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

/**
 * @class Texture
 * @brief GPU texture resource shared by the OpenGL and Vulkan backends.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * A Texture keeps its pixels on the CPU for its whole lifetime and treats both GPU handles as a
 * cache it can rebuild. That is what makes runtime backend switching work: `Game::SwitchRenderer`
 * destroys one backend, recreates the window, and re-uploads every texture from its CPU copy.
 *
 * @verbatim
 *   [Image File] --LoadFromFile()--> [CPU Buffer] --Upload--> [GPU Memory]
 *                                         |                        |
 *                                    m_ImageData              m_OpenGLID or
 *                                   (always kept)             m_VulkanImage
 * @endverbatim
 *
 * Loading normalizes every source to 4-channel RGBA. Vulkan does not universally support
 * `VK_FORMAT_R8G8B8_UNORM` with optimal tiling, so 3-channel images must expand anyway;
 * normalizing all of them lets both backends share one upload path.
 *
 * @par Texture coordinate origin
 * @verbatim
 *   OpenGL:              Vulkan:
 *   (0,1)-----(1,1)      (0,0)-----(1,0)
 *     |         |          |         |
 *     |  Image  |          |  Image  |
 *     |         |          |         |
 *   (0,0)-----(1,0)      (0,1)-----(1,1)
 * @endverbatim
 * Loading flips images vertically for OpenGL. Vulkan compensates by flipping UVs instead.
 *
 * @par GPU handle ownership across backend teardown
 * A Texture outlives the renderer that uploaded it, so the two backends release their handles
 * under deliberately different rules.
 * - **OpenGL.** Deleting a GL name is valid only while its owning context is current. Texture
 *   deletes the name only when @ref GetOpenGLContextGeneration still equals
 *   @ref GetCurrentOpenGLContextGeneration. On a mismatch the owning context is already gone, so
 *   Texture **abandons** the name instead of deleting it - a delete would hit an unrelated object
 *   in the new context. The destructor and move assignment apply the same rule.
 * - **Vulkan.** There is no generation check. @ref CreateVulkanTexture stores the `VkDevice`, and
 *   the destructor uses it to free any surviving handles after logging a warning. That warning
 *   means the renderer's `Shutdown()` missed this texture. The path is a backstop, not the
 *   lifecycle, and it is valid only while the `VkDevice` is still alive. Destroy every texture
 *   before the device, or call @ref DestroyVulkanTexture explicitly.
 *
 * @par Lifecycle
 * @code{.cpp}
 * Texture tex;
 * tex.LoadFromFile("sprites/player.png");  // CPU copy now; GL upload too if a context is current.
 *
 * std::vector<unsigned char> pixels(64 * 64 * 4);
 * tex.LoadFromData(pixels.data(), 64, 64, 4, true);
 *
 * tex.Bind(0);
 * tex.Unbind();
 *
 * tex.RecreateOpenGLTexture();  // After a context switch, rebuild from the CPU copy.
 *
 * tex.CreateVulkanTexture(device, physicalDevice, commandPool, queue);
 * tex.DestroyVulkanTexture(device);
 * @endcode
 *
 * Move-only, and not thread-safe: call every method on the render thread.
 *
 * @see IRenderer
 */
class Texture
{
public:
    /// @name Constructors and destructor
    /// @{

    /// @brief Construct a texture with no pixels and no GPU resources.
    Texture();

    /// Copying would have to duplicate GPU memory, so Texture is move-only.
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    /// @brief Take over @p other's CPU buffer and GPU handles, leaving @p other empty.
    Texture(Texture&& other) noexcept;

    /**
     * @brief Release this texture's resources, then take over @p other's.
     *
     * Releasing follows the same context-generation rule as the destructor.
     *
     * @param other  Texture to move from. Left empty.
     * @return       Reference to this texture.
     */
    Texture& operator=(Texture&& other) noexcept;

    /**
     * @brief Release the CPU buffer and any surviving GPU resources.
     *
     * Deletes the GL name only when a context is current and the context generation still
     * matches; otherwise it abandons the name. Destroys surviving Vulkan resources through the
     * stored device handle after logging a warning. Both rules are described in "GPU handle
     * ownership across backend teardown" above.
     */
    ~Texture();

    /// @}

    /// @name Loading methods
    /// @{

    /**
     * @brief Load pixels from an image file and keep a CPU copy.
     *
     * Creates the OpenGL texture immediately when a context is current. Vulkan creation stays
     * deferred until @ref CreateVulkanTexture. Decoding uses stb_image, so PNG, JPEG, BMP, TGA,
     * PSD, HDR, PIC and PNM all load; GIF yields its first frame only.
     *
     * Fails when the file cannot be decoded, when it has fewer than 1 or more than 4 channels,
     * or when width * height * 4 would overflow `size_t`. Every failure logs through Logger, but
     * they do not leave the same state: a decode failure returns with the texture completely
     * unchanged, while any later failure clears the pixel buffer yet keeps the width, height and
     * channel count of the rejected image. `GetImageData().empty()`, not `GetWidth() == 0`, is
     * therefore the reliable is-loaded test.
     *
     * @param path  Image file path, relative or absolute.
     * @return      True on success, false on any failure above.
     */
    bool LoadFromFile(const std::string& path);

    /**
     * @brief Load pixels from an in-memory buffer, for procedural textures.
     *
     * @p data must hold @p width * @p height consecutive pixels of @p channels bytes each:
     * 1 = grayscale, 2 = grayscale + alpha, 3 = RGB, 4 = RGBA. Anything other than 4 expands to
     * RGBA before storage, so @ref GetChannels reports 4 afterwards.
     *
     * @param data      Pixel buffer. Must not be null.
     * @param width     Image width in pixels. Must be greater than 0.
     * @param height    Image height in pixels. Must be greater than 0.
     * @param channels  Bytes per pixel in @p data, from 1 to 4.
     * @param flipY     Whether to flip the image vertically for OpenGL's origin.
     * @return          True on success, false when a parameter is out of range.
     */
    bool LoadFromData(unsigned char* data, int width, int height, int channels, bool flipY = true);

    /// @}

    /// @name OpenGL operations
    /// @{

    /// @brief Activate texture unit @p slot and bind this texture to it.
    void Bind(unsigned int slot = 0) const;

    /**
     * @brief Bind texture 0 to GL_TEXTURE_2D on the currently active texture unit.
     *
     * This never selects a unit, so it clears whichever unit is active at call time - not
     * necessarily the slot that was passed to @ref Bind.
     */
    void Unbind() const;

    /// @brief Get the OpenGL texture name, or 0 when nothing is uploaded.
    unsigned int GetID() const { return m_OpenGLID; }

    /**
     * @brief Upload the CPU buffer into a new GL texture after a context change.
     *
     * Destroying and recreating a GL context invalidates every texture name. Call this to
     * rebuild the GPU side from the retained pixels.
     *
     * @pre  The CPU buffer holds pixel data.
     * @pre  A GL context is current. In Vulkan mode it is not, which is a real caller path.
     * @post The texture owns a valid GL name in the current context when both preconditions
     *       hold. Otherwise the call logs an error and leaves the GL name at 0; being void,
     *       it has no other way to report that.
     */
    void RecreateOpenGLTexture() const;

    /**
     * @brief Advance the global context generation after creating a GL context.
     *
     * Call this once per new context, before any texture is used with it. Every texture uploaded
     * earlier then compares as older, which is what makes their stale GL names be abandoned
     * rather than deleted.
     */
    static void AdvanceOpenGLContextGeneration();

    /**
     * @brief Get the generation of the GL context that is current now.
     * @return  Monotonic counter. A texture's GL name is usable only while its own generation
     *          equals this value.
     */
    static std::uint64_t GetCurrentOpenGLContextGeneration();

    /// @}

    /// @name Vulkan operations
    /// @{

    /// @brief Get the image view shaders sample, or VK_NULL_HANDLE before creation.
    VkImageView GetVulkanImageView() const { return m_VulkanImageView; }

    /// @brief Get the sampler shaders filter with, or VK_NULL_HANDLE before creation.
    VkSampler GetVulkanSampler() const { return m_VulkanSampler; }

    /**
     * @brief Create every Vulkan resource this texture needs and upload its pixels.
     *
     * Runs the full setup in order:
     * 1. Create the VkImage with optimal tiling.
     * 2. Allocate device-local memory.
     * 3. Create the VkImageView.
     * 4. Create the VkSampler with nearest-neighbor filtering.
     * 5. Upload the pixels through a staging buffer.
     * 6. Transition the image to SHADER_READ_ONLY_OPTIMAL.
     *
     * @param device          Logical device. Remembered for the destructor's backstop teardown.
     * @param physicalDevice  Physical device, used to query memory types.
     * @param commandPool     Pool the transfer commands allocate from.
     * @param queue           Queue that executes the upload.
     *
     * @pre  The CPU buffer holds pixel data.
     * @post Every Vulkan handle is valid.
     * @note An empty CPU buffer is the one failure that does not throw: it logs and returns
     *       with the handles untouched, so a caller that must know checks
     *       @ref GetVulkanImageView afterwards. Every other failure throws
     *       `std::runtime_error` - including the three validations raised before the first
     *       Vulkan call: a channel count other than 4, a size overflow, and a CPU buffer
     *       shorter than width * height * 4.
     */
    void CreateVulkanTexture(VkDevice device,
                             VkPhysicalDevice physicalDevice,
                             VkCommandPool commandPool,
                             VkQueue queue) const;

    /**
     * @brief Destroy this texture's Vulkan resources.
     *
     * Frees sampler, image view, image and memory in reverse creation order, then clears the
     * stored device handle so the destructor has nothing left to warn about. Calling it more
     * than once is safe.
     *
     * @param device  Logical device, or VK_NULL_HANDLE to reuse the device
     *                @ref CreateVulkanTexture captured. When both are null the handles are
     *                dropped without any Vulkan call.
     */
    void DestroyVulkanTexture(VkDevice device) const;

    /// @}

    /// @name Accessors
    /// @{

    /// @brief Get the width in pixels, or 0 before the first successful load.
    int GetWidth() const { return m_Width; }

    /// @brief Get the height in pixels, or 0 before the first successful load.
    int GetHeight() const { return m_Height; }

    /**
     * @brief Get the stored channel count.
     * @return  Always 4 after a successful load, because loading expands non-RGBA sources. This
     *          differs from the channel count of the source file or input buffer.
     */
    int GetChannels() const { return m_Channels; }

    /// @brief Get the context generation this texture's GL name was created in.
    std::uint64_t GetOpenGLContextGeneration() const { return m_OpenGLContextGeneration; }

    /// @brief Get read-only access to the retained CPU pixel buffer.
    const std::vector<unsigned char>& GetImageData() const { return m_ImageData; }

    /**
     * @brief Pick a representative non-skin color from the stored pixels.
     *
     * Walks the CPU buffer once, converts each pixel to HSV, rejects the pixels listed below, and
     * returns the survivor with the highest @f$ saturation \times value @f$. This derives a
     * per-NPC accent tint for UI when no palette is authored.
     *
     * @par Rejection filters
     * - Alpha below 128, so transparent pixels never win.
     * - Saturation below 0.30, which drops greys, near-whites and near-blacks. It also drops
     *   real highlights, which have low saturation by definition.
     * - Value below 0.25, which drops shadows.
     * - Hue within [0deg, 30deg] while saturation is within [0.20, 0.60], the skin-tone band.
     *
     * @param fallback  Color returned when no pixel survives the filters.
     * @return          RGB of the most vibrant surviving pixel, or @p fallback.
     */
    glm::vec3 SampleDominantNonSkinColor(glm::vec3 fallback) const;

    /// @}

private:
    /// @name Internal methods
    /// @{

    /**
     * @brief Generate a GL name, upload @p data, and set filtering and wrapping.
     *
     * Filtering is NEAREST and wrapping is CLAMP_TO_EDGE.
     *
     * @param data   Pixels to upload.
     * @param flipY  Unused. Callers flip before reaching this point.
     */
    void CreateOpenGLTexture(const unsigned char* data, bool flipY) const;

    /// @}

    static std::uint64_t s_CurrentOpenGLContextGeneration;  ///< Global context generation counter.

    /**
     * @name OpenGL resources
     *
     * These members are `mutable` because uploading to the GPU is lazy initialization of a cache.
     * It rebuilds GPU state without changing the pixels or dimensions the texture represents, so
     * the upload methods stay logically const.
     * @{
     */
    mutable unsigned int m_OpenGLID{0};  ///< GL texture name; 0 = not created.
    /**
     * @brief GLFWwindow* of the context that created m_OpenGLID, kept for diagnostics.
     *
     * Ownership decisions use m_OpenGLContextGeneration, not this pointer, because a recreated
     * window can reuse the same address.
     */
    mutable void* m_OpenGLContextTag{nullptr};
    /**
     * @brief Value of s_CurrentOpenGLContextGeneration when m_OpenGLID was created.
     *
     * A mismatch means the owning context is gone, so the name is abandoned rather than deleted.
     */
    mutable std::uint64_t m_OpenGLContextGeneration{0};
    /// @}

    /// @name Vulkan resources (mutable for the same lazy-upload reason as the OpenGL group)
    /// @{
    mutable VkImage m_VulkanImage{VK_NULL_HANDLE};               ///< Device-local image.
    mutable VkDeviceMemory m_VulkanImageMemory{VK_NULL_HANDLE};  ///< Memory backing the image.
    mutable VkImageView m_VulkanImageView{VK_NULL_HANDLE};       ///< View shaders sample through.
    mutable VkSampler m_VulkanSampler{VK_NULL_HANDLE};           ///< Nearest-neighbor sampler.
    /**
     * @brief Non-owning VkDevice the handles above belong to.
     *
     * @ref CreateVulkanTexture captures it and @ref DestroyVulkanTexture clears it. A non-null
     * value is what lets the destructor free a missed texture, and what turns destroying the
     * VkDevice first into a use-after-free rather than a leak.
     */
    mutable VkDevice m_VulkanDevice{VK_NULL_HANDLE};
    /// @}

    /// @name Image data
    /// @{
    int m_Width{0};                          ///< Width in pixels.
    int m_Height{0};                         ///< Height in pixels.
    int m_Channels{0};                       ///< Stored channels; 4 after any successful load.
    std::vector<unsigned char> m_ImageData;  ///< Retained RGBA pixels; source for every upload.
    /// @}
};
