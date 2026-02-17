#pragma once

#include <vector>
#include <set>
#include <iostream>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <ranges>
#include <functional>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <cstdint>
#include <array>
#include <utility>

// --- Synchronization2 enum wrappers ---

enum class Stage : uint64_t {
    None        = VK_PIPELINE_STAGE_2_NONE,
    Transfer    = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    Compute     = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    Fragment    = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    MeshShader  = VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
    ColorOutput = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    EarlyFragment = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
    LateFragment  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
    AllCommands = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    AllGraphics = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    Host        = VK_PIPELINE_STAGE_2_HOST_BIT,
    DrawIndirect = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
};
inline Stage operator|(Stage a, Stage b) {
    return static_cast<Stage>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

enum class Access : uint64_t {
    None              = VK_ACCESS_2_NONE,
    ShaderRead        = VK_ACCESS_2_SHADER_READ_BIT,
    ShaderWrite       = VK_ACCESS_2_SHADER_WRITE_BIT,
    TransferRead      = VK_ACCESS_2_TRANSFER_READ_BIT,
    TransferWrite     = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    HostRead          = VK_ACCESS_2_HOST_READ_BIT,
    HostWrite         = VK_ACCESS_2_HOST_WRITE_BIT,
    MemoryRead        = VK_ACCESS_2_MEMORY_READ_BIT,
    MemoryWrite       = VK_ACCESS_2_MEMORY_WRITE_BIT,
    ColorAttachmentRead  = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
    ColorAttachmentWrite = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    DepthStencilRead     = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
    DepthStencilWrite    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    IndirectCommandRead  = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
};
inline Access operator|(Access a, Access b) {
    return static_cast<Access>(static_cast<uint64_t>(a) | static_cast<uint64_t>(b));
}

enum class Layout : int32_t {
    Undefined            = VK_IMAGE_LAYOUT_UNDEFINED,
    General              = VK_IMAGE_LAYOUT_GENERAL,
    ColorAttachment      = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    DepthStencilAttachment = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    ShaderReadOnly       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    TransferSrc          = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    TransferDst          = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    PresentSrc           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    Attachment           = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
    ReadOnly             = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
};

// --- Resource destruction ---

struct DestroyGeneration {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    std::vector<VkSampler> samplers;
    std::vector<uint32_t> storageBufferRIDs;
    std::vector<uint32_t> samplerRIDs;
    std::vector<uint32_t> storageImageRIDs;
    std::vector<VkPipeline> pipelines;
    void destroy();
    ~DestroyGeneration();
};

// --- Context ---

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

struct BindlessTable {
    static constexpr uint32_t MAX_STORAGE_BUFFERS = 16384;
    static constexpr uint32_t MAX_SAMPLERS = 16384;
    static constexpr uint32_t MAX_STORAGE_IMAGES = 4096;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    std::vector<uint32_t> freeStorageBufferIndices;
    std::vector<uint32_t> freeSamplerIndices;
    std::vector<uint32_t> freeStorageImageIndices;
    uint32_t nextStorageBufferIndex = 0;
    uint32_t nextSamplerIndex = 0;
    uint32_t nextStorageImageIndex = 0;

    void init(VkDevice device);
    void destroy(VkDevice device);

    uint32_t registerStorageBuffer(VkDevice device, VkBuffer buffer, VkDeviceSize size);
    uint32_t registerSampler(VkDevice device, VkImageView imageView, VkSampler sampler);
    uint32_t registerStorageImage(VkDevice device, VkImageView imageView);
    void releaseStorageBuffer(uint32_t index);
    void releaseSampler(uint32_t index);
    void releaseStorageImage(uint32_t index);
};

struct Commands;
struct ShaderModule;
class Pipeline;

class VulkanContext {
    friend struct Frame;
    friend struct Commands;
    friend struct Buffer;
    friend struct Image;
    friend struct ImageBuilder;
    friend struct ShaderModule;
    friend struct DestroyGeneration;
    friend struct GraphicsPipelineBuilder;
    friend class Pipeline;
    friend Pipeline createComputePipeline(ShaderModule &, const char *);
    friend void createSwapChain(VulkanContext &, VkSurfaceKHR, VkPhysicalDevice, VkDevice, VkSwapchainKHR &);
    friend VkFence createFence();
    friend VkSemaphore createSemaphore();
    friend void makeChainImageViews(VkDevice, VkFormat, std::vector<VkImage> &, std::vector<VkImageView> &);

    SDL_Window * window;
    VkInstance instance;
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    unsigned int graphicsQueueIndex;
    VkDebugReportCallbackEXT callback;
    VkSurfaceKHR presentationSurface;
    VkQueue presentationQueue;
    VkSwapchainKHR swapchain;
    VkFormat colorFormat;
    VkCommandPool commandPool;
    uint32_t maxSamples;
    VulkanContextOptions options;
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties;

    BindlessTable bindlessTable;
    std::function<void(Commands &, VkExtent2D)> resizeCallback;
    std::vector<VkCommandBuffer> frameCommandBuffers;

    size_t frameInFlightIndex;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> submittedBuffersFinishedFences;

    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    std::vector<VkSemaphore> semaphores;
    std::vector<VkFence> fences;
    std::set<VkPipeline> pipelines;

    std::vector<DestroyGeneration> destroyGenerations;

public:
    size_t windowWidth;
    size_t windowHeight;
    size_t swapchainImageCount;
    VkQueue graphicsQueue;

    VulkanContext(SDL_Window * window, VulkanContextOptions options);
    ~VulkanContext();
    VulkanContext & operator=(const VulkanContext & other) = delete;
    VulkanContext(const VulkanContext & other) = delete;
    VulkanContext(VulkanContext && other) = delete;

    void onSwapchainResize(std::function<void(Commands &, VkExtent2D)> callback);
};

struct VulkanContextSingleton {
    VulkanContext * contextInstance;
    VulkanContext& operator()();
};

extern VulkanContextSingleton g_context;

// --- Shaders ---

struct ShaderBuilder {
    VkShaderStageFlagBits stage;
    std::vector<uint8_t> code;
    std::string fileName;
    ShaderBuilder();
    ShaderBuilder& fragment();
    ShaderBuilder& compute();
    ShaderBuilder& mesh();
    ShaderBuilder& fromFile(const char * fileName);
    ShaderBuilder& fromBuffer(const uint8_t * data, size_t size);
};

struct ShaderReflection {
    uint32_t pushConstantSize = 0;
    std::array<uint32_t, 3> localSize = {0, 0, 0};
    uint32_t maxVertices = 0;
    uint32_t maxPrimitives = 0;
    std::set<uint32_t> inputLocations;
    std::set<uint32_t> outputLocations;
    std::set<std::pair<uint32_t, uint32_t>> descriptorBindings;
    VkShaderStageFlagBits executionModel = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
};

struct ShaderModule {
    VkShaderModule module;
    ShaderReflection reflection;
    std::string fileName;
    ShaderModule(ShaderBuilder & builder);
    ~ShaderModule();
    operator VkShaderModule() const;
};

// --- Buffers ---

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

class Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size;
    uint32_t rid_;

public:
    uint32_t rid() const;
    void upload(void * bytes, size_t size);
    void upload(void * bytes, size_t size, VkDeviceSize offset);
    void download(void * bytes, size_t size);
    Buffer(BufferBuilder & builder);
    Buffer(Buffer && other);
    ~Buffer();
    operator VkBuffer() const;
    operator VkBuffer*() const;
};

