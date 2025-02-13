#pragma once

#include <vector>
#include <set>
#include <iostream>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <ranges>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

// useful defaults
const char * appName = "VulkanExample";
const char * engineName = "VulkanExampleEngine";
VkPresentModeKHR preferredPresentationMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
VkImageUsageFlags desiredImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
VkSurfaceTransformFlagBitsKHR desiredTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
VkFormat surfaceFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT; // some options are VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT

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

    // presentation loop sync primitives
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;

    // presentation loop resources that may need to be rebuilt
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> presentFramebuffers;
    size_t currentFrame = 0;

    // managed resource collections that will be auto-cleaned when the context is destroyed
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> semaphores;
    std::vector<VkFence> fences;
    std::set<VkDescriptorSetLayout> layouts;
    std::set<VkPipelineLayout> pipelineLayouts;
    std::set<VkPipeline> pipelines;
    std::set<VkRenderPass> renderPasses;

    VulkanContext(SDL_Window * window);
    ~VulkanContext();
    VulkanContext & operator=(const VulkanContext & other) = delete;
    VulkanContext(const VulkanContext & other) = delete;
    VulkanContext(VulkanContext && other) = delete; 
};

struct VulkanContextSingleton {
    VulkanContext * contextInstance;
    VulkanContext& operator()() { return *contextInstance; }
} $context; // this global is guaranteed to null-initialize by C++ initialization rules

// a helper to start and end a command buffer which can be submitted and waited
struct ScopedCommandBuffer {
    VkDevice device;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;
    VkCommandBuffer commandBuffer;
    ScopedCommandBuffer(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) : device(device), commandPool(commandPool), graphicsQueue(graphicsQueue) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer");
        }
    }
    void submitAndWait() {
        if (VK_SUCCESS != vkEndCommandBuffer(commandBuffer)) {
            throw std::runtime_error("failed to end command buffer");
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        if (VK_SUCCESS != vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE)) {
            throw std::runtime_error("failed submit queue");
        }
        if (VK_SUCCESS != vkQueueWaitIdle(graphicsQueue)) {
            throw std::runtime_error("failed wait for queue to be idle");
        }
    }
    ~ScopedCommandBuffer() {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
};

void getAvailableVulkanExtensions(SDL_Window* window, std::vector<std::string>& outExtensions) {
    // Figure out the amount of extensions vulkan needs to interface with the os windowing system
    // This is necessary because vulkan is a platform agnostic API and needs to know how to interface with the windowing system
    unsigned int extensionCount = 0;
    if (SDL_TRUE != SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, nullptr)) {
        throw std::runtime_error("Unable to query the number of Vulkan instance extensions");
    }

    // Use the amount of extensions queried before to retrieve the names of the extensions
    std::vector<const char*> ext_names(extensionCount);
    if (SDL_TRUE != SDL_Vulkan_GetInstanceExtensions(window, &extensionCount, ext_names.data())) {
        throw std::runtime_error("Unable to query the number of Vulkan instance extension names");
    }

    // std::cout << "found " << extensionCount << " Vulkan instance extensions:\n";
    for (unsigned int i = 0; i < extensionCount; i++) {
        //std::cout << i << ": " << ext_names[i] << std::endl;
        outExtensions.emplace_back(ext_names[i]);
    }

    // Add debug display extension, we need this to relay debug messages
    outExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    //std::cout << std::endl;
}

void getAvailableVulkanLayers(std::vector<std::string>& outLayers) {
    // Figure out the amount of available layers
    // Layers are used for debugging / validation etc / profiling..
    unsigned int instanceLayerCount = 0;
    if (VK_SUCCESS != vkEnumerateInstanceLayerProperties(&instanceLayerCount, NULL)) {
        throw std::runtime_error("unable to query vulkan instance layer property count");
    }

    std::vector<VkLayerProperties> instance_layer_names(instanceLayerCount);
    if (VK_SUCCESS != vkEnumerateInstanceLayerProperties(&instanceLayerCount, instance_layer_names.data())) {
        throw std::runtime_error("unable to retrieve vulkan instance layer names");
    }

    std::cout << "found " << instanceLayerCount << " instance layers\n";

    std::set<std::string> requestedLayers({"VK_LAYER_KHRONOS_validation"});

    int count = 0;
    outLayers.clear();
    for (const auto& name : instance_layer_names) {
        // std::cout << count << ": " << name.layerName << ": " << name.description << std::endl;
        auto it = requestedLayers.find(std::string(name.layerName));
        if (it != requestedLayers.end())
            outLayers.emplace_back(name.layerName);
        count++;
    }

#if 0
    // Print the ones we're enabling
    std::cout << std::endl;
    for (const auto& layer : outLayers) {
        std::cout << "applying layer: " << layer.c_str() << std::endl;
    }
#endif
}

const std::set<std::string>& getRequestedLayerNames() {
    static std::set<std::string> layers;
    if (layers.empty()) {
        layers.emplace("VK_LAYER_NV_optimus");
        layers.emplace("VK_LAYER_KHRONOS_validation");
    }
    return layers;
}

void createVulkanInstance(const std::vector<std::string>& layerNameStrings, const std::vector<std::string>& extensionNameStrings, VkInstance& outInstance) {
    // Copy layers
    std::vector<const char*> layerNames;
    for (const auto& layer : layerNameStrings)
        layerNames.emplace_back(layer.c_str());

    // Copy extensions
    std::vector<const char*> extensionNames;
    for (const auto& ext : extensionNameStrings)
        extensionNames.emplace_back(ext.c_str());

    // Get the suppoerted vulkan instance version
    unsigned int api_version;
    vkEnumerateInstanceVersion(&api_version);

    // initialize the VkApplicationInfo structure
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = NULL;
    appInfo.pApplicationName = appName;
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = engineName;
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // initialize the VkInstanceCreateInfo structure
    VkInstanceCreateInfo instanceInfo = {};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pNext = NULL;
    instanceInfo.flags = 0;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
    instanceInfo.ppEnabledExtensionNames = extensionNames.data();
    instanceInfo.enabledLayerCount = static_cast<uint32_t>(layerNames.size());
    instanceInfo.ppEnabledLayerNames = layerNames.data();

    // Create vulkan runtime instance
    std::cout << "initializing Vulkan instance\n\n";
    VkResult res = vkCreateInstance(&instanceInfo, NULL, &outInstance);

    if (VK_SUCCESS == res) {
        return;
    } else if (VK_ERROR_INCOMPATIBLE_DRIVER == res) {
        throw std::runtime_error("unable to create vulkan instance, cannot find a compatible Vulkan ICD");
    }

    throw std::runtime_error("unable to create Vulkan instance: unknown error");
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData)
{
    std::cout << "validation layer: " << layerPrefix << ": " << msg << std::endl;
    return VK_FALSE;
}

