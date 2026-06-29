#ifdef _WIN32
#define NOMINMAX
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#undef DrawText
#include <excpt.h>
#endif

#include "Logger.hpp"
#include "Texture.hpp"
#include "VulkanCommon.hpp"
#include "VulkanRenderer.hpp"
#include "VulkanShader.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <set>
#include <vector>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Render";
}  // namespace

#ifdef _WIN32
namespace
{
HMODULE g_VulkanLib = nullptr;
}  // namespace

// Explicitly load vulkan-1.dll.
static bool LoadVulkanLibrary()
{
    g_VulkanLib = LoadLibraryA("vulkan-1.dll");
    if (g_VulkanLib == NULL)
    {
        Logger::WarnF(LOG_SUBSYSTEM, "Could not load vulkan-1.dll. Error: {}", GetLastError());
        return false;
    }
    Logger::Info(LOG_SUBSYSTEM, "Vulkan library loaded successfully");
    return true;
}
#endif

namespace
{
// Validation layers are enabled only in debug builds (gated by NDEBUG).
bool ShouldEnableValidationLayers()
{
#ifndef NDEBUG
    return true;
#else
    return false;
#endif
}
}  // namespace

// Push constants layout shared by all Vulkan draw calls.
struct CombinedPushConstants
{
    glm::mat4 projection;    // 0-63
    glm::mat4 model;         // 64-127
    glm::vec3 spriteColor;   // 128-139
    float useColorOnly;      // 140-143
    glm::vec4 colorOnly;     // 144-159
    glm::vec3 ambientColor;  // 160-171
    float spriteAlpha;       // 172-175
};
static_assert(sizeof(CombinedPushConstants) == 176,
              "CombinedPushConstants must be 176 bytes to match SPIR-V shader layout");

// Zero-initialize every Vulkan handle to VK_NULL_HANDLE; real bring-up is in Init().
VulkanRenderer::VulkanRenderer(GLFWwindow* window)
    : m_Instance(VK_NULL_HANDLE),
      m_PhysicalDevice(VK_NULL_HANDLE),
      m_Device(VK_NULL_HANDLE),
      m_GraphicsQueue(VK_NULL_HANDLE),
      m_PresentQueue(VK_NULL_HANDLE),
      m_Surface(VK_NULL_HANDLE),
      m_Swapchain(VK_NULL_HANDLE),
      m_RenderPass(VK_NULL_HANDLE),
      m_PipelineLayout(VK_NULL_HANDLE),
      m_GraphicsPipeline(VK_NULL_HANDLE),
      m_CommandPool(VK_NULL_HANDLE),
      m_CurrentFrame(0),
      m_ImageIndex(0),
      m_FrameActive(false),
      m_Window(window),
      m_GraphicsFamily(UINT32_MAX),
      m_PresentFamily(UINT32_MAX),
      m_VertexBuffers{VK_NULL_HANDLE, VK_NULL_HANDLE},
      m_VertexBufferMemories{VK_NULL_HANDLE, VK_NULL_HANDLE},
      m_VertexBuffersMapped{nullptr, nullptr},
      m_IndexBuffer(VK_NULL_HANDLE),
      m_IndexBufferMemory(VK_NULL_HANDLE),
      m_VertexBufferSize(0),
      m_CurrentVertexCount(0),
      m_BatchImageView(VK_NULL_HANDLE),
      m_BatchDescriptorSet(VK_NULL_HANDLE),
      m_BatchStartVertex(0),
      m_DescriptorPool(VK_NULL_HANDLE),
      m_DescriptorSetLayout(VK_NULL_HANDLE),
      m_TextureSampler(VK_NULL_HANDLE),
      m_WhiteTextureImage(VK_NULL_HANDLE),
      m_WhiteTextureImageMemory(VK_NULL_HANDLE),
      m_WhiteTextureImageView(VK_NULL_HANDLE),
      m_WhiteTextureSampler(VK_NULL_HANDLE)
{
    Logger::Debug(LOG_SUBSYSTEM, "VulkanRenderer constructor called");
}

VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

// Record the project-configured font search paths; consumed later by LoadFont().
void VulkanRenderer::SetFontCandidates(const std::vector<std::string>& fontCandidates)
{
    m_FontCandidates = fontCandidates;
}

// Full renderer bring-up: load the Vulkan loader, then run the create-* steps in
// dependency order (instance -> surface -> device -> swapchain -> render pass ->
// pipeline -> resources -> font), and cache device info for GetBackendInfo. Every
// step may throw; failures are caught here, logged, and reported as false.
bool VulkanRenderer::Init()
{
    try
    {
        Logger::Info(LOG_SUBSYSTEM, "Initializing Vulkan renderer...");

#ifdef _WIN32
        if (!LoadVulkanLibrary())
        {
            Logger::Warn(LOG_SUBSYSTEM, "Failed to load Vulkan library, but continuing...");
        }
#endif

        CreateInstance();
        CreateSurface();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateSwapchain();
        CreateImageViews();
        CreateRenderPass();
        CreateGraphicsPipeline();
        CreateFramebuffers();
        CreateCommandPool();
        // Shared fence must exist before the first upload.
        CreateSyncObjects();
        CreateBuffers();
        CreateDescriptorPool();
        CreatePerspectiveUBO();
        CreateTextureSampler();
        CreateWhiteTexture();
        LoadFont();
        CreateCommandBuffers();

        // Populate RendererInfo for GetBackendInfo (physical device was
        // selected in PickPhysicalDevice above).
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);
            m_Info.backendName = "Vulkan";
            char buf[64];
            std::snprintf(buf,
                          sizeof(buf),
                          "%u.%u.%u",
                          VK_VERSION_MAJOR(props.apiVersion),
                          VK_VERSION_MINOR(props.apiVersion),
                          VK_VERSION_PATCH(props.apiVersion));
            m_Info.apiVersion = buf;
            switch (props.vendorID)
            {
                case 0x10DE:
                    m_Info.vendor = "NVIDIA";
                    break;
                case 0x1002:
                    m_Info.vendor = "AMD";
                    break;
                case 0x8086:
                    m_Info.vendor = "Intel";
                    break;
                case 0x13B5:
                    m_Info.vendor = "ARM";
                    break;
                case 0x5143:
                    m_Info.vendor = "Qualcomm";
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "Vendor 0x%04X", props.vendorID);
                    m_Info.vendor = buf;
                    break;
            }
            m_Info.device = props.deviceName;
            std::snprintf(buf, sizeof(buf), "%u", props.driverVersion);
            m_Info.driverVersion = buf;
            m_Info.maxTextureSize = static_cast<int>(props.limits.maxImageDimension2D);
        }

        Logger::Info(LOG_SUBSYSTEM, "Vulkan renderer initialized successfully!");
        return true;
    }
    catch (const std::exception& e)
    {
        Logger::ErrorF(LOG_SUBSYSTEM, "Exception in VulkanRenderer::Init(): {}", e.what());
        return false;
    }
    catch (...)
    {
        Logger::Error(LOG_SUBSYSTEM, "Unknown exception in VulkanRenderer::Init()");
        return false;
    }
}

// Return the cached backend/device info populated at the end of Init().
RendererInfo VulkanRenderer::GetBackendInfo() const
{
    return m_Info;
}

