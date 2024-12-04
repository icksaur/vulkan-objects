#include <vector>
#include <set>
#include <iostream>
#include <stdexcept>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

const char * appName = "VulkanExample";
const char * engineName = "VulkanExampleEngine";
VkPresentModeKHR preferredPresentationMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
VkImageUsageFlags desiredImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
VkSurfaceTransformFlagBitsKHR desiredTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
VkFormat surfaceFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT; // some options are VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT

struct VulkanContext {
    bool initialized;
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
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;    // depth buffer
    VkImageView depthImageView;
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    VkCommandPool commandPool;
    VkQueue graphicsQueue;
    VulkanContext(SDL_Window * window);
    ~VulkanContext();
};

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

    std::cout << "found " << extensionCount << " Vulkan instance extensions:\n";
    for (unsigned int i = 0; i < extensionCount; i++) {
        std::cout << i << ": " << ext_names[i] << std::endl;
        outExtensions.emplace_back(ext_names[i]);
    }

    // Add debug display extension, we need this to relay debug messages
    outExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    std::cout << std::endl;
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

    std::cout << "found " << instanceLayerCount << " instance layers:\n";

    std::set<std::string> requestedLayers({"VK_LAYER_KHRONOS_validation"});

    int count = 0;
    outLayers.clear();
    for (const auto& name : instance_layer_names) {
        std::cout << count << ": " << name.layerName << ": " << name.description << std::endl;
        auto it = requestedLayers.find(std::string(name.layerName));
        if (it != requestedLayers.end())
            outLayers.emplace_back(name.layerName);
        count++;
    }

    // Print the ones we're enabling
    std::cout << std::endl;
    for (const auto& layer : outLayers) {
        std::cout << "applying layer: " << layer.c_str() << std::endl;
    }
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
        std::cout << count << ": " << extensionProperty.extensionName << std::endl;
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

VulkanContext::VulkanContext(SDL_Window * window):window(window) {
    int windowWidht, windowHeight;
    SDL_GetWindowSize(window, &windowWidht, &windowHeight);
    this->windowWidth = windowWidht;
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
    makeChainImageViews(this->device, this->swapchain, this->colorFormat, this->swapchainImages, this->swapchainImageViews);

    this->commandPool = createCommandPool(this->device, this->graphicsQueueIndex);

    vkGetDeviceQueue(this->device, this->graphicsQueueIndex, 0, &this->graphicsQueue);

    std::tie(this->depthImageView, this->depthImage, this->depthMemory)
        = createDepthBuffer(this->physicalDevice, this->device, this->commandPool, this->graphicsQueue, this->windowWidth, this->windowHeight);
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

void rebuildPresentationResources(VulkanContext & context) {
    vkDeviceWaitIdle(context.device);
    for (VkImageView view : context.swapchainImageViews) {
        vkDestroyImageView(context.device, view, nullptr);
    }
    vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);

    context.swapchain = VK_NULL_HANDLE;
    createSwapChain(context, context.presentationSurface, context.physicalDevice, context.device, context.swapchain);
    getSwapChainImageHandles(context.device, context.swapchain, context.swapchainImages);
    makeChainImageViews(context.device, context.swapchain, context.colorFormat, context.swapchainImages, context.swapchainImageViews);

    vkDestroyImageView(context.device, context.depthImageView, nullptr);
    vkDestroyImage(context.device, context.depthImage, nullptr);
    vkFreeMemory(context.device, context.depthMemory, nullptr);

    std::tie(context.depthImageView, context.depthImage, context.depthMemory)
        = createDepthBuffer(context.physicalDevice, context.device, context.commandPool, context.graphicsQueue, context.windowWidth, context.windowHeight);
}

VulkanContext::~VulkanContext() {
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);

    for (VkImageView view : swapchainImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, presentationSurface, nullptr);
    vkDestroyDevice(device, nullptr);
    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroyInstance(instance, nullptr);
}

struct RenderpassBuilder {
    VulkanContext & context;
    std::vector<VkAttachmentDescription> attachmentDescriptions;
    std::vector<VkSubpassDescription> subpassDescriptions;
    std::vector<VkSubpassDependency> dependencies;
    std::vector<VkAttachmentReference> colorAttachmentRefs;
    VkAttachmentReference depthAttachmentRef;
    bool hasDepthRef;
    RenderpassBuilder(VulkanContext & context):context(context) {
        attachmentDescriptions.reserve(2);
        subpassDescriptions.reserve(1);
        dependencies.reserve(1);
        hasDepthRef = false;
        depthAttachmentRef = {};
    }
    RenderpassBuilder& colorAttachment() {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = context.colorFormat;
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
};

struct RenderPass {
    VkRenderPass renderpass;
    VulkanContext & context;
    RenderPass(const RenderpassBuilder builder) : context(builder.context) {
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        if (builder.colorAttachmentRefs.size() > 0) {
            subpass.colorAttachmentCount = builder.colorAttachmentRefs.size();
            subpass.pColorAttachments = builder.colorAttachmentRefs.data();
        }
        if (builder.hasDepthRef) {
            subpass.pDepthStencilAttachment = &builder.depthAttachmentRef;
        }

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        renderPassInfo.attachmentCount = builder.attachmentDescriptions.size();
        renderPassInfo.pAttachments = builder.attachmentDescriptions.data();

        if (VK_SUCCESS != vkCreateRenderPass(builder.context.device, &renderPassInfo, nullptr, &renderpass)) {
            throw std::runtime_error("failed to create render pass");
        }
    }
    ~RenderPass() {
        vkDestroyRenderPass(context.device, renderpass, nullptr);
    }
};

struct ShaderBuilder {
    VulkanContext & context;
    VkShaderStageFlagBits stage;
    std::vector<char> code;