VkResult createDebugReportCallbackEXT(
    VkInstance instance,
    const VkDebugReportCallbackCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugReportCallbackEXT* pCallback)
{
    auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pCallback);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

bool setupDebugCallback(VkInstance instance, VkDebugReportCallbackEXT& callback) {
    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    createInfo.pfnCallback = debugCallback;

    if (createDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
        std::cout << "unable to create debug report callback extension\n";
        return false;
    }
    return true;
}

void selectGPU(VkInstance instance, VkPhysicalDevice& outDevice, unsigned int& outQueueFamilyIndex) {
    // Get number of available physical devices, needs to be at least 1
    unsigned int physicalDeviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    if (physicalDeviceCount == 0) {
        throw std::runtime_error("No physical devices found");
    }

    // Now get the devices
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data());

    // Show device information
    std::cout << "found " << physicalDeviceCount << " GPU(s):\n";
    int count = 0;
    std::vector<VkPhysicalDeviceProperties> physicalDeviceProperties(physicalDevices.size());
    for (auto& physical_device : physicalDevices) {
        vkGetPhysicalDeviceProperties(physical_device, &(physicalDeviceProperties[count]));
        std::cout << count << ": " << physicalDeviceProperties[count].deviceName << std::endl;
        count++;
    }

    // Select one if more than 1 is available
    unsigned int selectionId = 0;
    if (physicalDeviceCount > 1)  {
        while (true) {
            std::cout << "select device: ";
            std::cin  >> selectionId;
            if (selectionId >= physicalDeviceCount) {
                std::cout << "invalid selection, expected a value between 0 and " << physicalDeviceCount - 1 << std::endl;
                continue;
            }
            break;
        }
    }

    std::cout << "selected: " << physicalDeviceProperties[selectionId].deviceName << std::endl;
    VkPhysicalDevice selectedDevice = physicalDevices[selectionId];

    // Find the number queues this device supports, we want to make sure that we have a queue that supports graphics commands
    unsigned int familyQueueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &familyQueueCount, nullptr);
    if (familyQueueCount == 0) {
        throw std::runtime_error("device has no family of queues associated with it");
    }

    // Extract the properties of all the queue families
    std::vector<VkQueueFamilyProperties> queueProperties(familyQueueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(selectedDevice, &familyQueueCount, queueProperties.data());

    // Make sure the family of commands contains an option to issue graphical commands.
    int queueNodeIndex = -1;
    for (unsigned int i = 0; i < familyQueueCount; i++) {
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

    // Set the output variables
    outDevice = selectedDevice;
    outQueueFamilyIndex = queueNodeIndex;
}

VkDevice createLogicalDevice(VkPhysicalDevice& physicalDevice, unsigned int queueFamilyIndex, const std::vector<std::string>& layerNameStrings) {
    // Copy layer names
    std::vector<const char*> layerNames;
    for (const auto& layer : layerNameStrings) {
        layerNames.emplace_back(layer.c_str());
    }
    
    // Get the number of available extensions for our graphics card
    uint32_t devicePropertyCount(0);
    if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &devicePropertyCount, NULL)) {
        throw std::runtime_error("Unable to acquire device extension property count");
    }
    std::cout << "\nfound " << devicePropertyCount << " device extensions\n";

    // Acquire their actual names
    std::vector<VkExtensionProperties> extensionProperties(devicePropertyCount);
    if (VK_SUCCESS != vkEnumerateDeviceExtensionProperties(physicalDevice, NULL, &devicePropertyCount, extensionProperties.data())) {
        throw std::runtime_error("Unable to acquire device extension property names");
    }

    // Match names against requested extension
    std::vector<const char*> devicePropertyNames;
    const std::set<std::string> requiredExtensionNames{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    int count = 0;
    for (const auto& extensionProperty : extensionProperties) {
        // std::cout << count << ": " << extensionProperty.extensionName << std::endl;
        auto it = requiredExtensionNames.find(std::string(extensionProperty.extensionName));
        if (it != requiredExtensionNames.end()) {
            devicePropertyNames.emplace_back(extensionProperty.extensionName);
        }
        count++;
    }

    // Warn if not all required extensions were found
    if (requiredExtensionNames.size() != devicePropertyNames.size()) {
        throw std::runtime_error("not all required device extensions are supported!");
    }

    std::cout << std::endl;
    for (const auto& name : devicePropertyNames) {
        std::cout << "applying device extension: " << name << std::endl;
    }

    // Create queue information structure used by device based on the previously fetched queue information from the physical device
    // We create one command processing queue for graphics
    VkDeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    std::vector<float> queue_prio = { 1.0f };
    queueCreateInfo.pQueuePriorities = queue_prio.data();
    queueCreateInfo.pNext = NULL;
    queueCreateInfo.flags = 0;

    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE; // required for aniostropic filtering, the sampler must have anisotropy enabled too

    // Device creation information
    VkDeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.ppEnabledLayerNames = layerNames.data();
    deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layerNames.size());
    deviceCreateInfo.ppEnabledExtensionNames = devicePropertyNames.data();
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(devicePropertyNames.size());
    deviceCreateInfo.pNext = NULL;
    deviceCreateInfo.pEnabledFeatures = NULL;
    deviceCreateInfo.flags = 0;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

    // Finally we're ready to create a new device
    VkDevice device;
    if (VK_SUCCESS != vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)) {
        throw std::runtime_error("failed to create logical device!");
    }

    return device;
}

VkSurfaceKHR createSurface(SDL_Window* window, VkInstance instance, VkPhysicalDevice gpu, uint32_t graphicsFamilyQueueIndex) {
    VkSurfaceKHR surface; // SDL_Vulkan_DestroySurface doesn't seem to exist on my system.
    if (SDL_TRUE != SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        throw std::runtime_error("Unable to create Vulkan compatible surface using SDL");
    }

    // Make sure the surface is compatible with the queue family and gpu
    VkBool32 supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu, graphicsFamilyQueueIndex, surface, &supported);
    if (VK_TRUE != supported) {
        throw std::runtime_error("Surface is not supported by physical device!");
    }

    return surface;
}

VkQueue getPresentationQueue(VkPhysicalDevice gpu, VkDevice logicalDevice, uint graphicsQueueIndex, VkSurfaceKHR presentation_surface) {
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
    if(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, NULL) != VK_SUCCESS) {
        std::cout << "unable to query present mode count for physical device\n";
        return false;
    }

    std::vector<VkPresentModeKHR> availableModes(modeCount);
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modeCount, availableModes.data()) != VK_SUCCESS) {
        std::cout << "unable to query the various present modes for physical device\n";
        return false;
    }

    for (auto& mode : availableModes) {
        if (mode == ioMode) {
            return true;
        }
    }
    std::cout << "unable to obtain preferred display mode, fallback to FIFO\n";

    std::cout << "available present modes: " << std::endl;
    for (auto & mode : availableModes) {
        std::cout << "    "  << mode << std::endl;
    }

    ioMode = VK_PRESENT_MODE_FIFO_KHR;
    return true;
}