// Tear down every Vulkan object in reverse creation order after idling the device.
// Safe to call more than once (guards on m_Device / m_Instance) and tolerant of a
// lost device. Releases resources owned by uploaded Texture objects first, since
// they must outlive neither the device nor this teardown.
void VulkanRenderer::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // Idle the device before destroying anything.
        VkResult waitResult = vkDeviceWaitIdle(m_Device);
        if (waitResult != VK_SUCCESS && waitResult != VK_ERROR_DEVICE_LOST)
        {
            // Device may already be lost/invalid; continue cleanup anyway.
            Logger::WarnF(
                LOG_SUBSYSTEM, "vkDeviceWaitIdle failed: {}", static_cast<int>(waitResult));
        }

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (m_VertexBuffersMapped[i])
            {
                vkUnmapMemory(m_Device, m_VertexBufferMemories[i]);
                m_VertexBuffersMapped[i] = nullptr;
            }
            if (m_PerspUBOMapped[i])
            {
                vkUnmapMemory(m_Device, m_PerspUBOMemories[i]);
                m_PerspUBOMapped[i] = nullptr;
            }
        }

        // Release Vulkan resources owned by uploaded Texture objects. Must run
        // before destroying the device.
        for (const Texture* tex : m_UploadedTextures)
        {
            if (tex)
            {
                tex->DestroyVulkanTexture(m_Device);
            }
        }
        m_UploadedTextures.clear();
        m_UploadedTextureSet.clear();

        // Cache only holds non-owning references; DestroyVulkanTexture above
        // already released the owned resources.
        m_TextureCache.clear();

        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device, m_TextureSampler, nullptr);
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(m_Device, m_IndexBuffer, nullptr);
        }
        if (m_IndexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, m_IndexBufferMemory, nullptr);
        }
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (m_VertexBuffers[i] != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_Device, m_VertexBuffers[i], nullptr);
            }
            if (m_VertexBufferMemories[i] != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Device, m_VertexBufferMemories[i], nullptr);
            }
            if (m_PerspUBOBuffers[i] != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(m_Device, m_PerspUBOBuffers[i], nullptr);
                m_PerspUBOBuffers[i] = VK_NULL_HANDLE;
            }
            if (m_PerspUBOMemories[i] != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Device, m_PerspUBOMemories[i], nullptr);
                m_PerspUBOMemories[i] = VK_NULL_HANDLE;
            }
        }

        if (m_WhiteTextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device, m_WhiteTextureSampler, nullptr);
        }
        if (m_WhiteTextureImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_Device, m_WhiteTextureImageView, nullptr);
        }
        if (m_WhiteTextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(m_Device, m_WhiteTextureImage, nullptr);
        }
        if (m_WhiteTextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(m_Device, m_WhiteTextureImageMemory, nullptr);
        }

        // Skip glyphs that use the white texture as fallback (avoid double-destroy).
        for (auto& [c, glyph] : m_Glyphs)
        {
            if (glyph.imageView != VK_NULL_HANDLE && glyph.imageView != m_WhiteTextureImageView)
            {
                vkDestroyImageView(m_Device, glyph.imageView, nullptr);
            }
            if (glyph.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(m_Device, glyph.image, nullptr);
            }
            if (glyph.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(m_Device, glyph.memory, nullptr);
            }
        }
        m_Glyphs.clear();

        // Descriptor sets are freed when the pool is destroyed; just drop the cache.
        m_DescriptorSetCache.clear();

        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        }
        for (auto pool : m_OverflowPools)
        {
            vkDestroyDescriptorPool(m_Device, pool, nullptr);
        }
        m_OverflowPools.clear();
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
        }
        if (m_PerspDescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device, m_PerspDescriptorSetLayout, nullptr);
            m_PerspDescriptorSetLayout = VK_NULL_HANDLE;
        }

        if (m_TransferFence != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_Device, m_TransferFence, nullptr);
            m_TransferFence = VK_NULL_HANDLE;
        }

        for (auto fence : m_InFlightFences)
        {
            vkDestroyFence(m_Device, fence, nullptr);
        }
        for (auto semaphore : m_RenderFinishedSemaphores)
        {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }
        for (auto semaphore : m_ImageAvailableSemaphores)
        {
            vkDestroySemaphore(m_Device, semaphore, nullptr);
        }

        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        }

        for (auto framebuffer : m_SwapchainFramebuffers)
        {
            vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
        }

        if (m_GraphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
        }
        if (m_PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
        }
        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
        }

        for (auto imageView : m_SwapchainImageViews)
        {
            vkDestroyImageView(m_Device, imageView, nullptr);
        }

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        }
        if (m_Surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }

    if (m_Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;  // Prevent double-destroy on teardown.
    }

#ifdef _WIN32
    if (g_VulkanLib != nullptr)
    {
        FreeLibrary(g_VulkanLib);
        g_VulkanLib = nullptr;
    }
#endif
}

// Destroy the swapchain-derived objects (framebuffers, image views, and the
// swapchain itself) ahead of a resize-driven recreate. Leaves the device intact.
void VulkanRenderer::CleanupSwapchain()
{
    for (auto framebuffer : m_SwapchainFramebuffers)
    {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapchainFramebuffers.clear();

    for (auto imageView : m_SwapchainImageViews)
    {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapchainImageViews.clear();

    if (m_Swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    m_SwapchainImages.clear();
    m_ImagesInFlight.clear();
}

// Rebuild the swapchain and its dependents after a resize or out-of-date result.
// Blocks while the window is minimized (zero-size), idles the device, then recreates
// swapchain + image views + framebuffers.
void VulkanRenderer::RecreateSwapchain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);

    // Minimized - wait until the window is visible again.
    while (width == 0 || height == 0)
    {
        glfwGetFramebufferSize(m_Window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Device);

    CleanupSwapchain();

    CreateSwapchain();
    CreateImageViews();
    CreateFramebuffers();

    m_FramebufferResized = false;
}

// Create the VkInstance with the GLFW-required extensions, wiring up the debug
// messenger and validation layers in debug builds.
void VulkanRenderer::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Rift Game";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const bool enableValidationLayers = ShouldEnableValidationLayers();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    bool hasValidationLayers = enableValidationLayers && CheckValidationLayerSupport();

    if (hasValidationLayers)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageType,
               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
               void* pUserData) -> VkBool32
        {
            if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                Logger::WarnF(LOG_SUBSYSTEM, "Vulkan validation: {}", pCallbackData->pMessage);
            }
            return VK_FALSE;
        };
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance == nullptr)
    {
        throw std::runtime_error("Vulkan loader not properly initialized!");
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_Instance));
}

// Create the window surface we render into (platform-specific, delegated to GLFW).
void VulkanRenderer::CreateSurface()
{
    VK_CHECK(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface));
}

// Select the first GPU that exposes both a graphics queue family and a queue that
// can present to our surface, recording those family indices. Throws if none fits.
void VulkanRenderer::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    for (const auto& device : devices)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies)
        {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_GraphicsFamily = i;
            }

            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
            if (presentSupport)
            {
                m_PresentFamily = i;
            }

            if (m_GraphicsFamily != UINT32_MAX && m_PresentFamily != UINT32_MAX)
            {
                break;
            }
            i++;
        }

        if (m_GraphicsFamily != UINT32_MAX && m_PresentFamily != UINT32_MAX)
        {
            m_PhysicalDevice = device;
            break;
        }
    }

    if (m_PhysicalDevice == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

// Create the logical device and retrieve the graphics and present queues (one queue
// each; the two families may coincide, so they are de-duplicated).
void VulkanRenderer::CreateLogicalDevice()
{
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {m_GraphicsFamily, m_PresentFamily};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

    if (CheckValidationLayerSupport())
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_ValidationLayers.size());
        createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device));

    vkGetDeviceQueue(m_Device, m_GraphicsFamily, 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, m_PresentFamily, 0, &m_PresentQueue);
}

// Create the swapchain: prefer B8G8R8A8_UNORM (to match OpenGL's non-sRGB output)
// and an uncapped present mode (IMMEDIATE > MAILBOX > FIFO) so app-side FPS limiting
// works, sized to the current framebuffer. Retrieves the swapchain images.
void VulkanRenderer::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities));

    uint32_t formatCount = 0;
    VK_CHECK(
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr));
    if (formatCount == 0)
    {
        throw std::runtime_error(
            "Vulkan: no surface formats available for this physical device/surface");
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_PhysicalDevice, m_Surface, &formatCount, formats.data()));

    // Prefer UNORM to match OpenGL's non-gamma-corrected output (SRGB would
    // apply gamma correction and make textures appear brighter).
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            surfaceFormat = availableFormat;
            break;
        }
    }

    uint32_t presentModeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_PhysicalDevice, m_Surface, &presentModeCount, nullptr));
    if (presentModeCount == 0)
    {
        throw std::runtime_error(
            "Vulkan: no present modes available for this physical device/surface");
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data()));

    // Prefer uncapped presentation so app-side FPS limiting can work.
    // Fallback order: IMMEDIATE (no vsync, may tear) -> MAILBOX (low-latency
    // vsync) -> FIFO (always supported, vsync).
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& availablePresentMode : presentModes)
    {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR)
        {
            presentMode = availablePresentMode;
            break;
        }
    }
    if (presentMode == VK_PRESENT_MODE_FIFO_KHR)
    {
        for (const auto& availablePresentMode : presentModes)
        {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = availablePresentMode;
                break;
            }
        }
    }

    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        m_SwapchainExtent = capabilities.currentExtent;
    }
    else
    {
        int width, height;
        glfwGetFramebufferSize(m_Window, &width, &height);
        m_SwapchainExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

        m_SwapchainExtent.width = std::clamp(m_SwapchainExtent.width,
                                             capabilities.minImageExtent.width,
                                             capabilities.maxImageExtent.width);
        m_SwapchainExtent.height = std::clamp(m_SwapchainExtent.height,
                                              capabilities.minImageExtent.height,
                                              capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_Surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_SwapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    uint32_t queueFamilyIndices[] = {m_GraphicsFamily, m_PresentFamily};
    if (m_GraphicsFamily != m_PresentFamily)
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain));

    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
    m_SwapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());
    m_ImagesInFlight.assign(m_SwapchainImages.size(), VK_NULL_HANDLE);

    m_SwapchainImageFormat = surfaceFormat.format;
}

