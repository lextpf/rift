#pragma once

#include <vulkan/vulkan.h>
#include <iostream>
#include <stdexcept>
#include <string>

/**
 * @brief Check a VkResult and throw std::runtime_error on failure.
 *
 * Logs the file, line, and VkResult code to stderr before throwing.
 *
 * @param x Vulkan API call expression that returns VkResult.
 */
#define VK_CHECK(x)                                                                                \
    do                                                                                             \
    {                                                                                              \
        VkResult result = x;                                                                       \
        if (result != VK_SUCCESS)                                                                  \
        {                                                                                          \
            std::cerr << "Vulkan error: " #x " failed at " << __FILE__ << ":" << __LINE__ << " - " \
                      << result << std::endl;                                                      \
            throw std::runtime_error("Vulkan error: " #x " failed (VkResult " +                    \
                                     std::to_string(result) + ")");                                \
        }                                                                                          \
    } while (0)
