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
#include <type_traits>
#include <utility>
#include <span>
#include <string>
#include <memory>
#include <cassert>

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
    AccelStructureBuild = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
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
    AccelStructureRead   = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
    AccelStructureWrite  = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
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
    DepthReadOnly        = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
};

// --- Bindless resource IDs ---

#include "rid.h"

// --- Push constant compile-time validation ---

// CRTP base for push constant structs. Inherit to enforce the 128-byte Vulkan
// guaranteed minimum at compile time:
//
//   struct MyPush : PushConstantBase<MyPush> {
//       uint32_t verticesRID;
//       uint32_t textureRID;
//   };
//
// The static_assert fires when the struct is instantiated, not just defined.
template<typename T>
struct PushConstantBase {
    ~PushConstantBase() {
        static_assert(sizeof(T) <= 128, "Push constants exceed 128-byte Vulkan guaranteed minimum");
    }
};

// --- VMA forward declaration ---
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

// --- Resource destruction ---

struct DestroyGeneration {
    std::vector<std::pair<VkBuffer, VmaAllocation>> bufferAllocations;
    std::vector<std::pair<VkImage, VmaAllocation>> imageAllocations;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkImageView> imageViews;
    std::vector<VkSampler> samplers;
    std::vector<VkAccelerationStructureKHR> accelStructures;
    std::vector<uint32_t> storageBufferRIDs;
    std::vector<uint32_t> samplerRIDs;
    std::vector<uint32_t> storageImageRIDs;
    std::vector<uint32_t> tlasRIDs;
    std::vector<VkPipeline> pipelines;
    void destroy();
    ~DestroyGeneration();
};

// --- Context ---

enum class ValidationSeverity { Info, Warning, Error };

struct VulkanContextOptions {
    bool enableMultisampling;
    uint32_t multisampleCount;
    bool enableMeshShaders;
    bool enableValidationLayers;
    float shaderSampleRateShading;
    bool enableThrowOnValidationError;
    bool enableVerbose;
    bool enableGpuAssistedValidation;
    bool enableImmediateDestroy;
    bool enableRayTracing;
    std::string pipelineCacheDir;
    std::function<void(ValidationSeverity, const char*)> validationCallback;
    VulkanContextOptions();
    VulkanContextOptions & multisample(uint32_t count);
    VulkanContextOptions & meshShaders();
    VulkanContextOptions & rayTracing();
    VulkanContextOptions & validation();
    VulkanContextOptions & sampleRateShading(float rate);
    VulkanContextOptions & throwOnValidationError();
    VulkanContextOptions & verbose();
    VulkanContextOptions & gpuAssistedValidation(bool enable = true);
    VulkanContextOptions & immediateDestroy(bool v = true);
    VulkanContextOptions & pipelineCache(const std::string & dir);
};

struct BindlessTable {
    static constexpr uint32_t MAX_STORAGE_BUFFERS = 16384;
    static constexpr uint32_t MAX_SAMPLERS = 16384;
    static constexpr uint32_t MAX_STORAGE_IMAGES = 4096;
    static constexpr uint32_t MAX_TLAS = 16;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    std::vector<uint32_t> freeStorageBufferIndices;
    std::vector<uint32_t> freeSamplerIndices;
    std::vector<uint32_t> freeStorageImageIndices;
    std::vector<uint32_t> freeTlasIndices;
    uint32_t nextStorageBufferIndex = 0;
    uint32_t nextSamplerIndex = 0;
    uint32_t nextStorageImageIndex = 0;
    uint32_t nextTlasIndex = 0;
    bool tlasEnabled = false;

    void init(VkDevice device, uint32_t maxPushConstantSize = 128, bool enableTlas = false);
    void destroy(VkDevice device);

    uint32_t registerStorageBuffer(VkDevice device, VkBuffer buffer, VkDeviceSize size);
    uint32_t registerSampler(VkDevice device, VkImageView imageView, VkSampler sampler);
    uint32_t registerStorageImage(VkDevice device, VkImageView imageView);
    uint32_t registerTlas(VkDevice device, VkAccelerationStructureKHR tlas);
    void releaseStorageBuffer(uint32_t index);
    void releaseSampler(uint32_t index);
    void releaseStorageImage(uint32_t index);
    void releaseTlas(uint32_t index);
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
    friend class Blas;
    friend class Tlas;
    friend struct ShaderModule;
    friend struct DestroyGeneration;
    friend struct GraphicsPipelineBuilder;
    friend class Pipeline;
    friend class TimestampQuery;
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
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR presentationSurface;
    VkQueue presentationQueue;
    VkSwapchainKHR swapchain;
    VkFormat colorFormat;
    VkCommandPool commandPool;
    uint32_t maxSamples;
    VulkanContextOptions options;
    VkPhysicalDeviceLimits limits;
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties;
    uint32_t minAccelerationStructureScratchOffsetAlignment = 1;

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
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    std::vector<DestroyGeneration> destroyGenerations;