// --- Images ---

struct ImageBuilder {
    bool buildMipmaps;
    void * bytes;
    Buffer * stagingBuffer;
    VkFormat format;
    VkExtent2D extent;
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

class Image {
    VkImage image;
    VkDeviceMemory memory;
    VkSampler sampler;
    uint32_t rid_;
    bool isStorageImage;

public:
    VkImageView imageView;

    uint32_t rid() const;
    Image(Image && other);
    Image(ImageBuilder & builder, Commands & commands);
    operator VkImage() const;
    ~Image();
};

// --- Barrier ---

struct Barrier {
    VkCommandBuffer commandBuffer;
    bool hasBuffer;
    bool hasImage;
    VkBufferMemoryBarrier2 bufBarrier;
    VkImageMemoryBarrier2 imgBarrier;

    Barrier(VkCommandBuffer cmd);
    Barrier & buffer(VkBuffer buf);
    Barrier & image(VkImage img, uint32_t mipLevels = 1);
    Barrier & from(Stage stage, Access access);
    Barrier & from(Stage stage, Access access, Layout layout);
    Barrier & to(Stage stage, Access access);
    Barrier & to(Stage stage, Access access, Layout layout);
    Barrier & aspectMask(VkImageAspectFlags aspects);
    void record();
};

// --- Frame & Commands ---

class Frame {
    VulkanContext & context;
    size_t inFlightIndex;
    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence submittedBuffersFinishedFence;
    uint32_t imageIndex;
    bool submitted;

