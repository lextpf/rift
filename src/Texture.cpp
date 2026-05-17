#include "Texture.hpp"

#include "Logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

// Define STB_IMAGE_IMPLEMENTATION in EXACTLY ONE .cpp file (this one) to pull
// in stb_image's implementation without duplicate symbols.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

std::uint64_t Texture::s_CurrentOpenGLContextGeneration = 0;

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Texture";

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

    // Size math inlined so the static analyzer can verify
    // srcBase = i * channels < pixelCount * channels <= srcSize each iteration.
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h)
        return false;
    const size_t pixelCount = w * h;

    const size_t channels = static_cast<size_t>(srcChannels);
    if (pixelCount > std::numeric_limits<size_t>::max() / channels)
        return false;
    const size_t expectedSrcSize = pixelCount * channels;
    if (srcSize < expectedSrcSize)
        return false;

    size_t rgbaSize = 0;
    if (!TryComputeImageByteSize(width, height, 4, rgbaSize))
        return false;

    outRgba.resize(rgbaSize);
    for (size_t i = 0; i < pixelCount; ++i)
    {
        const size_t srcBase = i * channels;
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
    : m_Width(0),
      m_Height(0),
      m_Channels(0),
      m_ImageData(),
      m_OpenGLID(0),
      m_OpenGLContextTag(nullptr),
      m_OpenGLContextGeneration(0),
      m_VulkanImage(VK_NULL_HANDLE),
      m_VulkanImageMemory(VK_NULL_HANDLE),
      m_VulkanImageView(VK_NULL_HANDLE),
      m_VulkanSampler(VK_NULL_HANDLE),
      m_VulkanDevice(VK_NULL_HANDLE)
{
}

// Move ops transfer GPU-resource ownership so containers like vector can
// store textures without freeing their handles when temporaries are destroyed.
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
    // Null out source handles so its destructor doesn't free them.
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

// Move assignment may already own resources - free them before stealing.
Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this != &other)
    {
        // GL context may be gone during shutdown; skip glDeleteTextures then.
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

        // Null out source so its destructor doesn't double-free.
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
    // GL textures must be deleted while the context is valid. GLFW may
    // destroy the context before this destructor runs at shutdown - guard
    // with glfwGetCurrentContext() to avoid crashes.
    GLFWwindow* currentContext = glfwGetCurrentContext();
    if (m_OpenGLID != 0 && currentContext != nullptr &&
        m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration)
    {
        glDeleteTextures(1, &m_OpenGLID);
    }
    m_OpenGLID = 0;
    m_OpenGLContextTag = nullptr;
    m_OpenGLContextGeneration = 0;

    // Vulkan resources should have been explicitly destroyed before this
    // destructor runs. Reaching here means the renderer's Shutdown() missed
    // this texture - destroy to avoid leaks and log a warning.
    if (m_VulkanDevice != VK_NULL_HANDLE && m_VulkanImage != VK_NULL_HANDLE)
    {
        Logger::Warn(LOG_SUBSYSTEM,
                     "Texture destructor destroying Vulkan resources - DestroyVulkanTexture "
                     "should have been called before device destruction");
        DestroyVulkanTexture(m_VulkanDevice);
    }

    m_ImageData.clear();
}