    std::vector<std::function<void()>> preDestroyCallbacks;

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
    void waitIdle();
    void flushDestroys();

    // Register a callback to run at the very start of ~VulkanContext, before the device,
    // allocator, and pipelines are torn down. Use for releasing long-lived caches that own
    // GPU resources (buffers/pipelines) with no natural scope, so their VMA allocations are
    // freed before vmaDestroyAllocator. Callbacks run in registration order.
    void onPreDestroy(std::function<void()> callback);

    VkDevice deviceHandle() const { return device; }
    VkPhysicalDevice physicalDeviceHandle() const { return physicalDevice; }

    VkDescriptorSetLayout bindlessSetLayout() const { return bindlessTable.layout; }
    VkDescriptorSet bindlessDescriptorSet() const { return bindlessTable.set; }
    uint32_t accelerationStructureScratchAlignment() const { return minAccelerationStructureScratchOffsetAlignment; }
    bool rayTracingEnabled() const { return options.enableRayTracing; }
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
    bool isReadback = false;

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
    BufferBuilder & readback();
    BufferBuilder & size(size_t byteCount);
    BufferBuilder & deviceAddress();
    BufferBuilder & accelerationStructureInput();
    BufferBuilder & accelerationStructureStorage();
};

class Buffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    size_t size;
    uint32_t rid_;

public:
    uint32_t rid() const;
    size_t byteSize() const;
    bool isHostVisible() const;
    void upload(void * bytes, size_t size);
    void upload(void * bytes, size_t size, VkDeviceSize offset);
    void download(void * bytes, size_t size);
    VkDeviceAddress deviceAddress() const;
    Buffer(BufferBuilder & builder);
    Buffer(Buffer && other);
    ~Buffer();
    operator VkBuffer() const;
    operator VkBuffer*() const;
};

// --- Acceleration structures ---

// One triangle geometry in a BLAS. Fluent builder over a VkAccelerationStructureGeometryKHR: the
// constructor takes the vertex Buffer (for its device address) and sane triangle defaults
// (R32G32B32 positions, 12-byte stride, non-indexed); the setters override only what differs.
class BlasGeometry {
    friend class Blas;
    friend struct Commands;
    VkAccelerationStructureGeometryKHR geom_{};
    uint32_t primitiveCount_ = 0;

public:
    explicit BlasGeometry(Buffer& vertices);
    BlasGeometry& vertexCount(uint32_t count);          // sets maxVertex = count - 1
    BlasGeometry& vertexStrideBytes(uint32_t stride);
    BlasGeometry& vertexFormat(VkFormat format);
    BlasGeometry& indexBuffer(Buffer& indices, VkIndexType type = VK_INDEX_TYPE_UINT32);
    BlasGeometry& triangleCount(uint32_t count);
    BlasGeometry& opaque(bool value = true);
};

struct BlasBuilder {
    std::vector<BlasGeometry> geoms;
    bool allowUpdate = false;
    bool fastTrace = true;

    BlasBuilder& addGeometry(const BlasGeometry&);
    BlasBuilder& refittable();
    BlasBuilder& fastBuild();
};

class Blas {
    friend struct Commands;
    std::unique_ptr<Buffer> backing_, scratch_, updateScratch_;
    std::vector<BlasGeometry> geometry_;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress address_ = 0;
    VkDeviceAddress scratchAddress_ = 0;
    VkDeviceAddress updateScratchAddress_ = 0;
    bool updatable_ = false;
    bool built_ = false;
    VkBuildAccelerationStructureFlagsKHR flags_ = 0;

    void destroyHandle();

public:
    Blas(BlasBuilder&);
    Blas(Blas&& other) noexcept;
    Blas& operator=(Blas&& other) noexcept;
    Blas(const Blas&) = delete;
    Blas& operator=(const Blas&) = delete;
    ~Blas();

