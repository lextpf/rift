#include "Texture.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <utility>

// stb_image is a header-only library - this define tells it to include the implementation
// Only define this in ONE .cpp file to avoid duplicate symbol errors
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

std::uint64_t Texture::s_CurrentOpenGLContextGeneration = 0;

namespace
{
bool TryComputeImageByteSize(int width, int height, int channels, size_t& outSize)
{
    if (width <= 0 || height <= 0 || channels <= 0)
        return false;

    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    const size_t c = static_cast<size_t>(channels);

    if (w > std::numeric_limits<size_t>::max() / h)
        return false;
    const size_t pixels = w * h;
    if (pixels > std::numeric_limits<size_t>::max() / c)
        return false;

    outSize = pixels * c;
    return true;
}

bool ExpandToRgba(const unsigned char* src,
                  size_t srcSize,
                  int width,
                  int height,
                  int srcChannels,
                  std::vector<unsigned char>& outRgba)
{
    if (!src || width <= 0 || height <= 0 || srcChannels < 1 || srcChannels > 4)
        return false;

    size_t expectedSrcSize = 0;
    if (!TryComputeImageByteSize(width, height, srcChannels, expectedSrcSize))
        return false;
    if (srcSize < expectedSrcSize)
        return false;

    size_t rgbaSize = 0;
    if (!TryComputeImageByteSize(width, height, 4, rgbaSize))
        return false;

    outRgba.resize(rgbaSize);
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const size_t srcBase = i * static_cast<size_t>(srcChannels);
        const size_t dstBase = i * 4;

        unsigned char r = 255;
        unsigned char g = 255;
        unsigned char b = 255;
        unsigned char a = 255;

        if (srcChannels == 1)
        {
            r = g = b = src[srcBase];
        }
        else if (srcChannels == 2)
        {
            r = g = b = src[srcBase];
            a = src[srcBase + 1];
        }
        else if (srcChannels == 3)
        {
            r = src[srcBase];
            g = src[srcBase + 1];
            b = src[srcBase + 2];
        }
        else  // srcChannels == 4
        {
            r = src[srcBase];
            g = src[srcBase + 1];
            b = src[srcBase + 2];
            a = src[srcBase + 3];
        }

        outRgba[dstBase] = r;
        outRgba[dstBase + 1] = g;
        outRgba[dstBase + 2] = b;
        outRgba[dstBase + 3] = a;
    }

    return true;
}
}  // namespace

Texture::Texture()
    : m_Width(0)  // Image width in pixels (0 until loaded)
      ,
      m_Height(0)  // Image height in pixels (0 until loaded)
      ,
      m_Channels(0)  // Color channels: 3=RGB, 4=RGBA (0 until loaded)
      ,
      m_ImageData()  // CPU-side pixel buffer (retained for re-upload)
      ,
      m_OpenGLID(0)  // OpenGL texture handle (0 = not uploaded)
      ,
      m_OpenGLContextTag(nullptr)  // Context that owns m_OpenGLID
      ,
      m_OpenGLContextGeneration(0)  // Context generation that owns m_OpenGLID
      ,
      m_VulkanImage(VK_NULL_HANDLE)  // Vulkan image handle (device-local memory)
      ,
      m_VulkanImageMemory(VK_NULL_HANDLE)  // Vulkan memory backing the image
      ,
      m_VulkanImageView(VK_NULL_HANDLE)  // Vulkan image view for shader sampling
      ,
      m_VulkanSampler(VK_NULL_HANDLE)  // Vulkan sampler (filtering, wrapping)
      ,
      m_VulkanDevice(VK_NULL_HANDLE)  // Vulkan device that owns these resources
{
}