bool Texture::LoadFromFile(const std::string& path)
{
    // stb_image is (0,0)-top-left; OpenGL is (0,0)-bottom-left - flip on load.
    // Use the thread-local variant to avoid mutating the process-wide flag
    // (unsafe under concurrent loading or third-party stbi use).
    stbi_set_flip_vertically_on_load_thread(true);

    // Last param 0 = "whatever channels the file has" (returned in m_Channels).
    unsigned char* data = stbi_load(path.c_str(), &m_Width, &m_Height, &m_Channels, 0);
    if (!data)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Failed to load texture: {}", path);
        return false;
    }

    m_ImageData.clear();

    if (m_Channels < 1 || m_Channels > 4)
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM, "Unsupported channel count ({}) for texture: {}", m_Channels, path);
        stbi_image_free(data);
        return false;
    }

    // CPU copy lets us recreate GL after a context switch, create a Vulkan
    // texture later (deferred), and feed multiple backends from one source.
    std::vector<unsigned char> normalizedData;
    const unsigned char* sourceData = data;
    int sourceChannels = m_Channels;

    // Normalize to RGBA so both backends share one path. VK_FORMAT_R8G8B8_UNORM
    // isn't universally supported for optimal tiling, so 3-channel textures
    // must also be expanded.
    if (sourceChannels != 4)
    {
        const size_t srcSize = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) *
                               static_cast<size_t>(sourceChannels);
        if (!ExpandToRgba(data, srcSize, m_Width, m_Height, sourceChannels, normalizedData))
        {
            Logger::ErrorF(LOG_SUBSYSTEM, "Failed to expand texture to RGBA: {}", path);
            stbi_image_free(data);
            return false;
        }
        sourceData = normalizedData.data();
        sourceChannels = 4;
    }

    size_t dataSize = 0;
    if (!TryComputeImageByteSize(m_Width, m_Height, sourceChannels, dataSize))
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Texture is too large to load safely: {}", path);
        stbi_image_free(data);
        return false;
    }
    m_ImageData.resize(dataSize);
    memcpy(m_ImageData.data(), sourceData, dataSize);
    m_Channels = sourceChannels;

    // Create GL texture now only if a context is active. In Vulkan mode there
    // is no GL context; keep CPU data and upload later.
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

    // Vulkan creation is deferred - needs device/physical-device/cmd-pool/queue
    // handles we don't have here. Caller invokes CreateVulkanTexture() later.

    stbi_image_free(data);
    return true;
}

bool Texture::LoadFromData(unsigned char* data, int width, int height, int channels, bool flipY)
{
    // Zero/negative dimensions would crash on upload.
    if (!data || width <= 0 || height <= 0 || channels <= 0 || channels > 4)
    {
        Logger::Error(LOG_SUBSYSTEM, "Invalid data for texture loading");
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
            Logger::Error(LOG_SUBSYSTEM, "Failed to expand texture data to RGBA");
            return false;
        }
        sourceData = normalizedData.data();
        m_Channels = 4;
    }

    size_t dataSize = 0;
    if (!TryComputeImageByteSize(width, height, m_Channels, dataSize))
    {
        Logger::Error(LOG_SUBSYSTEM, "Texture data size overflow");
        return false;
    }
    m_ImageData.resize(dataSize);

    // Optional vertical flip for OpenGL coordinate system.
    std::vector<unsigned char> finalData;
    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(m_Channels);

    if (flipY)
    {
        // Copy rows in reverse order.
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

    glGenTextures(1, &m_OpenGLID);
    m_OpenGLContextTag = reinterpret_cast<void*>(currentContext);
    m_OpenGLContextGeneration = s_CurrentOpenGLContextGeneration;
    glBindTexture(GL_TEXTURE_2D, m_OpenGLID);

    // 1 = grayscale, 3 = RGB, 4 = RGBA.
    GLenum format = GL_RGB;
    if (m_Channels == 1)
        format = GL_RED;
    else if (m_Channels == 3)
        format = GL_RGB;
    else if (m_Channels == 4)
        format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, m_Width, m_Height, 0, format, GL_UNSIGNED_BYTE, data);

    // Clamp to edge so neighboring sprite-sheet tiles don't bleed in.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // NEAREST for pixel art (no blurring). Use GL_LINEAR for smooth textures.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);
}

namespace
{
/// Convert sRGB byte channels (0-255) to HSV (each in [0, 1]).
/// Hue is normalized to [0, 1] (multiply by 360 for degrees).
struct Hsv
{
    float h, s, v;
};

Hsv RgbToHsv(unsigned char r8, unsigned char g8, unsigned char b8)
{
    const float r = r8 / 255.0f;
    const float g = g8 / 255.0f;
    const float b = b8 / 255.0f;
    const float maxC = std::max({r, g, b});
    const float minC = std::min({r, g, b});
    const float delta = maxC - minC;

    Hsv out{0.0f, 0.0f, maxC};
    if (maxC > 0.0f)
        out.s = delta / maxC;

    if (delta > 0.0f)
    {
        if (maxC == r)
            out.h = std::fmod(((g - b) / delta) + 6.0f, 6.0f);
        else if (maxC == g)
            out.h = ((b - r) / delta) + 2.0f;
        else
            out.h = ((r - g) / delta) + 4.0f;
        out.h /= 6.0f;  // [0, 1]
    }
    return out;
}
}  // namespace