unsigned int getNumberOfSwapImages(const VkSurfaceCapabilitiesKHR& capabilities) {
    unsigned int number = capabilities.minImageCount + 1;
    return number > capabilities.maxImageCount ? capabilities.minImageCount : number;
}

template<typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
}

VkExtent2D getSwapImageSize(VulkanContext & context, const VkSurfaceCapabilitiesKHR& capabilities) {
    // Default size = window size
    VkExtent2D size = { (uint32_t)context.windowWidth, (uint32_t)context.windowHeight };

    // This happens when the window scales based on the size of an image
    if (capabilities.currentExtent.width == 0xFFFFFFF) {
        size.width  = clamp(size.width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        size.height = clamp(size.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    } else {
        size = capabilities.currentExtent;
    }
    return size;
}

bool getImageUsage(const VkSurfaceCapabilitiesKHR& capabilities, VkImageUsageFlags& foundUsages) {
    const std::vector<VkImageUsageFlags> desiredUsages{desiredImageUsage};

    foundUsages = desiredUsages[0];

    for (const auto& usage : desiredUsages) {
        VkImageUsageFlags image_usage = usage & capabilities.supportedUsageFlags;
        if (image_usage != usage) {
            std::cout << "unsupported image usage flag: " << usage << std::endl;
            return false;
        }

        // Add bit if found as supported color
        foundUsages = (foundUsages | usage);
    }

    return true;
}

VkSurfaceTransformFlagBitsKHR getSurfaceTransform(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.supportedTransforms & desiredTransform) {
        return desiredTransform;
    }
    std::cout << "unsupported surface transform: " << desiredTransform;
    return capabilities.currentTransform;
}

bool getSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR& outFormat) {
    unsigned int count(0);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr) != VK_SUCCESS) {
        std::cout << "unable to query number of supported surface formats";
        return false;
    }

    std::vector<VkSurfaceFormatKHR> foundFormats(count);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, foundFormats.data()) != VK_SUCCESS) {
        std::cout << "unable to query all supported surface formats\n";
        return false;
    }

    // This means there are no restrictions on the supported format.
    // Preference would work
    if (foundFormats.size() == 1 && foundFormats[0].format == VK_FORMAT_UNDEFINED) {
        outFormat.format = surfaceFormat;
        outFormat.colorSpace = colorSpace;
        return true;
    }

    // Otherwise check if both are supported
    for (const auto& outerFormat : foundFormats) {
        // Format found
        if (outerFormat.format == surfaceFormat) {
            outFormat.format = outerFormat.format;
            for (const auto& innerFormat : foundFormats) {
                // Color space found
                if (innerFormat.colorSpace == colorSpace) {
                    outFormat.colorSpace = innerFormat.colorSpace;
                    return true;
                }
            }

            // No matching color space, pick first one
            std::cout << "warning: no matching color space found, picking first available one\n!";
            outFormat.colorSpace = foundFormats[0].colorSpace;
            return true;
        }
    }

    // No matching formats found
    std::cout << "warning: no matching color format found, picking first available one\n";
    outFormat = foundFormats[0];
    return true;
}

void createSwapChain(VulkanContext & context, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain) {
    vkDeviceWaitIdle(device);

    // Get the surface capabilities
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS) {
        throw std::runtime_error("failed to acquire surface capabilities");
    }

    // Get the image presentation mode (synced, immediate etc.)
    VkPresentModeKHR presentation_mode = preferredPresentationMode;
    if (!getPresentationMode(surface, physicalDevice, presentation_mode)) {
        throw std::runtime_error("failed to get presentation mode");
    }

    // Get other swap chain related features
    unsigned int swapImageCount = getNumberOfSwapImages(surfaceCapabilities);
    std::cout << "swap chain image count: " << swapImageCount << std::endl;

    // Size of the images
    VkExtent2D swap_image_extent = getSwapImageSize(context, surfaceCapabilities);

    if (swap_image_extent.width != context.windowWidth || swap_image_extent.height != context.windowHeight) {
        throw std::runtime_error("unexpected swap image size");
    }

    // Get image usage (color etc.)
    VkImageUsageFlags usageFlags;
    if (!getImageUsage(surfaceCapabilities, usageFlags)) {
        throw std::runtime_error("failed to get image usage flags");
    }

    // Get the transform, falls back on current transform when transform is not supported
    VkSurfaceTransformFlagBitsKHR transform = getSurfaceTransform(surfaceCapabilities);

    // Get swapchain image format
    VkSurfaceFormatKHR imageFormat;
    if (!getSurfaceFormat(physicalDevice, surface, imageFormat)) {
        throw std::runtime_error("failed to get surface format");
    }

    context.colorFormat = imageFormat.format;

    // Old swap chain
    VkSwapchainKHR oldSwapChain = outSwapChain;

    // Populate swapchain creation info
    VkSwapchainCreateInfoKHR swapInfo;
    swapInfo.pNext = nullptr;
    swapInfo.flags = 0;
    swapInfo.surface = surface;
    swapInfo.minImageCount = swapImageCount;
    swapInfo.imageFormat = imageFormat.format;
    swapInfo.imageColorSpace = imageFormat.colorSpace;
    swapInfo.imageExtent = swap_image_extent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = usageFlags;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.queueFamilyIndexCount = 0;
    swapInfo.pQueueFamilyIndices = nullptr;
    swapInfo.preTransform = transform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = presentation_mode;
    swapInfo.clipped = true;
    swapInfo.oldSwapchain = NULL;
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    // Create a new one
    if (VK_SUCCESS != vkCreateSwapchainKHR(device, &swapInfo, nullptr, &outSwapChain)) {
        throw std::runtime_error("unable to create swap chain");
    }

    // Destroy old swap chain
    if (oldSwapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, oldSwapChain, nullptr);
        oldSwapChain = VK_NULL_HANDLE;
    }
}

void getSwapChainImageHandles(VkDevice device, VkSwapchainKHR chain, std::vector<VkImage>& outImageHandles) {
    unsigned int imageCount = 0;
    if (VK_SUCCESS != vkGetSwapchainImagesKHR(device, chain, &imageCount, nullptr)) {
        throw std::runtime_error("unable to get number of images in swap chain");
    }

    outImageHandles.clear();
    outImageHandles.resize(imageCount);
    if (VK_SUCCESS != vkGetSwapchainImagesKHR(device, chain, &imageCount, outImageHandles.data())) {
        throw std::runtime_error("unable to get image handles from swap chain");
    }
}

