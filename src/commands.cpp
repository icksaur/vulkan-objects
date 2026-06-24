#include "vkinternal.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

// --- Commands ---

static thread_local VkFence submitAndWaitFence = VK_NULL_HANDLE;
static thread_local VkDevice submitAndWaitFenceDevice = VK_NULL_HANDLE;

void destroyThreadLocalSubmitFence(VkDevice device) {
    if (submitAndWaitFence != VK_NULL_HANDLE && submitAndWaitFenceDevice == device) {
        vkDestroyFence(device, submitAndWaitFence, nullptr);
        submitAndWaitFence = VK_NULL_HANDLE;
        submitAndWaitFenceDevice = VK_NULL_HANDLE;
    }
}

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

void Commands::beginRendering() {
    beginRendering(0.0f, 0.0f, 0.0f, 1.0f);
}

void Commands::beginRendering(float r, float g, float b, float a) {
    if (!frame) throw std::runtime_error("beginRendering requires a frame-bound Commands (use frame.beginCommands())");

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = frame->swapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearColor = { .color = { r, g, b, a } };
    colorAttachment.clearValue = clearColor;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    renderingInfo.layerCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pDepthAttachment = nullptr;

    VkViewport vp = {};
    vp.width = (float)g_context().windowWidth;
    vp.height = (float)g_context().windowHeight;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = { (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::resumeRendering() {
    if (!frame) throw std::runtime_error("resumeRendering requires a frame-bound Commands");

    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = frame->swapchainImageView();
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    renderingInfo.layerCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pDepthAttachment = nullptr;

    VkViewport vp = {};
    vp.width = (float)g_context().windowWidth;
    vp.height = (float)g_context().windowHeight;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = { (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::beginRenderingOffscreen(VkImageView colorImage, VkExtent2D extent) {
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorImage;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearColor = { .color = { 0.0f, 0.0f, 0.0f, 0.0f } };
    colorAttachment.clearValue = clearColor;

    VkRenderingInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea = { 0, 0, extent.width, extent.height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = nullptr;

    VkViewport vp = {};
    vp.width = (float)extent.width; vp.height = (float)extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    vkBeginRendering(commandBuffer, &renderingInfo);
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

    VkViewport vp = {};
    vp.width = (float)g_context().windowWidth;
    vp.height = (float)g_context().windowHeight;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = { (uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight };
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::beginRendering(VkImageView depthImage, VkExtent2D extent) {
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
    renderingInfo.colorAttachmentCount = 0;
    renderingInfo.pColorAttachments = nullptr;
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    VkViewport vp = {};
    vp.x = 0; vp.y = 0;
    vp.width = (float)extent.width; vp.height = (float)extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.offset = {0, 0};
    sc.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::beginRendering(VkImageView colorImage, VkImageView depthImage, VkExtent2D extent) {
    VkImageView views[] = { colorImage };
    beginRendering(std::span<const VkImageView>(views), depthImage, extent);
}

void Commands::beginRendering(std::span<const VkImageView> colorImages, VkImageView depthImage, VkExtent2D extent) {
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    for (auto view : colorImages) {
        VkRenderingAttachmentInfo att = {};
        att.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        att.imageView = view;
        att.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clearColor = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };
        att.clearValue = clearColor;
        colorAttachments.push_back(att);
    }

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
    renderingInfo.colorAttachmentCount = (uint32_t)colorAttachments.size();
    renderingInfo.pColorAttachments = colorAttachments.data();
    renderingInfo.pDepthAttachment = &depthAttachmentInfo;

    VkViewport vp = {};
    vp.width = (float)extent.width; vp.height = (float)extent.height;
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);
    VkRect2D sc = {};
    sc.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);

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

void Commands::fillBuffer(VkBuffer buffer, uint32_t value, VkDeviceSize offset, VkDeviceSize size) {
    vkCmdFillBuffer(commandBuffer, buffer, offset, size, value);
}

void Commands::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset, VkDeviceSize dstOffset) {
    VkBufferCopy region = {};
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = size;
    vkCmdCopyBuffer(commandBuffer, src, dst, 1, &region);
}

static Access inferSrcAccess(Stage s) {
    auto v = static_cast<uint64_t>(s);
    if (v & static_cast<uint64_t>(Stage::Transfer)) return Access::TransferWrite;
    if (v & static_cast<uint64_t>(Stage::Host)) return Access::HostWrite;
    return Access::ShaderWrite;
}

static Access inferDstAccess(Stage s) {
    auto v = static_cast<uint64_t>(s);
    if (v & static_cast<uint64_t>(Stage::Transfer)) return Access::TransferRead;
    if (v & static_cast<uint64_t>(Stage::Host)) return Access::HostRead;
    if (v & static_cast<uint64_t>(Stage::DrawIndirect)) return Access::IndirectCommandRead;
    return Access::ShaderRead;
}

void Commands::bufferBarrier(VkBuffer buf, Stage srcStage, Stage dstStage) {
    Barrier(commandBuffer).buffer(buf)
        .from(srcStage, inferSrcAccess(srcStage))
        .to(dstStage, inferDstAccess(dstStage))
        .record();
}

void Commands::bufferBarrier(VkBuffer buf, Stage srcStage, Access srcAccess,
                             Stage dstStage, Access dstAccess) {
    Barrier(commandBuffer).buffer(buf)
        .from(srcStage, srcAccess)
        .to(dstStage, dstAccess)
        .record();
}

void Commands::bufferBarriers(std::span<const BufferBarrierDesc> barriers) {
    if (barriers.empty()) return;
    std::vector<VkBufferMemoryBarrier2> mem(barriers.size());
    for (size_t i = 0; i < barriers.size(); ++i) {
        VkBufferMemoryBarrier2 & b = mem[i];
        b = {};
        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = barriers[i].buffer;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
        b.srcStageMask = static_cast<VkPipelineStageFlags2>(barriers[i].srcStage);
        b.srcAccessMask = static_cast<VkAccessFlags2>(barriers[i].srcAccess);
        b.dstStageMask = static_cast<VkPipelineStageFlags2>(barriers[i].dstStage);
        b.dstAccessMask = static_cast<VkAccessFlags2>(barriers[i].dstAccess);
    }
    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(mem.size());
    depInfo.pBufferMemoryBarriers = mem.data();
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
}

void Commands::blasToTlasBarrier(std::span<const VkBuffer> blasBackings) {
    std::vector<BufferBarrierDesc> barriers;
    barriers.reserve(blasBackings.size());
    for (VkBuffer backing : blasBackings) {
        barriers.push_back({backing, Stage::AccelStructureBuild, Access::AccelStructureWrite,
                            Stage::AccelStructureBuild, Access::AccelStructureRead});
    }
    bufferBarriers(barriers);
}

void Commands::tlasToShaderReadBarrier(VkBuffer tlasBacking) {
    bufferBarrier(tlasBacking, Stage::AccelStructureBuild, Access::AccelStructureWrite,
                  Stage::AllCommands, Access::AccelStructureRead);
}

void Commands::imageBarrier(VkImage img, Stage srcStage, Access srcAccess, Layout oldLayout,
                            Stage dstStage, Access dstAccess, Layout newLayout, uint32_t mipLevels, uint32_t layerCount) {
    Barrier(commandBuffer).image(img, mipLevels, layerCount)
        .from(srcStage, srcAccess, oldLayout)
        .to(dstStage, dstAccess, newLayout)
        .record();
}

void Commands::submitAndWait() {
    const bool diag = std::getenv("HULL_FENCE_SLACK_DIAG") != nullptr;
    auto now = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    auto t0 = now();
    end();
    auto tEnd = now();

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = commandBuffer;

    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Reuse one fence per thread across submitAndWait calls: this function always waits for
    // completion before returning, so the fence is idle by the next call. vkCreateFence /
    // vkDestroyFence per submit cost ~0.3ms each on this driver — ~7ms over a ~12-submit bind.
    VkDevice device = g_context().device;
    if (submitAndWaitFence == VK_NULL_HANDLE || submitAndWaitFenceDevice != device) {
        if (vkCreateFence(device, &fenceInfo, nullptr, &submitAndWaitFence) != VK_SUCCESS) {
            throw std::runtime_error("failed to create fence for submitAndWait");
        }
        submitAndWaitFenceDevice = device;
    } else {
        vkResetFences(device, 1, &submitAndWaitFence);
    }
    VkFence fence = submitAndWaitFence;
    auto tFence = now();

    vkQueueSubmit2(g_context().graphicsQueue, 1, &submitInfo, fence);
    auto tSubmit = now();
    if (std::getenv("HULL_FENCE_SLACK_POLL") != nullptr) {
        while (vkGetFenceStatus(g_context().device, fence) == VK_NOT_READY) { /* busy spin */ }
    } else {
        vkWaitForFences(g_context().device, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    auto tWait = now();
    auto tDestroy = now();

    if (ownsBuffer) {
        vkFreeCommandBuffers(g_context().device, g_context().commandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
    }
    auto tFree = now();
    if (diag) {
        std::fprintf(stderr,
            "DIAG: vk_submit_wait end=%.3fms createFence=%.3fms queueSubmit=%.3fms waitFence=%.3fms destroyFence=%.3fms freeCmd=%.3fms total=%.3fms\n",
            ms(t0, tEnd), ms(tEnd, tFence), ms(tFence, tSubmit), ms(tSubmit, tWait),
            ms(tWait, tDestroy), ms(tDestroy, tFree), ms(t0, tFree));
    }
}

void Commands::end() {
    if (!ended) {
        vkEndCommandBuffer(commandBuffer);
        ended = true;
    }
}

Commands::operator VkCommandBuffer() { return commandBuffer; }