// Create a 2D color image view for each swapchain image.
void VulkanRenderer::CreateImageViews()
{
    m_SwapchainImageViews.resize(m_SwapchainImages.size());

    for (size_t i = 0; i < m_SwapchainImages.size(); i++)
    {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_SwapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_SwapchainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(m_Device, &createInfo, nullptr, &m_SwapchainImageViews[i]));
    }
}

// Create the single-subpass render pass: one color attachment cleared on load and
// stored for present, with an external dependency ordering the color-output stage.
void VulkanRenderer::CreateRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_SwapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass));
}

// Build the one graphics pipeline shared by every draw: the SpriteVertex input
// layout (pos + uv + perspectiveFlag), alpha blending, dynamic viewport/scissor (for
// the Y-flip), the combined 176-byte push-constant range, and two descriptor sets
// (set 0 = per-draw sampler, set 1 = per-frame perspective UBO). Loads the SPIR-V
// shaders and throws if they are missing or the pipeline fails to create.
void VulkanRenderer::CreateGraphicsPipeline()
{
    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 1: Starting...");
    // Shader stage info is built later, after shader modules are created.

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 5;  // pos + tex + perspectiveFlag
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[3]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 3;
    attributeDescriptions[2].format = VK_FORMAT_R32_SFLOAT;
    attributeDescriptions[2].offset = sizeof(float) * 4;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_SwapchainExtent.width;
    viewport.height = (float)m_SwapchainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_SwapchainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    // rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Push constants for matrices and uniforms.
    // Vertex: mat4 projection (0..63), mat4 model (64..127).
    // Fragment: vec3 spriteColor (128..139), float useColorOnly (140..143),
    // vec4 colorOnly (144..159), vec3 ambientColor (160..171),
    // float spriteAlpha (172..175). vec4 needs 16-byte alignment.
    // See CombinedPushConstants for the canonical layout (sizeof = 176).
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CombinedPushConstants);

    // Set 0: per-draw texture sampler (fragment stage).
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout));

    // Set 1: frame-stable perspective UBO (vertex stage). Layout matches the
    // GLSL `PerspectiveBlock` in shaders/Geometry.vert.
    VkDescriptorSetLayoutBinding perspBinding{};
    perspBinding.binding = 0;
    perspBinding.descriptorCount = 1;
    perspBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    perspBinding.pImmutableSamplers = nullptr;
    perspBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo perspLayoutInfo{};
    perspLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    perspLayoutInfo.bindingCount = 1;
    perspLayoutInfo.pBindings = &perspBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(
        m_Device, &perspLayoutInfo, nullptr, &m_PerspDescriptorSetLayout));

    VkDescriptorSetLayout setLayouts[2] = {m_DescriptorSetLayout, m_PerspDescriptorSetLayout};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout));

    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 2: Loading shaders...");

    std::vector<uint32_t> vertShaderCode = VulkanShader::GetVertexShaderSPIRV();
    std::vector<uint32_t> fragShaderCode = VulkanShader::GetFragmentShaderSPIRV();

    Logger::DebugF(LOG_SUBSYSTEM,
                   "CreateGraphicsPipeline() step 2: Vertex shader size: {} words",
                   vertShaderCode.size());
    Logger::DebugF(LOG_SUBSYSTEM,
                   "CreateGraphicsPipeline() step 2: Fragment shader size: {} words",
                   fragShaderCode.size());

    if (vertShaderCode.empty() || fragShaderCode.empty())
    {
        Logger::Error(LOG_SUBSYSTEM, "Vulkan shaders not found!");
        Logger::Error(LOG_SUBSYSTEM,
                      "Please compile shaders: glslangValidator -V shaders/Geometry.vert -o "
                      "shaders/Geometry.vert.spv");
        Logger::Error(LOG_SUBSYSTEM,
                      "                      glslangValidator -V shaders/Geometry.frag -o "
                      "shaders/Geometry.frag.spv");
        Logger::Error(LOG_SUBSYSTEM, "Or run: build.bat");
        throw std::runtime_error("Vulkan shaders not found. Please compile shaders first.");
    }

    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 3: Creating shader modules...");

    VkShaderModule vertShaderModule = VulkanShader::CreateShaderModule(m_Device, vertShaderCode);
    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 3: Vertex shader module created");

    VkShaderModule fragShaderModule = VulkanShader::CreateShaderModule(m_Device, fragShaderCode);
    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 3: Fragment shader module created");

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 3: Shader stages configured");

    // Enable dynamic viewport and scissor for Y-flip support
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_PipelineLayout;
    pipelineInfo.renderPass = m_RenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 4: Validating pipeline state...");
    Logger::DebugF(LOG_SUBSYSTEM, "  - Device: {}", static_cast<void*>(m_Device));
    Logger::DebugF(LOG_SUBSYSTEM, "  - RenderPass: {}", static_cast<void*>(m_RenderPass));
    Logger::DebugF(LOG_SUBSYSTEM, "  - PipelineLayout: {}", static_cast<void*>(m_PipelineLayout));
    Logger::DebugF(
        LOG_SUBSYSTEM, "  - Vertex shader module: {}", static_cast<void*>(vertShaderModule));
    Logger::DebugF(
        LOG_SUBSYSTEM, "  - Fragment shader module: {}", static_cast<void*>(fragShaderModule));
    Logger::DebugF(LOG_SUBSYSTEM,
                   "  - Swapchain extent: {}x{}",
                   m_SwapchainExtent.width,
                   m_SwapchainExtent.height);

    Logger::Debug(LOG_SUBSYSTEM,
                  "CreateGraphicsPipeline() step 5: Calling vkCreateGraphicsPipelines()...");

    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline);
    if (pipelineResult != VK_SUCCESS)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "vkCreateGraphicsPipelines failed with result: {}",
                       static_cast<int>(pipelineResult));

        switch (pipelineResult)
        {
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                Logger::Error(LOG_SUBSYSTEM, "  Reason: Out of host memory");
                break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                Logger::Error(LOG_SUBSYSTEM, "  Reason: Out of device memory");
                break;
            case VK_ERROR_INVALID_SHADER_NV:
                Logger::Error(LOG_SUBSYSTEM, "  Reason: Invalid shader");
                break;
            default:
                Logger::Error(LOG_SUBSYSTEM, "  Reason: Unknown error code");
                break;
        }

        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("Failed to create graphics pipeline!");
    }

    Logger::Debug(LOG_SUBSYSTEM,
                  "CreateGraphicsPipeline() step 4: Graphics pipeline created successfully");

    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() step 5: Cleaning up shader modules...");
    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
    Logger::Debug(LOG_SUBSYSTEM, "CreateGraphicsPipeline() complete!");
}

// Create one framebuffer per swapchain image view, bound to the render pass.
void VulkanRenderer::CreateFramebuffers()
{
    m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

    for (size_t i = 0; i < m_SwapchainImageViews.size(); i++)
    {
        VkImageView attachments[] = {m_SwapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_RenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_SwapchainExtent.width;
        framebufferInfo.height = m_SwapchainExtent.height;
        framebufferInfo.layers = 1;

        VK_CHECK(
            vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]));
    }
}

// Create the graphics-family command pool (individually resettable buffers).
void VulkanRenderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsFamily;

    VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool));
}

// Allocate one primary command buffer per swapchain framebuffer.
void VulkanRenderer::CreateCommandBuffers()
{
    m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

    VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()));
}

// Create the per-frame-in-flight sync objects (image-available and render-finished
// semaphores, in-flight fences created signaled) plus the transfer fence used for
// synchronous buffer/image uploads.
void VulkanRenderer::CreateSyncObjects()
{
    m_ImageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_RenderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_InFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VK_CHECK(
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]));
        VK_CHECK(
            vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]));
        VK_CHECK(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]));
    }

    // Transfer fence for synchronous buffer/image uploads. Not SIGNALED -
    // we reset before each use.
    VkFenceCreateInfo transferFenceInfo{};
    transferFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(m_Device, &transferFenceInfo, nullptr, &m_TransferFence));
}