    VkDeviceAddress address() const;
    Buffer& backing();
    bool updatable() const;
    operator VkAccelerationStructureKHR() const;
};

struct TlasInstances {
    std::vector<VkAccelerationStructureInstanceKHR> raw;
    TlasInstances& add(const Blas&, uint32_t customIndex, uint8_t mask = 0xFF);  // identity transform
    TlasInstances& add(const Blas&, const float (&xform3x4)[12], uint32_t customIndex, uint8_t mask = 0xFF);
    void clear();
};

class Tlas {
    friend struct Commands;
    std::unique_ptr<Buffer> backing_, scratch_, instanceBuf_;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkDeviceAddress scratchAddress_ = 0;
    uint32_t rid_ = kNullRid;
    uint32_t maxInstances_ = 0;

    void destroyHandle();

public:
    explicit Tlas(uint32_t maxInstances);
    Tlas(Tlas&& other) noexcept;
    Tlas& operator=(Tlas&& other) noexcept;
    Tlas(const Tlas&) = delete;
    Tlas& operator=(const Tlas&) = delete;
    ~Tlas();

    VkAccelerationStructureKHR handle() const;
    Buffer& backing();
    uint32_t rid() const;
    // RID of the host-visible instance-descriptor buffer that buildTlas() uploads into each call.
    // A ringed Tlas (one slot per frame in flight) lets the owner mark this buffer as ring-safe so
    // the frame-write audit does not flag the legitimate per-frame rebuild.
    uint32_t instanceBufferRid() const;
    operator VkAccelerationStructureKHR() const;
};

template<class Payload>
class InstanceTable {
    static_assert(std::is_trivially_copyable_v<Payload>, "InstanceTable payload must be trivially copyable");
    std::unique_ptr<Buffer> buf_;
    std::vector<Payload> data_;
    uint32_t next_ = 0;

public:
    explicit InstanceTable(uint32_t maxEntries) : data_(maxEntries) {
        assert(maxEntries <= (1u << 24));
        BufferBuilder builder(sizeof(Payload) * maxEntries);
        builder.storage().hostVisible();
        buf_ = std::make_unique<Buffer>(builder);
    }

    uint32_t add(const Payload& payload) {
        assert(next_ < data_.size());
        uint32_t index = next_++;
        data_[index] = payload;
        return index;
    }

    void set(uint32_t customIndex, const Payload& payload) {
        assert(customIndex < data_.size());
        data_[customIndex] = payload;
        if (customIndex >= next_) next_ = customIndex + 1;
    }

    void upload(Commands&) {
        if (!data_.empty()) buf_->upload(data_.data(), data_.size() * sizeof(Payload));
    }

    uint32_t rid() const { return buf_->rid(); }
};

// Debug instrumentation: invoked on every Buffer::upload with the buffer's RID. The
// frame-in-flight write audit (hull-side) uses this to detect single-buffered per-frame
// host writes. No-op unless set; pass nullptr to clear.
using BufferWriteHook = std::function<void(uint32_t rid)>;
void setBufferWriteHook(BufferWriteHook hook);

// --- Images ---

struct ImageBuilder {
    bool buildMipmaps;
    void * bytes;
    Buffer * stagingBuffer;
    VkFormat format;
    VkExtent2D extent;
    bool isDepthBuffer;
    bool isDepthSampled;
    bool isColorTarget;
    bool useNearest;
    bool isCube = false;
    bool isSampledStorage = false;
    uint32_t mipLevelsOverride = 0;
    VkSampleCountFlagBits sampleBits;
    VkImageUsageFlags usage;
    ImageBuilder();
    ImageBuilder & createMipmaps(bool buildMipmaps);
    ImageBuilder & depth();
    ImageBuilder & depthSampled(uint32_t width, uint32_t height);
    ImageBuilder & colorTarget(uint32_t width, uint32_t height);
    ImageBuilder & colorTarget(uint32_t width, uint32_t height, VkFormat format);
    ImageBuilder & fromStagingBuffer(Buffer & stagingBuffer, int width, int height, VkFormat format);
    ImageBuilder & color();
    ImageBuilder & multisample();
    ImageBuilder & storage();
    ImageBuilder & nearest();
    ImageBuilder & withFormat(VkFormat format);
    ImageBuilder & cube(uint32_t edge);
    ImageBuilder & mipLevels(uint32_t n);
    ImageBuilder & size(uint32_t width, uint32_t height);
    // 2D image that is BOTH sampled (primary rid()) and writable via
    // createStorageView(0, 0). Used for the IBL BRDF LUT and similar.
    ImageBuilder & sampledStorage();
};

