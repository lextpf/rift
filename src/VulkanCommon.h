#pragma once

#include <vulkan/vulkan.h>
#include <iostream>
#include <stdexcept>

#define VK_CHECK(x)                                                                           \
    do                                                                                        \
    {                                                                                         \
        VkResult result = x;                                                                  \
        if (result != VK_SUCCESS)                                                             \
        {                                                                                     \
            std::cerr << "Vulkan error at " << __FILE__ << ":" << __LINE__ << " - " << result \
                      << std::endl;                                                           \
            throw std::runtime_error("Vulkan operation failed");                              \
        }                                                                                     \
    } while (0)