// Move constructor - transfers ownership of GPU resources from another texture.
// This is important for storing textures in containers (vector, etc.) without
// accidentally freeing GPU resources when temporaries are destroyed.
Texture::Texture(Texture&& other) noexcept
    : m_Width(other.m_Width),
      m_Height(other.m_Height),
      m_Channels(other.m_Channels),
      m_ImageData(std::move(other.m_ImageData)),
      m_OpenGLID(other.m_OpenGLID),
      m_OpenGLContextTag(other.m_OpenGLContextTag),
      m_OpenGLContextGeneration(other.m_OpenGLContextGeneration),
      m_VulkanImage(other.m_VulkanImage),
      m_VulkanImageMemory(other.m_VulkanImageMemory),
      m_VulkanImageView(other.m_VulkanImageView),
      m_VulkanSampler(other.m_VulkanSampler),
      m_VulkanDevice(other.m_VulkanDevice)
{
    // Null out the source object's resources so its destructor won't free them.
    // We now own these resources exclusively.
    other.m_Width = other.m_Height = other.m_Channels = 0;
    other.m_OpenGLID = 0;
    other.m_OpenGLContextTag = nullptr;
    other.m_OpenGLContextGeneration = 0;
    other.m_VulkanImage = VK_NULL_HANDLE;
    other.m_VulkanImageMemory = VK_NULL_HANDLE;
    other.m_VulkanImageView = VK_NULL_HANDLE;
    other.m_VulkanSampler = VK_NULL_HANDLE;
    other.m_VulkanDevice = VK_NULL_HANDLE;
}

// Move assignment - same idea as move constructor but we might already own resources.
// Must free our existing resources before stealing from the other object.
Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        // First, clean up any resources we currently own
        // Check for valid GL context - during shutdown the context may already be gone
        GLFWwindow* currentContext = glfwGetCurrentContext();
        if (m_OpenGLID != 0 && currentContext != nullptr &&
            m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration)
        {
            glDeleteTextures(1, &m_OpenGLID);
        }
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
        if (m_VulkanDevice != VK_NULL_HANDLE)
        {
            DestroyVulkanTexture(m_VulkanDevice);
        }
        m_ImageData.clear();

        // Now steal the other object's resources
        m_Width = other.m_Width;
        m_Height = other.m_Height;
        m_Channels = other.m_Channels;
        m_ImageData = std::move(other.m_ImageData);
        m_OpenGLID = other.m_OpenGLID;
        m_OpenGLContextTag = other.m_OpenGLContextTag;
        m_OpenGLContextGeneration = other.m_OpenGLContextGeneration;
        m_VulkanImage = other.m_VulkanImage;
        m_VulkanImageMemory = other.m_VulkanImageMemory;
        m_VulkanImageView = other.m_VulkanImageView;
        m_VulkanSampler = other.m_VulkanSampler;
        m_VulkanDevice = other.m_VulkanDevice;

        // Null out the source so its destructor doesn't double-free
        other.m_Width = other.m_Height = other.m_Channels = 0;
        other.m_OpenGLID = 0;
        other.m_OpenGLContextTag = nullptr;
        other.m_OpenGLContextGeneration = 0;
        other.m_VulkanImage = VK_NULL_HANDLE;
        other.m_VulkanImageMemory = VK_NULL_HANDLE;
        other.m_VulkanImageView = VK_NULL_HANDLE;
        other.m_VulkanSampler = VK_NULL_HANDLE;
        other.m_VulkanDevice = VK_NULL_HANDLE;
    }
    return *this;
}

Texture::~Texture()
{
    // OpenGL textures must be deleted while the GL context is still valid.
    // During application shutdown, GLFW may destroy the context before our
    // destructor runs, so we check glfwGetCurrentContext() to avoid crashes.
    GLFWwindow* currentContext = glfwGetCurrentContext();
    if (m_OpenGLID != 0 && currentContext != nullptr &&
        m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration)
    {
        glDeleteTextures(1, &m_OpenGLID);
    }
    m_OpenGLID = 0;
    m_OpenGLContextTag = nullptr;
    m_OpenGLContextGeneration = 0;

    // Vulkan resources must be destroyed in a specific order and require the device handle
    if (m_VulkanDevice != VK_NULL_HANDLE && m_VulkanImage != VK_NULL_HANDLE)
    {
        // Vulkan resources should have been explicitly destroyed before the
        // destructor runs. If we reach here, the renderer's Shutdown() missed
        // this texture. Destroy now to avoid leaking, but log a warning.
        std::cerr << "Warning: Texture destructor destroying Vulkan resources - "
                  << "DestroyVulkanTexture should have been called before device destruction"
                  << std::endl;
        DestroyVulkanTexture(m_VulkanDevice);
    }

    // Free CPU-side image data last
    m_ImageData.clear();
}

