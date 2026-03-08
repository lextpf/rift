#include "VulkanShader.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

VkShaderModule VulkanShader::CreateShaderModule(VkDevice device, const std::vector<uint32_t>& code)
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

namespace
{
std::filesystem::path GetExecutableDirectory()
{
#ifdef _WIN32
    std::array<char, 4096> exePath{};
    DWORD len = GetModuleFileNameA(nullptr, exePath.data(), static_cast<DWORD>(exePath.size()));
    if (len == 0 || len >= exePath.size())
    {
        return {};
    }
    return std::filesystem::path(std::string(exePath.data(), len)).parent_path();
#else
    return {};
#endif
}

std::vector<std::filesystem::path> BuildShaderSearchPaths(const std::string& filename)
{
    std::vector<std::filesystem::path> paths;
    auto addUnique = [&paths](const std::filesystem::path& path)
    {
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    };

    const std::filesystem::path relPath(filename);
    addUnique(relPath);

    const std::filesystem::path exeDir = GetExecutableDirectory();
    if (!exeDir.empty())
    {
        // Typical runtime layout: <exe-dir>/shaders/*.spv
        addUnique(exeDir / relPath);
        // When launched from source root with exe in build/<Config>.
        addUnique(exeDir.parent_path() / relPath);
        addUnique(exeDir.parent_path().parent_path() / relPath);
    }

    return paths;
}

std::vector<uint32_t> ReadSPIRVFromPath(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        return {};
    }

    const std::streamoff streamSize = file.tellg();
    if (streamSize == static_cast<std::streamoff>(-1))
    {
        std::cerr << "Failed to read SPIR-V file size: " << path.string() << std::endl;
        return {};
    }
    if (streamSize <= 0)
    {
        std::cerr << "Invalid SPIR-V file size: " << path.string() << std::endl;
        return {};
    }
    if ((streamSize % static_cast<std::streamoff>(sizeof(uint32_t))) != 0)
    {
        std::cerr << "SPIR-V file size is not 4-byte aligned: " << path.string() << std::endl;
        return {};
    }
    // Sprite shaders should be tiny; very large files usually indicate a wrong file/path.
    static constexpr std::streamoff kMaxSPIRVBytes = 16 * 1024 * 1024;
    if (streamSize > kMaxSPIRVBytes)
    {
        std::cerr << "SPIR-V file is too large: " << path.string() << std::endl;
        return {};
    }
    const size_t wordCount =
        static_cast<size_t>(streamSize / static_cast<std::streamoff>(sizeof(uint32_t)));
    std::vector<uint32_t> buffer(wordCount);

    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), streamSize);
    if (!file)
    {
        std::cerr << "Failed to read full SPIR-V file: " << path.string() << std::endl;
        return {};
    }
    file.close();

    static constexpr uint32_t kSpirvMagic = 0x07230203u;
    if (buffer.empty() || buffer[0] != kSpirvMagic)
    {
        std::cerr << "Invalid SPIR-V magic in file: " << path.string() << std::endl;
        return {};
    }

    return buffer;
}

// Load SPIR-V file from common runtime locations.
static std::vector<uint32_t> ReadSPIRVFile(const std::string& filename)
{
    std::vector<std::filesystem::path> attemptedPaths;
    for (const auto& candidate : BuildShaderSearchPaths(filename))
    {
        attemptedPaths.push_back(candidate);
        std::vector<uint32_t> code = ReadSPIRVFromPath(candidate);
        if (!code.empty())
        {
            return code;
        }
    }

    std::cerr << "Failed to load SPIR-V file: " << filename << std::endl;
    std::cerr << "Checked paths:" << std::endl;
    for (const auto& path : attemptedPaths)
    {
        std::cerr << "  - " << path.string() << std::endl;
    }
    return {};
}
}  // namespace

std::vector<uint32_t> VulkanShader::GetVertexShaderSPIRV()
{
    // Try to load compiled SPIR-V file
    std::vector<uint32_t> code = ReadSPIRVFile("shaders/sprite.vert.spv");
    if (code.empty())
    {
        std::cerr << "Warning: Could not load shaders/sprite.vert.spv" << std::endl;
        std::cerr << "Please compile shaders/sprite.vert to SPIR-V using:" << std::endl;
        std::cerr << "  glslangValidator -V shaders/sprite.vert -o shaders/sprite.vert.spv"
                  << std::endl;
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
        std::cerr << "  glslangValidator -V shaders/sprite.frag -o shaders/sprite.frag.spv"
                  << std::endl;
    }
    return code;
}
