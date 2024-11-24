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

#include "tga.h"
#include "math.h"
#include "camera.h"

// Global Settings
const char * appName = "VulkanTest";
const char * engineName = "VulkanTestEngine";
int windowWidth = 1280;
int windowHeight = 720;
VkPresentModeKHR preferredPresentationMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
VkSurfaceTransformFlagBitsKHR desiredTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
VkFormat surfaceFormat = VK_FORMAT_B8G8R8A8_SRGB;
VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
VkImageUsageFlags desiredImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
VkFormat depthFormat = VK_FORMAT_D24_UNORM_S8_UINT; // some options are VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT

#define COMPUTE_VERTICES // comment out to try CPU uploaded vertex buffer
size_t quadCount = 100;

struct PipelineInfo {
    float w, h;
    VkExtent2D extent;
    VkFormat colorFormat;
} pipelineInfo;

std::vector<char> readFileBytes(std::istream & file) {
    return std::vector<char>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

const std::set<std::string>& getRequestedLayerNames() {
    static std::set<std::string> layers;
    if (layers.empty()) {
        layers.emplace("VK_LAYER_NV_optimus");
        layers.emplace("VK_LAYER_KHRONOS_validation");
    }
    return layers;
}

template<typename T>
T clamp(T value, T min, T max) {
    return (value < min) ? min : (value > max) ? max : value;
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

void destroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
    if (func != nullptr) {
        func(instance, callback, pAllocator);
    }
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

    // Finally we're ready to create a new device
    VkDevice device;
    if (VK_SUCCESS != vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device)) {
        throw std::runtime_error("failed to create logical device!");
    }

    return device;
}

void getDeviceQueue(VkDevice device, int familyQueueIndex, VkQueue& outGraphicsQueue) {
    vkGetDeviceQueue(device, familyQueueIndex, 0, &outGraphicsQueue);
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

bool getSurfaceProperties(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) {
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities) != VK_SUCCESS) {
        std::cout << "unable to acquire surface capabilities\n";
        return false;
    }
    return true;
}

unsigned int getNumberOfSwapImages(const VkSurfaceCapabilitiesKHR& capabilities) {
    unsigned int number = capabilities.minImageCount + 1;
    return number > capabilities.maxImageCount ? capabilities.minImageCount : number;
}