// Destroy and recreate one frame's image-available semaphore. Needed after an
// acquire returns OUT_OF_DATE/error with a pending signal, since reusing such a
// semaphore on the next acquire is illegal (VUID-...-semaphore-01779).
void VulkanRenderer::RecreateImageAvailableSemaphore(size_t frame)
{
    if (frame >= m_ImageAvailableSemaphores.size() || m_Device == VK_NULL_HANDLE)
        return;

    if (m_ImageAvailableSemaphores[frame] != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[frame], nullptr);
        m_ImageAvailableSemaphores[frame] = VK_NULL_HANDLE;
    }

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[frame]) !=
        VK_SUCCESS)
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM, "Failed to recreate image-available semaphore for frame {}", frame);
        m_ImageAvailableSemaphores[frame] = VK_NULL_HANDLE;
    }
}

// Return whether every requested validation layer is available on this instance.
bool VulkanRenderer::CheckValidationLayerSupport()
{
    Logger::Debug(
        LOG_SUBSYSTEM,
        "CheckValidationLayerSupport() step 1: Calling vkEnumerateInstanceLayerProperties()...");

    uint32_t layerCount;
    VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if (result != VK_SUCCESS)
    {
        Logger::WarnF(LOG_SUBSYSTEM,
                      "vkEnumerateInstanceLayerProperties failed: {}",
                      static_cast<int>(result));
        return false;
    }

    Logger::DebugF(LOG_SUBSYSTEM,
                   "CheckValidationLayerSupport() step 1 complete: Found {} layers",
                   layerCount);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    if (result != VK_SUCCESS)
    {
        Logger::WarnF(LOG_SUBSYSTEM,
                      "vkEnumerateInstanceLayerProperties (second call) failed: {}",
                      static_cast<int>(result));
        return false;
    }

    Logger::Debug(
        LOG_SUBSYSTEM,
        "CheckValidationLayerSupport() step 2: Checking for required validation layers...");

    for (const char* layerName : m_ValidationLayers)
    {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
        {
            Logger::DebugF(LOG_SUBSYSTEM,
                           "CheckValidationLayerSupport() step 2: Layer '{}' not found",
                           layerName);
            return false;
        }
    }

    Logger::Debug(LOG_SUBSYSTEM,
                  "CheckValidationLayerSupport() complete: All validation layers found");

    return true;
}

