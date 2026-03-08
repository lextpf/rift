#include "VulkanRenderer.h"
#include "Texture.h"
#include "VulkanCommon.h"
#include "VulkanShader.h"

#include <algorithm>
#include <cmath>
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

// Ensure Vulkan functions are loaded
#ifdef _WIN32
#define NOMINMAX
#include <excpt.h>
#include <windows.h>

// Try to load vulkan-1.dll explicitly
static bool LoadVulkanLibrary()
{
    HMODULE vulkanLib = LoadLibraryA("vulkan-1.dll");
    if (vulkanLib == NULL)
    {
        std::cerr << "Warning: Could not load vulkan-1.dll. Error: " << GetLastError() << std::endl;
        std::cerr.flush();
        return false;
    }
    std::cout << "Vulkan library loaded successfully" << std::endl;
    std::cout.flush();
    return true;
}
#endif

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

namespace
{
bool ShouldEnableValidationLayers()
{
#ifndef NDEBUG
    return true;
#else
    return false;
#endif
}
}  // namespace

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
    std::cout << "VulkanRenderer constructor called" << std::endl;
    std::cout.flush();
}

VulkanRenderer::~VulkanRenderer()
{
    Shutdown();
}

void VulkanRenderer::Init()
{
    try
    {
        std::cout << "Initializing Vulkan renderer..." << std::endl;
        std::cout.flush();

#ifdef _WIN32
        std::cout << "Init() step 0: Loading Vulkan library..." << std::endl;
        std::cout.flush();
        if (!LoadVulkanLibrary())
        {
            std::cerr << "Warning: Failed to load Vulkan library, but continuing..." << std::endl;
            std::cerr.flush();
        }
#endif

        std::cout << "Init() step 1: Calling CreateInstance()..." << std::endl;
        std::cout.flush();
        CreateInstance();
        std::cout << "Init() step 1 complete: Vulkan instance created" << std::endl;
        std::cout.flush();
        CreateSurface();
        std::cout << "Vulkan surface created" << std::endl;
        std::cout.flush();
        PickPhysicalDevice();
        std::cout << "Physical device selected" << std::endl;
        std::cout.flush();
        CreateLogicalDevice();
        std::cout << "Logical device created" << std::endl;
        std::cout.flush();
        CreateSwapchain();
        std::cout << "Swapchain created" << std::endl;
        std::cout.flush();
        CreateImageViews();
        std::cout << "Image views created" << std::endl;
        std::cout.flush();
        CreateRenderPass();
        std::cout << "Init() step 7 complete: Render pass created" << std::endl;
        std::cout.flush();

        std::cout << "Init() step 8: Creating graphics pipeline..." << std::endl;
        std::cout.flush();
        CreateGraphicsPipeline();
        std::cout << "Init() step 8 complete: Graphics pipeline created" << std::endl;
        std::cout.flush();
        CreateFramebuffers();
        std::cout << "Framebuffers created" << std::endl;
        std::cout.flush();
        CreateCommandPool();
        std::cout << "Command pool created" << std::endl;
        std::cout.flush();
        CreateBuffers();
        std::cout << "Buffers created" << std::endl;
        std::cout.flush();
        CreateDescriptorPool();
        std::cout << "Descriptor pool created" << std::endl;
        std::cout.flush();
        CreateTextureSampler();
        std::cout << "Texture sampler created" << std::endl;
        std::cout.flush();
        CreateWhiteTexture();
        std::cout << "White texture created" << std::endl;
        std::cout.flush();
        LoadFont();
        std::cout << "Font loading complete (Vulkan)" << std::endl;
        std::cout.flush();
        CreateCommandBuffers();
        std::cout << "Command buffers created" << std::endl;
        std::cout.flush();
        CreateSyncObjects();
        std::cout << "Sync objects created" << std::endl;
        std::cout.flush();
        std::cout << "Vulkan renderer initialized successfully!" << std::endl;
        std::cout.flush();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception in VulkanRenderer::Init(): " << e.what() << std::endl;
        std::cerr.flush();
        throw;  // Re-throw to be caught by Game::Initialize()
    }
    catch (...)
    {
        std::cerr << "Unknown exception in VulkanRenderer::Init()" << std::endl;
        std::cerr.flush();
        throw;
    }
}

void VulkanRenderer::Shutdown()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        // Wait for device to be idle before destroying
        VkResult waitResult = vkDeviceWaitIdle(m_Device);
        if (waitResult != VK_SUCCESS && waitResult != VK_ERROR_DEVICE_LOST)
        {
            // Device might already be lost/invalid, continue with cleanup
            std::cerr << "Warning: vkDeviceWaitIdle failed: " << waitResult << std::endl;
        }

        // Unmap vertex buffers if mapped
        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            if (m_VertexBuffersMapped[i])
            {
                vkUnmapMemory(m_Device, m_VertexBufferMemories[i]);
                m_VertexBuffersMapped[i] = nullptr;
            }
        }

        // Cleanup uploaded textures (Texture objects that hold Vulkan resources)
        // This must happen before destroying the device
        for (Texture* tex : m_UploadedTextures)
        {
            if (tex)
            {
                tex->DestroyVulkanTexture(m_Device);
            }
        }
        m_UploadedTextures.clear();

        // Texture cache stores non-owning references to Texture-managed Vulkan resources.
        // DestroyVulkanTexture() above already released owned resources, so only clear cache.
        m_TextureCache.clear();

        // Cleanup texture sampler
        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_Device, m_TextureSampler, nullptr);
        }

        // Cleanup buffers
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
        }

        // Cleanup white texture
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

        // Cleanup glyph textures
        // Skip glyphs that use the white texture as fallback (avoid double-destroy)
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

        // Cleanup descriptor set cache (descriptor sets are freed when pool is destroyed)
        m_DescriptorSetCache.clear();

        // Cleanup descriptor pool
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        }
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
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
        m_Instance = VK_NULL_HANDLE;  // Prevent double-destroy on teardown
    }
}

