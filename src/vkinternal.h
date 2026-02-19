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
VkSampler createShadowSampler(VkDevice device);
VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags imageAspects, size_t mipLevelCount);
void recordMipmapGeneration(VkCommandBuffer commandBuffer, VkImage image, int width, int height, size_t mipLevelCount);
void recordCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
void createSwapChain(VulkanContext & context, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, VkDevice device, VkSwapchainKHR& outSwapChain);
void getSwapChainImageHandles(VkDevice device, VkSwapchainKHR chain, std::vector<VkImage>& outImageHandles);
void makeChainImageViews(VkDevice device, VkFormat colorFormat, std::vector<VkImage> & images, std::vector<VkImageView> & imageViews);

// Loaded function pointers (set by VulkanContext constructor)
extern PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasks;
extern PFN_vkCmdDrawMeshTasksIndirectEXT vkCmdDrawMeshTasksIndirect;
extern PFN_vkCmdBeginRendering vkBeginRendering;
extern PFN_vkCmdEndRendering vkEndRendering;
