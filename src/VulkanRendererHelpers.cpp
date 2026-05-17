#include "Logger.hpp"
#include "Texture.hpp"
#include "VulkanCommon.hpp"
#include "VulkanRenderer.hpp"

#include <cstring>

namespace
{
constexpr const char* LOG_SUBSYSTEM = "Render";
}  // namespace

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

    CmdTransitionColorImageLayout(
        commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    const VkBufferImageCopy region = CreateColorImageCopyRegion(width, height);
    vkCmdCopyBufferToImage(
        commandBuffer, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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
    // Texture object's address as the cache key - each Texture has a unique
    // memory location, so collisions can't happen (unlike a size-based hash).
    const Texture* textureKey = &texture;

    auto it = m_TextureCache.find(textureKey);
    if (it != m_TextureCache.end() && it->second.initialized)
    {
        // Upgrade cached fallback once the real image view appears.
        VkImageView liveView = texture.GetVulkanImageView();
        if (liveView != VK_NULL_HANDLE && it->second.imageView != liveView)
        {
            it->second.imageView = liveView;
        }
        return it->second;
    }

    TextureResources resources{};
    resources.initialized = false;

    int width = texture.GetWidth();
    int height = texture.GetHeight();

    if (width <= 0 || height <= 0)
    {
        // Invalid size - fall back to white.
        resources.image = m_WhiteTextureImage;
        resources.imageView = m_WhiteTextureImageView;
        resources.memory = m_WhiteTextureImageMemory;
        resources.initialized = true;
        m_TextureCache[textureKey] = resources;
        return m_TextureCache[textureKey];
    }

    VkImageView texImageView = texture.GetVulkanImageView();
    if (texImageView != VK_NULL_HANDLE)
    {
        // Texture already has Vulkan resources.
        resources.imageView = texImageView;
        resources.initialized = true;
        m_TextureCache[textureKey] = resources;
        return m_TextureCache[textureKey];
    }

    // Texture has no Vulkan resources yet. We can't call CreateVulkanTexture
    // from here (no access to the Texture's private methods), so log and
    // fall back to white; the cache entry is upgraded above when the real
    // image view appears.
    Logger::WarnF(LOG_SUBSYSTEM,
                  "Texture {} (size {}x{}) not uploaded to Vulkan yet. Using white texture "
                  "fallback.",
                  static_cast<const void*>(textureKey),
                  width,
                  height);

    resources.image = m_WhiteTextureImage;
    resources.imageView = m_WhiteTextureImageView;
    resources.memory = m_WhiteTextureImageMemory;
    resources.initialized = true;
    m_TextureCache[textureKey] = resources;
    return m_TextureCache[textureKey];
}
