#pragma once

#include <vector>
#include <set>
#include <iostream>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <ranges>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <cstdint>

// A set of resources scheduled for destruction.
struct DestroyGeneration {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    std::vector<VkCommandBuffer> commandBuffers;
    void destroy();
    ~DestroyGeneration();
};

// function pointers for extensions
extern PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasks;
extern PFN_vkCmdBeginRendering vkBeginRendering;
extern PFN_vkCmdEndRendering vkEndRendering;

struct VulkanContextOptions {
    bool enableMultisampling;
    uint32_t multisampleCount;
    bool enableMeshShaders;
    bool enableValidationLayers;
    float shaderSampleRateShading;
    bool enableThrowOnValidationError;
    VulkanContextOptions();
    VulkanContextOptions & multisample(uint32_t count);
    VulkanContextOptions & meshShaders();
    VulkanContextOptions & validation();
    VulkanContextOptions & sampleRateShading(float rate);
    VulkanContextOptions & throwOnValidationError();
};

struct VulkanContext {
    // things that will not change during the context lifetime
    SDL_Window * window;
    size_t windowWidth;
    size_t windowHeight;
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    unsigned int graphicsQueueIndex;
    VkDebugReportCallbackEXT callback;
    VkSurfaceKHR presentationSurface;
    VkQueue presentationQueue;
    VkSwapchainKHR swapchain;
    VkFormat colorFormat;
    size_t swapchainImageCount;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;
    uint32_t maxSamples;
    VulkanContextOptions options;
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties;
    uint32_t nextResourceIndex;

    // things that will change during the context lifetime
    size_t frameInFlightIndex;

    // presentation loop sync primitives
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> submittedBuffersFinishedFences;

    // presentation loop resources that may need to be rebuilt
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    // managed resource collections that will be auto-cleaned when the context is destroyed
    std::vector<VkSemaphore> semaphores;
    std::vector<VkFence> fences;
    std::set<VkDescriptorSetLayout> layouts;
    std::set<VkPipelineLayout> pipelineLayouts;
    std::set<VkPipeline> pipelines;

    // managed resource collections that will be auto-cleaned after swapchain frames have passed
    std::vector<DestroyGeneration> destroyGenerations;
    VulkanContext(SDL_Window * window, VulkanContextOptions & options);
    ~VulkanContext();
    VulkanContext & operator=(const VulkanContext & other) = delete;
    VulkanContext(const VulkanContext & other) = delete;
    VulkanContext(VulkanContext && other) = delete; 

    // What's NOT automatically created and why not?
/*
    Command Buffers
    We need one per swapchain image.  The program may want static ones, or more 
    with complex semaphore dependencies, or others that are running concurrently.  All are outside the context.

    Pipelines
    Pipelines configuration will be unique to your program.  There's no one-size-fits-all or we'd have the OpenGL fixed-function pipeline!
*/
};

struct VulkanContextSingleton {
    VulkanContext * contextInstance;
    VulkanContext& operator()();
};

// Declare the global instance as extern
extern VulkanContextSingleton g_context;

struct ShaderBuilder {
    VkShaderStageFlagBits stage;
    std::vector<uint8_t> code;
    ShaderBuilder();
    ShaderBuilder& vertex();
    ShaderBuilder& fragment();
    ShaderBuilder& compute();
    ShaderBuilder& mesh();
    ShaderBuilder& fromFile(const char * fileName);
    ShaderBuilder& fromBuffer(const uint8_t * data, size_t size);
};

struct ShaderModule {
    VkShaderModule module;
    ShaderModule(ShaderBuilder & builder);
    ~ShaderModule();
    operator VkShaderModule() const;
};

struct BufferBuilder {
    VkBufferUsageFlags usage;
    VkMemoryPropertyFlags properties;
    size_t byteCount;

    BufferBuilder(size_t byteCount);
    BufferBuilder & index();
    BufferBuilder & uniform();
    BufferBuilder & storage();
    BufferBuilder & indirect();
    BufferBuilder & hostCoherent();
    BufferBuilder & hostVisible();
    BufferBuilder & deviceLocal();
    BufferBuilder & transferSource();
    BufferBuilder & transferDestination();
    BufferBuilder & size(size_t byteCount);
};

struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size;

    void setData(void * bytes, size_t size);
    void setData(void * bytes, size_t size, VkDeviceSize offset);
    void getData(void * bytes, size_t size);
    Buffer(BufferBuilder & builder);
    Buffer(Buffer && other);
    ~Buffer();
    operator VkBuffer() const;
    operator VkBuffer*() const;
};

