#pragma once

#include "IRenderer.hpp"
#include "RendererAPI.hpp"

#include <memory>

struct GLFWwindow;

/**
 * @brief Runtime selection and construction of the rendering backend.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Free functions rather than a factory class: there is no state to keep between
 * calls, and Game reaches them once at startup and once per `renderer.set`.
 */

/**
 * @brief Reports whether a renderer API was compiled into this build.
 * @ingroup Rendering
 *
 * Vulkan is a hard build dependency and OpenGL is always linked, so this
 * currently returns true for every enumerator of @ref RendererAPI. It is kept
 * as the single place a future conditionally-compiled backend would report
 * itself absent - do not read a `false` return as a reachable case today.
 *
 * @param api The renderer API to check.
 * @return true if available, false otherwise.
 */
bool IsRendererAvailable(RendererAPI api);

/**
 * @brief Creates a renderer instance for the requested API.
 * @ingroup Rendering
 *
 * Falls back to OpenGL if Vulkan construction throws; on fallback, @p api is
 * updated in-place so the caller can key context setup off the actual backend
 * that was created. The "requested API unavailable" fallback is unreachable
 * while @ref IsRendererAvailable always returns true.
 *
 * The returned renderer is not initialized - the caller must call Init() and
 * honour its false return before drawing with it.
 *
 * @param[in,out] api The renderer API to use; rewritten to RendererAPI::OpenGL
 *                    if a fallback occurred, otherwise left unchanged.
 * @param window GLFW window handle, forwarded to the VulkanRenderer constructor
 *               (unused by the OpenGL backend, which binds the current context).
 * @return Unique pointer to renderer instance. Never null in practice: every
 *         failure path substitutes an OpenGLRenderer.
 */
std::unique_ptr<IRenderer> CreateRenderer(RendererAPI& api, GLFWwindow* window);