// Collect the instance extensions to enable: GLFW's required set, plus the
// debug-utils extension when validation layers are active.
std::vector<const char*> VulkanRenderer::GetRequiredExtensions()
{
    Logger::Debug(LOG_SUBSYSTEM,
                  "GetRequiredExtensions() step 1: Calling glfwGetRequiredInstanceExtensions()...");

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    Logger::DebugF(LOG_SUBSYSTEM,
                   "GetRequiredExtensions() step 1 complete: Got {} extensions from GLFW",
                   glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    Logger::Debug(LOG_SUBSYSTEM,
                  "GetRequiredExtensions() step 2: Checking validation layer support...");

    // Debug extension only when validation layers are enabled.
    const bool enableValidationLayers = ShouldEnableValidationLayers();
    if (enableValidationLayers && CheckValidationLayerSupport())
    {
        Logger::Debug(
            LOG_SUBSYSTEM,
            "GetRequiredExtensions() step 2: Validation layers available, adding debug extension");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else
    {
        Logger::Debug(
            LOG_SUBSYSTEM,
            "GetRequiredExtensions() step 2: Validation layers disabled or not available");
    }

    Logger::DebugF(LOG_SUBSYSTEM,
                   "GetRequiredExtensions() complete: Returning {} extensions",
                   extensions.size());

    return extensions;
}

// Start a frame: reset per-frame batch state, refresh this frame's perspective UBO,
// wait on the in-flight fence, and acquire the next swapchain image (recreating the
// swapchain on OUT_OF_DATE). Then begin the command buffer + render pass and bind the
// pipeline with a Y-flipped dynamic viewport (to match OpenGL's coordinate space).
void VulkanRenderer::BeginFrame()
{
    // Suspension depth must net to zero across frames: any unbalanced
    // SuspendPerspective call inside the frame would leak depth and
    // gradually disable perspective entirely.
    assert(m_SuspensionDepth == 0 && "SuspendPerspective leaked depth across BeginFrame");

    m_FrameActive = false;

    m_CurrentVertexCount = 0;
    m_BatchImageView = VK_NULL_HANDLE;
    m_BatchDescriptorSet = VK_NULL_HANDLE;
    m_BatchStartVertex = 0;
    m_DrawCallCount = 0;

    // Refresh the perspective UBO for the current frame's index before any
    // draws read from it. The previous frame's UBO is still being consumed by
    // the GPU at this point - we only touch the slot we're about to record.
    UpdatePerspectiveUBO();

    if (m_Device == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE)
    {
        Logger::Error(LOG_SUBSYSTEM, "BeginFrame called but Vulkan not initialized!");
        return;
    }

    if (m_CurrentFrame >= m_InFlightFences.size())
    {
        Logger::Error(LOG_SUBSYSTEM, "CurrentFrame out of bounds!");
        return;
    }

    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(m_Device,
                                            m_Swapchain,
                                            UINT64_MAX,
                                            m_ImageAvailableSemaphores[m_CurrentFrame],
                                            VK_NULL_HANDLE,
                                            &m_ImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // vkAcquireNextImageKHR may have signaled the semaphore before
        // returning OUT_OF_DATE. Using a semaphore with a pending signal on
        // the next acquire is illegal (VUID-...-semaphore-01779), so recreate
        // it before recovering the swapchain.
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        RecreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Failed to acquire swapchain image! Result: {}",
                       static_cast<int>(result));
        // Semaphore state is ambiguous after an error - recreate.
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        return;
    }

    if (m_ImageIndex >= m_CommandBuffers.size())
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "ImageIndex out of bounds! ImageIndex={}, CommandBufferCount={}",
                       m_ImageIndex,
                       m_CommandBuffers.size());
        return;
    }

    if (m_ImageIndex >= m_ImagesInFlight.size())
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "ImageIndex out of bounds for image-fence tracking! ImageIndex={}, "
                       "ImagesInFlightCount={}",
                       m_ImageIndex,
                       m_ImagesInFlight.size());
        return;
    }

    if (m_ImagesInFlight[m_ImageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(m_Device, 1, &m_ImagesInFlight[m_ImageIndex], VK_TRUE, UINT64_MAX);
    }

    if (m_CurrentFrame >= m_CommandBuffers.size())
    {
        Logger::Error(LOG_SUBSYSTEM, "CurrentFrame out of bounds for command buffers!");
        return;
    }

    vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult beginResult = vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);
    if (beginResult != VK_SUCCESS)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Failed to begin command buffer! Result: {}",
                       static_cast<int>(beginResult));
        return;
    }

    if (m_ImageIndex >= m_SwapchainFramebuffers.size())
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "ImageIndex out of bounds for framebuffers! ImageIndex={}, "
                       "FramebufferCount={}",
                       m_ImageIndex,
                       m_SwapchainFramebuffers.size());
        return;
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_RenderPass;
    renderPassInfo.framebuffer = m_SwapchainFramebuffers[m_ImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_SwapchainExtent;

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(
        m_CommandBuffers[m_CurrentFrame], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_FrameActive = true;

    if (m_GraphicsPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(
            m_CommandBuffers[m_CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

        // Dynamic viewport with Y-flip to match OpenGL. Uses VK_KHR_maintenance1
        // behavior (core in Vulkan 1.1+): negative height flips Y.
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = static_cast<float>(m_SwapchainExtent.height);
        viewport.width = static_cast<float>(m_SwapchainExtent.width);
        viewport.height = -static_cast<float>(m_SwapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_CommandBuffers[m_CurrentFrame], 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = m_SwapchainExtent;
        vkCmdSetScissor(m_CommandBuffers[m_CurrentFrame], 0, 1, &scissor);
    }
    else
    {
        Logger::Warn(LOG_SUBSYSTEM, "Graphics pipeline is null, cannot bind!");
    }
}

// Offscreen scene-target phase hook. Vulkan PostFX is a later phase, so this
// renders straight to the swapchain with no bloom/grading/vignette/grain.
void VulkanRenderer::BeginScene()
{
    // Vulkan Post-FX is a later phase - the path currently renders directly
    // to swapchain without bloom/grading/vignette/grain. F1 still toggles;
    // Vulkan users see the unprocessed scene.
}

// PostFX composite hook. No-op on Vulkan for now (see BeginScene).
void VulkanRenderer::EndSceneApplyPostFX(const PostFXParams& /*params*/)
{
    // No-op: see BeginScene() comment.
}

// Finish the frame: flush the pending sprite batch, end the render pass and command
// buffer, submit (waiting on image-available, signaling render-finished), present,
// and advance the frame index. Recreates the swapchain on resize/out-of-date and
// recovers the sync objects on any mid-submit failure so the next frame can proceed.
void VulkanRenderer::EndFrame()
{
    if (!m_FrameActive)
    {
        return;
    }

    if (m_Device == VK_NULL_HANDLE)
    {
        Logger::Error(LOG_SUBSYSTEM, "EndFrame called but Vulkan not initialized!");
        m_FrameActive = false;
        return;
    }

    if (m_CurrentFrame >= m_CommandBuffers.size())
    {
        Logger::Error(LOG_SUBSYSTEM, "CurrentFrame out of bounds in EndFrame!");
        // BeginFrame signaled image-available - if we bail without submitting,
        // that signal never gets consumed and the next acquire on the same
        // slot is illegal.
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        m_FrameActive = false;
        return;
    }

    FlushSpriteBatch();

    vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]);

    VkResult endResult = vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);
    if (endResult != VK_SUCCESS)
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM, "Failed to end command buffer! Result: {}", static_cast<int>(endResult));
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        m_FrameActive = false;
        return;
    }

    if (m_CurrentFrame >= m_ImageAvailableSemaphores.size() ||
        m_CurrentFrame >= m_RenderFinishedSemaphores.size() ||
        m_CurrentFrame >= m_InFlightFences.size())
    {
        Logger::Error(LOG_SUBSYSTEM, "CurrentFrame out of bounds for sync objects!");
        // Best-effort: recreate the semaphore slot if it still exists.
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        m_FrameActive = false;
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_ImageAvailableSemaphores[m_CurrentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_CommandBuffers[m_CurrentFrame];

    VkSemaphore signalSemaphores[] = {m_RenderFinishedSemaphores[m_CurrentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult resetFenceResult = vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrame]);
    if (resetFenceResult != VK_SUCCESS)
    {
        Logger::ErrorF(
            LOG_SUBSYSTEM, "Failed to reset fence! Result: {}", static_cast<int>(resetFenceResult));
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        m_FrameActive = false;
        return;
    }

    VkResult submitResult =
        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);
    if (submitResult != VK_SUCCESS)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Failed to submit command buffer! Result: {}",
                       static_cast<int>(submitResult));
        // vkResetFences succeeded but submit failed: fence is unsignaled with
        // no work to signal it. Next BeginFrame would block forever on
        // vkWaitForFences. Destroy+recreate as signaled so the next frame can proceed.
        if (m_InFlightFences[m_CurrentFrame] != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_Device, m_InFlightFences[m_CurrentFrame], nullptr);
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[m_CurrentFrame]) !=
                VK_SUCCESS)
            {
                Logger::Error(LOG_SUBSYSTEM,
                              "Failed to recreate in-flight fence after submit failure");
                m_InFlightFences[m_CurrentFrame] = VK_NULL_HANDLE;
            }
        }
        RecreateImageAvailableSemaphore(m_CurrentFrame);
        m_FrameActive = false;
        return;
    }

    if (m_ImageIndex < m_ImagesInFlight.size())
    {
        m_ImagesInFlight[m_ImageIndex] = m_InFlightFences[m_CurrentFrame];
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {m_Swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &m_ImageIndex;
    presentInfo.pResults = nullptr;

    VkResult presentResult = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
        m_FramebufferResized)
    {
        m_FramebufferResized = false;
        RecreateSwapchain();
    }
    else if (presentResult != VK_SUCCESS)
    {
        Logger::ErrorF(LOG_SUBSYSTEM,
                       "Failed to present swapchain image! Result: {}",
                       static_cast<int>(presentResult));
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_FrameActive = false;
}

// Flag a swapchain recreate when the requested size differs from the current extent.
// The actual viewport is set dynamically in BeginFrame; x/y are unused here.
void VulkanRenderer::SetViewport(int x, int y, int width, int height)
{
    (void)x;
    (void)y;  // Unused.

    if (width != static_cast<int>(m_SwapchainExtent.width) ||
        height != static_cast<int>(m_SwapchainExtent.height))
    {
        m_FramebufferResized = true;
    }
}

// Set the projection matrix for subsequent draws. Flushes the current batch first
// since the matrix is a push constant and cannot change mid-batch.
void VulkanRenderer::SetProjection(const glm::mat4& projection)
{
    FlushSpriteBatch();
    m_Projection = projection;
}

// No-op: the color clear happens in BeginFrame via the render pass load op.
void VulkanRenderer::Clear(float r, float g, float b, float a)
{
    // No-op: clear is handled in BeginFrame via the render pass.
}

// Expand four quad corners and their UVs into the six vertices of two triangles,
// tagging each with perspectiveFlag (1 = apply the globe/vanishing-point warp in the
// shader, 0 = screen-space / no warp).
void VulkanRenderer::BuildQuadVertices(SpriteVertex outVertices[6],
                                       const glm::vec2 corners[4],
                                       const glm::vec2 texCoords[4],
                                       float perspectiveFlag)
{
    outVertices[0] = {
        {corners[0].x, corners[0].y}, {texCoords[0].x, texCoords[0].y}, perspectiveFlag};
    outVertices[1] = {
        {corners[2].x, corners[2].y}, {texCoords[2].x, texCoords[2].y}, perspectiveFlag};
    outVertices[2] = {
        {corners[3].x, corners[3].y}, {texCoords[3].x, texCoords[3].y}, perspectiveFlag};
    outVertices[3] = {
        {corners[0].x, corners[0].y}, {texCoords[0].x, texCoords[0].y}, perspectiveFlag};
    outVertices[4] = {
        {corners[1].x, corners[1].y}, {texCoords[1].x, texCoords[1].y}, perspectiveFlag};
    outVertices[5] = {
        {corners[2].x, corners[2].y}, {texCoords[2].x, texCoords[2].y}, perspectiveFlag};
}

// Append one quad's six vertices to this frame's vertex buffer and record an
// immediate 6-vertex draw with the given color/alpha/color-only push constants (plus
// the per-frame perspective UBO). Returns false if there is no active frame or the
// vertex buffer is full.
bool VulkanRenderer::SubmitQuad(VkDescriptorSet descriptorSet,
                                const SpriteVertex vertices[6],
                                glm::vec3 spriteColor,
                                float spriteAlpha,
                                bool useColorOnly,
                                glm::vec4 colorOnly)
{
    if (!m_FrameActive)
        return false;

    uint32_t maxVertices = static_cast<uint32_t>(m_VertexBufferSize / sizeof(SpriteVertex));
    if (m_CurrentVertexCount + 6 > maxVertices)
        return false;

    SpriteVertex* mapped = static_cast<SpriteVertex*>(m_VertexBuffersMapped[m_CurrentFrame]);
    memcpy(&mapped[m_CurrentVertexCount], vertices, sizeof(SpriteVertex) * 6);

    CombinedPushConstants pc;

    pc.projection = m_Projection;
    pc.model = glm::mat4(1.0f);
    pc.spriteColor = spriteColor;
    pc.useColorOnly = useColorOnly ? 1.0f : 0.0f;
    pc.colorOnly = colorOnly;
    pc.spriteAlpha = spriteAlpha;
    pc.ambientColor = m_AmbientColor;

    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];

    vkCmdPushConstants(commandBuffer,
                       m_PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(CombinedPushConstants),
                       &pc);

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout,
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);

    // Set 1: perspective UBO (per-frame).
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout,
                            1,
                            1,
                            &m_PerspDescriptorSets[m_CurrentFrame],
                            0,
                            nullptr);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);

    vkCmdDraw(commandBuffer, 6, 1, m_CurrentVertexCount, 0);
    ++m_DrawCallCount;
    m_CurrentVertexCount += 6;
    return true;
}

