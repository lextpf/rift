#pragma once

#include "Logger.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>

/**
 * @brief Check a VkResult and throw std::runtime_error on failure.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * Logs the file, line, and VkResult code via Logger before throwing. The
 * caller is expected to declare a `LOG_SUBSYSTEM` constant in scope.
 *
 * @warning This macro **throws**, so it must not be used in a destructor or any
 * other `noexcept` context: an escaping exception there calls std::terminate
 * rather than unwinding. That is why `VulkanRenderer::Shutdown`, which the
 * destructor calls, inspects the `vkDeviceWaitIdle` result by hand and only logs a
 * warning; the rest of teardown uses void-returning `vkDestroy*` calls. It is
 * equally unusable inside a GLFW C callback, where throwing across the C frame is
 * undefined behavior.
 *
 * @note @p x is evaluated exactly once, and its source text is stringified into
 * both the log line and the exception message. The macro declares a block-scoped
 * `VkResult result`, which does not leak into the caller's scope but does shadow an
 * outer `result` inside the wrapped expression, where it is still uninitialized. Do
 * not reference a variable named `result` inside a VK_CHECK argument.
 *
 * @param x Vulkan API call expression that returns VkResult.
 */
#define VK_CHECK(x)                                                             \
    do                                                                          \
    {                                                                           \
        VkResult result = x;                                                    \
        if (result != VK_SUCCESS)                                               \
        {                                                                       \
            Logger::ErrorF(LOG_SUBSYSTEM,                                       \
                           "Vulkan error: " #x " failed at {}:{} - {}",         \
                           __FILE__,                                            \
                           __LINE__,                                            \
                           static_cast<int>(result));                           \
            throw std::runtime_error("Vulkan error: " #x " failed (VkResult " + \
                                     std::to_string(result) + ")");             \
        }                                                                       \
    } while (0)
