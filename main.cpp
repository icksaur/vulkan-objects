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

void getDeviceQueue(VkDevice device, int familyQueueIndex, VkQueue& outGraphicsQueue) {
    vkGetDeviceQueue(device, familyQueueIndex, 0, &outGraphicsQueue);
}

bool getSurfaceProperties(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) {
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS) {
        std::cout << "unable to acquire surface capabilities\n";
        return false;
    }
    return true;
}

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
        framebufferInfo.attachmentCount = 2;  // We are using only a color attachment
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

std::tuple<VkBuffer, VkDeviceMemory> createUniformbuffer(VkPhysicalDevice gpu, VkDevice device) {
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    Camera camera;
    camera.perspective(0.5f*M_PI, windowWidth, windowHeight, 0.1f, 100.0f);
    camera.moveTo(1.0f, 0.0f, -0.1f).lookAt(0.0f, 0.0f, 1.0f);
    mat16f viewProjection = camera.getViewProjection();

    size_t byteCount = sizeof(float)*16; // 4x4 matrix
    std::tie(uniformBuffer, uniformBufferMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, byteCount);

    void* data;
    vkMapMemory(device, uniformBufferMemory, 0, byteCount, 0, &data);  // Map memory to CPU-accessible address
    memcpy(data, viewProjection, (size_t)byteCount);                // Copy vertex data
    vkUnmapMemory(device, uniformBufferMemory);                              // Unmap memory after copying

    return std::make_tuple(uniformBuffer, uniformBufferMemory);
}

std::tuple<VkBuffer, VkDeviceMemory> createShaderStorageBuffer(VkPhysicalDevice gpu, VkDevice device) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    size_t byteCount = sizeof(float) * 5 * 6 * quadCount; // 6 vertices of 5 floats each per quad

    std::tie(buffer, memory) = createBuffer(gpu, device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, byteCount);

    return std::make_tuple(buffer, memory);
}

std::tuple<VkBuffer, VkDeviceMemory> createVertexBuffer(VkPhysicalDevice gpu, VkDevice device) {
    // Vulkan clip space has -1,-1 as the upper-left corner of the display and Y increases as you go down.
    // This is similar to most window system conventions and file formats.
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

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;

    size_t byteCount = sizeof(vertices);
    std::tie(vertexBuffer, vertexBufferMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, byteCount);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, byteCount, 0, &data);  // Map memory to CPU-accessible address
    memcpy(data, vertices, (size_t)byteCount);                // Copy vertex data
    vkUnmapMemory(device, vertexBufferMemory);                              // Unmap memory after copying

    return std::make_tuple(vertexBuffer, vertexBufferMemory);
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device) {
    VkDescriptorSetLayoutBinding uboLayoutBinding = {};
    uboLayoutBinding.binding = 0; // match binding point in shader
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;  // No sampler here

    VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
    samplerLayoutBinding.binding = 1; // match binding point in shader
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // binds both VkImageView and VkSampler
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr; // no sampler here either?

    VkDescriptorSetLayoutBinding ssboLayoutBinding = {};
    ssboLayoutBinding.binding = 2, // match binding point in shader
    ssboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    ssboLayoutBinding.descriptorCount = 1;
    ssboLayoutBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    ssboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding bindings[] {uboLayoutBinding, samplerLayoutBinding, ssboLayoutBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout descriptorSetLayout;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout");
    }

    return descriptorSetLayout;
}

std::tuple<VkDescriptorPool, VkDescriptorSet> createDescriptorSet(VkDevice device, VkDescriptorSetLayout descriptorSetLayout) {
    VkDescriptorPoolSize poolSizes[3];
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // binds both VkImageView and VkSampler
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // compute shader storage buffer
    poolSizes[2].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.poolSizeCount = 3;
    descriptorPoolCreateInfo.pPoolSizes = poolSizes;
    descriptorPoolCreateInfo.maxSets = 3;

    VkDescriptorPool descriptorPool;
    vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool);

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool  = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet;
    vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);

    return std::make_tuple(descriptorPool, descriptorSet);
}

VkWriteDescriptorSet createBufferToDescriptorSetBinding(VkDevice device, VkDescriptorSet descriptorSet, VkBuffer uniformBuffer, VkDescriptorBufferInfo & bufferInfo) {
    bufferInfo = {};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(float)*16;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0; // match binding point in shader
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    return descriptorWrite;
}