bool Texture::LoadFromFile(const std::string& path)
{
    // stb_image loads images with (0,0) at the top-left by default.
    // OpenGL expects (0,0) at the bottom-left, so we flip vertically.
    // Use stbi__vertically_flip_on_load_set to avoid global state mutation
    // (stbi_set_flip_vertically_on_load modifies a process-wide flag that
    // would be unsafe under concurrent loading or third-party stbi use).
    stbi_set_flip_vertically_on_load_thread(true);

    // stb_load returns the actual channel count in m_Channels.
    // The last param (0) means "give me whatever channels the file has".
    unsigned char* data = stbi_load(path.c_str(), &m_Width, &m_Height, &m_Channels, 0);
    if (!data)
    {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return false;
    }

    m_ImageData.clear();

    if (m_Channels < 1 || m_Channels > 4)
    {
        std::cerr << "Unsupported channel count (" << m_Channels << ") for texture: " << path
                  << std::endl;
        stbi_image_free(data);
        return false;
    }

    // Keep a CPU copy of the image data. This allows us to:
    // 1. Recreate the OpenGL texture after a context switch
    // 2. Create a Vulkan texture later (deferred creation)
    // 3. Support multiple graphics backends from the same source
    std::vector<unsigned char> normalizedData;
    const unsigned char* sourceData = data;
    int sourceChannels = m_Channels;

    // Normalize non-RGBA formats to RGBA so both backends share one safe path.
    // VK_FORMAT_R8G8B8_UNORM is not universally supported for optimal tiling,
    // so 3-channel textures must also be expanded.
    if (sourceChannels != 4)
    {
        const size_t srcSize = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) *
                               static_cast<size_t>(sourceChannels);
        if (!ExpandToRgba(data, srcSize, m_Width, m_Height, sourceChannels, normalizedData))
        {
            std::cerr << "Failed to expand texture to RGBA: " << path << std::endl;
            stbi_image_free(data);
            return false;
        }
        sourceData = normalizedData.data();
        sourceChannels = 4;
    }

    size_t dataSize = 0;
    if (!TryComputeImageByteSize(m_Width, m_Height, sourceChannels, dataSize))
    {
        std::cerr << "Texture is too large to load safely: " << path << std::endl;
        stbi_image_free(data);
        return false;
    }
    m_ImageData.resize(dataSize);
    memcpy(m_ImageData.data(), sourceData, dataSize);
    m_Channels = sourceChannels;

    // Create OpenGL texture immediately only if a context is active.
    // In Vulkan mode there is no GL context; keep CPU data and upload later.
    if (glfwGetCurrentContext() != nullptr)
    {
        CreateOpenGLTexture(m_ImageData.data(), true);
    }
    else
    {
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
    }

    // Vulkan texture creation is deferred - it requires device, physical device,
    // command pool, and queue handles that we don't have access to here.
    // Call CreateVulkanTexture() later once those are available.

    // Free stb_image's allocation - we have our own copy now
    stbi_image_free(data);
    return true;
}

