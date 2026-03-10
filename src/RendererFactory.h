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
 * Falls back to OpenGL if the requested API is unavailable.
 * Caller must call Init() before use.
 *
 * @param api The renderer API to use.
 * @param window GLFW window handle for initialization.
 * @return Unique pointer to renderer instance, or nullptr on failure.
 */
std::unique_ptr<IRenderer> CreateRenderer(RendererAPI api, GLFWwindow* window);
