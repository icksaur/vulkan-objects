#include "vkinternal.h"

// --- Frame ---

Frame * Frame::currentGuard = nullptr;

Frame::Frame() :
    context(g_context()),
    inFlightIndex(context.frameInFlightIndex),
    imageAvailableSemaphore(context.imageAvailableSemaphores[inFlightIndex]),
    renderFinishedSemaphore(VK_NULL_HANDLE),
    submittedBuffersFinishedFence(context.submittedBuffersFinishedFences[inFlightIndex]),
    imageIndex(0),
    submitted(false)
{
    if (Frame::currentGuard != nullptr) {
        throw std::runtime_error("multiple frames in flight, only one frame is allowed at a time");
    }
    Frame::currentGuard = this;

    // Wait for oldest frame's work to complete
    vkWaitForFences(context.device, 1, &submittedBuffersFinishedFence, VK_TRUE, UINT64_MAX);

    // Clean up oldest generation
    context.destroyGenerations[inFlightIndex].destroy();

    if (context.isMinimized()) {
        // A zero-size window has no valid swapchain extent; the caller must check isMinimized()
        // before constructing a Frame rather than have this fail silently every frame.
        throw std::runtime_error("cannot construct Frame while the window is minimized "
                                  "(VulkanContext::isMinimized()); skip rendering until it is false");
    }

    // Wayland (and other platforms) do not reliably report a resize through
    // VK_ERROR_OUT_OF_DATE_KHR/VK_SUBOPTIMAL_KHR at all, so the window's pixel size is compared
    // against the size the swapchain was requested at every frame rather than relying on those
    // results.
    int pixelWidth, pixelHeight;
    SDL_GetWindowSizeInPixels(context.window, &pixelWidth, &pixelHeight);
    if (needsSwapchainRebuild({ (uint32_t)pixelWidth, (uint32_t)pixelHeight }, context.swapchainRequestedSize)) {
        context.recreateSwapchain();
    }

    // Acquire next image, recreating and retrying once if the swapchain was already out of date
    // (e.g. a resize the check above missed by one frame, or a transform/present-engine change
    // rather than a size change).
    VkResult acquireResult = vkAcquireNextImageKHR(context.device, context.swapchain, UINT64_MAX,
        imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        context.recreateSwapchain();
        acquireResult = vkAcquireNextImageKHR(context.device, context.swapchain, UINT64_MAX,
            imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    }
    // VK_SUBOPTIMAL_KHR is a successful acquire (the image is still presentable this frame); it is
    // handled after present, alongside the same result there, so it is not a recreate trigger here.
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire next swapchain image");
    }
    renderFinishedSemaphore = context.renderFinishedSemaphores[imageIndex];
}

Frame::~Frame() {
    Frame::currentGuard = nullptr;
    context.frameInFlightIndex =
        nextFrameInFlightSlot(context.frameInFlightIndex, context.frameInFlightCount());
}

uint32_t Frame::swapchainImageIndex() const { return imageIndex; }
VkImageView Frame::swapchainImageView() const { return context.swapchainImageViews[imageIndex]; }

Commands Frame::beginCommands() {
    VkCommandBuffer cmd = context.frameCommandBuffers[inFlightIndex];
    vkResetCommandBuffer(cmd, 0);
    Commands cmds(cmd, false);
    cmds.frame = &(*Frame::currentGuard);

    // Set default viewport and scissor (dynamic state)
    cmds.setViewport(0, 0, (float)context.windowWidth, (float)context.windowHeight);
    cmds.setScissor(0, 0, (uint32_t)context.windowWidth, (uint32_t)context.windowHeight);

    // Transition swapchain image to render target
    cmds.imageBarrier(context.swapchainImages[imageIndex],
        Stage::None, Access::None, Layout::Undefined,
        Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment);

    return cmds;
}

void Frame::submit(Commands & cmd) {
    // Transition swapchain image back for presentation
    cmd.imageBarrier(context.swapchainImages[imageIndex],
        Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment,
        Stage::None, Access::None, Layout::PresentSrc);

    cmd.end();

    vkResetFences(context.device, 1, &submittedBuffersFinishedFence);

    // Submit using vkQueueSubmit2
    VkSemaphoreSubmitInfo waitSemaphoreInfo = {};
    waitSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSemaphoreInfo.semaphore = imageAvailableSemaphore;
    waitSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;

    VkSemaphoreSubmitInfo signalSemaphoreInfo = {};
    signalSemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSemaphoreInfo.semaphore = renderFinishedSemaphore;
    signalSemaphoreInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo = {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd.commandBuffer;

    VkSubmitInfo2 submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitSemaphoreInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalSemaphoreInfo;

    VkResult submitResult =
        vkQueueSubmit2(context.graphicsQueue, 1, &submitInfo, submittedBuffersFinishedFence);
    if (submitResult != VK_SUCCESS) {
        throw std::runtime_error(std::string("failed to submit command buffer: ")
                                 + vkResultName(submitResult));
    }

    // Present
    VkSwapchainKHR swapchains[] = {context.swapchain};
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(context.presentationQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        context.recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present queue");
    }

    submitted = true;
}
