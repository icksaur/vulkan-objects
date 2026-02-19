#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#pragma GCC diagnostic pop

#include "vkobjects.h"
#include <cstddef>
#include <vulkan/vulkan_core.h>
#include <cmath>
#include <cstring>

// useful defaults
const char * appName = "VulkanExample";
const char * engineName = "VulkanExampleEngine";
const uint32_t vulkanVersion = VK_API_VERSION_1_3;
VkPresentModeKHR preferredPresentationMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR; // vsync
VkImageUsageFlags desiredImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
VkSurfaceTransformFlagBitsKHR desiredTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
VkFormat surfaceFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
VkFormat depthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasks;
PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirect;
PFN_vkCmdBeginRendering vkBeginRendering;
PFN_vkCmdEndRendering vkEndRendering;

VulkanContextOptions::VulkanContextOptions() :
    enableMultisampling(false),
    multisampleCount(1),
    enableMeshShaders(false),
    enableValidationLayers(false),
    shaderSampleRateShading(0.0f),
    enableThrowOnValidationError(false) {}
VulkanContextOptions & VulkanContextOptions::multisample(uint32_t count) {
    multisampleCount = count;
    enableMultisampling = count > 1;
    return *this;
}
VulkanContextOptions & VulkanContextOptions::meshShaders() {
    enableMeshShaders = true;
    return *this;
}
VulkanContextOptions & VulkanContextOptions::validation() {
    enableValidationLayers = true;
    return *this;
}
VulkanContextOptions & VulkanContextOptions::sampleRateShading(float rate) {
    if (rate < 0.0f || rate > 2.0f) {
        throw std::runtime_error("invalid sample rate shading value");
    }
    shaderSampleRateShading = rate;
    return *this;
}
VulkanContextOptions & VulkanContextOptions::throwOnValidationError() {
    enableThrowOnValidationError = true;
    return *this;
}

VulkanContext & VulkanContextSingleton::operator()() { return *contextInstance; }

VulkanContextSingleton g_context;
VmaAllocator g_allocator = VK_NULL_HANDLE;

// --- DestroyGeneration ---

void DestroyGeneration::destroy() {
    struct VulkanContext & context = g_context();
    for (uint32_t rid : storageBufferRIDs) context.bindlessTable.releaseStorageBuffer(rid);
    storageBufferRIDs.clear();
    for (uint32_t rid : samplerRIDs) context.bindlessTable.releaseSampler(rid);
    samplerRIDs.clear();
    for (uint32_t rid : storageImageRIDs) context.bindlessTable.releaseStorageImage(rid);
    storageImageRIDs.clear();
    for (VkPipeline p : pipelines) {
        context.pipelines.erase(p);
        vkDestroyPipeline(context.device, p, nullptr);
    }
    pipelines.clear();
    for (VkSampler s : samplers) {
        vkDestroySampler(context.device, s, nullptr);
    }
    samplers.clear();
    for (VkImageView v : imageViews) {
        vkDestroyImageView(context.device, v, nullptr);
    }
    imageViews.clear();
    for (auto & [img, alloc] : imageAllocations) {
        vmaDestroyImage(g_allocator, img, alloc);
    }
    imageAllocations.clear();
    for (auto & [buf, alloc] : bufferAllocations) {
        vmaDestroyBuffer(g_allocator, buf, alloc);
    }
    bufferAllocations.clear();
    if (!commandBuffers.empty()) {
        vkFreeCommandBuffers(context.device, context.commandPool, commandBuffers.size(), commandBuffers.data());
        commandBuffers.clear();
    }
}
DestroyGeneration::~DestroyGeneration() {
    destroy();
}

// --- Utility ---

VkSampleCountFlagBits getSampleBits(uint32_t sampleCount) {
    switch (sampleCount) {
        case 1: return VK_SAMPLE_COUNT_1_BIT;
        case 2: return VK_SAMPLE_COUNT_2_BIT;
        case 4: return VK_SAMPLE_COUNT_4_BIT;
        case 8: return VK_SAMPLE_COUNT_8_BIT;
        case 16: return VK_SAMPLE_COUNT_16_BIT;
        case 32: return VK_SAMPLE_COUNT_32_BIT;
        case 64: return VK_SAMPLE_COUNT_64_BIT;
        default: throw std::runtime_error("unsupported sample count");
    }
}

VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool) {
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!");
    }
    return commandBuffer;
}

// --- Instance & Device Setup ---

void getAvailableVulkanExtensions(SDL_Window * window, std::vector<std::string>& outExtensions) {
    uint32_t extensionCount = 0;
    const char * const * extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (extensionNames == nullptr) {
        throw std::runtime_error("unable to query vulkan extension count");
    }

    outExtensions.clear();
    for (uint32_t i = 0; i < extensionCount; i++) {
        outExtensions.emplace_back(extensionNames[i]);
    }
    (void)window;
}

std::vector<std::string> getRequestedLayerNames(VulkanContextOptions & options) {
    std::vector<std::string> layers;
    if (options.enableValidationLayers) {
        layers.emplace_back("VK_LAYER_KHRONOS_validation");
    }
    return layers;
}

void getAvailableVulkanLayers(std::vector<std::string>& outLayers) {
    uint32_t instanceLayerCount;
    vkEnumerateInstanceLayerProperties(&instanceLayerCount, NULL);

    std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
    vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());

    outLayers.clear();
    for (const auto& layerProperty : instanceLayerProperties) {
        std::string layerName(layerProperty.layerName);
        outLayers.emplace_back(layerName);
    }

    std::cout << "found " << outLayers.size() << " instance layers:\n";
    for (const auto& layer : outLayers) {
        std::cout << "  " << layer << std::endl;
    }
}

void createVulkanInstance(const std::vector<std::string> & layerNames, const std::vector<std::string> & extensionNames, VkInstance & outInstance) {
    std::vector<const char*> layers;
    for (const auto& layer : layerNames) {
        layers.emplace_back(layer.c_str());
    }

    std::vector<const char*> extensions;
    for (const auto& ext : extensionNames) {
        extensions.emplace_back(ext.c_str());
    }

    // add debug report extension if validation is on
    bool hasDebugReport = false;
    for (const auto& ext : extensions) {
        if (std::string(ext) == VK_EXT_DEBUG_REPORT_EXTENSION_NAME) {
            hasDebugReport = true;
            break;
        }
    }
    if (!hasDebugReport && !layers.empty()) {
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    }

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = engineName;
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = vulkanVersion;

    VkInstanceCreateInfo instanceCreateInfo = {};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    instanceCreateInfo.ppEnabledLayerNames = layers.data();
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &outInstance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create vulkan instance (VkResult " + std::to_string(result) + ")");
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData) {
    (void)flags; (void)objType; (void)obj; (void)location; (void)code; (void)layerPrefix; (void)userData;
    std::cerr << "validation layer: " << msg << std::endl;
    return VK_FALSE;
}

void setupDebugCallback(VkInstance instance, VkDebugReportCallbackEXT& callback) {
    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    createInfo.pfnCallback = debugCallback;

    auto CreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");

    if (CreateDebugReportCallbackEXT == nullptr || CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug callback");
    }
}

VkPhysicalDeviceMeshShaderPropertiesEXT getMeshShaderProperties(VkPhysicalDevice physicalDevice) {
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties = {};
    meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;

    VkPhysicalDeviceProperties2 deviceProperties2 = {};
    deviceProperties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProperties2.pNext = &meshShaderProperties;

    vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties2);

    std::cout << "max mesh shader output vertices: " << meshShaderProperties.maxMeshOutputVertices << std::endl;
    std::cout << "max mesh shader output primitives: " << meshShaderProperties.maxMeshOutputPrimitives << std::endl;
    std::cout << "max mesh work group invocations: " << meshShaderProperties.maxMeshWorkGroupInvocations << std::endl;
    std::cout << "max preferred mesh work group invocations: " << meshShaderProperties.maxPreferredMeshWorkGroupInvocations << std::endl;

    return meshShaderProperties;
}