bool Texture::LoadFromData(unsigned char* data, int width, int height, int channels, bool flipY)
{
    // Validate input - GPU textures with zero or negative dimensions will crash
    if (!data || width <= 0 || height <= 0 || channels <= 0 || channels > 4)
    {
        std::cerr << "Invalid data for texture loading" << std::endl;
        return false;
    }

    m_ImageData.clear();

    m_Width = width;
    m_Height = height;
    m_Channels = channels;

    std::vector<unsigned char> normalizedData;
    const unsigned char* sourceData = data;
    if (channels != 4)
    {
        const size_t srcSize = static_cast<size_t>(width) * static_cast<size_t>(height) *
                               static_cast<size_t>(channels);
        if (!ExpandToRgba(data, srcSize, width, height, channels, normalizedData))
        {
            std::cerr << "Failed to expand texture data to RGBA" << std::endl;
            return false;
        }
        sourceData = normalizedData.data();
        m_Channels = 4;
    }

    size_t dataSize = 0;
    if (!TryComputeImageByteSize(width, height, m_Channels, dataSize))
    {
        std::cerr << "Texture data size overflow" << std::endl;
        return false;
    }
    m_ImageData.resize(dataSize);

    // Handle vertical flip if requested (needed for OpenGL coordinate system)
    std::vector<unsigned char> finalData;
    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(m_Channels);

    if (flipY)
    {
        // Create a flipped copy by copying rows in reverse order
        finalData.resize(dataSize);
        for (int y = 0; y < height; ++y)
        {
            int srcY = height - 1 - y;  // Source row from bottom
            memcpy(finalData.data() + static_cast<size_t>(y) * rowBytes,
                   sourceData + static_cast<size_t>(srcY) * rowBytes,
                   rowBytes);
        }
        memcpy(m_ImageData.data(), finalData.data(), dataSize);
    }
    else
    {
        // No flip needed - just copy directly
        memcpy(m_ImageData.data(), sourceData, dataSize);
    }

    // Create GL texture from the (possibly flipped) data only if a context is active.
    if (glfwGetCurrentContext() != nullptr)
    {
        CreateOpenGLTexture(m_ImageData.data(), flipY);
    }
    else
    {
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
    }

    return true;
}

void Texture::CreateOpenGLTexture(const unsigned char* data, bool flipY) const
{
    (void)flipY;

    GLFWwindow* currentContext = glfwGetCurrentContext();
    if (currentContext == nullptr)
    {
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
        return;
    }

    // Replace existing texture when reloading in the same context generation.
    if (m_OpenGLID != 0 && m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration &&
        m_OpenGLContextTag == reinterpret_cast<void*>(currentContext))
    {
        glDeleteTextures(1, &m_OpenGLID);
        m_OpenGLID = 0;
    }

    // Generate a new texture object and bind it for configuration
    glGenTextures(1, &m_OpenGLID);
    m_OpenGLContextTag = reinterpret_cast<void*>(currentContext);
    m_OpenGLContextGeneration = s_CurrentOpenGLContextGeneration;
    glBindTexture(GL_TEXTURE_2D, m_OpenGLID);

    // Determine the appropriate OpenGL format based on channel count
    // GL_RED for grayscale, GL_RGB for color, GL_RGBA for color with alpha
    GLenum format = GL_RGB;
    if (m_Channels == 1)
        format = GL_RED;
    else if (m_Channels == 3)
        format = GL_RGB;
    else if (m_Channels == 4)
        format = GL_RGBA;

    // Upload pixel data to GPU. Parameters:
    // - target: GL_TEXTURE_2D (standard 2D texture)
    // - level: 0 (base mipmap level, we're not using mipmaps)
    // - internal format: how GPU stores it (same as format for simplicity)
    // - width, height: texture dimensions
    // - border: 0 (must be 0 in modern GL)
    // - format: pixel data format
    // - type: GL_UNSIGNED_BYTE (8 bits per channel)
    // - data: pointer to pixel data
    glTexImage2D(GL_TEXTURE_2D, 0, format, m_Width, m_Height, 0, format, GL_UNSIGNED_BYTE, data);

    // Texture wrapping - clamp to edge prevents sampling artifacts at borders
    // This is important for sprite sheets where we don't want neighboring tiles bleeding in
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Filtering - use NEAREST for pixel-art style graphics (no blurring)
    // Use GL_LINEAR for smooth/realistic textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Unbind to prevent accidental modification
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind(unsigned int slot) const
{
    // OpenGL has multiple texture units (GL_TEXTURE0, GL_TEXTURE1, etc.)
    // This allows shaders to sample from multiple textures simultaneously
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_OpenGLID);
}