struct ImageBuilder {
    bool buildMipmaps;
    void * bytes;
    Buffer * stagingBuffer;
    VkFormat format;
    VkExtent2D extent; // width and height
    bool isDepthBuffer;
    VkSampleCountFlagBits sampleBits;
    VkImageUsageFlags usage;
    ImageBuilder();
    ImageBuilder & createMipmaps(bool buildMipmaps);
    ImageBuilder & depth();
    ImageBuilder & fromStagingBuffer(Buffer & stagingBuffer, int width, int height, VkFormat format);
    ImageBuilder & color();
    ImageBuilder & multisample();
    ImageBuilder & storage();
    ImageBuilder & withFormat(VkFormat format);
};

struct Image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    Image(Image && other);
    Image(ImageBuilder & builder, VkCommandBuffer commandBuffer);
    operator VkImage() const;
    ~Image();
};

struct ImageTransition {
    VkImageMemoryBarrier barrier;
    VkPipelineStageFlags srcStageFlags, dstStageFlags;
    ImageTransition(VkImage image, size_t mipLevels, VkImageLayout oldLayout, VkImageLayout newLayout);
    ImageTransition & srcStages(VkPipelineStageFlags stage);
    ImageTransition & dstStages(VkPipelineStageFlags stage);
    ImageTransition & srcAccess(VkAccessFlags access);
    ImageTransition & dstAccess(VkAccessFlags access);
    ImageTransition & aspectMask(VkImageAspectFlags aspectMask);
    ImageTransition & record(VkCommandBuffer commandBuffer);
};

struct TextureSampler {
    VkSampler sampler;
    TextureSampler();
    ~TextureSampler();
    operator VkSampler() const;
};

struct CommandBuffer {
    VkCommandBuffer buffer;
    CommandBuffer();
    ~CommandBuffer();
    void reset();
    operator VkCommandBuffer() const;
};

struct PushConstantsBuilder {
    std::vector<VkPushConstantRange> ranges;
    VkShaderStageFlags currentBits;
    PushConstantsBuilder();
    PushConstantsBuilder & addRange(size_t offset, size_t size, VkShaderStageFlags stageBits);
    operator std::vector<VkPushConstantRange> & ();
};

struct DescriptorLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    DescriptorLayoutBuilder();
    void addDescriptor(uint32_t binding, uint32_t count, VkShaderStageFlags stages, VkDescriptorType type);
    DescriptorLayoutBuilder & addStorageBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    DescriptorLayoutBuilder & addSampler(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    DescriptorLayoutBuilder & addUniformBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    DescriptorLayoutBuilder & addStorageImage(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    DescriptorLayoutBuilder & addDynamicStorageBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    DescriptorLayoutBuilder & addDynamicUniformBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages);
    VkDescriptorSetLayout build();
    void reset();
    void throwIfDuplicate(uint32_t binding);
};

struct DescriptorPool {
    VkDescriptorPool pool;
    DescriptorPool(struct DescriptorPoolBuilder & builder);
    void reset();
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    ~DescriptorPool();
};

struct DescriptorPoolBuilder {
    std::vector<VkDescriptorPoolSize> sizes;
    uint32_t _maxDescriptorSets;
    DescriptorPoolBuilder();
    DescriptorPoolBuilder & addStorageBuffer(uint32_t count);
    DescriptorPoolBuilder & addSampler(uint32_t count);
    DescriptorPoolBuilder & addUniformBuffer(uint32_t count);
    DescriptorPoolBuilder & addStorageImage(uint32_t count);
    DescriptorPoolBuilder & addDynamicStorageBuffer(uint32_t count);
    DescriptorPoolBuilder & addDynamicUniformBuffer(uint32_t count);
    DescriptorPoolBuilder & maxSets(uint32_t count);
    DescriptorPool build();
};

struct DescriptorSetBinder {
    VkDescriptorSet descriptorSet;
    std::vector<VkWriteDescriptorSet> descriptorWriteSets;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    DescriptorSetBinder(VkDescriptorSet descriptorSet);
    DescriptorSetBinder & bindSampler(uint32_t bindingIndex, TextureSampler & sampler, Image & image);
    DescriptorSetBinder & bindBuffer(uint32_t bindingIndex, const Buffer & buffer, VkDescriptorType descriptorType, VkDeviceSize offset = 0, VkDeviceSize deviceSize = VK_WHOLE_SIZE);
    DescriptorSetBinder & bindUniformBuffer(uint32_t bindingIndex, const Buffer & buffer);
    DescriptorSetBinder & bindStorageBuffer(uint32_t bindingIndex, const Buffer & buffer);
    DescriptorSetBinder & bindStorageBuffer(uint32_t bindingIndex, const Buffer & buffer, size_t size);
    DescriptorSetBinder & bindDynamicStorageBuffer(uint32_t bindingIndex, const Buffer & buffer, size_t offset, VkDeviceSize size);
    DescriptorSetBinder & bindDynamicUniformBuffer(uint32_t bindingIndex, const Buffer & buffer, size_t offset, VkDeviceSize size);
    DescriptorSetBinder & bindStorageImage(uint32_t bindingIndex, Image & image);
    void updateSets();
};

