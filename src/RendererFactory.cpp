#include "RendererFactory.h"
#include "OpenGLRenderer.h"
#include "VulkanRenderer.h"

#include <iostream>
#include <memory>

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
// Falls back to OpenGL if the requested API is unavailable.
// Returns nullptr if no renderer can be created.
std::unique_ptr<IRenderer> CreateRenderer(RendererAPI api, GLFWwindow* window)
{
    std::cout << "CreateRenderer() called with API: "
              << (api == RendererAPI::OpenGL ? "OpenGL" : "Vulkan") << std::endl;
    std::cout.flush();

    if (!IsRendererAvailable(api))
    {
        std::cerr << "Requested renderer API is not available in this build!" << std::endl;
        // Fall back to OpenGL if available
        std::cerr << "Falling back to OpenGL..." << std::endl;
        api = RendererAPI::OpenGL;
    }

    switch (api)
    {
        case RendererAPI::OpenGL:
            std::cout << "Creating OpenGL renderer..." << std::endl;
            std::cout.flush();
            return std::make_unique<OpenGLRenderer>();

        case RendererAPI::Vulkan:
            std::cout << "Creating Vulkan renderer..." << std::endl;
            std::cout.flush();
            try
            {
                auto renderer = std::make_unique<VulkanRenderer>(window);
                std::cout << "Vulkan renderer created successfully" << std::endl;
                std::cout.flush();
                return renderer;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Exception creating Vulkan renderer: " << e.what() << std::endl;
                std::cerr << "Falling back to OpenGL..." << std::endl;
                std::cerr.flush();
                return std::make_unique<OpenGLRenderer>();
            }
            catch (...)
            {
                std::cerr << "Unknown exception creating Vulkan renderer" << std::endl;
                std::cerr << "Falling back to OpenGL..." << std::endl;
                std::cerr.flush();
                return std::make_unique<OpenGLRenderer>();
            }

        default:
            std::cerr << "Unknown renderer API, defaulting to OpenGL" << std::endl;
            return std::make_unique<OpenGLRenderer>();
    }
}
