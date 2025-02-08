#include <iostream>
#include <istream>
#include <fstream>
#include <vector>
#include <set>
#include <assert.h>
#include <memory>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "vkobjects.h"

#include "tga.h"
#include "math.h"
#include "camera.h"

// Global Settings
int windowWidth = 1280;
int windowHeight = 720;
size_t computedQuadCount = 100;

// You can have multiple vertex bindings in your vertex stage.  
// A single binding is common.  They are zero-indexed so zero here.
const size_t vertexBindingIndex = 0; 

// A helper to clean up an SDL Window.
struct SDLWindow {
    SDL_Window *window;
    SDLWindow(const char * title, int w, int h) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            throw std::runtime_error("Failed to initialize SDL");
        }
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_VULKAN);
        if (window == nullptr) {
            throw std::runtime_error("Failed to create SDL window");
        }
    }
    ~SDLWindow() {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    operator SDL_Window*() {
        return window;
    }
};

Image createImageFromTGAFile(const char * filename) {
    std::ifstream file(filename);
    std::vector<char> fileBytes = std::vector<char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());

    file.close();
    unsigned width, height;
    int bpp;
    void* bytes = read_tga(fileBytes, width, height, bpp);
    if (bytes == nullptr) {
        throw std::runtime_error("failed to read file as TGA");
    }

    unsigned byteCount = width*height*(bpp/8);

    // TGA is BGR order, not RGB
    // Further, TGA does not specify linear or non-linear color component intensity.
    // By convention, TGA values are going to be "gamma corrected" or non-linear.
    // Assuming the bytes are sRGB looks good.  If they are assumed to be linear here, the colors will be washed out.
    // Read more by looking up sRGB to linear Vulkan conversions.
    VkFormat format = (bpp == 32) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8_SRGB;

    ImageBuilder builder;
    builder.fromBytes(bytes, byteCount, width, height, format);

    Image image(builder);
    free(bytes);
    return image;
}

void recordRenderPass(
    VkExtent2D extent,
    VkPipeline computePipeline,
    VkPipeline graphicsPipeline,
    VkRenderPass renderPass,
    VkFramebuffer framebuffer,
    VkCommandBuffer commandBuffer,
    VkBuffer vertexBuffer,
    VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet
) {
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;  // Can be resubmitted multiple times

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin command buffer");
    }

    // bind and dispatch compute
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = renderPass;  // Your created render pass
    renderPassBeginInfo.framebuffer = framebuffer;  // The framebuffer corresponding to the swap chain image

    // Define the render area (usually the size of the swap chain image)
    renderPassBeginInfo.renderArea.offset = { 0, 0 };  // Starting at (0, 0)
    renderPassBeginInfo.renderArea.extent = extent;  // Covers the whole framebuffer (usually the swap chain image size)

    // Set clear values for attachments (e.g., clearing the color buffer to black and depth buffer to 1.0f)
    VkClearValue clearValues[2];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };  // Clear color: black
    clearValues[1].depthStencil = { 1.0f, 0 };               // Clear depth: 1.0, no stencil

    renderPassBeginInfo.clearValueCount = 2;                 // Two clear values (color and depth)
    renderPassBeginInfo.pClearValues = clearValues;

    // begin recording the render pass
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Bind the descriptor which contains the shader uniform buffer
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, vertexBindingIndex, 1, &vertexBuffer, offsets);  // bind the vertex buffer

    size_t vertexCount = 6 * computedQuadCount;

    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void submitCommandBuffer(VkQueue graphicsQueue, VkCommandBuffer commandBuffer, VkSemaphore imageAvailableSemaphore, VkSemaphore renderFinishedSemaphore, VkFence submitFinishedFence) {
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkCommandBuffer commandBuffers[] = {commandBuffer};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandBuffers;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, submitFinishedFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffer!");
    }
}

bool presentQueue(VkQueue presentQueue, VkSwapchainKHR & swapchain, VkSemaphore renderFinishedSemaphore, uint nextImage) {
    // Present the image to the screen, waiting for renderFinishedSemaphore
    VkSwapchainKHR swapChains[] = {swapchain};
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore; // waits for this
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &nextImage;

    VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);

    switch (result) {
        case VK_SUCCESS:
            return true;
        case VK_ERROR_OUT_OF_DATE_KHR:
        case VK_SUBOPTIMAL_KHR:
            return false; // swap chain needs to be recreated
    }

    throw std::runtime_error("failed to present swap chain image!");
}

