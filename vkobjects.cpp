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

// --- DestroyGeneration ---

void DestroyGeneration::destroy() {
    struct VulkanContext & context = g_context();
    for (VkSampler s : samplers) {
        vkDestroySampler(context.device, s, nullptr);
    }
    samplers.clear();
    for (VkImageView v : imageViews) {
        vkDestroyImageView(context.device, v, nullptr);
    }
    imageViews.clear();
    for (VkImage img : images) {
        vkDestroyImage(context.device, img, nullptr);
    }
    images.clear();
    for (VkDeviceMemory memory : memories) {
        vkFreeMemory(context.device, memory, nullptr);
    }
    memories.clear();
    for (VkBuffer buffer : buffers) {
        vkDestroyBuffer(context.device, buffer, nullptr);
    }
    buffers.clear();
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

// --- Memory ---

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

std::tuple<VkBuffer, VkDeviceMemory> createBuffer(VkPhysicalDevice gpu, VkDevice device, VkBufferUsageFlags usageFlags, size_t byteCount, VkMemoryPropertyFlags flags) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (bufferInfo.size == 0) {
        throw std::runtime_error("buffer size must be greater than zero");
    }

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu, memRequirements.memoryTypeBits, flags);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, buffer, memory, 0);
    return std::make_tuple(buffer, memory);
}

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

// --- Image helpers ---

void recordMipmapGeneration(VkCommandBuffer commandBuffer, VkImage image, int width, int height, size_t mipLevelCount) {
    VkImageBlit blit{};
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    int mipWidth = width;
    int mipHeight = height;

    for (size_t i = 1; i < mipLevelCount; i++) {
        // Transition current mip to transfer dst
        VkImageMemoryBarrier2 undefinedToWrite = {};
        undefinedToWrite.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        undefinedToWrite.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        undefinedToWrite.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        undefinedToWrite.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        undefinedToWrite.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        undefinedToWrite.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        undefinedToWrite.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        undefinedToWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        undefinedToWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        undefinedToWrite.image = image;
        undefinedToWrite.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)i, 1, 0, 1};

        // Transition previous mip to transfer src
        VkImageMemoryBarrier2 writeToRead = {};
        writeToRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        writeToRead.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        writeToRead.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        writeToRead.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        writeToRead.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        writeToRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        writeToRead.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        writeToRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        writeToRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        writeToRead.image = image;
        writeToRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 1, 0, 1};

        VkImageMemoryBarrier2 barriers[2] = {undefinedToWrite, writeToRead};
        VkDependencyInfo depInfo = {};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 2;
        depInfo.pImageMemoryBarriers = barriers;
        vkCmdPipelineBarrier2(commandBuffer, &depInfo);

        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.mipLevel = i - 1;
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.mipLevel = i;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        // Transition previous mip to shader read
        VkImageMemoryBarrier2 readToSample = {};
        readToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        readToSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readToSample.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        readToSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        readToSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        readToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        readToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        readToSample.image = image;
        readToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(i - 1), 1, 0, 1};

        VkDependencyInfo readDep = {};
        readDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        readDep.imageMemoryBarrierCount = 1;
        readDep.pImageMemoryBarriers = &readToSample;
        vkCmdPipelineBarrier2(commandBuffer, &readDep);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Transition final mip to shader read
    VkImageMemoryBarrier2 writeToSample = {};
    writeToSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    writeToSample.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    writeToSample.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    writeToSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    writeToSample.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    writeToSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    writeToSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    writeToSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToSample.image = image;
    writeToSample.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, (uint32_t)(mipLevelCount - 1), 1, 0, 1};

    VkDependencyInfo finalDep = {};
    finalDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    finalDep.imageMemoryBarrierCount = 1;
    finalDep.pImageMemoryBarriers = &writeToSample;
    vkCmdPipelineBarrier2(commandBuffer, &finalDep);
}

void recordCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags imageAspects, size_t mipLevelCount) {
    VkImageView textureImageView;
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = imageAspects;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture image views");
    }
    return textureImageView;
}