VkExtent2D getSwapImageSize(const VkSurfaceCapabilitiesKHR& capabilities) {
    // Default size = window size
    VkExtent2D size = { (unsigned int)windowWidth, (unsigned int)windowHeight };

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

std::tuple<VkBuffer, VkDeviceMemory> createBuffer(VkPhysicalDevice gpu, VkDevice device, VkBufferUsageFlags usageFlags, size_t byteCount) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = byteCount;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Not shared across multiple queue families

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(gpu, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(device, buffer, memory, 0);

    return std::make_tuple(buffer, memory);
}

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
    writeToReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    writeToReadBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    writeToReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    writeToReadBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

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
        writeToReadBarrier.subresourceRange.baseMipLevel = i - 1;
        readToSampleBarrier.subresourceRange.baseMipLevel = i - 1;

        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.mipLevel = i - 1;
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.mipLevel = i;

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

std::tuple<VkImage, VkDeviceMemory, VkImageView> createImageFromTGAFile(const char * filename, VkPhysicalDevice gpu, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) {
    VkImage image;
    VkDeviceMemory memory;

    std::ifstream file(filename);
    std::vector<char> fileBytes = readFileBytes(file);
    file.close();
    unsigned width, height;
    int bpp;
    void* tgaBytes = read_tga(fileBytes, width, height, bpp);
    if (tgaBytes == nullptr) {
        throw std::runtime_error("failed to read file as TGA");
    }

    unsigned tgaByteCount = width*height*(bpp/8);

    // TGA is BGR order, not RGB
    // Further, TGA does not specify linear or non-linear color component intensity.
    // By convention, TGA values are going to be "gamma corrected" or non-linear.
    // Assuming the bytes are sRGB looks good.  If they are assumed to be linear here, the colors will be washed out.
    // Read more by looking up sRGB to linear Vulkan conversions.
    VkFormat format = (bpp == 32) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8_SRGB;

    // put the image bytes into a buffer for transitioning
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    std::tie(stagingBuffer, stagingMemory) = createBuffer(gpu, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, tgaByteCount);

    void * stagingBytes;
    vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingBytes);
    memcpy(stagingBytes, tgaBytes, (size_t)tgaByteCount);
    vkUnmapMemory(device, stagingMemory);
    free(tgaBytes);

    size_t mipLevels = std::floor(log2(std::max(width, height))) + 1;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // we must "transition" this image to a device-optimal format

    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT // copy bytes from image into mip levels
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT // copy bytes into image
        | VK_IMAGE_USAGE_SAMPLED_BIT; // read by sampler in shader

    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan image");
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(device, image, &memoryRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(gpu, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory");
    }
    vkBindImageMemory(device, image, memory, 0);

    // Vulkan spec says images MUST be created either undefined or preinitialized layout, so we can't jump straight to DST_OPTIMAL.
    transitionImageLayout(device, commandPool, graphicsQueue, image, format, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Now the image is in DST_OPTIMAL layout and we can copy the image data to it.
    copyBufferToImage(device, commandPool, graphicsQueue, stagingBuffer, image, width, height);

    generateMipmaps(device, image, commandPool, graphicsQueue, width, height, mipLevels);

    // Transition to the final 2D image layout that allows us to sample in a shader.
    transitionImageLayout(device, commandPool, graphicsQueue, image, format, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkFreeMemory(device, stagingMemory, nullptr);
    vkDestroyBuffer(device, stagingBuffer, nullptr);

    VkImageView imageView = createImageView(device, image, format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);

    return std::make_tuple(image, memory, imageView);
}

void createSwapChain(VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain) {
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
    VkExtent2D swap_image_extent = getSwapImageSize(surfaceCapabilities);

    pipelineInfo.w = (float)swap_image_extent.width;
    pipelineInfo.h = (float)swap_image_extent.height;
    pipelineInfo.extent.height = pipelineInfo.h;
    pipelineInfo.extent.width = pipelineInfo.w;

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

    pipelineInfo.colorFormat = imageFormat.format;

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

std::tuple<VkImageView, VkImage, VkDeviceMemory> createDepthBuffer(VkPhysicalDevice gpu, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(gpu, depthFormat, &props);
    if (0 == (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
        throw std::runtime_error("requested format does not have tiling features");
    }

    const size_t oneMipLevel = 1;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = pipelineInfo.extent.width;
    imageInfo.extent.height = pipelineInfo.extent.height;
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

void makeChainImageViews(VkDevice device, VkSwapchainKHR swapChain, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews) {
    imageViews.resize(images.size());
    for (size_t i=0; i < images.size(); i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images[i];  // The image from the swap chain
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = pipelineInfo.colorFormat;  // Format of the swap chain images

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

void createFramebuffers(VkDevice device, VkRenderPass renderPass, std::vector<VkImageView> & chainImageViews, std::vector<VkFramebuffer> & frameBuffers, VkImageView depthImageView) {
    for (size_t i=0; i<chainImageViews.size(); i++) {
        VkImageView imageViews[] { chainImageViews[i], depthImageView };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 2;  // We are using only a color attachment
        framebufferInfo.pAttachments = imageViews;  // Image view as color attachment
        framebufferInfo.width = pipelineInfo.extent.width;
        framebufferInfo.height = pipelineInfo.extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create framebuffer!");
        }
    }
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cout << "unable to open file: " << filename << std::endl;
        return {};
     }
     std::vector<char> buffer(file.tellg());
     file.seekg(0);
     file.read(buffer.data(), buffer.size());
     file.close();
     return buffer;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo module_info = {};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = code.size();
    module_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule = VK_NULL_HANDLE;

    if (VK_SUCCESS != vkCreateShaderModule(device, &module_info, nullptr, &shaderModule)) {
        throw std::runtime_error("failed to create shader module");
    }

    return shaderModule;
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

VkRenderPass createRenderPass(VkDevice device) {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = pipelineInfo.colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // should already be in this format
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkAttachmentDescription attachments[] { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;

    VkRenderPass renderPass;
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    return renderPass;
}

VkPipeline createGraphicsPipeline(VkDevice device, VkPipelineLayout pipelineLayout, VkRenderPass renderPass, VkShaderModule vertexShaderModule, VkShaderModule fragmentShaderModule) {
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
    viewport.width = pipelineInfo.w;
    viewport.height = pipelineInfo.h;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = pipelineInfo.extent;

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
    
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline) != VK_SUCCESS) {
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

VkShaderModule loadShaderModule(VkDevice device, const std::string& filename) {
    std::vector<char> code = readFile(filename);
    return createShaderModule(device, code);
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
    renderPassBeginInfo.renderArea.extent = pipelineInfo.extent;  // Covers the whole framebuffer (usually the swap chain image size)

    // Set clear values for attachments (e.g., clearing the color buffer to black and depth buffer to 1.0f)
    VkClearValue clearValues[2];
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };  // Clear color: black
    clearValues[1].depthStencil = { 1.0f, 0 };               // Clear depth: 1.0, no stencil

    renderPassBeginInfo.clearValueCount = 2;                 // Two clear values (color and depth)
    renderPassBeginInfo.pClearValues = clearValues;

    // begin recording the render pass
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // bind and dispatch compute
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

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
    VkInstance instance;
    createVulkanInstance(foundLayers, foundExtensions, instance);

    // Vulkan messaging callback
    VkDebugReportCallbackEXT callback;
    setupDebugCallback(instance, callback);

    // Select GPU after succsessful creation of a vulkan instance (jeeeej no global states anymore)
    VkPhysicalDevice gpu;
    unsigned int graphicsQueueIndex(-1);
    selectGPU(instance, gpu, graphicsQueueIndex);

    // Create a logical device that interfaces with the physical device
    VkDevice device = createLogicalDevice(gpu, graphicsQueueIndex, foundLayers);

    // Create the surface we want to render to, associated with the window we created before
    // This call also checks if the created surface is compatible with the previously selected physical device and associated render queue
    VkSurfaceKHR presentationSurface = createSurface(window, instance, gpu, graphicsQueueIndex);

    VkQueue presentationQueue = getPresentationQueue(gpu, device, graphicsQueueIndex, presentationSurface);

    // swap chain with image handles and views
    VkSwapchainKHR swapchain = VK_NULL_HANDLE; // start null as this function will also recreate an old swapchain
    createSwapChain(presentationSurface, gpu, device, swapchain);

    std::vector<VkImage> chainImages;
    getSwapChainImageHandles(device, swapchain, chainImages);

    std::vector<VkImageView> chainImageViews(chainImages.size());
    makeChainImageViews(device, swapchain, chainImages, chainImageViews);
   
    // get the queue we want to submit the actual commands to
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueIndex, 0, &graphicsQueue);

    VkCommandPool commandPool = createCommandPool(device, graphicsQueueIndex);

    // shader objects
    VkShaderModule vertShader = loadShaderModule(device, "tri.vert.spv");
    VkShaderModule fragShader = loadShaderModule(device, "tri.frag.spv");
    VkShaderModule compShader = loadShaderModule(device, "vertices.comp.spv");

    // image for sampling
    VkDeviceMemory textureImageMemory;
    VkImage textureImage;
    VkImageView textureImageView;
    std::tie(textureImage, textureImageMemory, textureImageView) = createImageFromTGAFile("vulkan.tga", gpu, device, commandPool, graphicsQueue);

    VkSampler textureSampler = createSampler(device);

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
    descriptorWriteSets.push_back(createSamplerToDescriptorSetBinding(device, descriptorSet, textureSampler, textureImageView, imageInfo));
    descriptorWriteSets.push_back(createSsboToDescriptorSetBinding(device, descriptorSet, shaderStorageBuffer, shaderStorageBufferInfo));

    updateDescriptorSet(device, descriptorSet, descriptorWriteSets);

    // pipeline and render pass
    VkPipelineLayout pipelineLayout = createPipelineLayout(device, descriptorSetLayout);

    VkRenderPass renderPass = createRenderPass(device);

    // depth buffer
    VkImageView depthImageView;
    VkImage depthImage;
    VkDeviceMemory depthMemory;
    std::tie(depthImageView, depthImage, depthMemory) = createDepthBuffer(gpu, device, commandPool, graphicsQueue);

    // buffers to render to for presenting
    std::vector<VkFramebuffer> presentFramebuffers(chainImages.size());
    createFramebuffers(device, renderPass, chainImageViews, presentFramebuffers, depthImageView);

    VkPipeline graphicsPipeline = createGraphicsPipeline(device, pipelineLayout, renderPass, vertShader, fragShader);
    VkPipeline computePipeline = createComputePipeline(device, pipelineLayout, compShader);

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
        recordRenderPass(computePipeline, graphicsPipeline, renderPass, presentFramebuffers[nextImage], commandBuffers[nextImage], shaderStorageBuffer, pipelineLayout, descriptorSet);
#else
        recordRenderPass(computePipeline, graphicsPipeline, renderPass, frameBuffers[nextImage], commandBuffers[nextImage], vertexBuffer, pipelineLayout, descriptorSet);
#endif
        submitCommandBuffer(graphicsQueue, commandBuffers[nextImage], imageAvailableSemaphore, renderFinishedSemaphore);
        if (!presentQueue(presentationQueue, swapchain, renderFinishedSemaphore, nextImage)) {
            std::cout << "swap chain out of date, trying to remake" << std::endl;

            // This is a common Vulkan situation handled automatically by OpenGL.
            // We need to remake our swap chain, image views, and framebuffers.
            vkDeviceWaitIdle(device);
            for (VkFramebuffer framebuffer : presentFramebuffers) {
                vkDestroyFramebuffer(device, framebuffer, nullptr);
            }
            for (VkImageView view : chainImageViews) {
                vkDestroyImageView(device, view, nullptr);
            }
            vkDestroySwapchainKHR(device, swapchain, nullptr);

            vkDestroyImageView(device, depthImageView, nullptr);
            vkDestroyImage(device, depthImage, nullptr);
            vkFreeMemory(device, depthMemory, nullptr);

            std::tie(depthImageView, depthImage, depthMemory) = createDepthBuffer(gpu, device, commandPool, graphicsQueue);

            swapchain = VK_NULL_HANDLE;
            createSwapChain(presentationSurface, gpu, device, swapchain);
            getSwapChainImageHandles(device, swapchain, chainImages);
            makeChainImageViews(device, swapchain, chainImages, chainImageViews);
            createFramebuffers(device, renderPass, chainImageViews, presentFramebuffers, depthImageView);
        }
        SDL_Delay(100);
        
        vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetCommandBuffer(commandBuffers[nextImage], 0); // manually reset, otherwise implicit reset causes warnings
    }

    vkQueueWaitIdle(graphicsQueue); // wait until we're done or the render finished semaphore may be in use

    for (auto commandBuffer : commandBuffers) {
        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
    vkDestroyCommandPool(device, commandPool, nullptr);
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

    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory,  nullptr);

    vkDestroyImageView(device, depthImageView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);

    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    vkDestroyFence(device, fence, nullptr);
    vkDestroyShaderModule(device, vertShader, nullptr);
    vkDestroyShaderModule(device, fragShader, nullptr);
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (VkFramebuffer framebuffer : presentFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (VkImageView view : chainImageViews) {
        vkDestroyImageView(device, view, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);

    destroyDebugReportCallbackEXT(instance, callback, nullptr);
    vkDestroySurfaceKHR(instance, presentationSurface, nullptr);
    vkDestroyInstance(instance, nullptr);
    SDL_Quit();

    return 1;
}