void makeChainImageViews(VkDevice device, VkSwapchainKHR swapChain, VkFormat colorFormat, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews) {
    imageViews.resize(images.size());
    for (size_t i=0; i < images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];  // The image from the swap chain
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;  // Format of the swap chain images

        // Subresource range describes which parts of the image are accessible
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;  // Color attachment
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
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // can be 0, but validation warns about implicit command buffer resets

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    return commandPool;
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

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // Check if this memory type is included in memoryTypeBits (bitwise AND)
        if ((memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

void generateMipmaps(VkDevice device, VkImage image, VkCommandPool commandPool, VkQueue graphicsQueue, int width, int height, size_t mipLevelCount) {
    VkImageMemoryBarrier writeToReadBarrier = {};
    writeToReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    writeToReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    writeToReadBarrier.image = image;
    writeToReadBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    writeToReadBarrier.subresourceRange.baseArrayLayer = 0;
    writeToReadBarrier.subresourceRange.layerCount = 1;
    writeToReadBarrier.subresourceRange.levelCount = 1;
    writeToReadBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    writeToReadBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    writeToReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    writeToReadBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    VkImageMemoryBarrier undefinedToWriteBarrier = writeToReadBarrier;
    undefinedToWriteBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    undefinedToWriteBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    undefinedToWriteBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    undefinedToWriteBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkImageMemoryBarrier readToSampleBarrier = writeToReadBarrier;
    readToSampleBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    readToSampleBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    readToSampleBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    readToSampleBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkImageBlit blit{}; // blit configuration shared for all mip levels
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    ScopedCommandBuffer scopedCommandBuffer(device, commandPool, graphicsQueue);

    int mipWidth = width;
    int mipHeight = height;
    
    for (size_t i=1; i<mipLevelCount; i++) {
        undefinedToWriteBarrier.subresourceRange.baseMipLevel = i;
        writeToReadBarrier.subresourceRange.baseMipLevel = i - 1;
        readToSampleBarrier.subresourceRange.baseMipLevel = i - 1;

        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.mipLevel = i - 1;
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.mipLevel = i;

        // this mip undefined -> dest
        vkCmdPipelineBarrier(scopedCommandBuffer.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &undefinedToWriteBarrier);

        // previous mip write -> read
        vkCmdPipelineBarrier(scopedCommandBuffer.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &writeToReadBarrier);
        
        vkCmdBlitImage(scopedCommandBuffer.commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        // previous mip read -> sample
        vkCmdPipelineBarrier(scopedCommandBuffer.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &readToSampleBarrier);
        
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // transition the final mip to shader read
    VkImageMemoryBarrier writeToSampleBarrier = readToSampleBarrier;
    writeToSampleBarrier.subresourceRange.baseMipLevel = mipLevelCount - 1;
    writeToSampleBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    writeToSampleBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    vkCmdPipelineBarrier(scopedCommandBuffer.commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &writeToSampleBarrier);

    scopedCommandBuffer.submitAndWait();
}

void copyBufferToImage(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    ScopedCommandBuffer scopedCommandBuffer(device, commandPool, graphicsQueue);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(scopedCommandBuffer.commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    scopedCommandBuffer.submitAndWait();
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

void transitionImageLayout(VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, VkImage image, VkFormat format, size_t mipLevels, VkImageLayout oldLayout, VkImageLayout newLayout) {
    ScopedCommandBuffer scopedCommandBuffer(device, commandPool, graphicsQueue);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    // it would be nice to print descriptive strings instead of integers here
    std::cout << "transitioning image from " << oldLayout << " to " << newLayout << std::endl;

    vkCmdPipelineBarrier(
        scopedCommandBuffer.commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    scopedCommandBuffer.submitAndWait();
}

std::tuple<VkImageView, VkImage, VkDeviceMemory> createDepthBuffer(VkPhysicalDevice gpu, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue, uint32_t width, uint32_t height) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(gpu, depthFormat, &props);
    if (0 == (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        throw std::runtime_error("requested format does not have tiling features");
    }

    const size_t oneMipLevel = 1;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = oneMipLevel;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // we must "transition" this image to a device-optimal format
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkImage image;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create depth image");
    }

    VkImageAspectFlags imageAspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
        imageAspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkMemoryRequirements memoryRequirements;
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(gpu, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceMemory memory;
    if (VK_SUCCESS != vkAllocateMemory(device, &allocateInfo, nullptr, &memory)) {
        throw std::runtime_error("failed to allocate depth buffer memory");
    }

    if (VK_SUCCESS != vkBindImageMemory(device, image, memory, 0)) {
        throw std::runtime_error("failed to bind depth image memory");
    }
    
    // image view must be after binding image memory.  Moving this above bind will not cause a validation failure, but will fail to await the queue later.
    VkImageView imageView = createImageView(device, image, depthFormat, imageAspects, oneMipLevel);

    transitionImageLayout(device, commandPool, graphicsQueue, image, depthFormat, oneMipLevel, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    return std::make_tuple(imageView, image, memory);
}

VkSemaphore createSemaphore() {
    VkSemaphoreCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore semaphore;
    
    if (vkCreateSemaphore($context().device, &createInfo, NULL, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("failed to create semaphore");
    }

    $context().semaphores.push_back(semaphore);

    return semaphore;
}

VulkanContext::VulkanContext(SDL_Window * window):window(window) {
    if ($context.contextInstance != nullptr) {
        throw std::runtime_error("VulkanContext already exists");
    }

    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    this->windowWidth = windowWidth;
    this->windowHeight = windowHeight;

   // Get available vulkan extensions, necessary for interfacing with native window
    // SDL takes care of this call and returns, next to the default VK_KHR_surface a platform specific extension
    // When initializing the vulkan instance these extensions have to be enabled in order to create a valid
    // surface later on.
    std::vector<std::string> foundExtensions;
    getAvailableVulkanExtensions(window, foundExtensions);

    // Get available vulkan layer extensions, notify when not all could be found
    std::vector<std::string> foundLayers;
    getAvailableVulkanLayers(foundLayers);

    // Warn when not all requested layers could be found
    if (foundLayers.size() != getRequestedLayerNames().size())
        std::cout << "warning! not all requested layers could be found!\n";

    // Create Vulkan Instance
    createVulkanInstance(foundLayers, foundExtensions, this->instance);

    // Vulkan messaging callback
    setupDebugCallback(this->instance, this->callback);

    // Select GPU after succsessful creation of a vulkan instance (jeeeej no global states anymore)
    this->graphicsQueueIndex = -1;
    selectGPU(this->instance, this->physicalDevice, this->graphicsQueueIndex);

    // Create a logical device that interfaces with the physical device
    this->device = createLogicalDevice(this->physicalDevice, this->graphicsQueueIndex, foundLayers);

    // Create the surface we want to render to, associated with the window we created before
    // This call also checks if the created surface is compatible with the previously selected physical device and associated render queue
    this->presentationSurface = createSurface(window, this->instance, this->physicalDevice, this->graphicsQueueIndex);
    this->presentationQueue = getPresentationQueue(this->physicalDevice, this->device, this->graphicsQueueIndex, this->presentationSurface);

    // swap chain with image handles and views
    this->swapchain = VK_NULL_HANDLE; // start null as createSwapChain recreates the chain if it exists
    createSwapChain(*this, this->presentationSurface, this->physicalDevice, this->device, this->swapchain);
    getSwapChainImageHandles(this->device, this->swapchain, this->swapchainImages);

    // we have the image count now, this is used for every set of framebuffers we will need
    this->swapchainImageCount = this->swapchainImages.size();
    makeChainImageViews(this->device, this->swapchain, this->colorFormat, this->swapchainImages, this->swapchainImageViews);

    this->commandPool = createCommandPool(this->device, this->graphicsQueueIndex);
    this->commandBuffers.resize(this->swapchainImages.size());
    for (auto & commandBuffer : commandBuffers) {
        commandBuffer = createCommandBuffer(device, commandPool);
    }

    vkGetDeviceQueue(this->device, this->graphicsQueueIndex, 0, &this->graphicsQueue);

    $context.contextInstance = this;

    for (size_t i=0; i<swapchainImageCount; i++) {
        imageAvailableSemaphores.push_back(createSemaphore());
        renderFinishedSemaphores.push_back(createSemaphore());
    } 
}

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) {
        func(instance, callback, pAllocator);
    }
}

std::tuple<VkBuffer, VkDeviceMemory> createBuffer(VkPhysicalDevice gpu, VkDevice device, VkBufferUsageFlags usageFlags, size_t byteCount) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Not shared across multiple queue families

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

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
    samplerInfo.anisotropyEnable = VK_TRUE; // experiment with VK_FALSE to see blurring
    samplerInfo.maxAnisotropy = 16;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f; // we can sample at higher mip levels but the use cases are uncommon
    samplerInfo.maxLod = 13.0f; // 4k textures will have no more than 13 mip levels, so this is plenty

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("failed to create texture sampler");
    }

    return textureSampler;
}

void rebuildPresentationResources() {
    VulkanContext & context = $context();
    vkDeviceWaitIdle(context.device);
    for (VkImageView view : context.swapchainImageViews) {
        vkDestroyImageView(context.device, view, nullptr);
    }
    vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);

    context.swapchain = VK_NULL_HANDLE;
    createSwapChain(context, context.presentationSurface, context.physicalDevice, context.device, context.swapchain);
    getSwapChainImageHandles(context.device, context.swapchain, context.swapchainImages);
    makeChainImageViews(context.device, context.swapchain, context.colorFormat, context.swapchainImages, context.swapchainImageViews);
}

VulkanContext::~VulkanContext() {
    vkQueueWaitIdle(graphicsQueue); // wait until we're done or render semaphores may be in use

    // clean up managed resource collections
    for (auto semaphore : semaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    for (auto fence : fences) {
        vkDestroyFence(device, fence, nullptr);
    }
    for (auto commandBuffer : commandBuffers) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
    for (VkDescriptorSetLayout layout : layouts) {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    }
    for (VkPipelineLayout layout : pipelineLayouts) {
        vkDestroyPipelineLayout(device, layout, nullptr);
    }
    for (VkPipeline pipeline : pipelines) {
        vkDestroyPipeline(device, pipeline, nullptr);
    }
    for (VkRenderPass renderPass : renderPasses) {
        vkDestroyRenderPass(device, renderPass, nullptr);
    }

    // clean up other global resources
    vkDestroyCommandPool(device, commandPool, nullptr);
    for (VkImageView view : swapchainImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, presentationSurface, nullptr);
    vkDestroyDevice(device, nullptr);
    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroyInstance(instance, nullptr);

    $context.contextInstance = nullptr;
}

struct RenderpassBuilder {
    std::vector<VkAttachmentDescription> attachmentDescriptions;
    std::vector<VkSubpassDescription> subpassDescriptions;
    std::vector<VkSubpassDependency> dependencies;
    std::vector<VkAttachmentReference> colorAttachmentRefs;
    VkAttachmentReference depthAttachmentRef;
    bool hasDepthRef;
    RenderpassBuilder() {
        attachmentDescriptions.reserve(2);
        subpassDescriptions.reserve(1);
        dependencies.reserve(1);
        hasDepthRef = false;
        depthAttachmentRef = {};
    }
    RenderpassBuilder& colorAttachment() {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = $context().colorFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachmentDescriptions.push_back(colorAttachment);
        return *this;
    }
    RenderpassBuilder& depthAttachment() {
        VkAttachmentDescription depthAttachment = {};
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // should already be in this format
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachmentDescriptions.push_back(depthAttachment);
        return *this;
    }
    RenderpassBuilder& colorRef(int index) {
        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = index;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachmentRefs.push_back(colorAttachmentRef);
        return *this;
    }
    RenderpassBuilder& depthRef(int index) {
        if (hasDepthRef) {
            throw std::runtime_error("a depth ref already exists and has not been put into a subpass");
        }
        hasDepthRef = true;
        depthAttachmentRef.attachment = index;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return *this;
    }
    VkRenderPass build() {
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (this->colorAttachmentRefs.size() > 0) {
            subpass.colorAttachmentCount = this->colorAttachmentRefs.size();
            subpass.pColorAttachments = this->colorAttachmentRefs.data();
        }
        if (this->hasDepthRef) {
            subpass.pDepthStencilAttachment = &(this->depthAttachmentRef);
        }

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        renderPassInfo.attachmentCount = this->attachmentDescriptions.size();
        renderPassInfo.pAttachments = this->attachmentDescriptions.data();

        VkRenderPass renderPass;
        if (VK_SUCCESS != vkCreateRenderPass($context().device, &renderPassInfo, nullptr, &renderPass)) {
            throw std::runtime_error("failed to create render pass");
        }

        $context().renderPasses.emplace(renderPass);

        return renderPass;
    }
};

struct ShaderBuilder {
    VkShaderStageFlagBits stage;
    std::vector<char> code;

    ShaderBuilder() : stage(VK_SHADER_STAGE_VERTEX_BIT) {}
    ShaderBuilder& vertex() {
        stage = VK_SHADER_STAGE_VERTEX_BIT;
        return *this;
    }
    ShaderBuilder& fragment() {
        stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        return *this;
    }
    ShaderBuilder& compute() {
        stage = VK_SHADER_STAGE_COMPUTE_BIT;
        return *this;
    }

    ShaderBuilder& fromFile(const char * fileName) {
        std::ifstream file(fileName, std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open shader file");
        }
        size_t fileSize = (size_t)file.tellg();
        file.seekg(0);

        code.resize(fileSize);
        file.read((char*)&code[0], fileSize);
        file.close();
        return *this;
    }
    ShaderBuilder& fromBuffer(const char * data, size_t size) {
        code.clear();
        code.insert(code.end(), data, data + size);
        return *this;
    }
};

struct ShaderModule {
    VkShaderModule module;
    ShaderModule(ShaderBuilder & builder) {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = builder.code.size();
        createInfo.pCode = (uint32_t*)builder.code.data();

        if (VK_SUCCESS != vkCreateShaderModule($context().device, &createInfo, nullptr, &module)) {
            throw std::runtime_error("failed to create shader module");
        }
    }
    ~ShaderModule() {
        vkDestroyShaderModule($context().device, module, nullptr);
    }
    operator VkShaderModule() const { return module; }
};

struct ImageBuilder {
    bool buildMipmaps;
    void * bytes;
    int byteCount;
    VkFormat format;
    VkExtent2D extent; // width and height
    bool isDepthBuffer;
    ImageBuilder() : buildMipmaps(true), bytes(nullptr), isDepthBuffer(false) {}
    ImageBuilder & createMipmaps(bool buildMipmaps) {
        this->buildMipmaps = buildMipmaps;
        return *this;
    }
    ImageBuilder & forDepthBuffer() {
        bytes = nullptr;
        byteCount = 0;
        buildMipmaps = false;
        extent.width = $context().windowWidth;
        extent.height = $context().windowHeight;
        this->format = depthFormat;
        isDepthBuffer = true;
        return *this;
    }
    ImageBuilder & forFramebuffer(VkFormat format) {
        bytes = nullptr;
        byteCount = 0;
        buildMipmaps = false;
        extent.width = $context().windowWidth;
        extent.height = $context().windowHeight;
        this->format = format;
        isDepthBuffer = false;
        return *this;
    }
    ImageBuilder & fromBytes(void * bytes, int byteCount, int width, int height, VkFormat format) {
        this->bytes = bytes;
        this->byteCount = byteCount;
        extent.width = width;
        extent.height = height;
        this->format = format;
        isDepthBuffer = false;
        return *this;
    }
};

struct Image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;
    Image(Image && other) : image(other.image), memory(other.memory), imageView(other.imageView) {
        other.image = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.imageView = VK_NULL_HANDLE;
    }
    Image(ImageBuilder & builder) {
        size_t mipLevels;
        if (builder.buildMipmaps) {
            mipLevels = std::floor(log2(std::max(builder.extent.width, builder.extent.height))) + 1;
        } else {
            mipLevels = 1;
        }

        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = builder.extent.width;
        imageInfo.extent.height = builder.extent.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = builder.format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // we must "transition" this image to a device-optimal format

        if (builder.isDepthBuffer) {
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        } else {
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT // copy bytes from image into mip levels
                | VK_IMAGE_USAGE_TRANSFER_DST_BIT // copy bytes into image
                | VK_IMAGE_USAGE_SAMPLED_BIT; // read by sampler in shader
        }

        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage($context().device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan image");
        }

        VkMemoryRequirements memoryRequirements = {};
        vkGetImageMemoryRequirements($context().device, image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType($context().physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (VK_SUCCESS != vkAllocateMemory($context().device, &allocateInfo, nullptr, &memory)) {
            throw std::runtime_error("failed to allocate image memory");
        }
        if (VK_SUCCESS != vkBindImageMemory($context().device, image, memory, 0)) {
            throw std::runtime_error("failed to bind memory to image");
        }

        VkImageLayout desiredLayout;
        if (builder.isDepthBuffer) {
            desiredLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        } else {
            desiredLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        // Vulkan spec says images MUST be created either undefined or preinitialized layout, so we can't jump straight to desired layout.
        transitionImageLayout($context().device, $context().commandPool, $context().graphicsQueue, image, builder.format, 1, VK_IMAGE_LAYOUT_UNDEFINED, desiredLayout);

        if (builder.byteCount > 0) { 
            VkBuffer stagingBuffer;
            VkDeviceMemory stagingMemory;

            // put the image bytes into a buffer for transitioning
            std::tie(stagingBuffer, stagingMemory) = createBuffer($context().physicalDevice, $context().device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, builder.byteCount);
            void * stagingBytes;
            vkMapMemory($context().device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingBytes);
            memcpy(stagingBytes, builder.bytes, (size_t)builder.byteCount);
            vkUnmapMemory($context().device, stagingMemory);

            // Now the image is in DST_OPTIMAL layout and we can copy the image data to it.
            copyBufferToImage($context().device, $context().commandPool, $context().graphicsQueue, stagingBuffer, image, builder.extent.width, builder.extent.height);
            vkFreeMemory($context().device, stagingMemory, nullptr);
            vkDestroyBuffer($context().device, stagingBuffer, nullptr);
        }

        if (builder.buildMipmaps) {
            generateMipmaps($context().device, image, $context().commandPool, $context().graphicsQueue, builder.extent.width, builder.extent.height, mipLevels);
        } else {
            // todo: transition the single image to the optimal layout for sampling
        }

        VkImageAspectFlags aspectFlags;
        if (builder.isDepthBuffer) {
            aspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        } else {
            aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        imageView = createImageView($context().device, image, builder.format, aspectFlags, mipLevels);
    }
    ~Image() {
        // these are all safe if they are already VK_NULL_HANDLE
        vkDestroyImageView($context().device, imageView, nullptr);
        vkFreeMemory($context().device, memory, nullptr);
        vkDestroyImage($context().device, image, nullptr);
    }
};

struct TextureSampler {
    VkSampler sampler;
    TextureSampler() : sampler(createSampler($context().device)) {}
    ~TextureSampler() {
        vkDestroySampler($context().device, sampler, nullptr);
    }
    operator VkSampler() const {
        return sampler;
    }
};

struct BufferBuilder {
    VkBufferUsageFlags usage;
    size_t byteCount;

    BufferBuilder(size_t byteCount) : usage(0), byteCount(byteCount) {}
    BufferBuilder & vertex() {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        return *this;
    }
    BufferBuilder & index() {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        return *this;
    }
    BufferBuilder & uniform() {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        return *this;
    }
    BufferBuilder & storage() {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        return *this;
    }
    BufferBuilder & size(size_t byteCount) {
        this->byteCount = byteCount;
        return *this;
    }
};

struct Buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size;

    Buffer(BufferBuilder & builder) : buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(builder.byteCount) {
        std::tie(buffer, memory) = createBuffer($context().physicalDevice, $context().device, builder.usage, builder.byteCount);
    }
    void setData(void * bytes, size_t size) {
        if (size != this->size) {
            throw std::runtime_error("Uniform buffer size mismatch");
        }

        void* mapped;
        vkMapMemory($context().device, memory, 0, size, 0, &mapped);
        memcpy(mapped, bytes, size);
        vkUnmapMemory($context().device, memory);
    }
    ~Buffer() {
        vkFreeMemory($context().device, memory, nullptr);
        vkDestroyBuffer($context().device, buffer, nullptr);
    }
    operator VkBuffer() const {
        return buffer;
    }
};

struct DynamicBuffer {
    std::vector<Buffer> buffers;
    DynamicBuffer(BufferBuilder & builder) {
        buffers.reserve($context().swapchainImageCount);
        for (size_t i = 0; i < $context().swapchainImageCount; ++i) {
            buffers.emplace_back(builder);
        }
    }
    void setData(void* data, size_t size, size_t frameInFlightIndex) {
        buffers[frameInFlightIndex].setData(data, size);
    }
    VkBuffer getBuffer(size_t frameInFlightIndex) const {
        return buffers[frameInFlightIndex].buffer;
    }
};

struct DescriptorLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    DescriptorLayoutBuilder() { }
    DescriptorLayoutBuilder & addStorageBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding desc = {};
        desc.binding = binding,
        desc.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        desc.descriptorCount = count,
        desc.stageFlags = stages;
        bindings.push_back(desc);
        return *this;
    }
    DescriptorLayoutBuilder & addSampler(uint32_t binding, uint32_t count, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding desc = {};
        desc.binding = binding,
        desc.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        desc.descriptorCount = count,
        desc.stageFlags = stages;
        bindings.push_back(desc);
        return *this;
    }
    DescriptorLayoutBuilder & addUniformBuffer(uint32_t binding, uint32_t count, VkShaderStageFlags stages) {
        VkDescriptorSetLayoutBinding desc = {};
        desc.binding = binding,
        desc.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        desc.descriptorCount = count,
        desc.stageFlags = stages;
        bindings.push_back(desc);
        return *this;
    }
    VkDescriptorSetLayout build() {
        VkDescriptorSetLayoutCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings = bindings.data();

        VkDescriptorSetLayout layout;
        if (vkCreateDescriptorSetLayout($context().device, &info, nullptr, &layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout");
        }
        $context().layouts.emplace(layout);
        return layout;
    }
    void reset() {
        bindings.clear();
    }
};

struct DescriptorPoolBuilder {
    std::vector<VkDescriptorPoolSize> sizes;
    uint32_t _maxDescriptorSets;
    DescriptorPoolBuilder():_maxDescriptorSets(1) {}
    DescriptorPoolBuilder & addStorageBuffer(uint32_t count) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, count});
        return *this;
    }
    DescriptorPoolBuilder & addSampler(uint32_t count) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count});
        return *this;
    }
    DescriptorPoolBuilder & addUniformBuffer(uint32_t count) {
        sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, count});
        return *this;
    }
    DescriptorPoolBuilder & maxSets(uint32_t count) {
        _maxDescriptorSets = count;
        return *this;
    }
};