// Draw a whole texture as a sprite (a full-texture DrawSpriteRegion, honoring rotation).
void VulkanRenderer::DrawSprite(
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

// Draw a sub-rectangle of a texture as an (optionally rotated/mirrored) sprite. The
// main tile/sprite workhorse: normalizes the pixel region to UVs (with optional
// stb-style Y-flip), applies per-tile mirror as flip-then-rotate, and submits the
// quad. Falls back to the white texture if the texture has not been uploaded yet.
void VulkanRenderer::DrawSpriteRegion(const Texture& texture,
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
    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        Logger::WarnF(LOG_SUBSYSTEM,
                      "Attempting to draw but pipeline not ready. GraphicsPipeline={}, "
                      "DescriptorSetLayout={}",
                      static_cast<void*>(m_GraphicsPipeline),
                      static_cast<void*>(m_DescriptorSetLayout));
        return;  // Pipeline not ready
    }

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
    {
        Logger::WarnF(LOG_SUBSYSTEM,
                      "Command buffers not ready. CurrentFrame={}, BufferCount={}",
                      m_CurrentFrame,
                      m_CommandBuffers.size());
        return;
    }

    // Vulkan image view for the texture (white texture as fallback). Uploads
    // must happen outside a frame - callers call UploadTexture() at load
    // time. A cache miss here renders white rather than stalling the graphics
    // queue mid-render-pass.
    VkImageView imageView = m_WhiteTextureImageView;
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
        imageView = texImageView;

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(imageView);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    int texWidth = texture.GetWidth();
    int texHeight = texture.GetHeight();

    if (texWidth <= 0 || texHeight <= 0)
    {
        Logger::WarnF(LOG_SUBSYSTEM, "Invalid texture size: {}x{}", texWidth, texHeight);
        return;
    }

    // Normalize pixel coords to 0..1 UV. No texel offset for GL_NEAREST pixel art.
    float texX = texCoord.x / static_cast<float>(texWidth);
    float texY = texCoord.y / static_cast<float>(texHeight);
    float texW = texSize.x / static_cast<float>(texWidth);
    float texH = texSize.y / static_cast<float>(texHeight);

    float u0 = texX;
    float u1 = texX + texW;

    float vTop;
    float vBottom;
    if (flipY)
    {
        // OpenGL-style flip (textures loaded flipped by stb).
        vTop = 1.0f - (texY + texH);
        vBottom = 1.0f - texY;
    }
    else
    {
        vTop = texY;
        vBottom = texY + texH;
    }

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

    // Top-left gets vBottom to match OpenGL's inverted V (V=0 at bottom).
    glm::vec2 texCoords[4] = {
        {u0, vBottom},  // Top-left (matches OpenGL)
        {u1, vBottom},  // Top-right
        {u1, vTop},     // Bottom-right
        {u0, vTop},     // Bottom-left
    };

    glm::vec2 corners[4] = {{0.0f, 0.0f}, {size.x, 0.0f}, {size.x, size.y}, {0.0f, size.y}};

    RotateCorners(corners, size, rotation);
    for (int i = 0; i < 4; i++)
        corners[i] += position;
    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords, perspFlag);
    SubmitQuad(descriptorSet, vertices, color, 1.0f);
}

// Draw a full texture with an explicit alpha (color.a). Additive blending is not yet
// implemented on Vulkan (see TODO), so the additive flag is currently ignored.
void VulkanRenderer::DrawSpriteAlpha(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    // TODO(vulkan): Implement additive blending via a second VkPipeline with
    // VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE. Flush and
    // switch pipelines when the additive flag changes, mirroring OpenGL behavior.
    (void)additive;

    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    // See DrawSprite above for the upload-at-load contract.
    VkImageView imageView = m_WhiteTextureImageView;
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
        imageView = texImageView;

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(imageView);
    if (descriptorSet == VK_NULL_HANDLE)
        return;

    glm::vec2 texCoords[4] = {
        {0.0f, 1.0f},  // Top-left
        {1.0f, 1.0f},  // Top-right
        {1.0f, 0.0f},  // Bottom-right
        {0.0f, 0.0f},  // Bottom-left
    };

    glm::vec2 corners[4] = {{0.0f, 0.0f}, {size.x, 0.0f}, {size.x, size.y}, {0.0f, size.y}};

    RotateCorners(corners, size, rotation);
    for (int i = 0; i < 4; i++)
        corners[i] += position;
    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords, perspFlag);
    SubmitQuad(descriptorSet, vertices, glm::vec3(color.r, color.g, color.b), color.a);
}

// Draw a sprite from an atlas region given directly in UV space (uvMin..uvMax)
// rather than in pixels. Additive blending is not yet implemented (see TODO).
void VulkanRenderer::DrawSpriteAtlas(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec2 uvMin,
                                     glm::vec2 uvMax,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    // TODO(vulkan): Implement additive blending via a second VkPipeline.
    // See DrawSpriteAlpha TODO for details.
    (void)additive;

    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    VkImageView imageView = m_WhiteTextureImageView;
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
        imageView = texImageView;

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(imageView);
    if (descriptorSet == VK_NULL_HANDLE)
        return;

    float u0 = uvMin.x, u1 = uvMax.x;
    float v0 = uvMin.y, v1 = uvMax.y;

    glm::vec2 texCoords[4] = {
        {u0, v1},  // Top-left
        {u1, v1},  // Top-right
        {u1, v0},  // Bottom-right
        {u0, v0},  // Bottom-left
    };

    glm::vec2 corners[4] = {{0.0f, 0.0f}, {size.x, 0.0f}, {size.x, size.y}, {0.0f, size.y}};

    RotateCorners(corners, size, rotation);
    for (int i = 0; i < 4; i++)
        corners[i] += position;
    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords, perspFlag);
    SubmitQuad(descriptorSet, vertices, glm::vec3(color.r, color.g, color.b), color.a);
}

// Draw a solid-color rectangle (no texture) via the white texture and the shader's
// color-only path. Additive blending is not yet implemented (see TODO).
void VulkanRenderer::DrawColoredRect(glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec4 color,
                                     bool additive)
{
    // TODO(vulkan): Implement additive blending via a second VkPipeline.
    // See DrawSpriteAlpha TODO for details.
    (void)additive;
    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(m_WhiteTextureImageView);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    glm::vec2 corners[4] = {{position.x, position.y},
                            {position.x + size.x, position.y},
                            {position.x + size.x, position.y + size.y},
                            {position.x, position.y + size.y}};

    const float perspFlag = IsPerspectiveSuspended() ? 0.0f : 1.0f;

    glm::vec2 texCoords[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords, perspFlag);
    SubmitQuad(descriptorSet, vertices, glm::vec3(1.0f), 1.0f, true, color);
}

// Draw a textured quad whose four corners are already projected (used for the
// sphere-warped no-projection structures), so the shader applies no further
// perspective (perspectiveFlag defaults to 0). Requires the texture to be uploaded;
// bails otherwise.
void VulkanRenderer::DrawWarpedQuad(const Texture& texture,
                                    const glm::vec2 corners[4],
                                    glm::vec2 texCoord,
                                    glm::vec2 texSize,
                                    glm::vec3 color,
                                    bool flipY,
                                    bool tileFlipX,
                                    bool tileFlipY)
{
    if (!m_FrameActive)
        return;

    // Warped quads' corners already include projection - no further perspective.
    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        return;
    }

    // Bail if the texture hasn't been uploaded yet - callers upload at load
    // time (see DrawSprite above).
    TextureResources& texRes = GetOrCreateTexture(texture);
    if (texRes.imageView == VK_NULL_HANDLE)
    {
        return;
    }

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(texRes.imageView);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    float texW = std::max(1.0f, static_cast<float>(texture.GetWidth()));
    float texH = std::max(1.0f, static_cast<float>(texture.GetHeight()));

    float u0 = texCoord.x / texW;
    float u1 = (texCoord.x + texSize.x) / texW;

    float v0, v1;
    if (flipY)
    {
        // Y-flip for textures loaded with stb_image.
        float finalTexYTop = texH - texCoord.y;
        float finalTexYBottom = texH - (texCoord.y + texSize.y);
        v0 = finalTexYBottom / texH;
        v1 = finalTexYTop / texH;
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

    // UV mapping: TL/TR get visual top (v1), BL/BR get visual bottom (v0)
    glm::vec2 texCoords[4] = {
        {u0, v1},  // TL
        {u1, v1},  // TR
        {u1, v0},  // BR
        {u0, v0}   // BL
    };

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords);
    SubmitQuad(descriptorSet, vertices, color, 1.0f);
}

