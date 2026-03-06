#include "VulkanShader.h"

#include <iostream>
#include <fstream>
#include <limits>

VkShaderModule VulkanShader::CreateShaderModule(VkDevice device, const std::vector<uint32_t> &code)
{
    if (code.empty())
    {
        throw std::runtime_error("Cannot create shader module from empty code!");
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module!");
    }

    return shaderModule;
}

// Load SPIR-V file
static std::vector<uint32_t> ReadSPIRVFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        std::cerr << "Failed to open SPIR-V file: " << filename << std::endl;
        return {};
    }

    const std::streamoff streamSize = file.tellg();
    if (streamSize <= 0)
    {
        std::cerr << "Invalid SPIR-V file size: " << filename << std::endl;
        return {};
    }
    if ((streamSize % static_cast<std::streamoff>(sizeof(uint32_t))) != 0)
    {
        std::cerr << "SPIR-V file size is not 4-byte aligned: " << filename << std::endl;
        return {};
    }
    if (streamSize > static_cast<std::streamoff>(std::numeric_limits<size_t>::max()))
    {
        std::cerr << "SPIR-V file is too large: " << filename << std::endl;
        return {};
    }

    const size_t wordCount = static_cast<size_t>(streamSize / static_cast<std::streamoff>(sizeof(uint32_t)));
    std::vector<uint32_t> buffer(wordCount);

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), streamSize);
    if (!file)
    {
        std::cerr << "Failed to read full SPIR-V file: " << filename << std::endl;
        return {};
    }
    file.close();

    return buffer;
}

std::vector<uint32_t> VulkanShader::GetVertexShaderSPIRV()
{
    // Try to load compiled SPIR-V file
    std::vector<uint32_t> code = ReadSPIRVFile("shaders/sprite.vert.spv");
    if (code.empty())
    {
        std::cerr << "Warning: Could not load shaders/sprite.vert.spv" << std::endl;
        std::cerr << "Please compile shaders/sprite.vert to SPIR-V using:" << std::endl;
        std::cerr << "  glslangValidator -V shaders/sprite.vert -o shaders/sprite.vert.spv" << std::endl;
    }
    return code;
}

std::vector<uint32_t> VulkanShader::GetFragmentShaderSPIRV()
{
    // Try to load compiled SPIR-V file
    std::vector<uint32_t> code = ReadSPIRVFile("shaders/sprite.frag.spv");
    if (code.empty())
    {
        std::cerr << "Warning: Could not load shaders/sprite.frag.spv" << std::endl;
        std::cerr << "Please compile shaders/sprite.frag to SPIR-V using:" << std::endl;
        std::cerr << "  glslangValidator -V shaders/sprite.frag -o shaders/sprite.frag.spv" << std::endl;
    }
    return code;
}
