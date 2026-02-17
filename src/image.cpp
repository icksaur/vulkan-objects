#include "vkinternal.h"

// --- Image helpers ---

void recordMipmapGeneration(VkCommandBuffer commandBuffer, VkImage image, int width, int height, size_t mipLevelCount) {
    VkImageBlit blit{};
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    int mipWidth = width;
    int mipHeight = height;

    for (size_t i = 1; i < mipLevelCount; i++) {
        // Transition current mip to transfer dst
        VkImageMemoryBarrier2 undefinedToWrite = {};
        undefinedToWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        undefinedToWrite.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        undefinedToWrite.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        undefinedToWrite.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        undefinedToWrite.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        undefinedToWrite.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        undefinedToWrite.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        undefinedToWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        undefinedToWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        undefinedToWrite.image = image;
        undefinedToWrite.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, 1, 0, 1};

        // Transition previous mip to transfer src
        VkImageMemoryBarrier2 writeToRead = {};
        writeToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        writeToRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        writeToRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        writeToRead.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        writeToRead.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        writeToRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        writeToRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        writeToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        writeToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        writeToRead.image = image;
        writeToRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 1, 0, 1};

        VkImageMemoryBarrier2 barriers[2] = {undefinedToWrite, writeToRead};
        VkDependencyInfo depInfo = {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 2;
        depInfo.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(commandBuffer, &depInfo);

        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.mipLevel = i - 1;
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.mipLevel = i;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Transition previous mip to shader read
        VkImageMemoryBarrier2 readToSample = {};
        readToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        readToSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readToSample.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        readToSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        readToSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        readToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        readToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readToSample.image = image;
        readToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 1, 0, 1};

        VkDependencyInfo readDep = {};
        readDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        readDep.imageMemoryBarrierCount = 1;
        readDep.pImageMemoryBarriers = &readToSample;
        vkCmdPipelineBarrier2(commandBuffer, &readDep);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition final mip to shader read
    VkImageMemoryBarrier2 writeToSample = {};
    writeToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    writeToSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    writeToSample.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    writeToSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    writeToSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    writeToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    writeToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    writeToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToSample.image = image;
    writeToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(mipLevelCount - 1), 1, 0, 1};

    VkDependencyInfo finalDep = {};
    finalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    finalDep.imageMemoryBarrierCount = 1;
    finalDep.pImageMemoryBarriers = &writeToSample;
    vkCmdPipelineBarrier2(commandBuffer, &finalDep);
}

void recordCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags imageAspects, size_t mipLevelCount) {
    VkImageView textureImageView;
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = imageAspects;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image views");
    }
    return textureImageView;
}

// --- Image ---