void VulkanRenderer::CleanupSwapchain()
{
    // Cleanup framebuffers
    for (auto framebuffer : m_SwapchainFramebuffers)
    {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapchainFramebuffers.clear();

    // Cleanup image views
    for (auto imageView : m_SwapchainImageViews)
    {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapchainImageViews.clear();

    // Cleanup swapchain
    if (m_Swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    m_SwapchainImages.clear();
    m_ImagesInFlight.clear();
}

void VulkanRenderer::RecreateSwapchain()
{
    // Get new window size
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);

    // Handle minimization - wait until window is visible again
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

void VulkanRenderer::CreateInstance()
{
    std::cout << "CreateInstance() step 1: Creating VkApplicationInfo..." << std::endl;
    std::cout.flush();

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Rift Game";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::cout << "CreateInstance() step 2: Creating VkInstanceCreateInfo..." << std::endl;
    std::cout.flush();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::cout << "CreateInstance() step 3: Getting required extensions..." << std::endl;
    std::cout.flush();

    auto extensions = GetRequiredExtensions();
    std::cout << "CreateInstance() step 3 complete: Got " << extensions.size() << " extensions"
              << std::endl;
    std::cout.flush();

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    std::cout << "CreateInstance() step 4: Checking validation layer support..." << std::endl;
    std::cout.flush();

    const bool enableValidationLayers = ShouldEnableValidationLayers();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    bool hasValidationLayers = enableValidationLayers && CheckValidationLayerSupport();
    std::cout << "CreateInstance() step 4 complete: Validation layers "
              << (hasValidationLayers ? "enabled" : "disabled") << std::endl;
    std::cout.flush();

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
        debugCreateInfo.pfnUserCallback = nullptr;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    std::cout << "CreateInstance() step 5: Calling vkCreateInstance()..." << std::endl;
    std::cout.flush();

    // Check if vkCreateInstance function pointer is valid
    if (vkCreateInstance == nullptr)
    {
        std::cerr
            << "ERROR: vkCreateInstance function pointer is NULL! Vulkan loader may not be loaded."
            << std::endl;
        std::cerr.flush();
        throw std::runtime_error("Vulkan loader not properly initialized!");
    }

    std::cout << "vkCreateInstance function pointer is valid, calling..." << std::endl;
    std::cout.flush();

    // Print extension names for debugging
    std::cout << "Extensions being requested:" << std::endl;
    for (uint32_t i = 0; i < createInfo.enabledExtensionCount; i++)
    {
        std::cout << "  - " << createInfo.ppEnabledExtensionNames[i] << std::endl;
    }
    std::cout.flush();

    if (createInfo.enabledLayerCount > 0)
    {
        std::cout << "Validation layers being requested:" << std::endl;
        for (uint32_t i = 0; i < createInfo.enabledLayerCount; i++)
        {
            std::cout << "  - " << createInfo.ppEnabledLayerNames[i] << std::endl;
        }
    }
    else
    {
        std::cout << "No validation layers requested" << std::endl;
    }
    std::cout.flush();

    VkResult result;

    // Call vkCreateInstance - if it crashes, it's likely a driver/runtime issue
    result = vkCreateInstance(&createInfo, nullptr, &m_Instance);

    if (result != VK_SUCCESS)
    {
        std::cerr << "ERROR: vkCreateInstance failed with result: " << result << std::endl;
        std::cerr.flush();

        // Print more details about the error
        switch (result)
        {
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                std::cerr << "  Reason: Out of host memory" << std::endl;
                break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                std::cerr << "  Reason: Out of device memory" << std::endl;
                break;
            case VK_ERROR_INITIALIZATION_FAILED:
                std::cerr << "  Reason: Initialization failed" << std::endl;
                break;
            case VK_ERROR_LAYER_NOT_PRESENT:
                std::cerr << "  Reason: Layer not present" << std::endl;
                break;
            case VK_ERROR_EXTENSION_NOT_PRESENT:
                std::cerr << "  Reason: Extension not present" << std::endl;
                break;
            case VK_ERROR_INCOMPATIBLE_DRIVER:
                std::cerr << "  Reason: Incompatible driver" << std::endl;
                break;
            default:
                std::cerr << "  Reason: Unknown error code" << std::endl;
                break;
        }

        throw std::runtime_error("Failed to create Vulkan instance!");
    }

    std::cout << "CreateInstance() step 5 complete: Instance created successfully" << std::endl;
    std::cout.flush();
}

void VulkanRenderer::CreateSurface()
{
    VK_CHECK(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface));
}

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

void VulkanRenderer::CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &formatCount, formats.data());

    // Prefer UNORM format to match OpenGL's non-gamma-corrected output
    // SRGB format applies gamma correction which makes textures appear brighter
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
        {
            surfaceFormat = availableFormat;
            break;
        }
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_PhysicalDevice, m_Surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_PhysicalDevice, m_Surface, &presentModeCount, presentModes.data());

    // Prefer uncapped presentation when available so app-side FPS limiting can work.
    // Fallback order:
    //   1) IMMEDIATE (no vsync, may tear)
    //   2) MAILBOX   (low-latency vsync)
    //   3) FIFO      (always supported, vsync)
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