class Image {
    VkImage image;
    VmaAllocation allocation;
    VkSampler sampler;
    uint32_t rid_;
    bool isStorageImage;
    bool isCube_ = false;
    uint32_t mipLevels_ = 1;
    VkFormat format_ = VK_FORMAT_UNDEFINED;

public:
    VkImageView imageView;

    uint32_t rid() const;
    bool isCube() const;
    uint32_t mipLevelCount() const;
    Image(Image && other);
    Image(ImageBuilder & builder, Commands & commands);
    operator VkImage() const;
    ~Image();

    // Create a storage-image view for a single face × single mip of this
    // image (cube or 2D-array) and register it as a bindless storage
    // image. Returns { rid, view }. Caller owns destruction (use
    // destroyStorageView). Only valid when the image was created with
    // storage usage.
    struct StorageView { uint32_t rid; VkImageView view; };
    StorageView createStorageView(uint32_t face, uint32_t mip);
    static void destroyStorageView(StorageView view);
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
    Barrier & image(VkImage img, uint32_t mipLevels = 1, uint32_t layerCount = 1);
    Barrier & from(Stage stage, Access access);
    Barrier & from(Stage stage, Access access, Layout layout);
    Barrier & to(Stage stage, Access access);
    Barrier & to(Stage stage, Access access, Layout layout);
    Barrier & aspectMask(VkImageAspectFlags aspects);
    void record();
};

// One buffer memory dependency, for batching several into a single vkCmdPipelineBarrier2.
struct BufferBarrierDesc {
    VkBuffer buffer;
    Stage srcStage;
    Access srcAccess;
    Stage dstStage;
    Access dstAccess;
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

    // Frame-in-flight slot this frame occupies (0 .. swapchainImageCount-1). The coordinate
    // any per-frame-mutable resource ring must index by.
    size_t inFlight() const { return inFlightIndex; }
    // The live frame, or nullptr outside a Frame's scope (e.g. setup, oneShot work).
    static Frame * current() { return currentGuard; }
};

template<class T>
class AccelStructureRing {
    std::vector<T> slots_;

public:
    void init(uint32_t n, const std::function<T(uint32_t slot)>& makeSlot) {
        slots_.clear();
        slots_.reserve(n);
        for (uint32_t i = 0; i < n; ++i) slots_.emplace_back(makeSlot(i));
    }

    T& current() {
        assert(!slots_.empty());
        Frame* frame = Frame::current();
        uint32_t slot = frame ? static_cast<uint32_t>(frame->inFlight()) : 0;
        return slots_[slot % slots_.size()];
    }

    T& at(uint32_t i) {
        assert(i < slots_.size());
        return slots_[i];
    }