struct DescriptorPool {
    VkDescriptorPool pool;
    DescriptorPool(DescriptorPoolBuilder & builder) {
        if (builder.sizes.empty()) {
            throw std::runtime_error("No sizes provided for Descriptor Pool");
        }
        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.poolSizeCount = builder.sizes.size();
        descriptorPoolCreateInfo.pPoolSizes = builder.sizes.data();
        descriptorPoolCreateInfo.maxSets = builder._maxDescriptorSets;

        if (VK_SUCCESS != vkCreateDescriptorPool($context().device, &descriptorPoolCreateInfo, nullptr, &pool)) {
            throw std::runtime_error("Failed to create descriptor pool");
        }
    }
    void reset() {
        // freeing each descriptor individually requires the pool have the "free" bit. Look online for use cases for individual free.
        vkResetDescriptorPool($context().device, pool, 0);
    }
    VkDescriptorSet allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType  = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool  = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet descriptorSet;
        if (VK_SUCCESS != vkAllocateDescriptorSets($context().device, &allocInfo, &descriptorSet)) {
            throw std::runtime_error("Failed to allocate descriptor set");
        }
        return descriptorSet;
    }
    ~DescriptorPool() {
        reset();
        vkDestroyDescriptorPool($context().device, pool, nullptr);
    }
};