void VulkanRenderer::CreateGraphicsPipeline()
{
    std::cout << "CreateGraphicsPipeline() step 1: Starting..." << std::endl;
    std::cout.flush();
    // Shader code (simplified - in production, compile GLSL to SPIR-V)
    // For now, we'll create a minimal pipeline
    // NOTE: Full implementation requires SPIR-V shader compilation

    // Note: Shader stage info will be created after shader modules are loaded

    // Vertex input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 4;  // pos + tex
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributeDescriptions[2]{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 2;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
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

    // Push constant range for matrices and uniforms
    // Vertex shader: mat4 projection (offset 0, 64 bytes), mat4 model (offset 64, 64 bytes) = 128
    // bytes total Fragment shader: vec3 spriteColor (offset 128, 12 bytes), bool useColorOnly
    // (offset 140, 4 bytes), vec4 colorOnly (offset 144, 16 bytes) = 32 bytes Total: 160 bytes
    // (vec3 is 12 bytes, but vec4 needs 16-byte alignment, so we use 160 total)
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 192;  // 128 for vertex (2 mat4), 64 for fragment (vec3 + float + vec4
                                   // + float + padding + vec3)

    // Descriptor set layout for textures
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

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout));

    std::cout << "CreateGraphicsPipeline() step 2: Loading shaders..." << std::endl;
    std::cout.flush();

    // Load shaders
    std::vector<uint32_t> vertShaderCode = VulkanShader::GetVertexShaderSPIRV();
    std::vector<uint32_t> fragShaderCode = VulkanShader::GetFragmentShaderSPIRV();

    std::cout << "CreateGraphicsPipeline() step 2: Vertex shader size: " << vertShaderCode.size()
              << " words" << std::endl;
    std::cout << "CreateGraphicsPipeline() step 2: Fragment shader size: " << fragShaderCode.size()
              << " words" << std::endl;
    std::cout.flush();

    if (vertShaderCode.empty() || fragShaderCode.empty())
    {
        std::cerr << "ERROR: Vulkan shaders not found!" << std::endl;
        std::cerr << "Please compile shaders: glslangValidator -V shaders/sprite.vert -o "
                     "shaders/sprite.vert.spv"
                  << std::endl;
        std::cerr << "                      glslangValidator -V shaders/sprite.frag -o "
                     "shaders/sprite.frag.spv"
                  << std::endl;
        std::cerr << "Or run: build.bat" << std::endl;
        // Don't continue without shaders - throw exception
        throw std::runtime_error("Vulkan shaders not found. Please compile shaders first.");
    }

    std::cout << "CreateGraphicsPipeline() step 3: Creating shader modules..." << std::endl;
    std::cout.flush();

    VkShaderModule vertShaderModule = VulkanShader::CreateShaderModule(m_Device, vertShaderCode);
    std::cout << "CreateGraphicsPipeline() step 3: Vertex shader module created" << std::endl;
    std::cout.flush();

    VkShaderModule fragShaderModule = VulkanShader::CreateShaderModule(m_Device, fragShaderCode);
    std::cout << "CreateGraphicsPipeline() step 3: Fragment shader module created" << std::endl;
    std::cout.flush();

    // Create shader stage info AFTER modules are created
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

    std::cout << "CreateGraphicsPipeline() step 3: Shader stages configured" << std::endl;
    std::cout.flush();

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

    std::cout << "CreateGraphicsPipeline() step 4: Validating pipeline state..." << std::endl;
    std::cout << "  - Device: " << (void*)m_Device << std::endl;
    std::cout << "  - RenderPass: " << (void*)m_RenderPass << std::endl;
    std::cout << "  - PipelineLayout: " << (void*)m_PipelineLayout << std::endl;
    std::cout << "  - Vertex shader module: " << (void*)vertShaderModule << std::endl;
    std::cout << "  - Fragment shader module: " << (void*)fragShaderModule << std::endl;
    std::cout << "  - Swapchain extent: " << m_SwapchainExtent.width << "x"
              << m_SwapchainExtent.height << std::endl;
    std::cout.flush();

    std::cout << "CreateGraphicsPipeline() step 5: Calling vkCreateGraphicsPipelines()..."
              << std::endl;
    std::cout.flush();

    VkResult pipelineResult = vkCreateGraphicsPipelines(
        m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline);
    if (pipelineResult != VK_SUCCESS)
    {
        std::cerr << "ERROR: vkCreateGraphicsPipelines failed with result: " << pipelineResult
                  << std::endl;
        std::cerr.flush();

        // Print error details
        switch (pipelineResult)
        {
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                std::cerr << "  Reason: Out of host memory" << std::endl;
                break;
            case VK_ERROR_OUT_OF_DEVICE_MEMORY:
                std::cerr << "  Reason: Out of device memory" << std::endl;
                break;
            case VK_ERROR_INVALID_SHADER_NV:
                std::cerr << "  Reason: Invalid shader" << std::endl;
                break;
            default:
                std::cerr << "  Reason: Unknown error code" << std::endl;
                break;
        }

        vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
        vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
        throw std::runtime_error("Failed to create graphics pipeline!");
    }

    std::cout << "CreateGraphicsPipeline() step 4: Graphics pipeline created successfully"
              << std::endl;
    std::cout.flush();

    std::cout << "CreateGraphicsPipeline() step 5: Cleaning up shader modules..." << std::endl;
    std::cout.flush();
    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
    std::cout << "CreateGraphicsPipeline() complete!" << std::endl;
    std::cout.flush();
}

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