    uint32_t size() const { return static_cast<uint32_t>(slots_.size()); }
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
    template<typename T>
    void pushConstants(const T & data) {
        static_assert(sizeof(T) <= 128, "Push constants exceed 128-byte Vulkan guaranteed minimum");
        pushConstants(&data, sizeof(T));
    }
    void beginRendering();
    void beginRendering(float r, float g, float b, float a);
    void resumeRendering();
    void beginRenderingOffscreen(VkImageView colorImage, VkExtent2D extent);
    void beginRendering(VkImageView depthImage);
    void beginRendering(VkImageView depthImage, VkExtent2D extent);
    void beginRendering(VkImageView colorImage, VkImageView depthImage, VkExtent2D extent);
    void beginRendering(std::span<const VkImageView> colorImages, VkImageView depthImage, VkExtent2D extent);
    void endRendering();
    void setViewport(float x, float y, float width, float height);
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);
    void fillBuffer(VkBuffer buffer, uint32_t value, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size, VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);
    void bufferBarrier(VkBuffer buffer, Stage srcStage, Stage dstStage);
    void bufferBarrier(VkBuffer buffer, Stage srcStage, Access srcAccess,
                       Stage dstStage, Access dstAccess);
    // Records all given buffer memory dependencies in a single vkCmdPipelineBarrier2.
    // Semantically equivalent to issuing each bufferBarrier separately (each retains its own
    // per-buffer access scopes); only the command/barrier count is reduced.
    void bufferBarriers(std::span<const BufferBarrierDesc> barriers);
    void blasToTlasBarrier(std::span<const VkBuffer> blasBackings);
    void tlasToShaderReadBarrier(VkBuffer tlasBacking);
    void buildBlas(Blas&, bool refit);
    void buildTlas(Tlas&, const TlasInstances&);
    void imageBarrier(VkImage image, Stage srcStage, Access srcAccess, Layout oldLayout,
                      Stage dstStage, Access dstAccess, Layout newLayout, uint32_t mipLevels = 1, uint32_t layerCount = 1);
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
    bool isDepthOnly;
    bool noColorAttachments = false;
    bool enableAlphaBlend;
    bool disableDepthTest;
    VkFormat depthOnlyFormat;
    std::vector<VkFormat> colorAttachmentFormats;
    GraphicsPipelineBuilder();
    GraphicsPipelineBuilder & meshShader(ShaderModule & meshShaderModule, const char * entryPoint = "main");
    GraphicsPipelineBuilder & fragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint = "main");
    GraphicsPipelineBuilder & sampleCount(size_t sampleCount);
    GraphicsPipelineBuilder & depthOnly(VkFormat format = VK_FORMAT_D32_SFLOAT);
    GraphicsPipelineBuilder & colorFormats(std::vector<VkFormat> formats);
    GraphicsPipelineBuilder & alphaBlend();
    GraphicsPipelineBuilder & noDepth();
    GraphicsPipelineBuilder & noColor();
    Pipeline build();
};

Pipeline createComputePipeline(ShaderModule & computeShaderModule, const char * entryPoint = "main");

// --- Pipeline cache ---

// Derive a per-user, machine-local cache directory for `appName` and create it.
//   Linux:   $XDG_CACHE_HOME/<app>  (fallback $HOME/.cache/<app>)
//   Windows: %LOCALAPPDATA%\<app>
// Pass the result to VulkanContextOptions::pipelineCacheDir to enable cross-launch
// pipeline-cache persistence. Best-effort: returns "" on a bad appName or any
// filesystem failure, which simply leaves persistence disabled (never fatal).
std::string defaultPipelineCacheDir(const char * appName);

// --- Timestamp Queries ---

class TimestampQuery {
    std::vector<VkQueryPool> pools;
    uint32_t queryCount;
    uint32_t frameCount;
    float nanosPerTick_;
    uint32_t framesElapsed = 0;

public:
    // queryCount = number of timestamps per frame, frameCount = swapchain image count
    TimestampQuery(uint32_t queryCount, uint32_t frameCount);
    ~TimestampQuery();

    TimestampQuery(const TimestampQuery &) = delete;
    TimestampQuery & operator=(const TimestampQuery &) = delete;

    // Reset queries for the given frame index (call before writing)
    void reset(Commands & cmd, uint32_t frameIndex);

    // Write a timestamp at the current pipeline position
    void write(Commands & cmd, uint32_t frameIndex, uint32_t queryIndex);

    // Read results from a previous frame. Returns false if results not yet available.
    bool read(uint32_t frameIndex, uint64_t * timestamps, uint32_t count);

    // Nanoseconds per timestamp tick (device-specific)
    float nanosPerTick() const;
};

// --- GpuTimer ---

// Labeled per-segment GPU timing over TimestampQuery. begin() marks the start;
// each mark() closes one labeled segment. After the GPU work completes (e.g.
// submitAndWait), resolve() returns labeled millisecond durations. Keeps query
// pools and tick conversion out of call sites.
//
//   GpuTimer timer(passCount);
//   timer.begin(cmd);
//   /* pass A */  timer.mark(cmd, "A");
//   /* pass B */  timer.mark(cmd, "B");
//   cmd.submitAndWait();
//   for (auto & [label, ms] : timer.resolve()) ...
class GpuTimer {
    TimestampQuery query;
    std::vector<std::string> labels;
    uint32_t frame;
    uint32_t next;

public:
    // maxSegments = number of mark() calls between begin() and resolve().
    explicit GpuTimer(uint32_t maxSegments, uint32_t frameCount = 1);
    void begin(Commands & cmd, uint32_t frameIndex = 0);
    void mark(Commands & cmd, const char * label);
    // Labeled durations in milliseconds; empty if results are not yet ready.
    std::vector<std::pair<std::string, double>> resolve();
};
