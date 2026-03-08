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
 * // glslangValidator -V sprite.vert -o sprite.vert.spv
 * // glslangValidator -V sprite.frag -o sprite.frag.spv
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
 * | Vertex   | sprite.vert.spv  | Transform vertices, pass UVs/colors  |
 * | Fragment | sprite.frag.spv  | Sample texture, apply tint           |
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
     * Loads `shaders/sprite.vert.spv` from the working directory.
     *
     * @return SPIR-V bytecode, or empty vector if file not found.
     */
    static std::vector<uint32_t> GetVertexShaderSPIRV();

    /**
     * @brief Load pre-compiled fragment shader SPIR-V from file.
     *
     * Loads `shaders/sprite.frag.spv` from the working directory.
     *
     * @return SPIR-V bytecode, or empty vector if file not found.
     */
    static std::vector<uint32_t> GetFragmentShaderSPIRV();
};