struct DescriptorSetBinder {
    std::vector<VkWriteDescriptorSet> descriptorWriteSets;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    DescriptorSetBinder() {}
    void bindSampler(VkDescriptorSet descriptorSet, uint32_t bindingIndex, TextureSampler & sampler, Image & image) {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = image.imageView;
        imageInfo.sampler = sampler;

        imageInfos.push_back(imageInfo);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = bindingIndex; // match binding point in shader
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;

        // store index into info vector since pointer may become invalidated
        descriptorWrite.pImageInfo = (VkDescriptorImageInfo*)(imageInfos.size()-1);

        descriptorWriteSets.push_back(descriptorWrite);
    }
    void bindUniformBuffer(VkDescriptorSet descriptorSet, uint32_t bindingIndex, Buffer & buffer) {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = buffer.size;
        bufferInfos.push_back(bufferInfo);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = bindingIndex; // match binding point in shader
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;

        // store index into info vector since pointer may become invalidated
        descriptorWrite.pBufferInfo = (VkDescriptorBufferInfo*)(bufferInfos.size()-1);

        descriptorWriteSets.push_back(descriptorWrite);
    }
    void bindStorageBuffer(VkDescriptorSet descriptorSet, uint32_t bindingIndex, Buffer & buffer) {
        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = VK_WHOLE_SIZE;
        bufferInfos.push_back(bufferInfo);

        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = bindingIndex; // match binding point in shader
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrite.descriptorCount = 1;

        // store index into info vector since pointer may become invalidated
        descriptorWrite.pBufferInfo = (VkDescriptorBufferInfo*)(bufferInfos.size()-1);

        descriptorWriteSets.push_back(descriptorWrite);
    }
    void updateSets() {
        // we can't keep pointers into the vectors because vectors can resize
        // convert indices into up-to-date pointers
        for (auto& write : descriptorWriteSets) {
            switch (write.descriptorType)
            {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    write.pBufferInfo = &bufferInfos[(size_t)write.pBufferInfo];
                    break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    write.pImageInfo = &imageInfos[(size_t)write.pImageInfo];
                    break;
            }
        }
        vkUpdateDescriptorSets($context().device, descriptorWriteSets.size(), descriptorWriteSets.data(), 0, nullptr);
        descriptorWriteSets.clear();
        imageInfos.clear();
        bufferInfos.clear();
    }
};