int main(int argc, char *argv[]) {
    // these are destroyed in opposite order of creation, cleaning up things in the right order
    SDLWindow window(appName, windowWidth, windowHeight);

    // There can only be one context, and creating it is required for other objects to construct.
    VulkanContext context(window);

    std::vector<VkCommandBuffer> & commandBuffers = context.commandBuffers;

    // shaders
    ShaderModule fragShaderModule(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
    ShaderModule vertShaderModule(ShaderBuilder().vertex().fromFile("tri.vert.spv"));
    ShaderModule compShaderModule(ShaderBuilder().compute().fromFile("vertices.comp.spv"));

    // Image and sampler.  The image class encapsulates the image, memory, and imageview.
    Image textureImage = createImageFromTGAFile("vulkan.tga");
    TextureSampler textureSampler;
 
    // uniform buffer for our view projection matrix
    // In our program it's a static buffer, but if it were dynamic, we'd need to have one for each swapchain image.
    mat16f viewProjection = Camera()
        .perspective(0.5f*M_PI, windowWidth, windowHeight, 0.1f, 100.0f)
        .moveTo(1.0f, 0.0f, -0.1f)
        .lookAt(0.0f, 0.0f, 1.0f)
        .getViewProjection();
    Buffer uniformBuffer(BufferBuilder(sizeof(float) * 16).uniform());
    uniformBuffer.setData(&viewProjection, sizeof(float) * 16);

    // shader storage buffer for computed vertices
    // For simplicity, we're sticking with one buffer and one fence.
    // For a fully dynamic buffer, we'd need one per swapchain image.
    Buffer shaderStorageBuffer(BufferBuilder(sizeof(float) * 5 * 6 * computedQuadCount).storage().vertex());

    // descriptor layout of uniforms in our pipeline
    // we're going to use a single descriptor set layout that is used by by both pipelines
    // normally you would probably want to use two, one for graphics pipeline and another for compute
    DescriptorLayoutBuilder desriptorLayoutBuilder;
    desriptorLayoutBuilder.addUniformBuffer(0, 1, VK_SHADER_STAGE_VERTEX_BIT).addSampler(1, 1, VK_SHADER_STAGE_FRAGMENT_BIT).addStorageBuffer(2, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    VkDescriptorSetLayout descriptorSetLayout = desriptorLayoutBuilder.build();

    // descriptor pool for allocating descriptor sets
    // we've only got one pool here that can build the combined descriptor set above
    // you might want to have two pools if you have different sizes of descriptor sets
    DescriptorPoolBuilder poolBuilder;
    poolBuilder.addSampler(1).addStorageBuffer(1).addUniformBuffer(1).maxSets(1);
    DescriptorPool descriptorPool(poolBuilder);
    
    // create a descriptor set and bind resources to it
    VkDescriptorSet descriptorSet = descriptorPool.allocate(descriptorSetLayout); // pool-owned
    DescriptorSetBinder binder;
    binder.bindUniformBuffer(descriptorSet, 0, uniformBuffer, sizeof(float)*16);
    binder.bindSampler(descriptorSet, 1, textureSampler, textureImage);
    binder.bindStorageBuffer(descriptorSet, 2, shaderStorageBuffer);
    binder.updateSets();
    
    // render pass
    // VkRenderPass is a key object in building a Vulkan application.
    // It defines the layout of the framebuffer attachments and how they are used in the rendering process.
    // Each framebuffer must be related to a render pass.
    // Each pipeline must must be related to a rander pass.
    RenderpassBuilder renderpassBuilder;
    renderpassBuilder
        .colorAttachment().depthAttachment()
        .colorRef(0).depthRef(1);
    VkRenderPass renderPass = renderpassBuilder.build(); // context-owned
    
    // Framebuffers are used to store the results of rendering, including color and depth buffers, and other deep buffers for multi-pass uses.
    // The last framebuffer used in a frame is the swapchain framebuffer, which contains images owned by the Vulkan device that can present to your screen.
    std::vector<Framebuffer> presentFramebuffers; // auto cleaned up by vector destructor
    presentFramebuffers.reserve(context.swapchainImageCount); 
    for (size_t i = 0; i < context.swapchainImageCount; ++i) {
        presentFramebuffers.emplace_back(FramebufferBuilder().present(i).depth(), renderPass);
    }

    // pipelines
    // Pipelines represent the configurable pipeline stages that define what shaders are used and how their results are combined.
    // Take a look at the build() function to see all the options that are necessary and configurable.
    VkPipelineLayout pipelineLayout = createPipelineLayout({descriptorSetLayout}); // context-owned
    
    // vertex stage pipeline setup
    // You can have multiple vertex bindings for different use cases, and step through per vertex or per instance.
    // We only have one in this example.  See how we use vec3 position and vec2 UV in tri.vert
    // Locations have to be unique across the vertex shader in a pipeline.
    GraphicsPipelineBuilder graphicsPipelineBuilder(pipelineLayout, renderPass);
    graphicsPipelineBuilder
        .addVertexShader(vertShaderModule)
        .addFragmentShader(fragShaderModule)
        .vertexBinding(vertexBindingIndex, sizeof(float)*5) // vec3 location and vec2 UV is 5 floats
        .vertexFloats(vertexBindingIndex, 0, 3, 0) // location 0: position vec3
        .vertexFloats(vertexBindingIndex, 1, 2, sizeof(float)*3); // location 1: UV vec2, offset by the previous position vec3

    VkPipeline graphicsPipeline = graphicsPipelineBuilder.build(); // context-owned

    VkPipeline computePipeline = createComputePipeline(pipelineLayout, compShaderModule); // context-owned

    // sync primitives
    // A fence does GPU-CPU syncing.
    std::vector<VkFence> submittedBuffersFinishedFences(4);
    for (size_t i = 0; i < submittedBuffersFinishedFences.size(); ++i) {
        submittedBuffersFinishedFences[i] = createFence(); // context-owned
    }
    
    // index of the swapchain resources to use for the next frame
    uint nextImage = 0;

    // index of the next frame in flight, so we can use the oldest semaphore and fence
    // vkAcquireNextImageKHR indices are not predicatable, so we need to count these ourself
    uint frameInFlightIndex = 0;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }
        }

        VkSemaphore imageAvailableSemaphore = $context().imageAvailableSemaphores[frameInFlightIndex];
        VkFence submittedBuffersFinishedFence = submittedBuffersFinishedFences[frameInFlightIndex];
        VkCommandBuffer commandBuffer = $context().commandBuffers[frameInFlightIndex];

        // Wait for all buffers to finish submitting before resetting them.
        vkWaitForFences(context.device, 1, &submittedBuffersFinishedFence, VK_TRUE, UINT64_MAX);
        vkResetFences(context.device, 1, &submittedBuffersFinishedFence);

        if (VK_SUCCESS != vkAcquireNextImageKHR(context.device, context.swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &nextImage)) {
            throw std::runtime_error("failed to acquire next swapchain image index");
        }
    
        vkResetCommandBuffer(commandBuffer, 0); // manually reset the buffer, otherwise implicit reset causes warnings

        // This program has no dynamic data content, but we record in
        // the loop as an example of how you'd typically record a frame.
        recordRenderPass(
            VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight},
            computePipeline,
            graphicsPipeline,
            renderPass,
            presentFramebuffers[nextImage].framebuffer,
            commandBuffer,
            shaderStorageBuffer,
            pipelineLayout,
            descriptorSet);
            
        // Submit the command buffer to the graphics queue
        VkSemaphore renderFinishedSemaphore = $context().renderFinishedSemaphores[nextImage];
        submitCommandBuffer(context.graphicsQueue, commandBuffer, imageAvailableSemaphore, renderFinishedSemaphore, submittedBuffersFinishedFence);

        if (!presentQueue(context.presentationQueue, context.swapchain, renderFinishedSemaphore, nextImage)) {
            // This is a common Vulkan situation handled automatically by OpenGL.
            // We need to remake our swap chain, image views, and framebuffers.
            // This usually happens after the first frame, but if the image was being used,
            // the above fence will protect it from being modified by us while in use.

            std::cout << "swap chain out of date, trying to remake" << std::endl;
            rebuildPresentationResources();
            presentFramebuffers.clear();
            for (size_t i=0; i<context.swapchainImageCount; ++i) {
                presentFramebuffers.emplace_back(FramebufferBuilder().present(i).depth(), renderPass);
            }
        }

        frameInFlightIndex = (frameInFlightIndex + 1) % context.swapchainImageCount;
    }

    VkResult result = vkQueueWaitIdle(context.graphicsQueue);

    if (VK_SUCCESS != result) {
        throw std::runtime_error("failed to wait for the graphics queue to be idle");
    }

    return 0;
}