void VulkanRenderer::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_GraphicsFamily;

    VK_CHECK(vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool));
}

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
}

bool VulkanRenderer::CheckValidationLayerSupport()
{
    std::cout
        << "CheckValidationLayerSupport() step 1: Calling vkEnumerateInstanceLayerProperties()..."
        << std::endl;
    std::cout.flush();

    uint32_t layerCount;
    VkResult result = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if (result != VK_SUCCESS)
    {
        std::cerr << "Warning: vkEnumerateInstanceLayerProperties failed: " << result << std::endl;
        std::cerr.flush();
        return false;
    }

    std::cout << "CheckValidationLayerSupport() step 1 complete: Found " << layerCount << " layers"
              << std::endl;
    std::cout.flush();

    std::vector<VkLayerProperties> availableLayers(layerCount);
    result = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    if (result != VK_SUCCESS)
    {
        std::cerr << "Warning: vkEnumerateInstanceLayerProperties (second call) failed: " << result
                  << std::endl;
        std::cerr.flush();
        return false;
    }

    std::cout << "CheckValidationLayerSupport() step 2: Checking for required validation layers..."
              << std::endl;
    std::cout.flush();

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
            std::cout << "CheckValidationLayerSupport() step 2: Layer '" << layerName
                      << "' not found" << std::endl;
            std::cout.flush();
            return false;
        }
    }

    std::cout << "CheckValidationLayerSupport() complete: All validation layers found" << std::endl;
    std::cout.flush();

    return true;
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions()
{
    std::cout << "GetRequiredExtensions() step 1: Calling glfwGetRequiredInstanceExtensions()..."
              << std::endl;
    std::cout.flush();

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::cout << "GetRequiredExtensions() step 1 complete: Got " << glfwExtensionCount
              << " extensions from GLFW" << std::endl;
    std::cout.flush();

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    std::cout << "GetRequiredExtensions() step 2: Checking validation layer support..."
              << std::endl;
    std::cout.flush();

    // Only add debug extension if validation layers are enabled
    const bool enableValidationLayers = ShouldEnableValidationLayers();
    if (enableValidationLayers && CheckValidationLayerSupport())
    {
        std::cout
            << "GetRequiredExtensions() step 2: Validation layers available, adding debug extension"
            << std::endl;
        std::cout.flush();
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else
    {
        std::cout << "GetRequiredExtensions() step 2: Validation layers disabled or not available"
                  << std::endl;
        std::cout.flush();
    }

    std::cout << "GetRequiredExtensions() complete: Returning " << extensions.size()
              << " extensions" << std::endl;
    std::cout.flush();

    return extensions;
}

void VulkanRenderer::BeginFrame()
{
    m_FrameActive = false;

    // Reset vertex buffer counter and batch state at start of frame
    m_CurrentVertexCount = 0;
    m_BatchImageView = VK_NULL_HANDLE;
    m_BatchDescriptorSet = VK_NULL_HANDLE;
    m_BatchStartVertex = 0;
    m_DrawCallCount = 0;

    if (m_Device == VK_NULL_HANDLE || m_Swapchain == VK_NULL_HANDLE)
    {
        std::cerr << "Error: BeginFrame called but Vulkan not initialized!" << std::endl;
        return;
    }

    if (m_CurrentFrame >= m_InFlightFences.size())
    {
        std::cerr << "Error: CurrentFrame out of bounds!" << std::endl;
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
        RecreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        std::cerr << "Error: Failed to acquire swapchain image! Result: " << result << std::endl;
        return;  // Don't throw, just return
    }

    if (m_ImageIndex >= m_CommandBuffers.size())
    {
        std::cerr << "Error: ImageIndex out of bounds! ImageIndex=" << m_ImageIndex
                  << ", CommandBufferCount=" << m_CommandBuffers.size() << std::endl;
        return;
    }

    if (m_ImageIndex >= m_ImagesInFlight.size())
    {
        std::cerr << "Error: ImageIndex out of bounds for image-fence tracking! ImageIndex="
                  << m_ImageIndex << ", ImagesInFlightCount=" << m_ImagesInFlight.size()
                  << std::endl;
        return;
    }

    if (m_ImagesInFlight[m_ImageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(m_Device, 1, &m_ImagesInFlight[m_ImageIndex], VK_TRUE, UINT64_MAX);
    }

    if (m_CurrentFrame >= m_CommandBuffers.size())
    {
        std::cerr << "Error: CurrentFrame out of bounds for command buffers!" << std::endl;
        return;
    }

    vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrame], 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult beginResult = vkBeginCommandBuffer(m_CommandBuffers[m_CurrentFrame], &beginInfo);
    if (beginResult != VK_SUCCESS)
    {
        std::cerr << "Error: Failed to begin command buffer! Result: " << beginResult << std::endl;
        return;
    }

    if (m_ImageIndex >= m_SwapchainFramebuffers.size())
    {
        std::cerr << "Error: ImageIndex out of bounds for framebuffers! ImageIndex=" << m_ImageIndex
                  << ", FramebufferCount=" << m_SwapchainFramebuffers.size() << std::endl;
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

        // Set dynamic viewport with Y-flip for Vulkan coordinate system
        // This uses VK_KHR_maintenance1 behavior (core in Vulkan 1.1+)
        // Negative height flips Y to match OpenGL's coordinate system
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
        std::cerr << "Warning: Graphics pipeline is null, cannot bind!" << std::endl;
    }
}

void VulkanRenderer::EndFrame()
{
    if (!m_FrameActive)
    {
        return;
    }

    if (m_Device == VK_NULL_HANDLE)
    {
        std::cerr << "Error: EndFrame called but Vulkan not initialized!" << std::endl;
        m_FrameActive = false;
        return;
    }

    if (m_CurrentFrame >= m_CommandBuffers.size())
    {
        std::cerr << "Error: CurrentFrame out of bounds in EndFrame!" << std::endl;
        m_FrameActive = false;
        return;
    }

    // Flush any remaining batched sprites before ending the frame
    FlushSpriteBatch();

    vkCmdEndRenderPass(m_CommandBuffers[m_CurrentFrame]);

    VkResult endResult = vkEndCommandBuffer(m_CommandBuffers[m_CurrentFrame]);
    if (endResult != VK_SUCCESS)
    {
        std::cerr << "Error: Failed to end command buffer! Result: " << endResult << std::endl;
        m_FrameActive = false;
        return;
    }

    if (m_CurrentFrame >= m_ImageAvailableSemaphores.size() ||
        m_CurrentFrame >= m_RenderFinishedSemaphores.size() ||
        m_CurrentFrame >= m_InFlightFences.size())
    {
        std::cerr << "Error: CurrentFrame out of bounds for sync objects!" << std::endl;
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
        std::cerr << "Error: Failed to reset fence! Result: " << resetFenceResult << std::endl;
        m_FrameActive = false;
        return;
    }

    VkResult submitResult =
        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrame]);
    if (submitResult != VK_SUCCESS)
    {
        std::cerr << "Error: Failed to submit command buffer! Result: " << submitResult
                  << std::endl;
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
        std::cerr << "Error: Failed to present swapchain image! Result: " << presentResult
                  << std::endl;
    }

    m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    m_FrameActive = false;
}