VkFence createFence() {
    VkFenceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    if (VK_SUCCESS != vkCreateFence($context().device, &createInfo, nullptr, &fence)) {
        throw std::runtime_error("failed to create fence");
    }
    $context().fences.push_back(fence);
    return fence;
}

enum ImageType {
    None = 0,
    Present = 1,
    Color = 2,
    Depth = 3
};

struct FramebufferBuilder {
    std::vector<ImageType> imageTypes;
    size_t swapchainIndex;
    FramebufferBuilder():swapchainIndex(0) { }
    FramebufferBuilder & present(size_t swapchainIndex) {
        if (swapchainIndex >= $context().swapchainImageCount) {
            throw std::runtime_error("Invalid swapchain index");
        }
        imageTypes.push_back(ImageType::Present);
        this->swapchainIndex = swapchainIndex;
        return *this;
    }
    FramebufferBuilder & color() {
        imageTypes.push_back(ImageType::Color);
        return *this;
    }
    FramebufferBuilder & depth() {
        imageTypes.push_back(ImageType::Depth);
        return *this;
    }
};

struct Framebuffer {
    std::vector<Image> images;
    VkFramebuffer framebuffer;
    VkRenderPass renderpass;
    Framebuffer(Framebuffer && other):images(std::move(other.images)),framebuffer(other.framebuffer),renderpass(other.renderpass) {
        other.framebuffer = VK_NULL_HANDLE;
        other.renderpass = VK_NULL_HANDLE;
    }
    Framebuffer(FramebufferBuilder & builder, VkRenderPass renderpass):renderpass(renderpass) {
        if (builder.imageTypes.empty()) {
            throw std::runtime_error("Framebuffer builder must specify at least one image type");
        }
        std::vector<VkImageView> imageViews;
        for (ImageType imageType : builder.imageTypes) {
            switch (imageType) {
                case ImageType::Color:
                    images.emplace_back(ImageBuilder().forFramebuffer(VK_FORMAT_R8G8B8A8_UNORM));
                    imageViews.push_back(images.back().imageView);
                    break;
                case ImageType::Depth:
                    images.emplace_back(ImageBuilder().forDepthBuffer());
                    imageViews.push_back(images.back().imageView);
                    break;
                case ImageType::Present:
                    if (builder.swapchainIndex >= $context().swapchainImageCount) {
                        throw std::runtime_error("builder swapchain index is greater than swapchain image count");
                    }
                    imageViews.push_back($context().swapchainImageViews[builder.swapchainIndex]);
                    break;
            }
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderpass;
        framebufferInfo.attachmentCount = imageViews.size();
        framebufferInfo.pAttachments = imageViews.data();
        framebufferInfo.width = $context().windowWidth;
        framebufferInfo.height = $context().windowHeight; 
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer($context().device, &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer");
        }
    }
    ~Framebuffer() {
        vkDestroyFramebuffer($context().device, framebuffer, nullptr); // safe if already VK_NULL_HANDLE
    }
};

VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout> & descriptorSetLayouts) {
    if (descriptorSetLayouts.empty()) {
        throw std::runtime_error("builder reqires at least one pipeline layout to build");
    }
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount = descriptorSetLayouts.size();  
    pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts.data();

    VkPipelineLayout layout;
    if (VK_SUCCESS != vkCreatePipelineLayout($context().device, &pipelineLayoutCreateInfo, nullptr, &layout)) {
        throw std::runtime_error("failed to create pipeline layout");
    }
    $context().pipelineLayouts.emplace(layout);
    return layout;
}

