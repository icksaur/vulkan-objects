#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <iostream>
#include <istream>
#include <fstream>
#include <vulkan/vulkan_core.h>
#include <vector>
#include <set>
#include <assert.h>
#include <memory>

#include "vkobjects.h"

#include "tga.h"
#include "math.h"
#include "camera.h"

// Global Settings
int windowWidth = 1280;
int windowHeight = 720;

#define COMPUTE_VERTICES // comment out to try CPU uploaded vertex buffer
size_t quadCount = 100;

std::unique_ptr<Image> createImageFromTGAFile(const char * filename, VulkanContext & context) {
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

    ImageBuilder builder(context);
    builder.fromBytes(bytes, byteCount, width, height, format);

    std::unique_ptr<Image> image = std::make_unique<Image>(builder);
    free(bytes);
    return image;
}

void createPresentFramebuffers(VkDevice device, VkExtent2D extent, VkRenderPass renderPass, std::vector<VkImageView> & chainImageViews, std::vector<VkFramebuffer> & frameBuffers, VkImageView depthImageView) {
    for (size_t i=0; i<chainImageViews.size(); i++) {
        VkImageView imageViews[] { chainImageViews[i], depthImageView };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;  // color and depth attachment
        framebufferInfo.pAttachments = imageViews;  // Image view as color attachment
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;  
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

    VkPipelineLayout pipelineLayout;
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    return pipelineLayout;
}

VkPipeline createGraphicsPipeline(VkDevice device, VkExtent2D extent, VkPipelineLayout pipelineLayout, VkRenderPass renderPass, VkShaderModule vertexShaderModule, VkShaderModule fragmentShaderModule) {
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertexShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragmentShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Binding description (one vec2 per vertex)
    VkVertexInputBindingDescription bindingDescription = {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 5; // vec3 pos and vec2 uv
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    // Attribute description (vec3 -> location 0 in the shader)
    VkVertexInputAttributeDescription attributeDescriptions[2];
    attributeDescriptions[0] = {};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;

    // Attribute description (vec2 -> location 1 in the shader)
    VkVertexInputAttributeDescription attributeDescription = {};
    attributeDescriptions[1] = {};
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 3;

    // Pipeline vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2;
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = extent.width;
    viewport.height = extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;  // Fill the triangles
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;    // may cull backfacing faces, etc
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // Counter-clockwise vertices are front
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipeline pipeline;
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = 2;
    pipelineCreateInfo.pStages = shaderStages;  // Vertex and fragment shaders
    pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.layout = pipelineLayout;  // Pipeline layout created earlier
    pipelineCreateInfo.renderPass = renderPass;  // Render pass created earlier
    pipelineCreateInfo.subpass = 0;  // Index of the subpass where this pipeline will be used
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;  // Not deriving from another pipeline
    pipelineCreateInfo.pDepthStencilState = &depthStencil;
    
    if (vkCreateGraphicsPipelines(device,  VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
    
    return pipeline;
}

VkPipeline createComputePipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkShaderModule computeShaderModule) {
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    if (VK_SUCCESS != vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline)) {
        throw std::runtime_error("failed to create compute pipeline!");
    }

    return computePipeline;
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
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);  // Bind the vertex buffer

#ifdef COMPUTE_VERTICES
    size_t vertexCount = 6 * quadCount;
#else 
    size_t vertexCount = 6 * 2;
#endif
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void submitCommandBuffer(VkQueue graphicsQueue, VkCommandBuffer commandBuffer, VkSemaphore imageAvailableSemaphore, VkSemaphore renderFinishedSemaphore) {
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

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffer!");
    }

    VkResult result = vkQueueWaitIdle(graphicsQueue);
    if (VK_SUCCESS != result) {
        throw std::runtime_error("failed to wait for the graphics queue to be idle");
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
    if (result != VK_SUCCESS) {
        if (VK_ERROR_OUT_OF_DATE_KHR == result) {
            return false;
        } else {
            throw std::runtime_error("failed to present swap chain image!");
        }
    }

    return true;
}

int main(int argc, char *argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return -1;
    }

    // Create vulkan compatible window
    SDL_Window* window = SDL_CreateWindow(appName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        SDL_Quit();
        return -1;
    }

    std::unique_ptr<VulkanContext> contextPtr = std::make_unique<VulkanContext>(window);
    VulkanContext& context = *contextPtr;

    VkDevice & device = context.device;
    VkQueue & presentationQueue = context.presentationQueue;
    VkSwapchainKHR & swapchain = context.swapchain;
    std::vector<VkImage> & chainImages = context.swapchainImages;
    std::vector<VkImageView> & chainImageViews = context.swapchainImageViews;
    std::vector<VkCommandBuffer> & commandBuffers = context.commandBuffers;
    VkQueue & graphicsQueue = context.graphicsQueue;

    std::unique_ptr<Image> depthImage = std::make_unique<Image>(ImageBuilder(context).forDepthBuffer());


    // shader objects
    std::unique_ptr<ShaderModule> vertShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).vertex().fromFile("tri.vert.spv"));
    std::unique_ptr<ShaderModule> fragShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).fragment().fromFile("tri.frag.spv"));
    std::unique_ptr<ShaderModule> compShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).compute().fromFile("vertices.comp.spv"));

    // texture objects
    std::unique_ptr<Image> textureImage = createImageFromTGAFile("vulkan.tga", context);
    std::unique_ptr<TextureSampler> textureSampler = std::make_unique<TextureSampler>(context);

    // uniform buffer for our view projection matrix
    std::unique_ptr<Buffer> uniformBuffer = std::make_unique<Buffer>(context, BufferBuilder(sizeof(float) * 16).uniform());
    mat16f viewProjection = Camera()
        .perspective(0.5f*M_PI, windowWidth, windowHeight, 0.1f, 100.0f)
        .moveTo(1.0f, 0.0f, -0.1f)
        .lookAt(0.0f, 0.0f, 1.0f)
        .getViewProjection();
    uniformBuffer->setData(&viewProjection, sizeof(float) * 16);

    // shader storage buffer for computed vertices
    std::unique_ptr<Buffer> shaderStorageBuffer = std::make_unique<Buffer>(context, BufferBuilder(sizeof(float) * 5 * 6 * quadCount).storage().vertex());

    // static vertices
    float vertices[] {
        -0.5f, 0.5f, 0.0f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
        0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.0f, 1.0f, 1.0f,

        -0.5f, 0.5f, 0.2f, 0.0f, 0.0f,
        0.5f, 0.5f, 0.2f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.2f, 0.0f, 1.0f,
        -0.5f, -0.5f, 0.2f, 0.0f, 1.0f,
        0.5f, 0.5f, 0.2f, 1.0f, 0.0f,
        0.5f, -0.5f, 0.2f, 1.0f, 1.0f,
    };

    // static vertex buffer
    std::unique_ptr<Buffer> vertexBuffer = std::make_unique<Buffer>(context, BufferBuilder(sizeof(vertices)).vertex());
    vertexBuffer->setData(vertices, sizeof(vertices));

    // descriptor of uniforms, both uniform buffer and sampler
    std::unique_ptr<DescriptorLayoutBuilder> layoutBuilder = std::make_unique<DescriptorLayoutBuilder>(context);
    layoutBuilder->addUniformBuffer(0, 1, VK_SHADER_STAGE_VERTEX_BIT).addSampler(1, 1, VK_SHADER_STAGE_FRAGMENT_BIT).addStorageBuffer(2, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    VkDescriptorSetLayout descriptorSetLayout = layoutBuilder->build();

    // descriptor pool for allocating descriptors
    DescriptorPoolBuilder poolBuilder;
    poolBuilder.addSampler(1).addStorageBuffer(1).addUniformBuffer(1).maxSets(1);
    std::unique_ptr<DescriptorPool> descriptorPool = std::make_unique<DescriptorPool>(context, poolBuilder);
    
    // create a descriptor set and bind resources to it
    VkDescriptorSet descriptorSet = descriptorPool->allocate(descriptorSetLayout);
    DescriptorSetBinder binder(context);
    binder.bindUniformBuffer(descriptorSet, 0, *uniformBuffer, sizeof(float)*16);
    binder.bindSampler(descriptorSet, 1, *textureSampler, *textureImage);
    binder.bindStorageBuffer(descriptorSet, 2, *shaderStorageBuffer);
    binder.updateSets();
    
    // render pass and present buffers
    RenderpassBuilder renderpassBuilder(context);
    renderpassBuilder.colorAttachment().depthAttachment();
    renderpassBuilder.colorRef(0).depthRef(1);
    VkRenderPass renderPass = renderpassBuilder.build();
    
    std::vector<VkFramebuffer> presentFramebuffers(chainImages.size());
    createPresentFramebuffers(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, renderPass, chainImageViews, presentFramebuffers, depthImage->imageView);

    // pipelines
    VkPipelineLayout pipelineLayout = createPipelineLayout(device, descriptorSetLayout);
    VkPipeline graphicsPipeline = createGraphicsPipeline(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, pipelineLayout, renderPass, *vertShaderModule, *fragShaderModule);
    VkPipeline computePipeline = createComputePipeline(device, pipelineLayout, *compShaderModule);

    // sync primitives
    // It is a good idea to have a separate semaphore for each swapchain image, but for simplicity we use a single one.
    VkSemaphore imageAvailableSemaphore = createSemaphore(context);
    VkSemaphore renderFinishedSemaphore = createSemaphore(context);
    VkFence fence = createFence(context);
    
    uint nextImage = 0;

    SDL_Event event;
    bool done = false;
    while (!done) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }
        }
        vkResetFences(device, 1, &fence);

        VkResult nextImageResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, fence, &nextImage);
        if (nextImageResult != VK_SUCCESS) {
            std::cout << nextImageResult << std::endl;
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

#ifdef COMPUTE_VERTICES
        recordRenderPass(VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, computePipeline, graphicsPipeline, renderPass, presentFramebuffers[nextImage], commandBuffers[nextImage], *shaderStorageBuffer, pipelineLayout, descriptorSet);
#else
        recordRenderPass(VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, computePipeline, graphicsPipeline, renderPass, presentFramebuffers[nextImage], commandBuffers[nextImage], *vertexBuffer, pipelineLayout, descriptorSet);
#endif

        submitCommandBuffer(graphicsQueue, commandBuffers[nextImage], imageAvailableSemaphore, renderFinishedSemaphore);
        if (!presentQueue(presentationQueue, swapchain, renderFinishedSemaphore, nextImage)) {
            std::cout << "swap chain out of date, trying to remake" << std::endl;

            // This is a common Vulkan situation handled automatically by OpenGL.
            // We need to remake our swap chain, image views, and framebuffers.
            vkDeviceWaitIdle(device);
            rebuildPresentationResources(context);
    
            depthImage = std::make_unique<Image>(ImageBuilder(context).forDepthBuffer());

            for (VkFramebuffer framebuffer : presentFramebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            createPresentFramebuffers(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, renderPass, chainImageViews, presentFramebuffers, depthImage->imageView);
        }
        SDL_Delay(100);
        
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetCommandBuffer(commandBuffers[nextImage], 0); // manually reset, otherwise implicit reset causes warnings
    }

    vkQueueWaitIdle(graphicsQueue); // wait until we're done or the render finished semaphore may be in use

    descriptorPool.reset();
    layoutBuilder.reset();
    vertexBuffer.reset();
    shaderStorageBuffer.reset();
    uniformBuffer.reset();
    textureSampler.reset();
    textureImage.reset();
    depthImage.reset();

    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    for (VkFramebuffer framebuffer : presentFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    vertShaderModule.reset();
    compShaderModule.reset();
    fragShaderModule.reset();
    vkDestroyRenderPass(device, renderPass, nullptr);
    contextPtr.reset();
    SDL_Quit();

    return 0;
}