VkWriteDescriptorSet createSamplerToDescriptorSetBinding(VkDevice device, VkDescriptorSet descriptorSet, VkSampler sampler, VkImageView imageView, VkDescriptorImageInfo & imageInfo) {
    imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = imageView;
    imageInfo.sampler = sampler;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 1; // match binding point in shader
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    return descriptorWrite;
}

VkWriteDescriptorSet createSsboToDescriptorSetBinding(VkDevice device, VkDescriptorSet descriptorSet, VkBuffer shaderStorageBuffer, VkDescriptorBufferInfo & bufferInfo) {
    bufferInfo = {};
    bufferInfo.buffer = shaderStorageBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet descriptorWrite = {};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 2; // match binding point in shader
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    return descriptorWrite;
}

void updateDescriptorSet(VkDevice device, VkDescriptorSet descriptorSet, std::vector<VkWriteDescriptorSet> & writeDescrptorSets) {
    vkUpdateDescriptorSets(device, writeDescrptorSets.size(), writeDescrptorSets.data(), 0, nullptr);
}

VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool) {
    VkCommandBuffer commandBuffer;

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // primary can be submitted, secondary can be a sub-command of primaries
    allocInfo.commandBufferCount = 1;  // Number of command buffers to allocate

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!");
    }

    return commandBuffer;
}

VkSemaphore createSemaphore(VkDevice device) {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    createInfo.flags = 0;
    createInfo.pNext = nullptr;

    VkSemaphore semaphore;
    
    if (vkCreateSemaphore(device, &createInfo, NULL, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore");
    }

    return semaphore;
}

VkFence createFence(VkDevice device) {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    createInfo.pNext = nullptr;

    VkFence fence;
    if (vkCreateFence(device, &createInfo, NULL, &fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create fence");
    }

    return fence;
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

    // bind and dispatch compute
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

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
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore; // waits for this
    VkSwapchainKHR swapChains[] = {swapchain};
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

    VkInstance & instance = context.instance;
    VkPhysicalDevice & gpu = context.physicalDevice;
    VkDevice & device = context.device;
    unsigned & graphicsQueueIndex = context.graphicsQueueIndex;
    VkSurfaceKHR & presentationSurface = context.presentationSurface;
    VkQueue & presentationQueue = context.presentationQueue;
    VkSwapchainKHR & swapchain = context.swapchain;
    std::vector<VkImage> & chainImages = context.swapchainImages;
    std::vector<VkImageView> & chainImageViews = context.swapchainImageViews;
    VkImage & depthImage = context.depthImage;
    VkDeviceMemory & depthMemory = context.depthMemory;
    VkImageView & depthImageView = context.depthImageView;
    VkCommandPool & commandPool = context.commandPool;
    VkQueue & graphicsQueue = context.graphicsQueue;

    // shader objects
    std::unique_ptr<ShaderModule> vertShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).vertex().fromFile("tri.vert.spv"));
    std::unique_ptr<ShaderModule> fragShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).fragment().fromFile("tri.frag.spv"));
    std::unique_ptr<ShaderModule> compShaderModule = std::make_unique<ShaderModule>(ShaderBuilder(context).compute().fromFile("vertices.comp.spv"));

    // image for sampling
    std::unique_ptr<Image> textureImage = createImageFromTGAFile("vulkan.tga", context);

    std::unique_ptr<TextureSampler> textureSampler = std::make_unique<TextureSampler>(context);

    // uniform buffer for our view projection matrix
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    std::tie(uniformBuffer, uniformBufferMemory) = createUniformbuffer(gpu, device);

    // shader storage buffer
    VkBuffer shaderStorageBuffer;
    VkDeviceMemory shaderStorageBufferMemory;
    std::tie(shaderStorageBuffer, shaderStorageBufferMemory) = createShaderStorageBuffer(gpu, device);

    // descriptor of uniforms, both uniform buffer and sampler
    VkDescriptorSetLayout descriptorSetLayout = createDescriptorSetLayout(device);
    
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    std::tie(descriptorPool, descriptorSet) = createDescriptorSet(device, descriptorSetLayout);

    // memory for these have to survive until updateDescriptorSet below
    VkDescriptorBufferInfo uniformBufferInfo;
    VkDescriptorImageInfo imageInfo;
    VkDescriptorBufferInfo shaderStorageBufferInfo;

    std::vector<VkWriteDescriptorSet> descriptorWriteSets;
    descriptorWriteSets.push_back(createBufferToDescriptorSetBinding(device, descriptorSet, uniformBuffer, uniformBufferInfo));
    descriptorWriteSets.push_back(createSamplerToDescriptorSetBinding(device, descriptorSet, *textureSampler, textureImage->imageView, imageInfo));
    descriptorWriteSets.push_back(createSsboToDescriptorSetBinding(device, descriptorSet, shaderStorageBuffer, shaderStorageBufferInfo));

    updateDescriptorSet(device, descriptorSet, descriptorWriteSets);

    // render pass and present buffers
    RenderpassBuilder renderpassBuilder(context);
    renderpassBuilder.colorAttachment().depthAttachment();
    renderpassBuilder.colorRef(0).depthRef(1);

    std::unique_ptr<RenderPass> renderPassPtr = std::make_unique<RenderPass>(renderpassBuilder);
    VkRenderPass & renderPass = renderPassPtr->renderpass;
    
    std::vector<VkFramebuffer> presentFramebuffers(chainImages.size());
    createPresentFramebuffers(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, renderPass, chainImageViews, presentFramebuffers, depthImageView);

    // pipelines
    VkPipelineLayout pipelineLayout = createPipelineLayout(device, descriptorSetLayout);
    VkPipeline graphicsPipeline = createGraphicsPipeline(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, pipelineLayout, renderPass, *vertShaderModule, *fragShaderModule);
    VkPipeline computePipeline = createComputePipeline(device, pipelineLayout, *compShaderModule);

    // vertex buffer for our vertices
    VkBuffer vertexBuffer;
    VkDeviceMemory deviceMemory;
    std::tie(vertexBuffer, deviceMemory) = createVertexBuffer(gpu, device);

    // command buffers for drawing
    std::vector<VkCommandBuffer> commandBuffers(chainImages.size());
    for (auto & commandBuffer : commandBuffers) {
        commandBuffer = createCommandBuffer(device, commandPool);
    }

    // sync primitives
    // It is a good idea to have a separate semaphore for each swapchain image, but for simplicity we use a single one.
    VkSemaphore imageAvailableSemaphore = createSemaphore(device);
    VkSemaphore renderFinishedSemaphore = createSemaphore(device);
    VkFence fence = createFence(device);
    
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
        recordRenderPass(VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, computePipeline, graphicsPipeline, renderPass, presentFramebuffers[nextImage], commandBuffers[nextImage], shaderStorageBuffer, pipelineLayout, descriptorSet);