void Texture::Unbind() const
{
    // Bind texture 0 (the "null" texture) to clear the binding
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::RecreateOpenGLTexture() const
{
    // This is called after an OpenGL context switch (e.g., switching renderers)
    // The old texture ID is invalid in the new context, so we recreate it

    if (m_ImageData.empty())
    {
        std::cerr << "Cannot recreate OpenGL texture: no image data" << std::endl;
        return;
    }

    GLFWwindow* currentContext = glfwGetCurrentContext();
    if (currentContext == nullptr)
    {
        std::cerr << "Cannot recreate OpenGL texture: no active OpenGL context" << std::endl;
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
        return;
    }

    // Delete old texture only if it belongs to the current context.
    // After renderer hot-swap, stale IDs from the previous context may collide
    // with live IDs in the new context and must not be deleted.
    if (m_OpenGLID != 0 && currentContext != nullptr &&
        m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration)
    {
        glDeleteTextures(1, &m_OpenGLID);
    }
    m_OpenGLID = 0;
    m_OpenGLContextTag = nullptr;
    m_OpenGLContextGeneration = 0;

    // Recreate from our stored CPU copy
    // Pass flipY=false because m_ImageData is already in the correct orientation
    CreateOpenGLTexture(m_ImageData.data(), false);
}

void Texture::AdvanceOpenGLContextGeneration()
{
    ++s_CurrentOpenGLContextGeneration;
    if (s_CurrentOpenGLContextGeneration == 0)
    {
        s_CurrentOpenGLContextGeneration = 1;
    }
}

std::uint64_t Texture::GetCurrentOpenGLContextGeneration()
{
    return s_CurrentOpenGLContextGeneration;
}

void Texture::CreateVulkanTexture(VkDevice device,
                                  VkPhysicalDevice physicalDevice,
                                  VkCommandPool commandPool,
                                  VkQueue queue) const
{
    // Vulkan texture creation is more complex than OpenGL because we must:
    // 1. Create the image object (describes the texture properties)
    // 2. Allocate GPU memory for the image
    // 3. Create an image view (how shaders see the image)
    // 4. Create a sampler (how to filter/wrap the texture)
    // 5. Upload pixel data via a staging buffer

    if (m_ImageData.empty())
    {
        std::cerr << "Cannot create Vulkan texture: no image data" << std::endl;
        return;
    }
    if (m_Channels != 4)
    {
        throw std::runtime_error("Unsupported channel count for Vulkan texture (expected 4 RGBA)");
    }

    size_t imageSizeBytes = 0;
    if (!TryComputeImageByteSize(m_Width, m_Height, m_Channels, imageSizeBytes))
    {
        throw std::runtime_error("Vulkan texture size overflow");
    }
    if (m_ImageData.size() < imageSizeBytes)
    {
        throw std::runtime_error("Vulkan texture image data is smaller than expected");
    }

    // Re-create safely if this texture already has Vulkan resources.
    if (m_VulkanImage != VK_NULL_HANDLE || m_VulkanImageView != VK_NULL_HANDLE ||
        m_VulkanImageMemory != VK_NULL_HANDLE || m_VulkanSampler != VK_NULL_HANDLE)
    {
        VkDevice destroyDevice = (m_VulkanDevice != VK_NULL_HANDLE) ? m_VulkanDevice : device;
        DestroyVulkanTexture(destroyDevice);
    }

    // Store device handle for cleanup later
    m_VulkanDevice = device;

    // Step 1: Create the VkImage object
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(m_Width);
    imageInfo.extent.height = static_cast<uint32_t>(m_Height);
    imageInfo.extent.depth = 1;  // 2D texture, depth is 1
    imageInfo.mipLevels = 1;     // No mipmaps
    imageInfo.arrayLayers = 1;   // Not a texture array

    // Use UNORM format (linear color space) to match OpenGL behavior.
    // SRGB format would apply gamma correction, making textures appear brighter.
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;

    // OPTIMAL tiling lets the GPU arrange pixels however is fastest for sampling
    // LINEAR tiling would allow CPU access but is slower for rendering
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // We'll transition this

    // TRANSFER_DST: we'll copy data into this image
    // SAMPLED: shaders will sample from this image
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // Only one queue family uses this
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;          // No multisampling

    if (vkCreateImage(device, &imageInfo, nullptr, &m_VulkanImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan image!");
    }

    // Step 2: Allocate GPU memory for the image
    // Vulkan separates resource creation from memory allocation for flexibility
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_VulkanImage, &memRequirements);

    // Find a memory type that is device-local (fast GPU memory)
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // Check if this memory type is compatible with our image AND is device-local
        if ((memRequirements.memoryTypeBits & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            memoryTypeIndex = i;
            break;
        }
    }

    if (memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyImage(device, m_VulkanImage, nullptr);
        m_VulkanImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to find suitable memory type!");
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_VulkanImageMemory) != VK_SUCCESS)
    {
        vkDestroyImage(device, m_VulkanImage, nullptr);
        m_VulkanImage = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to allocate Vulkan image memory!");
    }

    // Bind memory to image - now the image has backing storage
    vkBindImageMemory(device, m_VulkanImage, m_VulkanImageMemory, 0);

    // Step 3: Create image view - this is what shaders actually reference
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_VulkanImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    // Subresource range specifies which mip levels and array layers to access
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_VulkanImageView) != VK_SUCCESS)
    {
        vkDestroyImage(device, m_VulkanImage, nullptr);
        m_VulkanImage = VK_NULL_HANDLE;
        vkFreeMemory(device, m_VulkanImageMemory, nullptr);
        m_VulkanImageMemory = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create Vulkan image view!");
    }

    // Step 4: Create sampler - controls texture filtering and wrapping
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // NEAREST filtering for pixel-art (no interpolation)
    samplerInfo.magFilter = VK_FILTER_NEAREST;  // When texture is magnified
    samplerInfo.minFilter = VK_FILTER_NEAREST;  // When texture is minified
    // Clamp to edge prevents artifacts at texture borders
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;  // No anisotropic filtering for pixel art
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;  // Use 0-1 UV coordinates
    samplerInfo.compareEnable = VK_FALSE;            // Not a depth/shadow map
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &m_VulkanSampler) != VK_SUCCESS)
    {
        vkDestroyImageView(device, m_VulkanImageView, nullptr);
        m_VulkanImageView = VK_NULL_HANDLE;
        vkDestroyImage(device, m_VulkanImage, nullptr);
        m_VulkanImage = VK_NULL_HANDLE;
        vkFreeMemory(device, m_VulkanImageMemory, nullptr);
        m_VulkanImageMemory = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create Vulkan sampler!");
    }

    // Step 5: Upload pixel data using a staging buffer
    // We can't write directly to device-local memory, so we:
    // 1. Create a host-visible staging buffer
    // 2. Copy pixel data to staging buffer
    // 3. Issue a GPU command to copy from staging buffer to image

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(imageSizeBytes);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Lambda to find appropriate memory type for staging buffer
    auto FindMemoryType = [&](uint32_t typeFilter, VkMemoryPropertyFlags properties) -> uint32_t
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
        return UINT32_MAX;
    };

    // Create staging buffer - host visible so CPU can write to it
    VkBufferCreateInfo stagingBufferInfo{};
    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = imageSize;
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;  // Source for copy
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to create staging buffer!");
    }

    VkMemoryRequirements stagingMemRequirements;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingMemRequirements);

    VkMemoryAllocateInfo stagingAllocInfo{};
    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = stagingMemRequirements.size;
    // HOST_VISIBLE: CPU can map and write to this memory
    // HOST_COHERENT: writes are immediately visible to GPU (no explicit flush needed)
    stagingAllocInfo.memoryTypeIndex =
        FindMemoryType(stagingMemRequirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (stagingAllocInfo.memoryTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to find suitable memory type for staging buffer!");
    }

    if (vkAllocateMemory(device, &stagingAllocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(device, stagingBuffer, stagingBufferMemory, 0);

    // Map staging buffer memory and copy pixel data
    // Note: m_ImageData is already flipped for OpenGL. Vulkan uses the same data
    // and handles the Y-axis difference via UV coordinate flipping in the renderer.
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, m_ImageData.data(), imageSize);
    vkUnmapMemory(device, stagingBufferMemory);

    // Now we need to issue GPU commands to:
    // 1. Transition image layout from UNDEFINED to TRANSFER_DST
    // 2. Copy data from staging buffer to image
    // 3. Transition image layout from TRANSFER_DST to SHADER_READ_ONLY

    // Allocate a one-time command buffer for these operations
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = commandPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &commandBuffer) != VK_SUCCESS)
    {
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to allocate command buffer!");
    }

    // Begin recording commands
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;  // Only used once

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to begin command buffer!");
    }

    // Image memory barrier to transition layout from UNDEFINED to TRANSFER_DST
    // This tells the GPU that we're about to write to this image
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;  // Not transferring queue ownership
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_VulkanImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;                             // No prior access to wait for
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;  // Transfer write access needed

    // Pipeline barrier ensures layout transition completes before transfer stage
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    // Copy from staging buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // 0 means tightly packed (no padding)
    region.bufferImageHeight = 0;  // 0 means tightly packed
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(m_Width), static_cast<uint32_t>(m_Height), 1};

    vkCmdCopyBufferToImage(commandBuffer,
                           stagingBuffer,
                           m_VulkanImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &region);

    // Transition to SHADER_READ_ONLY layout so shaders can sample from it
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);

    // Done recording commands
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to end command buffer!");
    }

    // Submit command buffer to GPU for execution
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Use a local fence to wait for the transfer to complete instead of
    // vkQueueWaitIdle, which would stall all work on the queue.
    VkFenceCreateInfo uploadFenceInfo{};
    uploadFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence uploadFence;
    if (vkCreateFence(device, &uploadFenceInfo, nullptr, &uploadFence) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to create upload fence!");
    }

    if (vkQueueSubmit(queue, 1, &submitInfo, uploadFence) != VK_SUCCESS)
    {
        vkDestroyFence(device, uploadFence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
        vkFreeMemory(device, stagingBufferMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyVulkanTexture(device);
        throw std::runtime_error("Failed to submit command buffer!");
    }

    // Wait for this specific submit to complete via fence
    vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, uploadFence, nullptr);

    // Clean up staging resources - no longer needed, data is now on GPU
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
}