// --- VulkanContext ---

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) func(instance, callback, pAllocator);
}

VulkanContext::VulkanContext(SDL_Window * window, VulkanContextOptions & options)
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
    vkDestroyDevice(device, nullptr);
    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroyInstance(instance, nullptr);

    g_context.contextInstance = nullptr;
}

void VulkanContext::onSwapchainResize(std::function<void(Commands &, VkExtent2D)> callback) {
    resizeCallback = callback;
}

// --- Shaders ---

ShaderBuilder::ShaderBuilder() : stage(VK_SHADER_STAGE_VERTEX_BIT) {}
ShaderBuilder& ShaderBuilder::vertex() { stage = VK_SHADER_STAGE_VERTEX_BIT; return *this; }
ShaderBuilder& ShaderBuilder::fragment() { stage = VK_SHADER_STAGE_FRAGMENT_BIT; return *this; }
ShaderBuilder& ShaderBuilder::compute() { stage = VK_SHADER_STAGE_COMPUTE_BIT; return *this; }
ShaderBuilder& ShaderBuilder::mesh() { stage = VK_SHADER_STAGE_MESH_BIT_EXT; return *this; }

ShaderBuilder& ShaderBuilder::fromFile(const char * fileName) {
    std::ifstream file(fileName, std::ios::ate|std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open shader file");
    size_t fileSize = (size_t)file.tellg();
    file.seekg(0);
    code.resize(fileSize);
    file.read((char*)&code[0], fileSize);
    file.close();
    return *this;
}

ShaderBuilder& ShaderBuilder::fromBuffer(const uint8_t * data, size_t size) {
    code.clear();
    code.insert(code.end(), data, data + size);
    return *this;
}

ShaderModule::ShaderModule(ShaderBuilder & builder) {
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = builder.code.size();
    createInfo.pCode = (uint32_t*)builder.code.data();
    if (VK_SUCCESS != vkCreateShaderModule(g_context().device, &createInfo, nullptr, &module)) {
        throw std::runtime_error("failed to create shader module");
    }
}
ShaderModule::~ShaderModule() { vkDestroyShaderModule(g_context().device, module, nullptr); }
ShaderModule::operator VkShaderModule() const { return module; }

// --- Buffer ---

BufferBuilder::BufferBuilder(size_t byteCount) : usage(0), properties(0), byteCount(byteCount) {}
BufferBuilder & BufferBuilder::index() { usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::uniform() {
    // In bindless model, uniform buffers are storage buffers with host visibility
    properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    return *this;
}
BufferBuilder & BufferBuilder::storage() { usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::indirect() { usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::hostCoherent() { properties |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; return *this; }
BufferBuilder & BufferBuilder::hostVisible() { properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; return *this; }
BufferBuilder & BufferBuilder::deviceLocal() { properties |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT; return *this; }
BufferBuilder & BufferBuilder::transferSource() { usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT; return *this; }
BufferBuilder & BufferBuilder::transferDestination() { usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; return *this; }
BufferBuilder & BufferBuilder::size(size_t byteCount) { this->byteCount = byteCount; return *this; }

Buffer::Buffer(BufferBuilder & builder) : buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(builder.byteCount), rid_(UINT32_MAX) {
    // All buffers get STORAGE_BUFFER_BIT for bindless registration
    VkBufferUsageFlags usage = builder.usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    std::tie(buffer, memory) = createBuffer(g_context().physicalDevice, g_context().device, usage, builder.byteCount, builder.properties);
    rid_ = g_context().bindlessTable.registerStorageBuffer(g_context().device, buffer, builder.byteCount);
}
Buffer::Buffer(Buffer && other) : buffer(other.buffer), memory(other.memory), size(other.size), rid_(other.rid_) {
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.size = 0;
    other.rid_ = UINT32_MAX;
}
uint32_t Buffer::rid() const { return rid_; }
void Buffer::setData(void * bytes, size_t size) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, 0, size, 0, &mapped);
    memcpy(mapped, bytes, size);
    vkUnmapMemory(g_context().device, memory);
}
void Buffer::setData(void * bytes, size_t size, VkDeviceSize offset) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, offset, size, 0, &mapped);
    memcpy(mapped, bytes, size);
    vkUnmapMemory(g_context().device, memory);
}
void Buffer::getData(void * bytes, size_t size) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, 0, size, 0, &mapped);
    memcpy(bytes, mapped, size);
    vkUnmapMemory(g_context().device, memory);
}
Buffer::~Buffer() {
    VulkanContext & context = g_context();
    if (rid_ != UINT32_MAX) {
        context.bindlessTable.releaseStorageBuffer(rid_);
    }
    context.destroyGenerations[context.frameInFlightIndex].memories.push_back(memory);
    context.destroyGenerations[context.frameInFlightIndex].buffers.push_back(buffer);
}
Buffer::operator VkBuffer() const { return buffer; }
Buffer::operator VkBuffer*() const { return (VkBuffer*)&buffer; }