#else
        recordRenderPass(computePipeline, graphicsPipeline, renderPass, frameBuffers[nextImage], commandBuffers[nextImage], vertexBuffer, pipelineLayout, descriptorSet);
#endif
        submitCommandBuffer(graphicsQueue, commandBuffers[nextImage], imageAvailableSemaphore, renderFinishedSemaphore);
        if (!presentQueue(presentationQueue, swapchain, renderFinishedSemaphore, nextImage)) {
            std::cout << "swap chain out of date, trying to remake" << std::endl;

            // This is a common Vulkan situation handled automatically by OpenGL.
            // We need to remake our swap chain, image views, and framebuffers.
            vkDeviceWaitIdle(device);
            rebuildPresentationResources(context);

            for (VkFramebuffer framebuffer : presentFramebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            createPresentFramebuffers(device, VkExtent2D{(uint32_t)context.windowWidth, (uint32_t)context.windowHeight}, renderPass, chainImageViews, presentFramebuffers, depthImageView);
        }
        SDL_Delay(100);
        
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetCommandBuffer(commandBuffers[nextImage], 0); // manually reset, otherwise implicit reset causes warnings
    }

    vkQueueWaitIdle(graphicsQueue); // wait until we're done or the render finished semaphore may be in use

    for (auto commandBuffer : commandBuffers) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, deviceMemory, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory,  nullptr);

    vkDestroyBuffer(device, shaderStorageBuffer, nullptr);
    vkFreeMemory(device, shaderStorageBufferMemory, nullptr);

    // freeing each descriptor requires the pool have the "free" bit. Look online for use cases for individual free.
    vkResetDescriptorPool(device, descriptorPool, 0); // frees all the descriptors
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    textureSampler.reset();
    textureImage.reset();

    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyPipeline(device, computePipeline, nullptr);
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    for (VkFramebuffer framebuffer : presentFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    vertShaderModule.reset();
    compShaderModule.reset();
    fragShaderModule.reset();
    renderPassPtr.reset();
    contextPtr.reset();
    SDL_Quit();

    return 1;
}