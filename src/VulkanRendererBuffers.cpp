#include "VulkanCommon.hpp"
#include "VulkanRenderer.hpp"

#include "Logger.hpp"
#include "PerspectiveTransform.hpp"

#include <cstring>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Render";
}  // namespace

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        // typeFilter bit i is set if memory type i is compatible with the
        // resource; also require the requested property flags
        // (e.g., HOST_VISIBLE, DEVICE_LOCAL).
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size,
                                  VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer& buffer,
                                  VkDeviceMemory& bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    VK_CHECK(vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory));
    VK_CHECK(vkBindBufferMemory(m_Device, buffer, bufferMemory, 0));
}

void VulkanRenderer::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkResetFences(m_Device, 1, &m_TransferFence));
    VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_TransferFence));
    VK_CHECK(vkWaitForFences(m_Device, 1, &m_TransferFence, VK_TRUE, UINT64_MAX));

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}

void VulkanRenderer::CreateBuffers()
{
    // One vertex buffer per frame in flight - GPU may be reading frame N's
    // data while CPU writes frame N+1. Size: 4 floats * 6 verts * 10000 quads
    // ~937 KB (typical frames use ~2000 quads).
    const uint32_t maxSprites = 10000;
    m_VertexBufferSize = sizeof(SpriteVertex) * 6 * maxSprites;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        // HOST_VISIBLE | HOST_COHERENT, persistently mapped for CPU writes each frame.
        CreateBuffer(m_VertexBufferSize,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_VertexBuffers[i],
                     m_VertexBufferMemories[i]);

        VK_CHECK(vkMapMemory(m_Device,
                             m_VertexBufferMemories[i],
                             0,
                             m_VertexBufferSize,
                             0,
                             &m_VertexBuffersMapped[i]));
    }

    // Index buffer: static, shared across frames.
    uint32_t indices[] = {0, 1, 2, 3, 4, 5};
    VkDeviceSize indexBufferSize = sizeof(indices);

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(indexBufferSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer,
                 stagingBufferMemory);

    void* data;
    VK_CHECK(vkMapMemory(m_Device, stagingBufferMemory, 0, indexBufferSize, 0, &data));
    memcpy(data, indices, (size_t)indexBufferSize);
    vkUnmapMemory(m_Device, stagingBufferMemory);

    CreateBuffer(indexBufferSize,
                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_IndexBuffer,
                 m_IndexBufferMemory);

    CopyBuffer(stagingBuffer, m_IndexBuffer, indexBufferSize);

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::CreatePerspectiveUBO()
{
    // One UBO per frame in flight, persistently mapped so we can write the
    // 48-byte block in BeginFrame without re-mapping.
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        CreateBuffer(sizeof(PerspectiveBlock),
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     m_PerspUBOBuffers[i],
                     m_PerspUBOMemories[i]);

        VK_CHECK(vkMapMemory(
            m_Device, m_PerspUBOMemories[i], 0, sizeof(PerspectiveBlock), 0, &m_PerspUBOMapped[i]));
    }

    // Allocate one descriptor set per frame from the shared pool.
    VkDescriptorSetLayout layouts[MAX_FRAMES_IN_FLIGHT];
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        layouts[i] = m_PerspDescriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_DescriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(m_Device, &allocInfo, m_PerspDescriptorSets));

    // Bind each descriptor set to its corresponding buffer (one-time hookup).
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_PerspUBOBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(PerspectiveBlock);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_PerspDescriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }
}

void VulkanRenderer::UpdatePerspectiveUBO()
{
    // Pack the current `m_Persp` into the current frame's mapped UBO. Called
    // from BeginFrame so the GPU always sees fresh state for the frame.
    if (m_PerspUBOMapped[m_CurrentFrame] == nullptr)
    {
        return;
    }

    const bool hasGlobe =
        (m_Persp.mode == ProjectionMode::Globe || m_Persp.mode == ProjectionMode::Fisheye);
    const bool hasVanishing =
        (m_Persp.mode == ProjectionMode::VanishingPoint || m_Persp.mode == ProjectionMode::Fisheye);

    PerspectiveBlock block{};
    block.flags[0] = m_Persp.enabled ? 1 : 0;
    block.flags[1] = hasGlobe ? 1 : 0;
    block.flags[2] = hasVanishing ? 1 : 0;
    block.flags[3] = 0;
    block.horizon[0] = m_Persp.horizonY;
    block.horizon[1] = m_Persp.horizonScale;
    block.horizon[2] = m_Persp.viewWidth;
    block.horizon[3] = m_Persp.viewHeight;
    block.sphere[0] =
        m_Persp.sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusXScale);
    block.sphere[1] =
        m_Persp.sphereRadius * static_cast<float>(perspectiveTransform::kGlobeRadiusYScale);
    block.sphere[2] = 0.0f;
    block.sphere[3] = 0.0f;

    std::memcpy(m_PerspUBOMapped[m_CurrentFrame], &block, sizeof(PerspectiveBlock));
}

void VulkanRenderer::CreateDescriptorPool()
{
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = DESCRIPTOR_POOL_MAX_SETS;
    // Perspective UBO: one descriptor set per frame in flight.
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = DESCRIPTOR_POOL_MAX_SETS + MAX_FRAMES_IN_FLIGHT;
    poolInfo.flags =
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // Allow freeing single sets.

    VK_CHECK(vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool));
}

void VulkanRenderer::CreateWhiteTexture()
{
    // 1x1 white texture used as a fallback for solid-color draws.
    unsigned char whitePixel[] = {255, 255, 255, 255};

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = 1;
    imageInfo.extent.height = 1;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VK_CHECK(vkCreateImage(m_Device, &imageInfo, nullptr, &m_WhiteTextureImage));

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, m_WhiteTextureImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_WhiteTextureImageMemory));
    VK_CHECK(vkBindImageMemory(m_Device, m_WhiteTextureImage, m_WhiteTextureImageMemory, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_WhiteTextureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_WhiteTextureImageView));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_WhiteTextureSampler));

    // Upload via staging buffer.
    VkDeviceSize imageSize = 4;  // 1x1 RGBA.
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer,
                 stagingBufferMemory);

    void* data;
    VK_CHECK(vkMapMemory(m_Device, stagingBufferMemory, 0, imageSize, 0, &data));
    memcpy(data, whitePixel, imageSize);
    vkUnmapMemory(m_Device, stagingBufferMemory);

    UploadStagingBufferToImage(stagingBuffer, m_WhiteTextureImage, 1, 1);

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);
}