// Return the combined-image-sampler descriptor set for an image view, creating and
// caching it on first use. Spills into an overflow descriptor pool when the main pool
// is exhausted. Returns VK_NULL_HANDLE on a null view or allocation failure.
VkDescriptorSet VulkanRenderer::GetOrCreateDescriptorSet(VkImageView imageView)
{
    if (imageView == VK_NULL_HANDLE || m_DescriptorPool == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    auto it = m_DescriptorSetCache.find(imageView);
    if (it != m_DescriptorSetCache.end())
    {
        return it->second;
    }

    if (!m_DescriptorPoolWarned)
    {
        size_t currentCount = m_DescriptorSetCache.size();
        if (currentCount >= static_cast<size_t>(DESCRIPTOR_POOL_MAX_SETS * 0.9f))
        {
            Logger::WarnF(LOG_SUBSYSTEM,
                          "Descriptor pool at {}/{} sets",
                          currentCount,
                          DESCRIPTOR_POOL_MAX_SETS);
            m_DescriptorPoolWarned = true;
        }
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_DescriptorSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    auto tryAllocate = [&]() -> VkResult
    { return vkAllocateDescriptorSets(m_Device, &allocInfo, &descriptorSet); };

    VkResult result = tryAllocate();
    if (result != VK_SUCCESS)
    {
        // Create an overflow pool and retry.
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            if (!m_DescriptorPoolWarned)
            {
                Logger::Warn(LOG_SUBSYSTEM, "Descriptor pool overflow, creating additional pool");
                m_DescriptorPoolWarned = true;
            }

            VkDescriptorPoolSize poolSize{};
            poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            poolSize.descriptorCount = DESCRIPTOR_POOL_MAX_SETS;

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &poolSize;
            poolInfo.maxSets = DESCRIPTOR_POOL_MAX_SETS;
            poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

            VkDescriptorPool overflowPool;
            VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &overflowPool));
            m_OverflowPools.push_back(overflowPool);

            allocInfo.descriptorPool = overflowPool;
            descriptorSet = VK_NULL_HANDLE;
            result = vkAllocateDescriptorSets(m_Device, &allocInfo, &descriptorSet);
        }

        if (result != VK_SUCCESS)
        {
            Logger::WarnF(LOG_SUBSYSTEM,
                          "Failed to allocate descriptor set. VkResult={}",
                          static_cast<int>(result));
            return VK_NULL_HANDLE;
        }
    }

    // Bind the texture to the descriptor set.
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = m_TextureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(m_Device, 1, &descriptorWrite, 0, nullptr);

    m_DescriptorSetCache[imageView] = descriptorSet;

    return descriptorSet;
}

// Emit a single draw for the sprite vertices accumulated in the current batch range
// (m_BatchStartVertex..m_CurrentVertexCount) with the batch's bound texture, then
// advance the batch cursor. Returns early when the range is empty or no batch
// texture/descriptor is bound.
void VulkanRenderer::FlushSpriteBatch()
{
    if (m_CurrentVertexCount == m_BatchStartVertex)
        return;

    if (m_BatchImageView == VK_NULL_HANDLE || m_BatchDescriptorSet == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];

    // Identity model since vertices are pre-transformed; white tint, full alpha.
    CombinedPushConstants pushConstants;

    pushConstants.projection = m_Projection;
    pushConstants.model = glm::mat4(1.0f);
    pushConstants.spriteColor = glm::vec3(1.0f);
    pushConstants.useColorOnly = 0.0f;
    pushConstants.colorOnly = glm::vec4(0.0f);
    pushConstants.spriteAlpha = 1.0f;
    pushConstants.ambientColor = m_AmbientColor;

    vkCmdPushConstants(commandBuffer,
                       m_PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(CombinedPushConstants),
                       &pushConstants);

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout,
                            0,
                            1,
                            &m_BatchDescriptorSet,
                            0,
                            nullptr);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);

    // Single draw for all accumulated vertices.
    uint32_t vertexCount = m_CurrentVertexCount - m_BatchStartVertex;
    vkCmdDraw(commandBuffer, vertexCount, 1, m_BatchStartVertex, 0);
    ++m_DrawCallCount;

    // Start the next batch at the current position.
    m_BatchStartVertex = m_CurrentVertexCount;
    m_BatchImageView = VK_NULL_HANDLE;
    m_BatchDescriptorSet = VK_NULL_HANDLE;
}

// Build the model matrix for a quad: translate to position, rotate about the quad's
// center, then scale unit (0..1) vertices up to `size`. Matches the OpenGL path (the
// vertex Y is pre-flipped so the math is identical).
glm::mat4 VulkanRenderer::CalculateModelMatrix(glm::vec2 position, glm::vec2 size, float rotation)
{
    // Matches OpenGL: vertices are 0..1 (top-left to bottom-right). Vulkan
    // clip-space Y points down, but we've already flipped vertex Y so the math
    // can be the same as OpenGL.
    glm::mat4 model = glm::mat4(1.0f);

    // Translate to position (top-left corner).
    model = glm::translate(model, glm::vec3(position, 0.0f));

    // Translate to quad center so rotation happens around the center.
    model = glm::translate(model, glm::vec3(0.5f * size.x, 0.5f * size.y, 0.0f));

    if (rotation != 0.0f)
    {
        model = glm::rotate(model, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Translate back so scaling happens around the origin.
    model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.0f));

    // Scale from 0..1 vertices up to 0..size.
    model = glm::scale(model, glm::vec3(size, 1.0f));

    return model;
}

// Upload a texture's pixels to the GPU (creating its Vulkan image/view/memory) and
// register it for cleanup at shutdown. Must be called at load time, outside a frame,
// so the render path never stalls the queue on a mid-render cache miss.
void VulkanRenderer::UploadTexture(const Texture& texture)
{
    // Upload the texture to the GPU. CreateVulkanTexture is logically const
    // (it only initializes GPU-side cached resources, not the texture's pixel data).
    texture.CreateVulkanTexture(m_Device, m_PhysicalDevice, m_CommandPool, m_GraphicsQueue);
    m_TextureCache.erase(&texture);

    // Track for cleanup during shutdown (set provides O(1) dedup)
    if (m_UploadedTextureSet.insert(&texture).second)
    {
        m_UploadedTextures.push_back(&texture);
    }
}

// Return the maximum glyph ascent (bearing.y) across loaded glyphs, scaled by
// `scale`. Falls back to 24px when no glyphs are loaded.
float VulkanRenderer::GetTextAscent(float scale) const
{
    // Find the maximum bearing.y (ascent) across all loaded glyphs
    int maxAscent = 0;
    for (const auto& pair : m_Glyphs)
    {
        if (pair.second.bearing.y > maxAscent)
        {
            maxAscent = pair.second.bearing.y;
        }
    }
    if (maxAscent == 0)
    {
        maxAscent = 24;  // Fallback if no glyphs loaded.
    }
    return static_cast<float>(maxAscent) * scale;
}

// Return the pixel width of `text` at the given scale, summing per-glyph advances.
float VulkanRenderer::GetTextWidth(const std::string& text, float scale) const
{
    if (m_Glyphs.empty() || text.empty())
    {
        return 0.0f;
    }

    float width = 0.0f;
    for (char c : text)
    {
        auto it = m_Glyphs.find(c);
        if (it != m_Glyphs.end())
        {
            width += (it->second.advance >> 6) * scale;
        }
    }
    return width;
}