void VulkanRenderer::SetViewport(int x, int y, int width, int height)
{
    (void)x;
    (void)y;  // Suppress unused parameter warnings

    // Check if size has changed
    if (width != static_cast<int>(m_SwapchainExtent.width) ||
        height != static_cast<int>(m_SwapchainExtent.height))
    {
        m_FramebufferResized = true;
    }
}

void VulkanRenderer::Clear(float r, float g, float b, float a)
{
    // Clear is handled in BeginFrame via render pass
}

void VulkanRenderer::BuildQuadVertices(SpriteVertex outVertices[6],
                                       const glm::vec2 corners[4],
                                       const glm::vec2 texCoords[4])
{
    outVertices[0] = {corners[0].x, corners[0].y, texCoords[0].x, texCoords[0].y};
    outVertices[1] = {corners[2].x, corners[2].y, texCoords[2].x, texCoords[2].y};
    outVertices[2] = {corners[3].x, corners[3].y, texCoords[3].x, texCoords[3].y};
    outVertices[3] = {corners[0].x, corners[0].y, texCoords[0].x, texCoords[0].y};
    outVertices[4] = {corners[1].x, corners[1].y, texCoords[1].x, texCoords[1].y};
    outVertices[5] = {corners[2].x, corners[2].y, texCoords[2].x, texCoords[2].y};
}

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

    struct CombinedPushConstants
    {
        glm::mat4 projection;
        glm::mat4 model;
        glm::vec3 spriteColor;
        float useColorOnly;
        glm::vec4 colorOnly;
        float spriteAlpha;
        float _padding[3];
        glm::vec3 ambientColor;
        float _padding2;
    } pc;

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
                       192,
                       &pc);

    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout,
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);

    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);

    vkCmdDraw(commandBuffer, 6, 1, m_CurrentVertexCount, 0);
    ++m_DrawCallCount;
    m_CurrentVertexCount += 6;
    return true;
}

void VulkanRenderer::DrawSprite(
    const Texture& texture, glm::vec2 position, glm::vec2 size, float rotation, glm::vec3 color)
{
    DrawSpriteRegion(
        texture, position, size, glm::vec2(0.0f), glm::vec2(1.0f), rotation, color, true);
}

void VulkanRenderer::DrawSpriteRegion(const Texture& texture,
                                      glm::vec2 position,
                                      glm::vec2 size,
                                      glm::vec2 texCoord,
                                      glm::vec2 texSize,
                                      float rotation,
                                      glm::vec3 color,
                                      bool flipY)
{
    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        std::cerr << "Warning: Attempting to draw but pipeline not ready. GraphicsPipeline="
                  << (void*)m_GraphicsPipeline
                  << ", DescriptorSetLayout=" << (void*)m_DescriptorSetLayout << std::endl;
        return;  // Pipeline not ready
    }

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
    {
        std::cerr << "Warning: Command buffers not ready. CurrentFrame=" << m_CurrentFrame
                  << ", BufferCount=" << m_CommandBuffers.size() << std::endl;
        return;
    }

    // Get texture's Vulkan image view (or use white texture as fallback)
    VkImageView imageView = m_WhiteTextureImageView;