    ShaderBuilder(VulkanContext & context) : context(context), stage(VK_SHADER_STAGE_VERTEX_BIT) {}
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
    VulkanContext & context;
    VkShaderModule module;
    ShaderModule(ShaderBuilder & builder):context(builder.context) {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = builder.code.size();
        createInfo.pCode = (uint32_t*)builder.code.data();

        if (VK_SUCCESS != vkCreateShaderModule(builder.context.device, &createInfo, nullptr, &module)) {
            throw std::runtime_error("failed to create shader module");
        }
    }
    ~ShaderModule() {
        vkDestroyShaderModule(context.device, module, nullptr);
    }
    operator VkShaderModule() const { return module; }
};

struct ImageBuilder {
    VulkanContext & context;
    bool buildMipmaps;
    void * bytes;
    int byteCount;
    VkFormat format;
    VkExtent2D extent; // width and height
    ImageBuilder(VulkanContext & context) : context(context), buildMipmaps(true), bytes(nullptr) {}
    ImageBuilder & createMipmaps(bool buildMipmaps) {
        this->buildMipmaps = buildMipmaps;
        return *this;
    }
    ImageBuilder & fromBytes(void * bytes, int byteCount, int width, int height, VkFormat format) {
        this->bytes = bytes;
        this->byteCount = byteCount;
        extent.width = width;
        extent.height = height;
        this->format = format;
        return *this;
    }
};

struct Image {
    VulkanContext & context;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;

    Image(ImageBuilder & builder) : context(builder.context) {
        // put the image bytes into a buffer for transitioning
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        std::tie(stagingBuffer, stagingMemory) = createBuffer(context.physicalDevice, context.device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, builder.byteCount);

        void * stagingBytes;
        vkMapMemory(context.device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingBytes);
        memcpy(stagingBytes, builder.bytes, (size_t)builder.byteCount);
        vkUnmapMemory(context.device, stagingMemory);
        
        // bytes belong to caller, so don't free them here

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

        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT // copy bytes from image into mip levels
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT // copy bytes into image
            | VK_IMAGE_USAGE_SAMPLED_BIT; // read by sampler in shader

        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(context.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
            throw std::runtime_error("failed to create Vulkan image");
        }

        VkMemoryRequirements memoryRequirements = {};
        vkGetImageMemoryRequirements(context.device, image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex = findMemoryType(context.physicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(context.device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate image memory");
        }
        vkBindImageMemory(context.device, image, memory, 0);

        // Vulkan spec says images MUST be created either undefined or preinitialized layout, so we can't jump straight to DST_OPTIMAL.
        transitionImageLayout(context.device, context.commandPool, context.graphicsQueue, image, builder.format, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Now the image is in DST_OPTIMAL layout and we can copy the image data to it.
        copyBufferToImage(context.device, context.commandPool, context.graphicsQueue, stagingBuffer, image, builder.extent.width, builder.extent.height);

        if (builder.buildMipmaps) {
            generateMipmaps(context.device, image, context.commandPool, context.graphicsQueue, builder.extent.width, builder.extent.height, mipLevels);
        } else {
            // todo: transition the single image to the optimal layout for sampling
        }

        vkFreeMemory(context.device, stagingMemory, nullptr);
        vkDestroyBuffer(context.device, stagingBuffer, nullptr);

        imageView = createImageView(context.device, image, builder.format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
    }
    ~Image() {
        vkDestroyImageView(context.device, imageView, nullptr);
        vkFreeMemory(context.device, memory, nullptr);
        vkDestroyImage(context.device, image, nullptr);
    }
};

struct TextureSampler {
    VkSampler sampler;
    VulkanContext & context;
    TextureSampler(VulkanContext& context) : context(context), sampler(createSampler(context.device)) {}
    ~TextureSampler() {
        vkDestroySampler(context.device, sampler, nullptr);
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
    VulkanContext & context;

    Buffer(VulkanContext& context, BufferBuilder & builder) : context(context), buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(builder.byteCount) {
        std::tie(buffer, memory) = createBuffer(context.physicalDevice, context.device, builder.usage, builder.byteCount);
    }
    void setData(void * bytes, size_t size) {
        if (size != this->size) {
            throw std::runtime_error("Uniform buffer size mismatch");
        }
        void * mapped;
        vkMapMemory(context.device, memory, 0, size, 0, &mapped);
        memcpy(mapped, bytes, size);
        vkUnmapMemory(context.device, memory);           
    }
    ~Buffer() {
        vkFreeMemory(context.device, memory, nullptr);
        vkDestroyBuffer(context.device, buffer, nullptr);
    }
    operator VkBuffer() const {
        return buffer;
    }
};