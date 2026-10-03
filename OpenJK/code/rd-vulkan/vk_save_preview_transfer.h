#pragma once
#include <vulkan/vulkan.h>
#include <utility>
#include <algorithm>

// Caller owns the acquired image and waits for submission before reading or
// releasing it. Restore COLOR_ATTACHMENT_OPTIMAL as required by OpenXR.
inline void VK_RecordSavePreviewCopy(VkCommandBuffer cmd, VkImage image, VkBuffer buffer,
                                    uint32_t width, uint32_t height,
                                    VkImage thumbnail = VK_NULL_HANDLE,
                                    uint32_t thumbWidth = 512, uint32_t thumbHeight = 512)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkImage copyImage = image;
    if (thumbnail != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier target = barrier;
        target.image = thumbnail;
        target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        target.srcAccessMask = 0;
        target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &target);
        // The legacy 512-square JPEG is displayed in a 4:3 menu rectangle.
        const int cropWidth = std::min(width, height*4/3);
        const int cropHeight = std::min(height, width*3/4);
        const int left = (width-cropWidth)/2, top = (height-cropHeight)/2;
        VkImageBlit blit = {};
        blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
        blit.srcOffsets[0] = {left, top, 0};
        blit.srcOffsets[1] = {left+cropWidth, top+cropHeight, 1};
        blit.dstOffsets[1] = {int(thumbWidth), int(thumbHeight), 1};
        vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            thumbnail, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        target.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        target.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        target.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &target);
        copyImage = thumbnail;
        width = thumbWidth;
        height = thumbHeight;
    }
    VkBufferImageCopy copy = {};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(cmd, copyImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkBufferMemoryBarrier host = {};
    host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = buffer;
    host.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0, 0, nullptr, 1, &host, 1, &barrier);
}