void Texture::DestroyVulkanTexture(VkDevice device) const
{
    VkDevice destroyDevice = (device != VK_NULL_HANDLE) ? device : m_VulkanDevice;
    if (destroyDevice == VK_NULL_HANDLE)
    {
        m_VulkanImage = VK_NULL_HANDLE;
        m_VulkanImageMemory = VK_NULL_HANDLE;
        m_VulkanImageView = VK_NULL_HANDLE;
        m_VulkanSampler = VK_NULL_HANDLE;
        m_VulkanDevice = VK_NULL_HANDLE;
        return;
    }

    // Destroy Vulkan resources in reverse creation order
    // Sampler first (depends on nothing)
    if (m_VulkanSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(destroyDevice, m_VulkanSampler, nullptr);
        m_VulkanSampler = VK_NULL_HANDLE;
    }
    // Image view depends on image
    if (m_VulkanImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(destroyDevice, m_VulkanImageView, nullptr);
        m_VulkanImageView = VK_NULL_HANDLE;
    }
    // Image depends on memory
    if (m_VulkanImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(destroyDevice, m_VulkanImage, nullptr);
        m_VulkanImage = VK_NULL_HANDLE;
    }
    // Memory last
    if (m_VulkanImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(destroyDevice, m_VulkanImageMemory, nullptr);
        m_VulkanImageMemory = VK_NULL_HANDLE;
    }

    m_VulkanDevice = VK_NULL_HANDLE;
}
