#include "vkinternal.h"

// --- Pipelines ---

GraphicsPipelineBuilder::GraphicsPipelineBuilder() : sampleCountBit(VK_SAMPLE_COUNT_1_BIT), isDepthOnly(false), enableAlphaBlend(false), disableDepthTest(false), depthOnlyFormat(VK_FORMAT_D32_SFLOAT) {}
GraphicsPipelineBuilder & GraphicsPipelineBuilder::meshShader(ShaderModule & meshShaderModule, const char * entryPoint) {
    if (meshShaderModule.reflection.executionModel != VK_SHADER_STAGE_MESH_BIT_EXT) {
        throw std::runtime_error("pipeline build error: shader '" + meshShaderModule.fileName +
            "' is not a mesh shader (wrong execution model)");
    }
    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    stageInfo.module = meshShaderModule.module;
    stageInfo.pName = entryPoint;
    shaderStages.push_back(stageInfo);
    shaderModules.push_back(&meshShaderModule);
    return *this;
}
GraphicsPipelineBuilder & GraphicsPipelineBuilder::fragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint) {
    if (fragmentShaderModule.reflection.executionModel != VK_SHADER_STAGE_FRAGMENT_BIT) {
        throw std::runtime_error("pipeline build error: shader '" + fragmentShaderModule.fileName +
            "' is not a fragment shader (wrong execution model)");
    }
    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stageInfo.module = fragmentShaderModule.module;
    stageInfo.pName = entryPoint;
    shaderStages.push_back(stageInfo);
    shaderModules.push_back(&fragmentShaderModule);
    return *this;
}
GraphicsPipelineBuilder & GraphicsPipelineBuilder::sampleCount(size_t sampleCount) {
    if (sampleCount > g_context().maxSamples) {
        throw std::runtime_error("requested sample count exceeds maximum supported by device");
    } else if (sampleCount == 0) {
        throw std::runtime_error("sample count must be greater than 0");
    }
    sampleCountBit = getSampleBits(sampleCount);
    return *this;
}

GraphicsPipelineBuilder & GraphicsPipelineBuilder::depthOnly(VkFormat format) {
    isDepthOnly = true;
    depthOnlyFormat = format;
    return *this;
}

GraphicsPipelineBuilder & GraphicsPipelineBuilder::colorFormats(std::vector<VkFormat> formats) {
    colorAttachmentFormats = std::move(formats);
    return *this;
}

GraphicsPipelineBuilder & GraphicsPipelineBuilder::alphaBlend() {
    enableAlphaBlend = true;
    return *this;
}

GraphicsPipelineBuilder & GraphicsPipelineBuilder::noDepth() {
    disableDepthTest = true;
    return *this;
}

static void validateShaderBindings(const ShaderReflection & r, const std::string & name) {
    for (auto & [set, binding] : r.descriptorBindings) {
        if (set != 0) {
            throw std::runtime_error("pipeline build error: shader '" + name +
                "' references descriptor set " + std::to_string(set) + " (only set 0 allowed in bindless)");
        }
        if (binding > 2) {
            throw std::runtime_error("pipeline build error: shader '" + name +
                "' references binding " + std::to_string(binding) + " (valid: 0=storage, 1=samplers, 2=storage images)");
        }
    }
}

static void validatePushConstantLimit(const ShaderReflection & r, const std::string & name, uint32_t limit) {
    if (r.pushConstantSize > limit) {
        throw std::runtime_error("pipeline build error: shader '" + name +
            "' push constant size (" + std::to_string(r.pushConstantSize) +
            " bytes) exceeds device limit (" + std::to_string(limit) + " bytes)");
    }
}

Pipeline::~Pipeline() {
    if (pipeline != VK_NULL_HANDLE) destroyPipeline(pipeline);
}

void Pipeline::destroyPipeline(VkPipeline pipeline) {
    VulkanContext & context = g_context();
    context.pipelines.erase(pipeline);
    context.destroyGenerations[context.frameInFlightIndex].pipelines.push_back(pipeline);
}