void selectGPU(VkInstance instance, VkPhysicalDevice & outDevice, unsigned int & outQueueFamilyIndex, uint32_t & outMaxSamples, VkPhysicalDeviceLimits & outLimits) {
    uint32_t count = 0;
    if (VK_SUCCESS != vkEnumeratePhysicalDevices(instance, &count, nullptr)) {
        throw std::runtime_error("unable to count physical devices");
    }

    std::vector<VkPhysicalDevice> physicalDevices(count);
    if (VK_SUCCESS != vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data())) {
        throw std::runtime_error("unable to enumerate physical devices");
    }

    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    for (const auto& device : physicalDevices) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(device, &properties);
        std::cout << "device: " << properties.deviceName << std::endl;

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selectedDevice = device;
            outLimits = properties.limits;
            outMaxSamples = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
            std::cout << "selected device: " << properties.deviceName << std::endl;
        }
    }

    if (selectedDevice == VK_NULL_HANDLE) {
        selectedDevice = physicalDevices[0];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(selectedDevice, &properties);
        outLimits = properties.limits;
        outMaxSamples = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
        std::cout << "no discrete GPU found, using first device: " << properties.deviceName << std::endl;
    }

    uint32_t familyQueueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &familyQueueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueProperties(familyQueueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &familyQueueCount, queueProperties.data());

    int queueNodeIndex = -1;
    for (uint32_t i = 0; i < familyQueueCount; i++) {
        if (queueProperties[i].queueCount > 0
        && (queueProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
        && (queueProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            queueNodeIndex = i;
            break;
        }
    }

    if (queueNodeIndex == -1) {
        throw std::runtime_error("Unable to find a queue command family that accepts graphics commands");
    }

    outDevice = selectedDevice;
    outQueueFamilyIndex = queueNodeIndex;
}

VkDevice createLogicalDevice(VulkanContextOptions & options, VkPhysicalDevice& physicalDevice, uint32_t queueFamilyIndex) {
    uint32_t devicePropertyCount(0);
    if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &devicePropertyCount, NULL)) {
        throw std::runtime_error("Unable to acquire device extension property count");
    }
    std::cout << "found " << devicePropertyCount << " device extensions\n";

    std::vector<VkExtensionProperties> extensionProperties(devicePropertyCount);
    if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &devicePropertyCount, extensionProperties.data())) {
        throw std::runtime_error("Unable to acquire device extension property names");
    }

    std::vector<const char*> devicePropertyNames;
    std::set<std::string> requiredExtensionNames{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    if (options.enableMeshShaders) {
        requiredExtensionNames.emplace(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }
    requiredExtensionNames.emplace(VK_EXT_SHADER_IMAGE_ATOMIC_INT64_EXTENSION_NAME);

    for (const auto& extensionProperty : extensionProperties) {
        auto it = requiredExtensionNames.find(std::string(extensionProperty.extensionName));
        if (it != requiredExtensionNames.end()) {
            devicePropertyNames.emplace_back(extensionProperty.extensionName);
            requiredExtensionNames.erase(it);
        }
    }

    if (!requiredExtensionNames.empty()) {
        for (auto missing : requiredExtensionNames) {
            std::cout << "missing extension: " << missing << std::endl;
        }
        throw std::runtime_error("not all required device extensions are supported!\n");
    }

    for (const auto& name : devicePropertyNames) {
        std::cout << "applying device extension: " << name << std::endl;
    }

    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    std::vector<float> queue_prio = { 1.0f };
    queueCreateInfo.pQueuePriorities = queue_prio.data();

    void* previousInChain = nullptr;

    // Vulkan 1.2 features: descriptor indexing for bindless
    VkPhysicalDeviceVulkan12Features device12Features = {};
    device12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    device12Features.descriptorIndexing = VK_TRUE;
    device12Features.runtimeDescriptorArray = VK_TRUE;
    device12Features.descriptorBindingPartiallyBound = VK_TRUE;
    device12Features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    device12Features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    device12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    device12Features.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    device12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    device12Features.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    device12Features.bufferDeviceAddress = VK_FALSE;
    previousInChain = &device12Features;

    // Vulkan 1.3 features: dynamic rendering and synchronization2
    VkPhysicalDeviceVulkan13Features device13Features = {};
    device13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    device13Features.maintenance4 = VK_TRUE;
    device13Features.dynamicRendering = VK_TRUE;
    device13Features.synchronization2 = VK_TRUE;
    device13Features.pNext = previousInChain;
    previousInChain = &device13Features;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {};
    meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshShaderFeatures.taskShader = VK_TRUE;
    meshShaderFeatures.meshShader = VK_TRUE;
    meshShaderFeatures.pNext = previousInChain;

    if (options.enableMeshShaders) {
        previousInChain = &meshShaderFeatures;
    }

    VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT imageAtomicInt64Features = {};
    imageAtomicInt64Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_IMAGE_ATOMIC_INT64_FEATURES_EXT;
    imageAtomicInt64Features.shaderImageInt64Atomics = VK_TRUE;
    imageAtomicInt64Features.sparseImageInt64Atomics = VK_FALSE;
    imageAtomicInt64Features.pNext = previousInChain;
    previousInChain = &imageAtomicInt64Features;

    VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
    deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    deviceFeatures2.features.samplerAnisotropy = VK_TRUE;
    deviceFeatures2.features.shaderInt64 = VK_TRUE;
    deviceFeatures2.pNext = previousInChain;
    if (options.shaderSampleRateShading > 0.0f) {
        deviceFeatures2.features.sampleRateShading = VK_TRUE;
    }
    deviceFeatures2.features.alphaToOne = VK_FALSE;
    previousInChain = &deviceFeatures2;

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.ppEnabledExtensionNames = devicePropertyNames.data();
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(devicePropertyNames.size());
    deviceCreateInfo.pNext = previousInChain;
    deviceCreateInfo.flags = 0;

    VkDevice device;
    if (VK_SUCCESS != vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)) {
        throw std::runtime_error("failed to create logical device!");
    }

    return device;
}

