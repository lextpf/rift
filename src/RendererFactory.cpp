#include "RendererFactory.h"
#include "Logger.h"
#include "OpenGLRenderer.h"
#include "VulkanRenderer.h"

#include <memory>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Render";
}  // namespace

// Checks if a renderer API was compiled into this build.
bool IsRendererAvailable(RendererAPI api)
{
    switch (api)
    {
        case RendererAPI::OpenGL:
            return true;

        case RendererAPI::Vulkan:
            return true;

        default:
            return false;
    }
}

// Creates a renderer instance for the requested API.
// Falls back to OpenGL if the requested API is unavailable; rewrites @p api
// to the actual backend that was constructed so callers can branch on it.
// Returns nullptr if no renderer can be created.
std::unique_ptr<IRenderer> CreateRenderer(RendererAPI& api, GLFWwindow* window)
{
    Logger::InfoF(LOG_SUBSYSTEM,
                  "CreateRenderer() called with API: {}",
                  api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan");

    if (!IsRendererAvailable(api))
    {
        Logger::Warn(LOG_SUBSYSTEM, "Requested renderer API is not available in this build!");
        Logger::Warn(LOG_SUBSYSTEM, "Falling back to OpenGL...");
        api = RendererAPI::OpenGL;
    }

    switch (api)
    {
        case RendererAPI::OpenGL:
            Logger::Info(LOG_SUBSYSTEM, "Creating OpenGL renderer...");
            return std::make_unique<OpenGLRenderer>();

        case RendererAPI::Vulkan:
            Logger::Info(LOG_SUBSYSTEM, "Creating Vulkan renderer...");
            try
            {
                auto renderer = std::make_unique<VulkanRenderer>(window);
                Logger::Info(LOG_SUBSYSTEM, "Vulkan renderer created successfully");
                return renderer;
            }
            catch (const std::exception& e)
            {
                Logger::ErrorF(LOG_SUBSYSTEM, "Exception creating Vulkan renderer: {}", e.what());
                Logger::Warn(LOG_SUBSYSTEM, "Falling back to OpenGL...");
                api = RendererAPI::OpenGL;
                return std::make_unique<OpenGLRenderer>();
            }
            catch (...)
            {
                Logger::Error(LOG_SUBSYSTEM, "Unknown exception creating Vulkan renderer");
                Logger::Warn(LOG_SUBSYSTEM, "Falling back to OpenGL...");
                api = RendererAPI::OpenGL;
                return std::make_unique<OpenGLRenderer>();
            }

        default:
            Logger::Error(LOG_SUBSYSTEM, "Unknown renderer API, defaulting to OpenGL");
            api = RendererAPI::OpenGL;
            return std::make_unique<OpenGLRenderer>();
    }
}
