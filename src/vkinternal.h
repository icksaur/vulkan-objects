#pragma once
// Internal helpers shared across compilation units. Not part of the public API.

#include "vkobjects.h"
#include "vk_mem_alloc.h"
#include <tuple>

extern VkFormat depthFormat;
extern VmaAllocator g_allocator;

VkSampleCountFlagBits getSampleBits(uint32_t sampleCount);
VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool);
VkSampler createSampler(VkDevice device);
VkSampler createNearestSampler(VkDevice device);
VkSampler createShadowSampler(VkDevice device);
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags imageAspects, size_t mipLevelCount);
void recordMipmapGeneration(VkCommandBuffer commandBuffer, VkImage image, int width, int height, size_t mipLevelCount);
void recordCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
void createSwapChain(VulkanContext & context, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain);
void getSwapChainImageHandles(VkDevice device, VkSwapchainKHR chain, std::vector<VkImage>& outImageHandles);
void makeChainImageViews(VkDevice device, VkFormat colorFormat, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews);
void destroyThreadLocalSubmitFence(VkDevice device);

// A VkResult rendered as its spec name ("VK_ERROR_DEVICE_LOST"), for error messages.
//
// Motivation: `vkEndCommandBuffer` and the `submitAndWait` `vkQueueSubmit2`/`vkWaitForFences`
// used to discard their VkResult entirely. A recording error therefore produced no diagnostic at
// all, and a device loss during setup stayed silent until the NEXT checked submit threw a bare
// "failed to submit command buffer" — pointing at innocent code, with the real failure long past.
// Every submit-path result is now checked and named.
const char* vkResultName(VkResult result);

// Loaded function pointers (set by VulkanContext constructor)
extern PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasks;
extern PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirect;
extern PFN_vkCmdBeginRendering vkBeginRendering;
extern PFN_vkCmdEndRendering vkEndRendering;
extern PFN_vkGetAccelerationStructureBuildSizesKHR rtGetAccelerationStructureBuildSizes;
extern PFN_vkCreateAccelerationStructureKHR rtCreateAccelerationStructure;
extern PFN_vkDestroyAccelerationStructureKHR rtDestroyAccelerationStructure;
extern PFN_vkCmdBuildAccelerationStructuresKHR rtCmdBuildAccelerationStructures;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR rtGetAccelerationStructureDeviceAddress;