// --- Swapchain ---

VkSurfaceKHR createSurface(SDL_Window* window, VkInstance instance, VkPhysicalDevice gpu, uint32_t graphicsFamilyQueueIndex) {
    VkSurfaceKHR surface;
    if (false == SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        throw std::runtime_error("Unable to create Vulkan compatible surface using SDL: " + std::string(SDL_GetError()));
    }
    VkBool32 supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphicsFamilyQueueIndex, surface, &supported);
    if (VK_TRUE != supported) {
        throw std::runtime_error("Surface is not supported by physical device!");
    }
    return surface;
}

VkQueue getPresentationQueue(VkPhysicalDevice gpu, VkDevice logicalDevice, uint32_t graphicsQueueIndex, VkSurfaceKHR presentation_surface) {
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphicsQueueIndex, presentation_surface, &presentSupport);
    if (VK_FALSE == presentSupport) {
        throw std::runtime_error("presentation queue is not supported on graphics queue index");
    }
    VkQueue presentQueue;
    vkGetDeviceQueue(logicalDevice, graphicsQueueIndex, 0, &presentQueue);
    return presentQueue;
}

bool getPresentationMode(VkSurfaceKHR surface, VkPhysicalDevice device, VkPresentModeKHR& ioMode) {
    uint32_t modeCount = 0;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, NULL) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkPresentModeKHR> availableModes(modeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, availableModes.data()) != VK_SUCCESS) {
        return false;
    }
    for (auto& mode : availableModes) {
        if (mode == ioMode) return true;
    }
    ioMode = VK_PRESENT_MODE_FIFO_KHR;
    return true;
}

uint32_t getNumberOfSwapImages(const VkSurfaceCapabilitiesKHR& capabilities) {
    uint32_t number = capabilities.minImageCount + 1;
    return number > capabilities.maxImageCount ? capabilities.minImageCount : number;
}

template<typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

VkExtent2D getSwapImageSize(VulkanContext & context, const VkSurfaceCapabilitiesKHR& capabilities) {
    VkExtent2D size = { (uint32_t)context.windowWidth, (uint32_t)context.windowHeight };
    if (capabilities.currentExtent.width == UINT32_MAX) {
        size.width  = clamp(size.width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        size.height = clamp(size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    } else {
        size = capabilities.currentExtent;
    }
    return size;
}

bool getImageUsage(const VkSurfaceCapabilitiesKHR& capabilities, VkImageUsageFlags& foundUsages) {
    foundUsages = desiredImageUsage;
    VkImageUsageFlags image_usage = desiredImageUsage & capabilities.supportedUsageFlags;
    if (image_usage != desiredImageUsage) {
        return false;
    }
    return true;
}

VkSurfaceTransformFlagBitsKHR getSurfaceTransform(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.supportedTransforms & desiredTransform) return desiredTransform;
    return capabilities.currentTransform;
}

bool getSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR& outFormat) {
    uint32_t count(0);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkSurfaceFormatKHR> foundFormats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, foundFormats.data()) != VK_SUCCESS) return false;

    if (foundFormats.size() == 1 && foundFormats[0].format == VK_FORMAT_UNDEFINED) {
        outFormat.format = surfaceFormat;
        outFormat.colorSpace = colorSpace;
        return true;
    }
    for (const auto& outerFormat : foundFormats) {
        if (outerFormat.format == surfaceFormat) {
            outFormat.format = outerFormat.format;
            for (const auto& innerFormat : foundFormats) {
                if (innerFormat.colorSpace == colorSpace) {
                    outFormat.colorSpace = innerFormat.colorSpace;
                    return true;
                }
            }
            outFormat.colorSpace = foundFormats[0].colorSpace;
            return true;
        }
    }
    outFormat = foundFormats[0];
    return true;
}

