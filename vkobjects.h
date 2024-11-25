#include <vector>
#include <set>
#include <iostream>
#include <stdexcept>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

const char * appName = "VulkanExample";
const char * engineName = "VulkanExampleEngine";

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

void initVulkan(VulkanContext & context, SDL_Window * window) {
    if (context.initialized) {
        throw std::runtime_error("Vulkan context already initialized");
    }
    context.window = window;

    int windowWidht, windowHeight;
    SDL_GetWindowSize(window, &windowWidht, &windowHeight);
    context.windowWidth = windowWidht;
    context.windowHeight = windowHeight;

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
    createVulkanInstance(foundLayers, foundExtensions, context.instance);

    // Vulkan messaging callback
    setupDebugCallback(context.instance, context.callback);

    // Select GPU after succsessful creation of a vulkan instance (jeeeej no global states anymore)
    context.graphicsQueueIndex = -1;
    selectGPU(context.instance, context.physicalDevice, context.graphicsQueueIndex);

    // Create a logical device that interfaces with the physical device
    context.device = createLogicalDevice(context.physicalDevice, context.graphicsQueueIndex, foundLayers);
}