ImageBuilder::ImageBuilder() : buildMipmaps(true), bytes(nullptr), stagingBuffer(nullptr), isDepthBuffer(false), sampleBits(VK_SAMPLE_COUNT_1_BIT), usage(0) {}
ImageBuilder & ImageBuilder::createMipmaps(bool buildMipmaps) { this->buildMipmaps = buildMipmaps; return *this; }
ImageBuilder & ImageBuilder::depth() {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = depthFormat;
    isDepthBuffer = true;
    return *this;
}
ImageBuilder & ImageBuilder::fromStagingBuffer(Buffer & stagingBuffer, int width, int height, VkFormat format) {
    this->bytes = nullptr;
    this->stagingBuffer = &stagingBuffer;
    extent.width = width; extent.height = height;
    this->format = format;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::color() {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = g_context().colorFormat;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::withFormat(VkFormat format) {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = format;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::multisample() {
    sampleBits = getSampleBits(g_context().options.multisampleCount);
    return *this;
}
ImageBuilder & ImageBuilder::storage() {
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    return *this;
}

Image::Image(Image && other) : image(other.image), memory(other.memory), sampler(other.sampler), rid_(other.rid_), isStorageImage(other.isStorageImage), imageView(other.imageView) {
    other.image = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.imageView = VK_NULL_HANDLE;
    other.sampler = VK_NULL_HANDLE;
    other.rid_ = UINT32_MAX;
}

Image::Image(ImageBuilder & builder, Commands & commands) : sampler(VK_NULL_HANDLE), rid_(UINT32_MAX), isStorageImage(false) {
    VkCommandBuffer commandBuffer = commands.commandBuffer;
    VkImageFormatProperties formatProps;

    VkImageUsageFlags usageFlags = builder.usage;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (0 == (usageFlags & VK_IMAGE_USAGE_STORAGE_BIT)) {
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (builder.isDepthBuffer) {
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    VkResult result = vkGetPhysicalDeviceImageFormatProperties(
        g_context().physicalDevice, builder.format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usageFlags, 0, &formatProps);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to get image format properties " + std::to_string(result));
    }

    size_t mipLevels;
    if (builder.buildMipmaps) {
        size_t maxMipLevels = std::floor(std::log2(std::max(builder.extent.width, builder.extent.height))) + 1;
        mipLevels = std::min(maxMipLevels, static_cast<size_t>(formatProps.maxMipLevels));
    } else {
        mipLevels = 1;
    }

    VkExtent3D extent = {
        std::min(builder.extent.width, formatProps.maxExtent.width),
        std::min(builder.extent.height, formatProps.maxExtent.height),
        1
    };

    if (!(formatProps.sampleCounts & builder.sampleBits)) {
        throw std::runtime_error("requested sample count not supported");
    }

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = builder.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usageFlags;
    imageInfo.samples = builder.sampleBits;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(g_context().device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan image");
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(g_context().device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(g_context().physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (VK_SUCCESS != vkAllocateMemory(g_context().device, &allocateInfo, nullptr, &memory)) {
        throw std::runtime_error("failed to allocate image memory");
    }
    if (VK_SUCCESS != vkBindImageMemory(g_context().device, image, memory, 0)) {
        throw std::runtime_error("failed to bind memory to image");
    }

    // Layout transitions using sync2
    if (builder.isDepthBuffer) {
        Barrier(commandBuffer).image(image)
            .from(Stage::None, Access::None, Layout::Undefined)
            .to(Stage::EarlyFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
            .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            .record();
    } else {
        Barrier(commandBuffer).image(image)
            .from(Stage::None, Access::None, Layout::Undefined)
            .to(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
            .record();
    }

    if (builder.stagingBuffer != nullptr) {
        recordCopyBufferToImage(commandBuffer, *(builder.stagingBuffer), image, builder.extent.width, builder.extent.height);
    }

    if (builder.buildMipmaps) {
        recordMipmapGeneration(commandBuffer, image, builder.extent.width, builder.extent.height, mipLevels);
    } else if (!builder.isDepthBuffer) {
        Barrier(commandBuffer).image(image, mipLevels)
            .from(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
            .to(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
            .record();
    }

    VkImageAspectFlags aspectFlags;
    if (builder.isDepthBuffer) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    imageView = createImageView(g_context().device, image, builder.format, aspectFlags, mipLevels);

    // Register with bindless table
    isStorageImage = (builder.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    if (isStorageImage) {
        rid_ = g_context().bindlessTable.registerStorageImage(g_context().device, imageView);
    } else if (!builder.isDepthBuffer) {
        sampler = createSampler(g_context().device);
        rid_ = g_context().bindlessTable.registerSampler(g_context().device, imageView, sampler);
    }
}

uint32_t Image::rid() const { return rid_; }
Image::operator VkImage() const { return image; }

Image::~Image() {
    VulkanContext & context = g_context();
    auto & gen = context.destroyGenerations[context.frameInFlightIndex];

    if (rid_ != UINT32_MAX) {
        if (isStorageImage) {
            gen.storageImageRIDs.push_back(rid_);
        } else {
            gen.samplerRIDs.push_back(rid_);
        }
    }

    if (imageView != VK_NULL_HANDLE) gen.imageViews.push_back(imageView);
    if (sampler != VK_NULL_HANDLE) gen.samplers.push_back(sampler);
    if (memory != VK_NULL_HANDLE) gen.memories.push_back(memory);
    if (image != VK_NULL_HANDLE) gen.images.push_back(image);
}