#ifdef USE_VULKAN
    // Try to use texture's Vulkan resources if available
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
    {
        imageView = texImageView;
    }
    else
    {
        // Texture not uploaded yet - try to upload it now
        std::cout << "Texture not uploaded, uploading now... (size: " << texture.GetWidth() << "x"
                  << texture.GetHeight() << ")" << std::endl;
        std::cout.flush();
        try
        {
            UploadTexture(texture);
            // Try again after upload
            texImageView = texture.GetVulkanImageView();
            if (texImageView != VK_NULL_HANDLE)
            {
                std::cout << "Texture uploaded successfully!" << std::endl;
                std::cout.flush();
                imageView = texImageView;
            }
            else
            {
                // Still failed - use white texture fallback
                std::cerr
                    << "Warning: UploadTexture succeeded but GetVulkanImageView() returned NULL"
                    << std::endl;
                std::cerr.flush();
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error uploading texture: " << e.what() << std::endl;
            std::cerr.flush();
        }
        catch (...)
        {
            std::cerr << "Unknown error uploading texture" << std::endl;
            std::cerr.flush();
        }
    }
#endif

    // Get or create descriptor set for this texture (cached)
    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(imageView);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        // Failed to get descriptor set - skip this draw
        return;
    }

    // Texture coordinates - need to normalize to 0-1 range
    // texCoord and texSize are in pixels, we need to normalize them by texture size
    int texWidth = texture.GetWidth();
    int texHeight = texture.GetHeight();

    if (texWidth <= 0 || texHeight <= 0)
    {
        std::cerr << "Warning: Invalid texture size: " << texWidth << "x" << texHeight << std::endl;
        return;
    }

    // Normalize texture coordinates to 0-1 range
    // No texel offset needed for GL_NEAREST filtering with pixel art
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
        // Match OpenGL-style flip (textures are loaded flipped by stb)
        vTop = 1.0f - (texY + texH);
        vBottom = 1.0f - texY;
    }
    else
    {
        vTop = texY;
        vBottom = texY + texH;
    }

    // Match OpenGL's UV assignment where top-left vertex gets vBottom
    // This accounts for OpenGL's inverted V coordinate (V=0 at bottom)
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
    ApplyPerspective(corners);

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords);
    SubmitQuad(descriptorSet, vertices, color, 1.0f);
}

void VulkanRenderer::DrawSpriteAlpha(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    // Note: additive blending not yet fully implemented in Vulkan renderer
    (void)additive;

    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    // Get texture's Vulkan image view
    VkImageView imageView = m_WhiteTextureImageView;
#ifdef USE_VULKAN
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
    {
        imageView = texImageView;
    }
    else
    {
        try
        {
            UploadTexture(texture);
            texImageView = texture.GetVulkanImageView();
            if (texImageView != VK_NULL_HANDLE)
                imageView = texImageView;
        }
        catch (...)
        {
        }
    }
#endif

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
    ApplyPerspective(corners);

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords);
    SubmitQuad(descriptorSet, vertices, glm::vec3(color.r, color.g, color.b), color.a);
}

void VulkanRenderer::DrawSpriteAtlas(const Texture& texture,
                                     glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec2 uvMin,
                                     glm::vec2 uvMax,
                                     float rotation,
                                     glm::vec4 color,
                                     bool additive)
{
    // Atlas version with custom UV coordinates
    (void)additive;

    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    VkImageView imageView = m_WhiteTextureImageView;
#ifdef USE_VULKAN
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
    {
        imageView = texImageView;
    }
    else
    {
        try
        {
            UploadTexture(texture);
            texImageView = texture.GetVulkanImageView();
            if (texImageView != VK_NULL_HANDLE)
                imageView = texImageView;
        }
        catch (...)
        {
        }
    }
#endif

    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(imageView);
    if (descriptorSet == VK_NULL_HANDLE)
        return;

    // Use provided UV coordinates
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
    ApplyPerspective(corners);

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords);
    SubmitQuad(descriptorSet, vertices, glm::vec3(color.r, color.g, color.b), color.a);
}

void VulkanRenderer::DrawColoredRect(glm::vec2 position,
                                     glm::vec2 size,
                                     glm::vec4 color,
                                     bool additive)
{
    // Note: additive blending not yet implemented in Vulkan renderer
    (void)additive;
    if (!m_FrameActive)
        return;

    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        return;  // Pipeline not ready
    }

    // Get or create descriptor set for white texture (cached)
    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(m_WhiteTextureImageView);
    if (descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }

    glm::vec2 corners[4] = {{position.x, position.y},
                            {position.x + size.x, position.y},
                            {position.x + size.x, position.y + size.y},
                            {position.x, position.y + size.y}};

    ApplyPerspective(corners);

    glm::vec2 texCoords[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    SpriteVertex vertices[6];
    BuildQuadVertices(vertices, corners, texCoords);
    SubmitQuad(descriptorSet, vertices, glm::vec3(1.0f), 1.0f, true, color);
}

