#include "vkobjects.h"

// --- Barrier ---

Barrier::Barrier(VkCommandBuffer cmd) : commandBuffer(cmd), hasBuffer(false), hasImage(false) {
    bufBarrier = {};
    bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imgBarrier = {};
    imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imgBarrier.subresourceRange.baseMipLevel = 0;
    imgBarrier.subresourceRange.levelCount = 1;
    imgBarrier.subresourceRange.baseArrayLayer = 0;
    imgBarrier.subresourceRange.layerCount = 1;
}

Barrier & Barrier::buffer(VkBuffer buf) {
    hasBuffer = true;
    bufBarrier.buffer = buf;
    bufBarrier.offset = 0;
    bufBarrier.size = VK_WHOLE_SIZE;
    return *this;
}

Barrier & Barrier::image(VkImage img, uint32_t mipLevels) {
    hasImage = true;
    imgBarrier.image = img;
    imgBarrier.subresourceRange.levelCount = mipLevels;
    return *this;
}

Barrier & Barrier::from(Stage stage, Access access) {
    if (hasBuffer) {
        bufBarrier.srcStageMask = static_cast<VkPipelineStageFlags2>(stage);
        bufBarrier.srcAccessMask = static_cast<VkAccessFlags2>(access);
    }
    if (hasImage) {
        imgBarrier.srcStageMask = static_cast<VkPipelineStageFlags2>(stage);
        imgBarrier.srcAccessMask = static_cast<VkAccessFlags2>(access);
    }
    return *this;
}

Barrier & Barrier::from(Stage stage, Access access, Layout layout) {
    from(stage, access);
    if (hasImage) imgBarrier.oldLayout = static_cast<VkImageLayout>(layout);
    return *this;
}

Barrier & Barrier::to(Stage stage, Access access) {
    if (hasBuffer) {
        bufBarrier.dstStageMask = static_cast<VkPipelineStageFlags2>(stage);
        bufBarrier.dstAccessMask = static_cast<VkAccessFlags2>(access);
    }
    if (hasImage) {
        imgBarrier.dstStageMask = static_cast<VkPipelineStageFlags2>(stage);
        imgBarrier.dstAccessMask = static_cast<VkAccessFlags2>(access);
    }
    return *this;
}

Barrier & Barrier::to(Stage stage, Access access, Layout layout) {
    to(stage, access);
    if (hasImage) imgBarrier.newLayout = static_cast<VkImageLayout>(layout);
    return *this;
}

Barrier & Barrier::aspectMask(VkImageAspectFlags aspects) {
    imgBarrier.subresourceRange.aspectMask = aspects;
    return *this;
}

void Barrier::record() {
    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    if (hasBuffer) {
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &bufBarrier;
    }
    if (hasImage) {
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imgBarrier;
    }
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
}
