#include "Texture.h"
#include "VulkanCommon.h"
#include "VulkanRenderer.h"

#include <cstring>

namespace
{
VkImageMemoryBarrier CreateColorImageBarrier(VkImage image,
                                             VkImageLayout oldLayout,
                                             VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    return barrier;
}

void CmdTransitionColorImageLayout(VkCommandBuffer commandBuffer,
                                   VkImage image,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout)
{
    VkImageMemoryBarrier barrier = CreateColorImageBarrier(image, oldLayout, newLayout);

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw std::runtime_error("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkBufferImageCopy CreateColorImageCopyRegion(uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    return region;
}
}  // namespace

void VulkanRenderer::TransitionImageLayout(VkImage image,
                                           VkFormat format,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout)
{
    (void)format;
    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];
    CmdTransitionColorImageLayout(commandBuffer, image, oldLayout, newLayout);
}

void VulkanRenderer::UploadStagingBufferToImage(VkBuffer stagingBuffer,
                                                VkImage image,
                                                uint32_t width,
                                                uint32_t height)
{
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = m_CommandPool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    // Transition to transfer destination
    CmdTransitionColorImageLayout(
        commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy staging buffer to image
    const VkBufferImageCopy region = CreateColorImageCopyRegion(width, height);
    vkCmdCopyBufferToImage(
        commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to shader read
    CmdTransitionColorImageLayout(commandBuffer,
                                  image,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer,
                                       VkImage image,
                                       uint32_t width,
                                       uint32_t height)
{
    VkCommandBuffer commandBuffer = m_CommandBuffers[m_CurrentFrame];
    const VkBufferImageCopy region = CreateColorImageCopyRegion(width, height);

    vkCmdCopyBufferToImage(
        commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanRenderer::CreateTextureSampler()
{
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

    VK_CHECK(vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_TextureSampler));
}

VulkanRenderer::TextureResources& VulkanRenderer::GetOrCreateTexture(const Texture& texture)
{
    // Use the Texture object's address as the unique cache key.
    // Each Texture object occupies a unique memory location, so this guarantees
    // no collisions between different textures (unlike a size-based hash).
    const Texture* textureKey = &texture;

    // Check if texture already exists in cache
    auto it = m_TextureCache.find(textureKey);
    if (it != m_TextureCache.end() && it->second.initialized)
    {
#ifdef USE_VULKAN
        // Refresh cached fallback entry once a real image view becomes available.
        VkImageView liveView = texture.GetVulkanImageView();
        if (liveView != VK_NULL_HANDLE && it->second.imageView != liveView)
        {
            it->second.imageView = liveView;
        }
#endif
        return it->second;
    }

    // Create new texture resources
    TextureResources resources{};
    resources.initialized = false;

    int width = texture.GetWidth();
    int height = texture.GetHeight();

    if (width <= 0 || height <= 0)
    {
        // Return white texture if invalid
        resources.image = m_WhiteTextureImage;
        resources.imageView = m_WhiteTextureImageView;
        resources.memory = m_WhiteTextureImageMemory;
        resources.initialized = true;
        m_TextureCache[textureKey] = resources;
        return m_TextureCache[textureKey];
    }

// Try to use texture's Vulkan resources if they exist
#ifdef USE_VULKAN
    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
    {
        // Texture already has Vulkan resources - use them
        resources.imageView = texImageView;
        resources.initialized = true;
        m_TextureCache[textureKey] = resources;
        return m_TextureCache[textureKey];
    }

    // Texture doesn't have Vulkan resources yet - need to create them
    // But we can't call CreateVulkanTexture from here because we don't have access to the Texture's
    // private methods So we'll return white texture for now and log a warning
    std::cerr << "Warning: Texture " << static_cast<const void*>(textureKey) << " (size " << width
              << "x" << height << ") not uploaded to Vulkan yet. Using white texture fallback."
              << std::endl;
#endif

    // Use white texture as fallback. Cached entries are refreshed above when
    // a real image view becomes available on the Texture object.
    resources.image = m_WhiteTextureImage;
    resources.imageView = m_WhiteTextureImageView;
    resources.memory = m_WhiteTextureImageMemory;
    resources.initialized = true;
    m_TextureCache[textureKey] = resources;
    return m_TextureCache[textureKey];
}
