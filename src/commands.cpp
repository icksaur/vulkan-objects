#include "vkinternal.h"

// --- Commands ---

Commands::Commands(VkCommandBuffer cmd, bool owns) : commandBuffer(cmd), ended(false), ownsBuffer(owns), frame(nullptr) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }

    // Bind the global bindless descriptor set
    VkDescriptorSet bindlessSet = g_context().bindlessTable.set;
    VkPipelineLayout layout = g_context().bindlessTable.pipelineLayout;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &bindlessSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &bindlessSet, 0, nullptr);
}

Commands::Commands(Commands && other)
    : commandBuffer(other.commandBuffer), ended(other.ended), ownsBuffer(other.ownsBuffer), frame(other.frame) {
    other.commandBuffer = VK_NULL_HANDLE;
    other.ended = true;
    other.ownsBuffer = false;
    other.frame = nullptr;
}

Commands::~Commands() {
    if (commandBuffer != VK_NULL_HANDLE) {
        if (!ended) vkEndCommandBuffer(commandBuffer);
        if (ownsBuffer) {
            vkFreeCommandBuffers(g_context().device, g_context().commandPool, 1, &commandBuffer);
        }
    }
}

Commands Commands::oneShot() {
    VkCommandBuffer cmd = createCommandBuffer(g_context().device, g_context().commandPool);
    return Commands(cmd, true);
}

void Commands::bindCompute(VkPipeline pipeline) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}
void Commands::bindGraphics(VkPipeline pipeline) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}
void Commands::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDispatch(commandBuffer, x, y, z);
}
void Commands::dispatchIndirect(VkBuffer buffer, VkDeviceSize offset) {
    vkCmdDispatchIndirect(commandBuffer, buffer, offset);
}
void Commands::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDrawMeshTasks(commandBuffer, x, y, z);
}
void Commands::drawMeshTasksIndirect(VkBuffer buffer, uint32_t drawCount, VkDeviceSize offset, uint32_t stride) {
    vkCmdDrawMeshTasksIndirect(commandBuffer, buffer, offset, drawCount, stride);
}
void Commands::pushConstants(const void * data, uint32_t size) {
    vkCmdPushConstants(commandBuffer, g_context().bindlessTable.pipelineLayout, VK_SHADER_STAGE_ALL, 0, size, data);
}

void Commands::beginRendering(VkImageView depthImage) {
    if (!frame) throw std::runtime_error("beginRendering requires a frame-bound Commands (use frame.beginCommands())");

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = frame->swapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearColor = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };
    colorAttachment.clearValue = clearColor;

    VkRenderingAttachmentInfo depthAttachmentInfo = {};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depthImage;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearDepth = { .depthStencil = { 1.0f, 0 } };
    depthAttachmentInfo.clearValue = clearDepth;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    renderingInfo.layerCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::beginRendering(VkImageView colorImage, VkImageView depthImage, VkExtent2D extent) {
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorImage;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearColor = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };
    colorAttachment.clearValue = clearColor;

    VkRenderingAttachmentInfo depthAttachmentInfo = {};
    depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachmentInfo.imageView = depthImage;
    depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearDepth = { .depthStencil = { 1.0f, 0 } };
    depthAttachmentInfo.clearValue = clearDepth;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
    renderingInfo.layerCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::endRendering() {
    vkEndRendering(commandBuffer);
}

void Commands::setViewport(float x, float y, float width, float height) {
    VkViewport viewport = {};
    viewport.x = x; viewport.y = y;
    viewport.width = width; viewport.height = height;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}

void Commands::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor = {};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void Commands::bufferBarrier(VkBuffer buf, Stage srcStage, Stage dstStage) {
    Barrier(commandBuffer).buffer(buf)
        .from(srcStage, Access::ShaderWrite)
        .to(dstStage, Access::ShaderRead)
        .record();
}

void Commands::imageBarrier(VkImage img, Stage srcStage, Access srcAccess, Layout oldLayout,
                            Stage dstStage, Access dstAccess, Layout newLayout, uint32_t mipLevels) {
    Barrier(commandBuffer).image(img, mipLevels)
        .from(srcStage, srcAccess, oldLayout)
        .to(dstStage, dstAccess, newLayout)
        .record();
}

void Commands::submitAndWait() {
    end();

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = commandBuffer;

    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;

    vkQueueSubmit2(g_context().graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_context().graphicsQueue);

    if (ownsBuffer) {
        vkFreeCommandBuffers(g_context().device, g_context().commandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
    }
}

void Commands::end() {
    if (!ended) {
        vkEndCommandBuffer(commandBuffer);
        ended = true;
    }
}

Commands::operator VkCommandBuffer() { return commandBuffer; }