// --- Image ---

ImageBuilder::ImageBuilder() : buildMipmaps(true), bytes(nullptr), stagingBuffer(nullptr), isDepthBuffer(false), sampleBits(VK_SAMPLE_COUNT_1_BIT), usage(0) {}
ImageBuilder & ImageBuilder::createMipmaps(bool buildMipmaps) { this->buildMipmaps = buildMipmaps; return *this; }
ImageBuilder & ImageBuilder::depth() {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = depthFormat;
    isDepthBuffer = true;
    return *this;
}
ImageBuilder & ImageBuilder::fromStagingBuffer(Buffer & stagingBuffer, int width, int height, VkFormat format) {
    this->bytes = nullptr;
    this->stagingBuffer = &stagingBuffer;
    extent.width = width; extent.height = height;
    this->format = format;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::color() {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = g_context().colorFormat;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::withFormat(VkFormat format) {
    bytes = nullptr; stagingBuffer = nullptr; buildMipmaps = false;
    extent.width = g_context().windowWidth;
    extent.height = g_context().windowHeight;
    this->format = format;
    isDepthBuffer = false;
    return *this;
}
ImageBuilder & ImageBuilder::multisample() {
    sampleBits = getSampleBits(g_context().options.multisampleCount);
    return *this;
}
ImageBuilder & ImageBuilder::storage() {
    usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    return *this;
}

Image::Image(Image && other) : image(other.image), memory(other.memory), imageView(other.imageView), sampler(other.sampler), rid_(other.rid_), isStorageImage(other.isStorageImage) {
    other.image = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.imageView = VK_NULL_HANDLE;
    other.sampler = VK_NULL_HANDLE;
    other.rid_ = UINT32_MAX;
}

Image::Image(ImageBuilder & builder, VkCommandBuffer commandBuffer) : sampler(VK_NULL_HANDLE), rid_(UINT32_MAX), isStorageImage(false) {
    VkImageFormatProperties formatProps;

    VkImageUsageFlags usageFlags = builder.usage;
    usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (0 == (usageFlags & VK_IMAGE_USAGE_STORAGE_BIT)) {
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (builder.isDepthBuffer) {
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    VkResult result = vkGetPhysicalDeviceImageFormatProperties(
        g_context().physicalDevice, builder.format, VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL, usageFlags, 0, &formatProps);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to get image format properties " + std::to_string(result));
    }

    size_t mipLevels;
    if (builder.buildMipmaps) {
        size_t maxMipLevels = std::floor(std::log2(std::max(builder.extent.width, builder.extent.height))) + 1;
        mipLevels = std::min(maxMipLevels, static_cast<size_t>(formatProps.maxMipLevels));
    } else {
        mipLevels = 1;
    }

    VkExtent3D extent = {
        std::min(builder.extent.width, formatProps.maxExtent.width),
        std::min(builder.extent.height, formatProps.maxExtent.height),
        1
    };

    if (!(formatProps.sampleCounts & builder.sampleBits)) {
        throw std::runtime_error("requested sample count not supported");
    }

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = builder.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usageFlags;
    imageInfo.samples = builder.sampleBits;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(g_context().device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan image");
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(g_context().device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(g_context().physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (VK_SUCCESS != vkAllocateMemory(g_context().device, &allocateInfo, nullptr, &memory)) {
        throw std::runtime_error("failed to allocate image memory");
    }
    if (VK_SUCCESS != vkBindImageMemory(g_context().device, image, memory, 0)) {
        throw std::runtime_error("failed to bind memory to image");
    }

    // Layout transitions using sync2
    if (builder.isDepthBuffer) {
        Barrier(commandBuffer).image(image)
            .from(Stage::None, Access::None, Layout::Undefined)
            .to(Stage::EarlyFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
            .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
            .record();
    } else {
        Barrier(commandBuffer).image(image)
            .from(Stage::None, Access::None, Layout::Undefined)
            .to(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
            .record();
    }

    if (builder.stagingBuffer != nullptr) {
        recordCopyBufferToImage(commandBuffer, *(builder.stagingBuffer), image, builder.extent.width, builder.extent.height);
    }

    if (builder.buildMipmaps) {
        recordMipmapGeneration(commandBuffer, image, builder.extent.width, builder.extent.height, mipLevels);
    } else if (!builder.isDepthBuffer) {
        Barrier(commandBuffer).image(image, mipLevels)
            .from(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
            .to(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
            .record();
    }

    VkImageAspectFlags aspectFlags;
    if (builder.isDepthBuffer) {
        aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    imageView = createImageView(g_context().device, image, builder.format, aspectFlags, mipLevels);

    // Register with bindless table
    isStorageImage = (builder.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
    if (isStorageImage) {
        rid_ = g_context().bindlessTable.registerStorageImage(g_context().device, imageView);
    } else if (!builder.isDepthBuffer) {
        sampler = createSampler(g_context().device);
        rid_ = g_context().bindlessTable.registerSampler(g_context().device, imageView, sampler);
    }
}

uint32_t Image::rid() const { return rid_; }
Image::operator VkImage() const { return image; }

Image::~Image() {
    VulkanContext & context = g_context();
    auto & gen = context.destroyGenerations[context.frameInFlightIndex];

    if (rid_ != UINT32_MAX) {
        if (isStorageImage) {
            context.bindlessTable.releaseStorageImage(rid_);
        } else {
            context.bindlessTable.releaseSampler(rid_);
        }
    }

    if (imageView != VK_NULL_HANDLE) gen.imageViews.push_back(imageView);
    if (sampler != VK_NULL_HANDLE) gen.samplers.push_back(sampler);
    if (memory != VK_NULL_HANDLE) gen.memories.push_back(memory);
    if (image != VK_NULL_HANDLE) gen.images.push_back(image);
}

// --- Barrier ---

Barrier::Barrier(VkCommandBuffer cmd) : commandBuffer(cmd), hasBuffer(false), hasImage(false) {
    bufBarrier = {};
    bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    imgBarrier = {};
    imgBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imgBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imgBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imgBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imgBarrier.subresourceRange.baseMipLevel = 0;
    imgBarrier.subresourceRange.levelCount = 1;
    imgBarrier.subresourceRange.baseArrayLayer = 0;
    imgBarrier.subresourceRange.layerCount = 1;
}

Barrier & Barrier::buffer(VkBuffer buf) {
    hasBuffer = true;
    bufBarrier.buffer = buf;
    bufBarrier.offset = 0;
    bufBarrier.size = VK_WHOLE_SIZE;
    return *this;
}

Barrier & Barrier::image(VkImage img, uint32_t mipLevels) {
    hasImage = true;
    imgBarrier.image = img;
    imgBarrier.subresourceRange.levelCount = mipLevels;
    return *this;
}

Barrier & Barrier::from(Stage stage, Access access) {
    if (hasBuffer) {
        bufBarrier.srcStageMask = static_cast<VkPipelineStageFlags2>(stage);
        bufBarrier.srcAccessMask = static_cast<VkAccessFlags2>(access);
    }
    if (hasImage) {
        imgBarrier.srcStageMask = static_cast<VkPipelineStageFlags2>(stage);
        imgBarrier.srcAccessMask = static_cast<VkAccessFlags2>(access);
    }
    return *this;
}

Barrier & Barrier::from(Stage stage, Access access, Layout layout) {
    from(stage, access);
    if (hasImage) imgBarrier.oldLayout = static_cast<VkImageLayout>(layout);
    return *this;
}

Barrier & Barrier::to(Stage stage, Access access) {
    if (hasBuffer) {
        bufBarrier.dstStageMask = static_cast<VkPipelineStageFlags2>(stage);
        bufBarrier.dstAccessMask = static_cast<VkAccessFlags2>(access);
    }
    if (hasImage) {
        imgBarrier.dstStageMask = static_cast<VkPipelineStageFlags2>(stage);
        imgBarrier.dstAccessMask = static_cast<VkAccessFlags2>(access);
    }
    return *this;
}

Barrier & Barrier::to(Stage stage, Access access, Layout layout) {
    to(stage, access);
    if (hasImage) imgBarrier.newLayout = static_cast<VkImageLayout>(layout);
    return *this;
}

Barrier & Barrier::aspectMask(VkImageAspectFlags aspects) {
    imgBarrier.subresourceRange.aspectMask = aspects;
    return *this;
}

void Barrier::record() {
    VkDependencyInfo depInfo = {};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    if (hasBuffer) {
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &bufBarrier;
    }
    if (hasImage) {
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imgBarrier;
    }
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
}

// --- Commands ---

Commands::Commands(VkCommandBuffer cmd, bool owns) : commandBuffer(cmd), ended(false), ownsBuffer(owns) {
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
    : commandBuffer(other.commandBuffer), ended(other.ended), ownsBuffer(other.ownsBuffer) {
    other.commandBuffer = VK_NULL_HANDLE;
    other.ended = true;
    other.ownsBuffer = false;
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
void Commands::drawMeshTasks(uint32_t x, uint32_t y, uint32_t z) {
    vkCmdDrawMeshTasks(commandBuffer, x, y, z);
}
void Commands::pushConstants(const void * data, uint32_t size) {
    vkCmdPushConstants(commandBuffer, g_context().bindlessTable.pipelineLayout, VK_SHADER_STAGE_ALL, 0, size, data);
}

void Commands::beginRendering(VkImageView colorImage, VkImageView depthImage) {
    VkRenderingAttachmentInfo colorAttachment = {};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorImage;
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

    vkBeginRendering(commandBuffer, &renderingInfo);
}

void Commands::endRendering() {
    vkEndRendering(commandBuffer);
}

void Commands::bufferBarrier(VkBuffer buf, Stage srcStage, Stage dstStage) {
    Barrier(commandBuffer).buffer(buf)
        .from(srcStage, Access::ShaderWrite)
        .to(dstStage, Access::ShaderRead)
        .record();
}

void Commands::imageBarrier(VkImage img, Stage srcStage, Access srcAccess, Layout oldLayout,
                            Stage dstStage, Access dstAccess, Layout newLayout, uint32_t mipLevels) {
    Barrier(commandBuffer).image(img, mipLevels)
        .from(srcStage, srcAccess, oldLayout)
        .to(dstStage, dstAccess, newLayout)
        .record();
}

void Commands::submitAndWait() {
    end();
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(g_context().graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_context().graphicsQueue);

    if (ownsBuffer) {
        vkFreeCommandBuffers(g_context().device, g_context().commandPool, 1, &commandBuffer);
        commandBuffer = VK_NULL_HANDLE;
    }
}

void Commands::end() {
    if (!ended) {
        vkEndCommandBuffer(commandBuffer);
        ended = true;
    }
}

Commands::operator VkCommandBuffer() { return commandBuffer; }

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

    // Acquire next image
    if (VK_SUCCESS != vkAcquireNextImageKHR(context.device, context.swapchain, UINT64_MAX,
            imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex)) {
        throw std::runtime_error("failed to acquire next swapchain image");
    }
    renderFinishedSemaphore = context.renderFinishedSemaphores[imageIndex];
}

Frame::~Frame() {
    Frame::currentGuard = nullptr;
    context.frameInFlightIndex = (context.frameInFlightIndex + 1) % context.swapchainImageCount;
}

uint32_t Frame::swapchainImageIndex() const { return imageIndex; }
VkImageView Frame::swapchainImageView() const { return context.swapchainImageViews[imageIndex]; }

Commands Frame::beginCommands() {
    VkCommandBuffer cmd = context.frameCommandBuffers[inFlightIndex];
    vkResetCommandBuffer(cmd, 0);
    Commands cmds(cmd, false);

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

    if (vkQueueSubmit2(context.graphicsQueue, 1, &submitInfo, submittedBuffersFinishedFence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffer");
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
        vkDeviceWaitIdle(context.device);

        int w, h;
        SDL_GetWindowSize(context.window, &w, &h);
        context.windowWidth = w;
        context.windowHeight = h;

        for (VkImageView view : context.swapchainImageViews) {
            vkDestroyImageView(context.device, view, nullptr);
        }
        createSwapChain(context, context.presentationSurface, context.physicalDevice, context.device, context.swapchain);
        getSwapChainImageHandles(context.device, context.swapchain, context.swapchainImages);
        makeChainImageViews(context.device, context.colorFormat, context.swapchainImages, context.swapchainImageViews);

        Commands rebuildCmd = Commands::oneShot();
        for (VkImage & img : context.swapchainImages) {
            Barrier(rebuildCmd).image(img)
                .from(Stage::None, Access::None, Layout::Undefined)
                .to(Stage::None, Access::None, Layout::PresentSrc)
                .record();
        }
        if (context.resizeCallback) {
            VkExtent2D extent = {(uint32_t)w, (uint32_t)h};
            context.resizeCallback(rebuildCmd, extent);
        }
        rebuildCmd.submitAndWait();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to present queue");
    }

    submitted = true;
}

// --- Pipelines ---

GraphicsPipelineBuilder::GraphicsPipelineBuilder() : sampleCountBit(VK_SAMPLE_COUNT_1_BIT) {}
GraphicsPipelineBuilder & GraphicsPipelineBuilder::addMeshShader(ShaderModule & meshShaderModule, const char * entryPoint) {
    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
    stageInfo.module = meshShaderModule.module;
    stageInfo.pName = entryPoint;
    shaderStages.push_back(stageInfo);
    return *this;
}
GraphicsPipelineBuilder & GraphicsPipelineBuilder::addFragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint) {
    VkPipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stageInfo.module = fragmentShaderModule.module;
    stageInfo.pName = entryPoint;
    shaderStages.push_back(stageInfo);
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

VkPipeline GraphicsPipelineBuilder::build() {
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

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
    if (sampleCountBit == VK_SAMPLE_COUNT_1_BIT) {
        multisampling.sampleShadingEnable = VK_FALSE;
    } else if (g_context().options.shaderSampleRateShading > 0.0f) {
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.minSampleShading = 1.0f;
    }
    multisampling.rasterizationSamples = sampleCountBit;

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkFormat colorFormat = g_context().colorFormat;
    VkPipelineRenderingCreateInfo renderingInfo = {};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = depthFormat;
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
    pipelineCreateInfo.pNext = &renderingInfo;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(g_context().device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline");
    }
    g_context().pipelines.emplace(pipeline);
    return pipeline;
}

VkPipeline createComputePipeline(VkShaderModule computeShaderModule, const char * entryPoint) {
    VkPipelineLayout pipelineLayout = g_context().bindlessTable.pipelineLayout;

    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule;
    pipelineInfo.stage.pName = entryPoint;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    if (VK_SUCCESS != vkCreateComputePipelines(g_context().device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline)) {
        throw std::runtime_error("failed to create compute pipeline");
    }
    g_context().pipelines.emplace(computePipeline);
    return computePipeline;
}