void createSwapChain(VulkanContext & context, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain) {
    vkDeviceWaitIdle(device);

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
        throw std::runtime_error("failed to acquire surface capabilities");
    }

    VkPresentModeKHR presentation_mode = preferredPresentationMode;
    if (!getPresentationMode(surface, physicalDevice, presentation_mode)) {
        throw std::runtime_error("failed to get presentation mode");
    }

    uint32_t swapImageCount = getNumberOfSwapImages(surfaceCapabilities);
    VkExtent2D swap_image_extent = getSwapImageSize(context, surfaceCapabilities);

    VkImageUsageFlags usageFlags;
    if (!getImageUsage(surfaceCapabilities, usageFlags)) {
        throw std::runtime_error("failed to get image usage flags");
    }

    VkSurfaceTransformFlagBitsKHR transform = getSurfaceTransform(surfaceCapabilities);

    VkSurfaceFormatKHR imageFormat;
    if (!getSurfaceFormat(physicalDevice, surface, imageFormat)) {
        throw std::runtime_error("failed to get surface format");
    }
    context.colorFormat = imageFormat.format;

    VkSwapchainKHR oldSwapChain = outSwapChain;

    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = surface;
    swapInfo.minImageCount = swapImageCount;
    swapInfo.imageFormat = imageFormat.format;
    swapInfo.imageColorSpace = imageFormat.colorSpace;
    swapInfo.imageExtent = swap_image_extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = usageFlags;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = transform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentation_mode;
    swapInfo.clipped = true;
    swapInfo.oldSwapchain = NULL;

    if (VK_SUCCESS != vkCreateSwapchainKHR(device, &swapInfo, nullptr, &outSwapChain)) {
        throw std::runtime_error("unable to create swap chain");
    }

    if (oldSwapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, oldSwapChain, nullptr);
    }
}

void getSwapChainImageHandles(VkDevice device, VkSwapchainKHR chain, std::vector<VkImage>& outImageHandles) {
    uint32_t imageCount = 0;
    if (VK_SUCCESS != vkGetSwapchainImagesKHR(device, chain, &imageCount, nullptr)) {
        throw std::runtime_error("unable to get number of images in swap chain");
    }
    outImageHandles.clear();
    outImageHandles.resize(imageCount);
    if (VK_SUCCESS != vkGetSwapchainImagesKHR(device, chain, &imageCount, outImageHandles.data())) {
        throw std::runtime_error("unable to get image handles from swap chain");
    }
}

void makeChainImageViews(VkDevice device, VkFormat colorFormat, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews) {
    imageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create image views!");
        }
    }
}

VkCommandPool createCommandPool(VkDevice device, uint32_t queueFamilyIndex) {
    VkCommandPool commandPool;
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
    return commandPool;
}

// --- BindlessTable ---

void BindlessTable::init(VkDevice device) {
    VkDescriptorSetLayoutBinding bindings[3] = {};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = MAX_STORAGE_BUFFERS;
    bindings[0].stageFlags = VK_SHADER_STAGE_ALL;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = MAX_SAMPLERS;
    bindings[1].stageFlags = VK_SHADER_STAGE_ALL;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = MAX_STORAGE_IMAGES;
    bindings[2].stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorBindingFlags bindingFlags[3] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {};
    bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bindingFlagsInfo.bindingCount = 3;
    bindingFlagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &bindingFlagsInfo;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bindless descriptor set layout");
    }

    VkDescriptorPoolSize poolSizes[3] = {};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_STORAGE_BUFFERS};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SAMPLERS};
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_IMAGES};

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bindless descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &set) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate bindless descriptor set");
    }

    VkPushConstantRange pushRange = {};
    pushRange.stageFlags = VK_SHADER_STAGE_ALL;
    pushRange.offset = 0;
    pushRange.size = 128;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create bindless pipeline layout");
    }
}

