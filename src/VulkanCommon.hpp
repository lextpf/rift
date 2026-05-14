#pragma once

#include "Logger.hpp"

#include <vulkan/vulkan.h>
#include <stdexcept>
#include <string>

/**
 * @brief Check a VkResult and throw std::runtime_error on failure.
 * @ingroup Rendering
 *
 * Logs the file, line, and VkResult code via Logger before throwing. The
 * caller is expected to declare a `LOG_SUBSYSTEM` constant in scope.
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