Pipeline GraphicsPipelineBuilder::build() {
    uint32_t pcLimit = g_context().limits.maxPushConstantsSize;

    // validate all shaders
    for (auto * sm : shaderModules) {
        validateShaderBindings(sm->reflection, sm->fileName);
        validatePushConstantLimit(sm->reflection, sm->fileName, pcLimit);
    }

    // push constant size consistency
    if (shaderModules.size() >= 2) {
        uint32_t firstSize = shaderModules[0]->reflection.pushConstantSize;
        for (size_t i = 1; i < shaderModules.size(); ++i) {
            uint32_t otherSize = shaderModules[i]->reflection.pushConstantSize;
            if (otherSize != firstSize) {
                throw std::runtime_error("pipeline build error: push constant size mismatch\n"
                    "  " + shaderModules[0]->fileName + ": " + std::to_string(firstSize) + " bytes\n"
                    "  " + shaderModules[i]->fileName + ": " + std::to_string(otherSize) + " bytes");
            }
        }
    }

    // inter-stage location matching: fragment inputs must be subset of mesh outputs
    ShaderModule * meshSM = nullptr;
    ShaderModule * fragSM = nullptr;
    for (auto * sm : shaderModules) {
        if (sm->reflection.executionModel == VK_SHADER_STAGE_MESH_BIT_EXT) meshSM = sm;
        if (sm->reflection.executionModel == VK_SHADER_STAGE_FRAGMENT_BIT) fragSM = sm;
    }
    if (meshSM && fragSM) {
        for (uint32_t loc : fragSM->reflection.inputLocations) {
            if (!meshSM->reflection.outputLocations.count(loc)) {
                std::string outputs = "{";
                for (uint32_t o : meshSM->reflection.outputLocations) {
                    if (outputs.size() > 1) outputs += ", ";
                    outputs += std::to_string(o);
                }
                outputs += "}";
                throw std::runtime_error("pipeline build error: unmatched fragment input location\n"
                    "  " + fragSM->fileName + " reads location " + std::to_string(loc) + "\n"
                    "  " + meshSM->fileName + " outputs: " + outputs);
            }
        }
    }

    // depth-only: skip location validation when no fragment shader
    // (mesh shader outputs are unused by rasterizer for depth-only)

#ifndef NDEBUG
    std::cerr << "[pipeline] validation passed:";
    for (auto * sm : shaderModules) {
        std::cerr << " " << sm->fileName << "(pc=" << sm->reflection.pushConstantSize << "B)";
    }
    std::cerr << std::endl;
#endif

    VkPipelineLayout pipelineLayout = g_context().bindlessTable.pipelineLayout;

    VkViewport viewport = {};
    viewport.x = 0.0f; viewport.y = 0.0f;
    viewport.width = g_context().windowWidth;
    viewport.height = g_context().windowHeight;
    viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = VkExtent2D{(uint32_t)g_context().windowWidth, (uint32_t)g_context().windowHeight};

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1; viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1; viewportState.pScissors = &scissor;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    if (isDepthOnly) {
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = 1.25f;
        rasterizer.depthBiasSlopeFactor = 1.75f;
        rasterizer.depthBiasClamp = 0.0f;
    }

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (enableAlphaBlend) {
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        colorBlendAttachment.blendEnable = VK_FALSE;
    }

    std::vector<VkFormat> formats = colorAttachmentFormats.empty()
        ? std::vector<VkFormat>{g_context().colorFormat}
        : colorAttachmentFormats;
    uint32_t colorCount = isDepthOnly ? 0u : (uint32_t)formats.size();
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorCount, colorBlendAttachment);

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = colorCount;
    colorBlending.pAttachments = colorCount > 0 ? colorBlendAttachments.data() : nullptr;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    if (sampleCountBit == VK_SAMPLE_COUNT_1_BIT) {
        multisampling.sampleShadingEnable = VK_FALSE;
    } else if (g_context().options.shaderSampleRateShading > 0.0f) {
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.minSampleShading = 1.0f;
    }
    multisampling.rasterizationSamples = sampleCountBit;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = disableDepthTest ? VK_FALSE : VK_TRUE;
    depthStencil.depthWriteEnable = disableDepthTest ? VK_FALSE : VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    renderingInfo.colorAttachmentCount = colorCount;
    renderingInfo.pColorAttachmentFormats = colorCount > 0 ? formats.data() : nullptr;
    renderingInfo.depthAttachmentFormat = isDepthOnly ? depthOnlyFormat : depthFormat;
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.stageCount = shaderStages.size();
    pipelineCreateInfo.pStages = shaderStages.data();
    pipelineCreateInfo.pVertexInputState = nullptr;
    pipelineCreateInfo.pInputAssemblyState = nullptr;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampling;
    pipelineCreateInfo.pColorBlendState = &colorBlending;
    pipelineCreateInfo.layout = pipelineLayout;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.pDepthStencilState = &depthStencil;
    pipelineCreateInfo.pDynamicState = &dynamicState;
    pipelineCreateInfo.pNext = &renderingInfo;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(g_context().device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }
    g_context().pipelines.emplace(pipeline);
    return Pipeline(pipeline);
}

Pipeline createComputePipeline(ShaderModule & computeShaderModule, const char * entryPoint) {
    if (computeShaderModule.reflection.executionModel != VK_SHADER_STAGE_COMPUTE_BIT) {
        throw std::runtime_error("pipeline build error: shader '" + computeShaderModule.fileName +
            "' is not a compute shader (wrong execution model)");
    }
    validateShaderBindings(computeShaderModule.reflection, computeShaderModule.fileName);
    validatePushConstantLimit(computeShaderModule.reflection, computeShaderModule.fileName,
        g_context().limits.maxPushConstantsSize);

#ifndef NDEBUG
    std::cerr << "[pipeline] compute validation passed: " << computeShaderModule.fileName
        << "(pc=" << computeShaderModule.reflection.pushConstantSize << "B)" << std::endl;
#endif

    VkPipelineLayout pipelineLayout = g_context().bindlessTable.pipelineLayout;

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule.module;
    pipelineInfo.stage.pName = entryPoint;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    if (VK_SUCCESS != vkCreateComputePipelines(g_context().device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline)) {
        throw std::runtime_error("failed to create compute pipeline");
    }
    g_context().pipelines.emplace(computePipeline);
    return Pipeline(computePipeline);
}