void advancePostFrame(VulkanContext & context);

// Help to advance the frame and do post-frame generational resource cleanup scheduling.
// This class does too much and its methods MUST be called in order to work properly. :(
struct Frame {
    VulkanContext & context;
    bool preparedOldResources;
    bool cleanedup;
    size_t inFlightIndex;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence submittedBuffersFinishedFence;
    int nextImageIndex;
    static const int UnacquiredIndex = -1;

    static Frame * currentGuard;

    Frame();
    ~Frame();
    void prepareOldestFrameResources();
    void acquireNextImageIndex(uint32_t & nextImage, VkSemaphore & renderFinishedSemaphore);
    uint32_t acquireNextImageIndex();
    void submitCommandBuffer(VkCommandBuffer commandBuffer);
    void submitCommandBuffer(VkCommandBuffer commandBuffer, std::vector<VkSemaphore> & additionalWaitSemaphores, std::vector<VkSemaphore> & additionalSignalSemaphores);
    bool tryPresentQueue();
    void cleanup();
};

VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout> & descriptorSetLayouts, const std::vector<VkPushConstantRange> & pushConstantRanges);
VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout> & descriptorSetLayouts);

struct GraphicsPipelineBuilder {
    VkPipelineLayout pipelineLayout;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    VkSampleCountFlagBits sampleCountBit;
    GraphicsPipelineBuilder(VkPipelineLayout layout);
    GraphicsPipelineBuilder & addMeshShader(ShaderModule & meshShaderModule, const char * entryPoint = "main");
    GraphicsPipelineBuilder & addFragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint = "main");
    GraphicsPipelineBuilder & sampleCount(size_t sampleCount);
    VkPipeline build();
};

VkPipeline createComputePipeline(VkPipelineLayout pipelineLayout, VkShaderModule computeShaderModule, const char * entryPoint = "main");

struct MultisampleRenderingRecording {
    VkCommandBuffer commandBuffer;
    MultisampleRenderingRecording(
        VkCommandBuffer commandBuffer,
        VkImageView multisampleColor,
        VkImageView multisampleResolveImage,
        VkImageView depthImage);
    ~MultisampleRenderingRecording();
};

struct RenderingRecording {
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    VkRenderingAttachmentInfo depthAttachment;
    VkCommandBuffer commandBuffer;
    void init(std::vector<VkImageView> & colorImages, VkImageView depthImage);
    RenderingRecording(VkCommandBuffer commandBuffer, std::vector<VkImageView> & colorImages, VkImageView depthImage);
    RenderingRecording(VkCommandBuffer commandBuffer, VkImageView colorImage, VkImageView depthImage);
    ~RenderingRecording();
};

struct CommandBufferRecording {
    VkCommandBuffer commandBuffer;
    CommandBufferRecording(VkCommandBuffer commandBuffer);
    operator VkCommandBuffer();
    ~CommandBufferRecording();
};

struct BufferBarrier {
    VkBufferMemoryBarrier barrier;
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
    bool toIndirect;
    VkCommandBuffer commandBuffer;
    BufferBarrier(VkCommandBuffer commandBuffer);
    BufferBarrier & buffer(VkBuffer buffer);
    BufferBarrier & fromHost();
    BufferBarrier & toHost();
    BufferBarrier & fromCompute();
    BufferBarrier & toCompute();
    BufferBarrier & toFragment();
    BufferBarrier & indirect();
    void command();
};

struct ImageBarrier {
    VkImageMemoryBarrier barrier;
    VkPipelineStageFlags srcStageFlags;
    VkPipelineStageFlags dstStageFlags;
    VkCommandBuffer commandBuffer;
    ImageBarrier(VkCommandBuffer commandBuffer);
    ImageBarrier & fromLayout(VkImageLayout oldLayout);
    ImageBarrier & toLayout(VkImageLayout newLayout);
    ImageBarrier & image(VkImage image, size_t mipLevels);
    ImageBarrier & srcAccess(VkAccessFlags access);
    ImageBarrier & dstAccess(VkAccessFlags access);
    ImageBarrier & srcStage(VkPipelineStageFlags stage);
    ImageBarrier & dstStage(VkPipelineStageFlags stage);
    void command();
};

// a helper to start and end a command buffer which can be submitted and waited
struct ScopedCommandBuffer {
    VkCommandBuffer commandBuffer;
    ScopedCommandBuffer();
    ScopedCommandBuffer(VkDevice device, VkCommandPool commandPool);
    void submit();
    void reset();
    void submitAndWait();
    operator VkCommandBuffer();
    ~ScopedCommandBuffer();
};

void rebuildPresentationResources(VkCommandBuffer commandBuffer);