    static Frame * currentGuard;
    friend class VulkanContext;

public:
    Frame();
    ~Frame();

    uint32_t swapchainImageIndex() const;
    VkImageView swapchainImageView() const;
    Commands beginCommands();
    void submit(Commands & cmd);
};

class Commands {
    VkCommandBuffer commandBuffer;
    bool ended;
    bool ownsBuffer;
    Frame * frame;

    Commands(VkCommandBuffer cmd, bool owns = false);
    friend class Frame;
    friend struct Image;

public:
    Commands(Commands && other);
    Commands(const Commands &) = delete;
    Commands & operator=(const Commands &) = delete;
    ~Commands();

    static Commands oneShot();

    void bindCompute(VkPipeline pipeline);
    void bindGraphics(VkPipeline pipeline);
    void dispatch(uint32_t x, uint32_t y, uint32_t z);
    void dispatchIndirect(VkBuffer buffer, VkDeviceSize offset = 0);
    void drawMeshTasks(uint32_t x, uint32_t y, uint32_t z);
    void drawMeshTasksIndirect(VkBuffer buffer, uint32_t drawCount, VkDeviceSize offset = 0, uint32_t stride = 12);
    void pushConstants(const void * data, uint32_t size);
    void beginRendering(VkImageView depthImage);
    void beginRendering(VkImageView colorImage, VkImageView depthImage, VkExtent2D extent);
    void endRendering();
    void setViewport(float x, float y, float width, float height);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void bufferBarrier(VkBuffer buffer, Stage srcStage, Stage dstStage);
    void imageBarrier(VkImage image, Stage srcStage, Access srcAccess, Layout oldLayout,
                      Stage dstStage, Access dstAccess, Layout newLayout, uint32_t mipLevels = 1);
    void submitAndWait();
    void end();

    operator VkCommandBuffer();
};

// --- Pipelines ---

class Pipeline {
    VkPipeline pipeline;
public:
    Pipeline() : pipeline(VK_NULL_HANDLE) {}
    explicit Pipeline(VkPipeline pipeline) : pipeline(pipeline) {}
    Pipeline(Pipeline && other) : pipeline(other.pipeline) { other.pipeline = VK_NULL_HANDLE; }
    Pipeline & operator=(Pipeline && other) {
        if (this != &other) {
            if (pipeline != VK_NULL_HANDLE) destroyPipeline(pipeline);
            pipeline = other.pipeline;
            other.pipeline = VK_NULL_HANDLE;
        }
        return *this;
    }
    Pipeline(const Pipeline &) = delete;
    Pipeline & operator=(const Pipeline &) = delete;
    ~Pipeline();
    operator VkPipeline() const { return pipeline; }

private:
    static void destroyPipeline(VkPipeline pipeline);
};

struct GraphicsPipelineBuilder {
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    std::vector<ShaderModule *> shaderModules;
    VkSampleCountFlagBits sampleCountBit;
    GraphicsPipelineBuilder();
    GraphicsPipelineBuilder & meshShader(ShaderModule & meshShaderModule, const char * entryPoint = "main");
    GraphicsPipelineBuilder & fragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint = "main");
    GraphicsPipelineBuilder & sampleCount(size_t sampleCount);
    Pipeline build();
};

Pipeline createComputePipeline(ShaderModule & computeShaderModule, const char * entryPoint = "main");