struct GraphicsPipelineBuilder {
    VkPipelineLayout pipelineLayout;
    VkRenderPass renderPass;
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    GraphicsPipelineBuilder(VkPipelineLayout layout, VkRenderPass renderPass) : pipelineLayout(layout), renderPass(renderPass) {}
    GraphicsPipelineBuilder & addVertexShader(ShaderModule & vertexShaderModule, const char * entryPoint = "main") {
        VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertexShaderModule.module;
        vertShaderStageInfo.pName = entryPoint;
        shaderStages.push_back(vertShaderStageInfo);
        return *this;
    }
    GraphicsPipelineBuilder & addFragmentShader(ShaderModule & fragmentShaderModule, const char *entryPoint = "main") {
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragmentShaderModule.module;
        fragShaderStageInfo.pName = entryPoint;
        shaderStages.push_back(fragShaderStageInfo);
        return *this;
    }
    GraphicsPipelineBuilder & vertexBinding(size_t bindingIndex, size_t stride) {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = bindingIndex;
        bindingDescription.stride = stride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.push_back(bindingDescription);
        return *this;
    }
    GraphicsPipelineBuilder & instanceVertexBinding(size_t bindingIndex, size_t stride) {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = bindingIndex;
        bindingDescription.stride = stride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        bindingDescriptions.push_back(bindingDescription);
        return *this;
    }
    GraphicsPipelineBuilder & vertexFloats(size_t bindingIndex, size_t location, size_t floatCount, size_t offset) {
        VkVertexInputAttributeDescription attributeDescription = {};
        attributeDescription.binding = bindingIndex;
        attributeDescription.location = location;
        attributeDescription.offset = offset;
        switch(floatCount) {
            case 3:
                attributeDescription.format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
            case 2:
                attributeDescription.format = VK_FORMAT_R32G32_SFLOAT;
                break;
            default:
                throw std::invalid_argument("unsupported float count");
        }
        vertexAttributeDescriptions.push_back(attributeDescription);
        return *this;
    }
    VkPipeline build() {
        // Pipeline vertex input state
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = bindingDescriptions.size();
        vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = vertexAttributeDescriptions.size();
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = $context().windowWidth;
        viewport.height = $context().windowHeight;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = VkExtent2D{(unsigned int)$context().windowWidth, (unsigned int)$context().windowHeight};

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
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pRasterizationState = &rasterizer;
        pipelineCreateInfo.pMultisampleState = &multisampling;
        pipelineCreateInfo.pColorBlendState = &colorBlending;
        pipelineCreateInfo.layout = pipelineLayout;  // Pipeline layout created earlier
        pipelineCreateInfo.renderPass = renderPass;
        pipelineCreateInfo.subpass = 0;  // Index of the subpass where this pipeline will be used
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;  // Not deriving from another pipeline
        pipelineCreateInfo.pDepthStencilState = &depthStencil;
        
        if (vkCreateGraphicsPipelines($context().device,  VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline");
        }
        
        $context().pipelines.emplace(pipeline);
        return pipeline;
    }
};

VkPipeline createComputePipeline(VkPipelineLayout pipelineLayout, VkShaderModule computeShaderModule, const char * entryPoint = "main") {
    VkComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = computeShaderModule;
    pipelineInfo.stage.pName = entryPoint;
    pipelineInfo.layout = pipelineLayout;

    VkPipeline computePipeline;
    if (VK_SUCCESS != vkCreateComputePipelines($context().device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline)) {
        throw std::runtime_error("failed to create compute pipeline");
    }

    $context().pipelines.emplace(computePipeline);
    return computePipeline;
}