void VulkanRenderer::DrawWarpedQuad(const Texture& texture,
                                    const glm::vec2 corners[4],
                                    glm::vec2 texCoord,
                                    glm::vec2 texSize,
                                    glm::vec3 color,
                                    bool flipY)
{
    if (!m_FrameActive)
        return;

    // Warped quads are pre-transformed - corners already include projection
    // We draw them directly without additional perspective transformation
    if (m_GraphicsPipeline == VK_NULL_HANDLE || m_DescriptorSetLayout == VK_NULL_HANDLE)
    {
        return;
    }

#ifdef USE_VULKAN
    if (texture.GetVulkanImageView() == VK_NULL_HANDLE)
    {
        try
        {
            UploadTexture(texture);
        }
        catch (...)
        {
        }
    }
#endif

    // Get texture resources
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

    // Calculate UV coordinates from pixel coordinates
    float texW = std::max(1.0f, static_cast<float>(texture.GetWidth()));
    float texH = std::max(1.0f, static_cast<float>(texture.GetHeight()));

    float u0 = texCoord.x / texW;
    float u1 = (texCoord.x + texSize.x) / texW;

    float v0, v1;
    if (flipY)
    {
        // Flip Y for textures loaded with stb_image
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

VkDescriptorSet VulkanRenderer::GetOrCreateDescriptorSet(VkImageView imageView)
{
    if (imageView == VK_NULL_HANDLE || m_DescriptorPool == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    // Check cache first
    auto it = m_DescriptorSetCache.find(imageView);
    if (it != m_DescriptorSetCache.end())
    {
        return it->second;
    }

    // Allocate new descriptor set
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
        // Try one recovery pass: reset the pool and rebuild cache.
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            std::cerr << "Warning: Descriptor pool exhausted, resetting descriptor pool cache."
                      << std::endl;
            vkDeviceWaitIdle(m_Device);
            vkResetDescriptorPool(m_Device, m_DescriptorPool, 0);
            m_DescriptorSetCache.clear();
            descriptorSet = VK_NULL_HANDLE;
            result = tryAllocate();
        }

        if (result != VK_SUCCESS)
        {
            std::cerr << "Warning: Failed to allocate descriptor set. VkResult=" << result
                      << std::endl;
            return VK_NULL_HANDLE;
        }
    }

    // Update descriptor set with texture
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

    // Cache the descriptor set
    m_DescriptorSetCache[imageView] = descriptorSet;

    return descriptorSet;
}

void VulkanRenderer::FlushSpriteBatch()
{
    // Nothing to flush if no vertices accumulated
    if (m_CurrentVertexCount == m_BatchStartVertex)
        return;

    // Need a valid batch state
    if (m_BatchImageView == VK_NULL_HANDLE || m_BatchDescriptorSet == VK_NULL_HANDLE)
        return;

    if (m_CommandBuffers.empty() || m_CurrentFrame >= m_CommandBuffers.size())
        return;

    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];

    // Push constants - identity model since vertices are pre-transformed
    struct CombinedPushConstants
    {
        glm::mat4 projection;    // 0-63
        glm::mat4 model;         // 64-127
        glm::vec3 spriteColor;   // 128-139
        float useColorOnly;      // 140-143
        glm::vec4 colorOnly;     // 144-159
        float spriteAlpha;       // 160-163
        float _padding[3];       // 164-175 (padding to align ambientColor)
        glm::vec3 ambientColor;  // 176-187
        float _padding2;         // 188-191 (padding to 192)
    } pushConstants;

    pushConstants.projection = m_Projection;
    pushConstants.model = glm::mat4(1.0f);        // Identity - vertices already transformed
    pushConstants.spriteColor = glm::vec3(1.0f);  // White (no tint) for batched sprites
    pushConstants.useColorOnly = 0.0f;
    pushConstants.colorOnly = glm::vec4(0.0f);
    pushConstants.spriteAlpha = 1.0f;  // Full opacity for sprites
    pushConstants.ambientColor = m_AmbientColor;

    vkCmdPushConstants(commandBuffer,
                       m_PipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       192,
                       &pushConstants);

    // Bind descriptor set for batch texture
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_PipelineLayout,
                            0,
                            1,
                            &m_BatchDescriptorSet,
                            0,
                            nullptr);

    // Bind vertex buffer
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);

    // Draw all accumulated vertices in one call
    uint32_t vertexCount = m_CurrentVertexCount - m_BatchStartVertex;
    vkCmdDraw(commandBuffer, vertexCount, 1, m_BatchStartVertex, 0);
    ++m_DrawCallCount;

    // Reset batch - start new batch at current position
    m_BatchStartVertex = m_CurrentVertexCount;
    m_BatchImageView = VK_NULL_HANDLE;
    m_BatchDescriptorSet = VK_NULL_HANDLE;
}

glm::mat4 VulkanRenderer::CalculateModelMatrix(glm::vec2 position, glm::vec2 size, float rotation)
{
    // Create model matrix matching OpenGL behavior
    // Vertices are 0.0 to 1.0 (top-left to bottom-right)
    // Note: In Vulkan, clip space Y points down, but we've already flipped vertex Y coordinates
    // so the model matrix calculation can be the same as OpenGL
    glm::mat4 model = glm::mat4(1.0f);

    // Translate to position (top-left corner)
    model = glm::translate(model, glm::vec3(position, 0.0f));

    // Translate to center of quad (since vertices are 0.0-1.0, center is at 0.5*size)
    model = glm::translate(model, glm::vec3(0.5f * size.x, 0.5f * size.y, 0.0f));

    // Rotate around Z axis (rotation center is now at quad center)
    if (rotation != 0.0f)
    {
        model = glm::rotate(model, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Translate back (so scaling happens around origin)
    model = glm::translate(model, glm::vec3(-0.5f * size.x, -0.5f * size.y, 0.0f));

    // Scale by size (vertices go from 0.0-1.0 to 0.0-size)
    model = glm::scale(model, glm::vec3(size, 1.0f));

    return model;
}

void VulkanRenderer::UploadTexture(const Texture& texture)
{
    // Call CreateVulkanTexture on the texture to upload it to the GPU
    // Cast away const since we're modifying Vulkan state, not logical texture state
    Texture* texPtr = const_cast<Texture*>(&texture);
    texPtr->CreateVulkanTexture(m_Device, m_PhysicalDevice, m_CommandPool, m_GraphicsQueue);
    m_TextureCache.erase(texPtr);

    // Track for cleanup during shutdown
    // Check if already tracked to avoid duplicates
    auto it = std::find(m_UploadedTextures.begin(), m_UploadedTextures.end(), texPtr);
    if (it == m_UploadedTextures.end())
    {
        m_UploadedTextures.push_back(texPtr);
    }
}

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
    // If no glyphs loaded, use font size as fallback
    if (maxAscent == 0)
    {
        maxAscent = 24;  // Default font size
    }
    return static_cast<float>(maxAscent) * scale;
}

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
            width += it->second.advance * scale;
        }
    }
    return width;
}

