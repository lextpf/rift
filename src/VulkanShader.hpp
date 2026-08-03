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
 * @par Shader pipeline
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
 * @par Shader sources
 * Two shader programs: the 2D sprite pair, and the world-space pair used by the
 * `world3d` path.
 *
 * | Shader   | File                | Purpose                                    |
 * |----------|---------------------|--------------------------------------------|
 * | Vertex   | Geometry.vert.spv   | Transform vertices, pass UVs/colors        |
 * | Fragment | Geometry.frag.spv   | Sample texture, apply tint                 |
 * | Vertex   | Geometry3D.vert.spv | View-projection transform for world quads  |
 * | Fragment | Geometry3D.frag.spv | Sample texture, ambient tint, alpha cutoff |
 *
 * A missing Geometry3D pair is non-fatal: @ref VulkanRenderer disables the world 3D
 * path with a warning and keeps the 2D path working. A missing Geometry pair is fatal.
 *
 * @par Build integration
 * SPIR-V compilation is a CMake step, not a script step: `find_program(GLSLANG_VALIDATOR
 * ...)` drives the `compile_shaders` target, which every build of `rift` depends on.
 * Every `.vert` and `.frag` file in `shaders/` is compiled with `-V -DUSE_VULKAN` to
 * `shaders/<name>.spv` in the source tree, then copied next to the executable post-build.
 *
 * @warning Validation proves only that a file is SPIR-V, never that it is current. A
 * stale `.spv` - left behind when glslangValidator is absent, or copied next to the
 * executable by an earlier build - loads silently and keeps an old shader alive. When
 * Vulkan output disagrees with the GLSL source, delete the `.spv` files in `shaders/` and
 * the copies beside the executable, then rebuild.
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
     * @return VkShaderModule handle. Never VK_NULL_HANDLE - failure throws.
     * @note Throws std::runtime_error if @p code is empty or module creation
     *       fails, so this cannot be called from a `noexcept` context.
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
     * @brief Load any pre-compiled SPIR-V blob by its path relative to the
     *        shader search roots.
     *
     * The general form of @ref GetVertexShaderSPIRV / @ref GetFragmentShaderSPIRV,
     * exposed so additional shader pairs (the world-space `Geometry3D` program)
     * can be loaded without a bespoke accessor each. Same search order and
     * validation; same "empty vector, never throws" failure contract.
     *
     * @param relativePath e.g. `"shaders/Geometry3D.vert.spv"`.
     * @return SPIR-V bytecode, or empty vector if not found or invalid.
     */
    static std::vector<uint32_t> LoadSPIRV(const std::string& relativePath);

    /**
     * @brief Load pre-compiled vertex shader SPIR-V from file.
     *
     * Searches for `shaders/Geometry.vert.spv` relative to the current working
     * directory, the executable directory, and the executable's parent and
     * grandparent directories (which is what makes a `build/<Config>/` launch
     * find the source-tree shaders). The first candidate that yields a valid
     * blob wins. Rejects empty files, files over 16 MiB, files whose byte size
     * is not 4-byte aligned, and files whose first word is not the SPIR-V magic
     * number 0x07230203 - so a text `.vert` renamed to `.spv` is caught here
     * rather than by the driver.
     *
     * Failure is reported by return value (and an error log listing every path
     * tried), never by an exception.
     *
     * @return SPIR-V bytecode, or empty vector if file is not found or invalid.
     */
    static std::vector<uint32_t> GetVertexShaderSPIRV();

    /**
     * @brief Load pre-compiled fragment shader SPIR-V from file.
     *
     * Same search order and validation as @ref GetVertexShaderSPIRV, applied to
     * `shaders/Geometry.frag.spv`.
     *
     * @return SPIR-V bytecode, or empty vector if file is not found or invalid.
     */
    static std::vector<uint32_t> GetFragmentShaderSPIRV();
};