glm::vec3 Texture::SampleDominantNonSkinColor(glm::vec3 fallback) const
{
    // No upper-value cap: saturated colors like #ff8030 or #ffff00 have
    // value=1.0 (one RGB channel at max). Real highlights are already caught
    // by the saturation < 0.30 filter (highlights have low sat).
    constexpr float kMinSat = 0.30f;
    constexpr float kMinValue = 0.25f;
    constexpr float kSkinHueMaxNorm = 30.0f / 360.0f;  // 30 degrees normalized
    constexpr float kSkinSatMin = 0.20f;
    constexpr float kSkinSatMax = 0.60f;
    constexpr unsigned char kAlphaThreshold = 128;

    if (m_Width <= 0 || m_Height <= 0 || m_Channels < 4 || m_ImageData.empty())
        return fallback;

    const size_t pixelCount = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height);
    const size_t expectedBytes = pixelCount * static_cast<size_t>(m_Channels);
    if (m_ImageData.size() < expectedBytes)
        return fallback;

    float bestScore = -1.0f;  // saturation * value of best survivor so far
    glm::vec3 bestRgb = fallback;

    for (size_t i = 0; i < pixelCount; ++i)
    {
        const unsigned char* p = m_ImageData.data() + i * static_cast<size_t>(m_Channels);
        if (p[3] < kAlphaThreshold)
            continue;

        const Hsv hsv = RgbToHsv(p[0], p[1], p[2]);
        if (hsv.s < kMinSat)
            continue;
        if (hsv.v < kMinValue)
            continue;

        // Skin-tone band: orange-tan hues at low-mid saturation read as skin.
        // Saturated reds and oranges (sat > kSkinSatMax) escape the filter.
        const bool inSkinHue = (hsv.h >= 0.0f && hsv.h <= kSkinHueMaxNorm);
        const bool inSkinSat = (hsv.s >= kSkinSatMin && hsv.s <= kSkinSatMax);
        if (inSkinHue && inSkinSat)
            continue;

        const float score = hsv.s * hsv.v;
        if (score > bestScore)
        {
            bestScore = score;
            bestRgb = glm::vec3(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f);
        }
    }

    return bestRgb;
}

void Texture::Bind(unsigned int slot) const
{
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, m_OpenGLID);
}

void Texture::Unbind() const
{
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::RecreateOpenGLTexture() const
{
    // Called after a GL context switch (e.g., renderer hot-swap). Old IDs are
    // invalid in the new context, so we recreate from the CPU copy.
    if (m_ImageData.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "Cannot recreate OpenGL texture: no image data");
        return;
    }

    GLFWwindow* currentContext = glfwGetCurrentContext();
    if (currentContext == nullptr)
    {
        Logger::Error(LOG_SUBSYSTEM, "Cannot recreate OpenGL texture: no active OpenGL context");
        m_OpenGLID = 0;
        m_OpenGLContextTag = nullptr;
        m_OpenGLContextGeneration = 0;
        return;
    }

    // Only delete if the old ID belongs to the current generation - stale IDs
    // from the prior context may collide with live IDs in the new one.
    if (m_OpenGLID != 0 && currentContext != nullptr &&
        m_OpenGLContextGeneration == s_CurrentOpenGLContextGeneration)
    {
        glDeleteTextures(1, &m_OpenGLID);
    }
    m_OpenGLID = 0;
    m_OpenGLContextTag = nullptr;
    m_OpenGLContextGeneration = 0;

    // m_ImageData is already in the correct orientation, so flipY=false.
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
    if (m_ImageData.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "Cannot create Vulkan texture: no image data");
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

    m_VulkanDevice = device;

    // VkImage: 2D, no mipmaps, no array, UNORM (linear) to match OpenGL.
    // SRGB would apply gamma correction and make textures brighter.
    // OPTIMAL tiling = GPU-fastest layout (LINEAR would allow CPU access but
    // is slower for rendering). EXCLUSIVE = single queue family.
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(m_Width);
    imageInfo.extent.height = static_cast<uint32_t>(m_Height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(device, &imageInfo, nullptr, &m_VulkanImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan image!");
    }

    // Allocate device-local (fast GPU) memory. Vulkan separates resource
    // creation from memory allocation.
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, m_VulkanImage, &memRequirements);

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    uint32_t memoryTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // Compatible with our image AND device-local.
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

    vkBindImageMemory(device, m_VulkanImage, m_VulkanImageMemory, 0);

    // Image view - what shaders actually reference.
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_VulkanImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
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

    // Sampler: NEAREST filter for pixel art (no interpolation); clamp to edge
    // prevents border artifacts; no anisotropy; normalized 0..1 UVs.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
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