void BindlessTable::destroy(VkDevice device) {
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, pool, nullptr);
    if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, layout, nullptr);
}

uint32_t BindlessTable::registerStorageBuffer(VkDevice device, VkBuffer buffer, VkDeviceSize size) {
    uint32_t index;
    if (!freeStorageBufferIndices.empty()) {
        index = freeStorageBufferIndices.back();
        freeStorageBufferIndices.pop_back();
    } else {
        index = nextStorageBufferIndex++;
    }

    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = buffer;
    bufferInfo.offset = 0;
    bufferInfo.range = size;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return index;
}

uint32_t BindlessTable::registerSampler(VkDevice device, VkImageView imageView, VkSampler sampler) {
    uint32_t index;
    if (!freeSamplerIndices.empty()) {
        index = freeSamplerIndices.back();
        freeSamplerIndices.pop_back();
    } else {
        index = nextSamplerIndex++;
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler = sampler;
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 1;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return index;
}

uint32_t BindlessTable::registerStorageImage(VkDevice device, VkImageView imageView) {
    uint32_t index;
    if (!freeStorageImageIndices.empty()) {
        index = freeStorageImageIndices.back();
        freeStorageImageIndices.pop_back();
    } else {
        index = nextStorageImageIndex++;
    }

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageView = imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 2;
    write.dstArrayElement = index;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return index;
}

void BindlessTable::releaseStorageBuffer(uint32_t index) { freeStorageBufferIndices.push_back(index); }
void BindlessTable::releaseSampler(uint32_t index) { freeSamplerIndices.push_back(index); }
void BindlessTable::releaseStorageImage(uint32_t index) { freeStorageImageIndices.push_back(index); }

VkSampler createSampler(VkDevice device) {
    VkSampler textureSampler;
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 13.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler");
    }
    return textureSampler;
}

VkSampler createShadowSampler(VkDevice device) {
    VkSampler sampler;
    VkSamplerCreateInfo samplerInfo = {};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow sampler");
    }
    return sampler;
}

VkFence createFence() {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    if (VK_SUCCESS != vkCreateFence(g_context().device, &createInfo, nullptr, &fence)) {
        throw std::runtime_error("failed to create fence");
    }
    g_context().fences.push_back(fence);
    return fence;
}

VkSemaphore createSemaphore() {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore;
    if (vkCreateSemaphore(g_context().device, &createInfo, NULL, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore");
    }
    g_context().semaphores.push_back(semaphore);
    return semaphore;
}

// --- VulkanContext ---

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) func(instance, callback, pAllocator);
}