// Draw a string as textured glyph quads at `position`, laying out left-to-right with
// '\n' line breaks. Renders a black outline in four cardinal offsets first, then the
// main colored text on top. Text bypasses perspective (it is a UI overlay).
void VulkanRenderer::DrawText(const std::string& text,
                              glm::vec2 position,
                              float scale,
                              glm::vec3 color,
                              float outlineSize,
                              float alpha)
{
    if (!m_FrameActive)
        return;

    if (m_Glyphs.empty() || text.empty())
    {
        return;
    }

    // Line height from the first printable glyph.
    float lineHeight = 24.0f;
    for (const char c : text)
    {
        if (c == '\n')
            continue;
        auto it = m_Glyphs.find(c);
        if (it != m_Glyphs.end())
        {
            lineHeight = static_cast<float>(it->second.size.y) * scale;
            break;
        }
    }

    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];

    auto renderTextPass = [&, alpha](glm::vec2 basePos, glm::vec3 passColor)
    {
        float x = basePos.x;
        float y = basePos.y;

        for (const char c : text)
        {
            if (c == '\n')
            {
                x = basePos.x;
                y += lineHeight;
                continue;
            }

            auto it = m_Glyphs.find(c);
            if (it == m_Glyphs.end())
            {
                continue;
            }
            const Glyph& glyph = it->second;

            float xpos = x + glyph.bearing.x * scale;
            float ypos = y - glyph.bearing.y * scale;
            float w = glyph.size.x * scale;
            float h = glyph.size.y * scale;

            struct Vertex
            {
                float pos[2];
                float tex[2];
                float perspectiveFlag;
            };

            // perspectiveFlag = 0: text bypasses the perspective warp (UI overlay).
            Vertex vertices[6] = {
                {{0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f},  // Top-left
                {{1.0f, 1.0f}, {1.0f, 1.0f}, 0.0f},  // Bottom-right
                {{0.0f, 1.0f}, {0.0f, 1.0f}, 0.0f},  // Bottom-left
                {{0.0f, 0.0f}, {0.0f, 0.0f}, 0.0f},  // Top-left
                {{1.0f, 0.0f}, {1.0f, 0.0f}, 0.0f},  // Top-right
                {{1.0f, 1.0f}, {1.0f, 1.0f}, 0.0f}   // Bottom-right
            };

            // Capacity check per glyph.
            uint32_t maxVerts = static_cast<uint32_t>(m_VertexBufferSize / sizeof(Vertex));
            if (m_CurrentVertexCount + 6 > maxVerts)
            {
                return;
            }

            VkDeviceSize vertexDataSize = sizeof(vertices);
            Vertex* mappedVertices = static_cast<Vertex*>(m_VertexBuffersMapped[m_CurrentFrame]);
            memcpy(&mappedVertices[m_CurrentVertexCount], vertices, vertexDataSize);

            CombinedPushConstants pushConstants;

            pushConstants.projection = m_Projection;
            pushConstants.model =
                CalculateModelMatrix(glm::vec2(xpos, ypos), glm::vec2(w, h), 0.0f);
            pushConstants.spriteColor = passColor;
            pushConstants.useColorOnly = 0.0f;
            pushConstants.colorOnly = glm::vec4(0.0f);
            pushConstants.spriteAlpha = alpha;
            pushConstants.ambientColor = glm::vec3(1.0f);  // Text not affected by ambient.

            vkCmdPushConstants(commandBuffer,
                               m_PipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               sizeof(CombinedPushConstants),
                               &pushConstants);

            VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(glyph.imageView);
            if (descriptorSet == VK_NULL_HANDLE)
            {
                x += (glyph.advance >> 6) * scale;
                continue;
            }
            vkCmdBindDescriptorSets(commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_PipelineLayout,
                                    0,
                                    1,
                                    &descriptorSet,
                                    0,
                                    nullptr);

            // Set 1: perspective UBO (text never wants perspective, but the
            // shader's pipeline layout requires the set to be bound).
            vkCmdBindDescriptorSets(commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_PipelineLayout,
                                    1,
                                    1,
                                    &m_PerspDescriptorSets[m_CurrentFrame],
                                    0,
                                    nullptr);

            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);
            vkCmdDraw(commandBuffer, 6, 1, m_CurrentVertexCount, 0);
            ++m_DrawCallCount;

            m_CurrentVertexCount += 6;
            x += (glyph.advance >> 6) * scale;
        }
    };

    // Black outline in 4 cardinal directions, then main text on top.
    glm::vec3 outlineColor(0.0f, 0.0f, 0.0f);
    float outlineOffset = 2.0f * scale * outlineSize;

    static const int outlineDirections[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int dir = 0; dir < 4; dir++)
    {
        int dx = outlineDirections[dir][0];
        int dy = outlineDirections[dir][1];
        glm::vec2 offsetPos = position + glm::vec2(dx * outlineOffset, dy * outlineOffset);
        renderTextPass(offsetPos, outlineColor);
    }

    renderTextPass(position, color);
}

// Create a sampled R8G8B8A8 image + view for one glyph and upload its RGBA bitmap via
// a staging buffer. Zero-sized glyphs fall back to the shared white texture view.
void VulkanRenderer::CreateGlyphTexture(int width,
                                        int height,
                                        const std::vector<unsigned char>& rgbaData,
                                        Glyph& outGlyph)
{
    if (width <= 0 || height <= 0)
    {
        // Fall back to caller's white texture.
        outGlyph.image = VK_NULL_HANDLE;
        outGlyph.memory = VK_NULL_HANDLE;
        outGlyph.imageView = m_WhiteTextureImageView;
        return;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(width);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VK_CHECK(vkCreateImage(m_Device, &imageInfo, nullptr, &outGlyph.image));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, outGlyph.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_Device, &allocInfo, nullptr, &outGlyph.memory));
    vkBindImageMemory(m_Device, outGlyph.image, outGlyph.memory, 0);

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    CreateBuffer(imageSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer,
                 stagingMemory);

    void* dataPtr = nullptr;
    vkMapMemory(m_Device, stagingMemory, 0, imageSize, 0, &dataPtr);
    memcpy(dataPtr, rgbaData.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(m_Device, stagingMemory);

    UploadStagingBufferToImage(
        stagingBuffer, outGlyph.image, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outGlyph.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &outGlyph.imageView));
}

// Load the first available font (project candidates, then OS fallbacks) via FreeType
// and rasterize ASCII 0-127 into per-glyph textures. No-op with a warning when
// FreeType is unavailable or no font is found (text is then skipped at draw time).
void VulkanRenderer::LoadFont()
{
#ifdef USE_FREETYPE
    if (FT_Init_FreeType(&m_FreeType))
    {
        Logger::Error(LOG_SUBSYSTEM, "FREETYPE: Could not init FreeType Library (Vulkan)");
        return;
    }

    std::vector<std::string> fontCandidates = m_FontCandidates;
    if (fontCandidates.empty())
    {
        fontCandidates.push_back("assets/fonts/c8ab67e0-519a-49b5-b693-e8fc86d08efa.ttf");
    }
#ifdef _WIN32
    fontCandidates.push_back("C:/Windows/Fonts/segoeui.ttf");
    fontCandidates.push_back("C:/Windows/Fonts/arial.ttf");
#endif

    bool loaded = false;
    for (const auto& fontPath : fontCandidates)
    {
        if (!std::filesystem::exists(fontPath))
            continue;

        if (FT_New_Face(m_FreeType, fontPath.c_str(), 0, &m_Face))
        {
            continue;
        }

        FT_Set_Pixel_Sizes(m_Face, 0, 24);

        m_Glyphs.clear();

        for (unsigned char c = 0; c < 128; c++)
        {
            if (FT_Load_Char(m_Face, c, FT_LOAD_RENDER))
            {
                continue;
            }

            int width = m_Face->glyph->bitmap.width;
            int height = m_Face->glyph->bitmap.rows;

            Glyph glyph;
            glyph.size = glm::ivec2(width, height);
            glyph.bearing = glm::ivec2(m_Face->glyph->bitmap_left, m_Face->glyph->bitmap_top);
            glyph.advance = static_cast<unsigned int>(m_Face->glyph->advance.x);

            // Some glyphs (e.g., space) have zero-sized bitmaps. Reuse the
            // white texture to avoid invalid images.
            if (width == 0 || height == 0)
            {
                glyph.imageView = m_WhiteTextureImageView;
                glyph.image = VK_NULL_HANDLE;
                glyph.memory = VK_NULL_HANDLE;
                m_Glyphs.insert({static_cast<char>(c), glyph});
                continue;
            }

            std::vector<unsigned char> rgbaData(static_cast<size_t>(width * height * 4));
            for (int i = 0; i < width * height; i++)
            {
                unsigned char value = m_Face->glyph->bitmap.buffer[i];
                rgbaData[i * 4 + 0] = 255;
                rgbaData[i * 4 + 1] = 255;
                rgbaData[i * 4 + 2] = 255;
                rgbaData[i * 4 + 3] = value;
            }

            CreateGlyphTexture(width, height, rgbaData, glyph);
            m_Glyphs.insert({static_cast<char>(c), glyph});
        }

        FT_Done_Face(m_Face);
        m_Face = nullptr;
        loaded = true;
        Logger::InfoF(LOG_SUBSYSTEM,
                      "Loaded font for Vulkan text: {} ({} glyphs)",
                      fontPath,
                      m_Glyphs.size());
        break;
    }

    if (!loaded)
    {
        Logger::Warn(LOG_SUBSYSTEM,
                     "No font loaded for Vulkan renderer text. Text will be skipped.");
    }

    FT_Done_FreeType(m_FreeType);
    m_FreeType = nullptr;
#else
    Logger::Warn(LOG_SUBSYSTEM, "FreeType not available; Vulkan text rendering disabled.");
#endif
}
