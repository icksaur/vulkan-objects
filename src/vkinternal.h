#pragma once
// Internal helpers shared across compilation units. Not part of the public API.

#include "vkobjects.h"
#include <tuple>

extern VkFormat depthFormat;

VkSampleCountFlagBits getSampleBits(uint32_t sampleCount);
VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool);
uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties);
std::tuple<VkBuffer, VkDeviceMemory> createBuffer(VkPhysicalDevice gpu, VkDevice device, VkBufferUsageFlags usageFlags, size_t byteCount, VkMemoryPropertyFlags flags);
VkSampler createSampler(VkDevice device);
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
