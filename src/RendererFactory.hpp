#pragma once

#include "IRenderer.h"
#include "RendererAPI.h"

#include <memory>

struct GLFWwindow;

/**
 * @brief Checks if a renderer API was compiled into this build.
 * @ingroup Rendering
 *
 * @param api The renderer API to check.
 * @return true if available, false otherwise.
 */
bool IsRendererAvailable(RendererAPI api);

/**
 * @brief Creates a renderer instance for the requested API.
 * @ingroup Rendering
 *
 * Falls back to OpenGL if the requested API is unavailable or if Vulkan
 * construction throws; on fallback, @p api is updated in-place so the
 * caller can key context setup off the actual backend that was created.
 * Caller must call Init() before use.
 *
 * @param[in,out] api The renderer API to use; rewritten to the actual API
 *                    that was constructed if a fallback occurred.
 * @param window GLFW window handle for initialization.
 * @return Unique pointer to renderer instance, or nullptr on failure.
 */
std::unique_ptr<IRenderer> CreateRenderer(RendererAPI& api, GLFWwindow* window);