VulkanContext::VulkanContext(SDL_Window * window, VulkanContextOptions options)
    : window(window), options(options), frameInFlightIndex(0) {
    if (g_context.contextInstance != nullptr) {
        throw std::runtime_error("VulkanContext already exists");
    }

    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    this->windowWidth = windowWidth;
    this->windowHeight = windowHeight;

    std::vector<std::string> foundExtensions;
    getAvailableVulkanExtensions(window, foundExtensions);

    std::vector<std::string> foundLayers;
    getAvailableVulkanLayers(foundLayers);

    std::vector<std::string> requestedLayers = getRequestedLayerNames(options);
    std::vector<std::string> enabledLayers;
    for (const auto& requested : requestedLayers) {
        bool found = false;
        for (const auto& available : foundLayers) {
            if (requested == available) { found = true; break; }
        }
        if (found) {
            enabledLayers.push_back(requested);
        } else {
            std::cout << "  Missing layer: " << requested << std::endl;
        }
    }

    createVulkanInstance(enabledLayers, foundExtensions, this->instance);

    if (options.enableValidationLayers) {
        setupDebugCallback(this->instance, this->callback);
    }

    this->graphicsQueueIndex = -1;
    selectGPU(this->instance, this->physicalDevice, this->graphicsQueueIndex, this->maxSamples, this->limits);

    this->device = createLogicalDevice(options, this->physicalDevice, this->graphicsQueueIndex);

    // Initialize VMA
    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.instance = this->instance;
    allocatorInfo.physicalDevice = this->physicalDevice;
    allocatorInfo.device = this->device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (vmaCreateAllocator(&allocatorInfo, &g_allocator) != VK_SUCCESS) {
        throw std::runtime_error("failed to create VMA allocator");
    }

    this->meshShaderProperties = getMeshShaderProperties(this->physicalDevice);

    this->presentationSurface = createSurface(window, this->instance, this->physicalDevice, this->graphicsQueueIndex);
    this->presentationQueue = getPresentationQueue(this->physicalDevice, this->device, this->graphicsQueueIndex, this->presentationSurface);

    this->swapchain = VK_NULL_HANDLE;
    createSwapChain(*this, this->presentationSurface, this->physicalDevice, this->device, this->swapchain);
    getSwapChainImageHandles(this->device, this->swapchain, this->swapchainImages);

    this->swapchainImageCount = this->swapchainImages.size();
    makeChainImageViews(this->device, this->colorFormat, this->swapchainImages, this->swapchainImageViews);

    this->commandPool = createCommandPool(this->device, this->graphicsQueueIndex);

    // Init bindless descriptor table
    this->bindlessTable.init(this->device);

    // Pre-allocate frame command buffers
    for (size_t i = 0; i < swapchainImageCount; i++) {
        frameCommandBuffers.push_back(createCommandBuffer(this->device, this->commandPool));
    }

    this->destroyGenerations.resize(this->swapchainImageCount);

    vkGetDeviceQueue(this->device, this->graphicsQueueIndex, 0, &this->graphicsQueue);

    g_context.contextInstance = this;

    // Transition swapchain images using Commands
    Commands imageInitCmd = Commands::oneShot();
    for (VkImage & image : this->swapchainImages) {
        Barrier(imageInitCmd).image(image)
            .from(Stage::None, Access::None, Layout::Undefined)
            .to(Stage::None, Access::None, Layout::PresentSrc)
            .record();
    }
    imageInitCmd.submitAndWait();

    for (size_t i = 0; i < swapchainImageCount; i++) {
        imageAvailableSemaphores.push_back(createSemaphore());
        renderFinishedSemaphores.push_back(createSemaphore());
        submittedBuffersFinishedFences.push_back(createFence());
    }

    vkCmdDrawMeshTasks = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(g_context().device, "vkCmdDrawMeshTasksEXT");
    vkCmdDrawMeshTasksIndirect = (PFN_vkCmdDrawMeshTasksIndirectEXT)vkGetDeviceProcAddr(g_context().device, "vkCmdDrawMeshTasksIndirectEXT");
    vkBeginRendering = (PFN_vkCmdBeginRendering)vkGetDeviceProcAddr(g_context().device, "vkCmdBeginRendering");
    vkEndRendering = (PFN_vkCmdEndRendering)vkGetDeviceProcAddr(g_context().device, "vkCmdEndRendering");

    Frame::currentGuard = nullptr;
}

VulkanContext::~VulkanContext() {
    vkQueueWaitIdle(graphicsQueue);

    destroyGenerations.clear();

    for (auto semaphore : semaphores) vkDestroySemaphore(device, semaphore, nullptr);
    for (auto fence : fences) vkDestroyFence(device, fence, nullptr);
    for (VkPipeline pipeline : pipelines) vkDestroyPipeline(device, pipeline, nullptr);

    bindlessTable.destroy(device);

    vkDestroyCommandPool(device, commandPool, nullptr);
    for (VkImageView view : swapchainImageViews) vkDestroyImageView(device, view, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, presentationSurface, nullptr);
    vmaDestroyAllocator(g_allocator);
    g_allocator = VK_NULL_HANDLE;
    vkDestroyDevice(device, nullptr);
    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroyInstance(instance, nullptr);

    g_context.contextInstance = nullptr;
}

void VulkanContext::onSwapchainResize(std::function<void(Commands &, VkExtent2D)> callback) {
    resizeCallback = callback;
}