#ifdef DrawText
#undef DrawText
#endif
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

    // Estimate line height from first glyph
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

    // Helper lambda to render text at a given position with a given color
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
            };

            Vertex vertices[6] = {
                {0.0f, 0.0f, 0.0f, 0.0f},  // Top-left
                {1.0f, 1.0f, 1.0f, 1.0f},  // Bottom-right
                {0.0f, 1.0f, 0.0f, 1.0f},  // Bottom-left
                {0.0f, 0.0f, 0.0f, 0.0f},  // Top-left
                {1.0f, 0.0f, 1.0f, 0.0f},  // Top-right
                {1.0f, 1.0f, 1.0f, 1.0f}   // Bottom-right
            };

            // Capacity check per glyph
            uint32_t maxVerts = static_cast<uint32_t>(m_VertexBufferSize / sizeof(Vertex));
            if (m_CurrentVertexCount + 6 > maxVerts)
            {
                return;
            }

            // Copy vertex data into mapped buffer - use current frame's buffer
            VkDeviceSize vertexDataSize = sizeof(vertices);
            Vertex* mappedVertices = static_cast<Vertex*>(m_VertexBuffersMapped[m_CurrentFrame]);
            memcpy(&mappedVertices[m_CurrentVertexCount], vertices, vertexDataSize);

            // Push constants (same layout as sprites)
            struct CombinedPushConstants
            {
                glm::mat4 projection;
                glm::mat4 model;
                glm::vec3 spriteColor;
                float useColorOnly;
                glm::vec4 colorOnly;
                float spriteAlpha;
                float _padding[3];
                glm::vec3 ambientColor;
                float _padding2;
            } pushConstants;

            pushConstants.projection = m_Projection;
            pushConstants.model =
                CalculateModelMatrix(glm::vec2(xpos, ypos), glm::vec2(w, h), 0.0f);
            pushConstants.spriteColor = passColor;
            pushConstants.useColorOnly = 0.0f;
            pushConstants.colorOnly = glm::vec4(0.0f);
            pushConstants.spriteAlpha = alpha;             // Text transparency from parameter
            pushConstants.ambientColor = glm::vec3(1.0f);  // Text not affected by ambient lighting

            vkCmdPushConstants(commandBuffer,
                               m_PipelineLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0,
                               192,
                               &pushConstants);

            // Bind descriptor set for this glyph texture
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

            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VertexBuffers[m_CurrentFrame], offsets);
            vkCmdDraw(commandBuffer, 6, 1, m_CurrentVertexCount, 0);
            ++m_DrawCallCount;

            m_CurrentVertexCount += 6;
            x += (glyph.advance >> 6) * scale;
        }
    };

    // Render outline first (black, in 4 cardinal directions for performance)
    glm::vec3 outlineColor(0.0f, 0.0f, 0.0f);
    float outlineOffset =
        2.0f * scale * outlineSize;  // Outline thickness (scaled by outlineSize parameter)

    static const int outlineDirections[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int dir = 0; dir < 4; dir++)
    {
        int dx = outlineDirections[dir][0];
        int dy = outlineDirections[dir][1];
        glm::vec2 offsetPos = position + glm::vec2(dx * outlineOffset, dy * outlineOffset);
        renderTextPass(offsetPos, outlineColor);
    }

    // Render main text on top
    renderTextPass(position, color);
}

void VulkanRenderer::CreateGlyphTexture(int width,
                                        int height,
                                        const std::vector<unsigned char>& rgbaData,
                                        Glyph& outGlyph)
{
    if (width <= 0 || height <= 0)
    {
        // Fallback handled by caller (white texture)
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

    // Create staging buffer
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

    // Create image view
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

void VulkanRenderer::LoadFont()
{
#ifdef USE_FREETYPE
    if (FT_Init_FreeType(&m_FreeType))
    {
        std::cerr << "ERROR::FREETYPE: Could not init FreeType Library (Vulkan)" << std::endl;
        return;
    }

    const std::vector<std::string> fontCandidates = {
        "assets/fonts/c8ab67e0-519a-49b5-b693-e8fc86d08efa.ttf",
#ifdef _WIN32
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
#endif
    };

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

            // Some glyphs (e.g., space) have zero-sized bitmaps. Reuse white texture to avoid
            // invalid images.
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
        std::cout << "Loaded font for Vulkan text: " << fontPath << " (" << m_Glyphs.size()
                  << " glyphs)" << std::endl;
        break;
    }

    if (!loaded)
    {
        std::cerr << "WARNING: No font loaded for Vulkan renderer text. Text will be skipped."
                  << std::endl;
    }

    FT_Done_FreeType(m_FreeType);
    m_FreeType = nullptr;
#else
    std::cerr << "WARNING: FreeType not available; Vulkan text rendering disabled." << std::endl;
#endif
}
