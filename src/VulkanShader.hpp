#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>

/**
 * @class VulkanShader
 * @brief Utility class for Vulkan shader module creation and SPIR-V loading.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Rendering
 *
 * VulkanShader provides static methods for creating Vulkan shader modules
 * from pre-compiled SPIR-V bytecode. The engine uses pre-compiled shaders
 * loaded from .spv files at runtime.
 *
 * @par Shader Pipeline
 * The Vulkan renderer requires shaders in SPIR-V format:
 * @code
 * // Shaders are pre-compiled during build:
 * // glslangValidator -V Geometry.vert -o Geometry.vert.spv
 * // glslangValidator -V Geometry.frag -o Geometry.frag.spv
 *
 * // At runtime, load and create modules:
 * auto vertSPV = VulkanShader::GetVertexShaderSPIRV();
 * auto fragSPV = VulkanShader::GetFragmentShaderSPIRV();
 * VkShaderModule vertModule = VulkanShader::CreateShaderModule(device, vertSPV);
 * VkShaderModule fragModule = VulkanShader::CreateShaderModule(device, fragSPV);
 * @endcode
 *
 * @par Shader Sources
 * The engine uses two main shaders:
 *
 * | Shader   | File             | Purpose                              |
 * |----------|------------------|--------------------------------------|
 * | Vertex   | Geometry.vert.spv  | Transform vertices, pass UVs/colors  |
 * | Fragment | Geometry.frag.spv  | Sample texture, apply tint           |
 *
 * @par Build Integration
 * SPIR-V compilation is handled by the build script:
 * - `build.bat` automatically compiles shaders if glslangValidator is available
 * - Compiled .spv files are copied to the build output directory
 *
 * @see VulkanRenderer For shader module usage in the graphics pipeline.
 * @see IRenderer For the renderer interface these shaders implement.
 */
class VulkanShader
{
public:
    /**
     * @brief Create a Vulkan shader module from SPIR-V bytecode.
     *
     * Wraps SPIR-V bytecode in a VkShaderModule for use in a graphics pipeline.
     * The caller is responsible for destroying the module with vkDestroyShaderModule().
     *
     * @param device Vulkan logical device handle.
     * @param code   SPIR-V bytecode as 32-bit words.
     * @return VkShaderModule handle.
     * @throws std::runtime_error If code is empty or module creation fails.
     *
     * @par Example
     * @code
     * auto spirv = VulkanShader::GetVertexShaderSPIRV();
     * VkShaderModule module = VulkanShader::CreateShaderModule(device, spirv);
     * // Use module in VkPipelineShaderStageCreateInfo...
     * vkDestroyShaderModule(device, module, nullptr);
     * @endcode
     */
    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code);

    /**
     * @brief Load pre-compiled vertex shader SPIR-V from file.
     *
     * Searches for
     * `shaders/Geometry.vert.spv` relative to the current working
     * directory, the executable
     * directory, and parent directories used by
     * build-tree launches. Rejects empty files,
     * files over 16 MiB, and files
     * whose byte size is not 4-byte aligned.
     *
     *
     * @return SPIR-V bytecode, or empty vector if file is not found or invalid.
     */
    static std::vector<uint32_t> GetVertexShaderSPIRV();

    /**
     * @brief Load pre-compiled fragment shader SPIR-V from file.
     *
     * Searches for
     * `shaders/Geometry.frag.spv` relative to the current working
     * directory, the executable
     * directory, and parent directories used by
     * build-tree launches. Rejects empty files,
     * files over 16 MiB, and files
     * whose byte size is not 4-byte aligned.
     *
     *
     * @return SPIR-V bytecode, or empty vector if file is not found or invalid.
     */
    static std::vector<uint32_t> GetFragmentShaderSPIRV();
};
