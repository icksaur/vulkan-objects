# VMA Integration
Status: done

## Goal

Replace per-resource `vkAllocateMemory` calls with VMA (Vulkan Memory Allocator) sub-allocation. This removes the silent ~4096 allocation limit footgun while keeping the public API (`BufferBuilder`, `ImageBuilder`, `Buffer`, `Image`) completely unchanged.

## Design

### Approach

VMA is a single-header library (`vk_mem_alloc.h`). It creates a `VmaAllocator` at startup that manages memory pools internally. Every `vkAllocateMemory` + `vkFreeMemory` pair is replaced with `vmaCreateBuffer` / `vmaCreateImage` and `vmaDestroyBuffer` / `vmaDestroyImage`. VMA handles memory type selection, sub-allocation, and defragmentation.

### Changes by file

**New file: `src/vk_mem_alloc.h`** — vendored VMA header. Single header, no build dependency.

**`src/vkinternal.h`**
- Add `#include "vk_mem_alloc.h"`
- Remove `findMemoryType` and `createBuffer` declarations
- Add `extern VmaAllocator g_allocator;`

**`src/vkobjects.h`** (public header — minimal changes)
- `DestroyGeneration`: replace `std::vector<VkDeviceMemory> memories` with `std::vector<VmaAllocation> allocations`
- `Buffer`: replace `VkDeviceMemory memory` with `VmaAllocation allocation`
- `Image`: replace `VkDeviceMemory memory` with `VmaAllocation allocation`

**`src/vkobjects.cpp`**
- `VulkanContext::VulkanContext`: create `VmaAllocator` after device creation
- `VulkanContext::~VulkanContext`: destroy `VmaAllocator` after all other cleanup
- `DestroyGeneration::destroy()`: call `vmaFreeMemory` for each allocation instead of `vkFreeMemory`; VMA buffers/images use `vmaDestroyBuffer`/`vmaDestroyImage` instead of separate destroy+free
- Remove `findMemoryType()` and `createBuffer()` functions entirely

**`src/buffer.cpp`**
- `Buffer::Buffer`: replace `createBuffer()` with `vmaCreateBuffer()`
- `Buffer::upload/download`: replace `vkMapMemory`/`vkUnmapMemory` with `vmaMapMemory`/`vmaUnmapMemory`
- `Buffer::~Buffer`: push `VmaAllocation` to `DestroyGeneration` instead of `VkDeviceMemory`

**`src/image.cpp`**
- `Image::Image`: replace `vkCreateImage` + `vkAllocateMemory` + `vkBindImageMemory` with `vmaCreateImage()`
- `Image::~Image`: push `VmaAllocation` to `DestroyGeneration` instead of `VkDeviceMemory`

### VMA creation

```cpp
VmaAllocatorCreateInfo allocatorInfo = {};
allocatorInfo.instance = instance;
allocatorInfo.physicalDevice = physicalDevice;
allocatorInfo.device = device;
allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;

vmaCreateAllocator(&allocatorInfo, &g_allocator);
```

### Buffer allocation (before → after)

Before:
```cpp
std::tie(buffer, memory) = createBuffer(gpu, device, usage, size, properties);
```

After:
```cpp
VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
bufferInfo.size = size;
bufferInfo.usage = usage;

VmaAllocationCreateInfo allocInfo = {};
// Map VkMemoryPropertyFlags to VMA usage
if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

vmaCreateBuffer(g_allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
```

### Image allocation (before → after)

Before:
```cpp
vkCreateImage(device, &imageInfo, nullptr, &image);
vkGetImageMemoryRequirements(device, image, &memReq);
allocInfo.memoryTypeIndex = findMemoryType(gpu, memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
vkAllocateMemory(device, &allocInfo, nullptr, &memory);
vkBindImageMemory(device, image, memory, 0);
```

After:
```cpp
VmaAllocationCreateInfo allocInfo = {};
allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
vmaCreateImage(g_allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
```

### Deferred destruction

`DestroyGeneration::destroy()` currently calls `vkFreeMemory` and `vkDestroyBuffer`/`vkDestroyImage` separately. With VMA, buffers and images are destroyed together with their allocation via `vmaDestroyBuffer`/`vmaDestroyImage`. This means buffer+allocation and image+allocation must be paired in the destroy generation.

New approach: store `std::vector<std::pair<VkBuffer, VmaAllocation>>` and `std::vector<std::pair<VkImage, VmaAllocation>>` instead of separate buffer/image/memory vectors.

### Map/Unmap

`Buffer::upload`/`download` currently use `vkMapMemory`/`vkUnmapMemory`. With VMA, use `vmaMapMemory`/`vmaUnmapMemory` or persistent mapping via `VMA_ALLOCATION_CREATE_MAPPED_BIT`.

## Considerations

**No public API change.** `BufferBuilder`, `ImageBuilder`, `Buffer`, `Image` keep identical signatures. `VmaAllocation` only appears in internal fields and `DestroyGeneration`. The `vk_mem_alloc.h` header is only included from `vkinternal.h`, never from `vkobjects.h`.

**Forward-declare VMA types in public header.** `VmaAllocation` is a pointer to an opaque struct (`VmaAllocation_T*`). We can forward-declare it in `vkobjects.h` to avoid including VMA in the public header.

**VMA_IMPLEMENTATION in exactly one TU.** `#define VMA_IMPLEMENTATION` before `#include "vk_mem_alloc.h"` in `vkobjects.cpp` only. All other files include it as a regular header.

**Allocation limit eliminated.** VMA sub-allocates from a small number of large `vkAllocateMemory` calls. Thousands of buffers/images share a handful of actual allocations.

**No behavioral change.** Rendering output is pixel-identical. The only observable difference is the allocation pattern reported by validation layers.

## Acceptance

1. Project builds cleanly with no warnings.
2. Application runs and renders identically (visual confirmation).
3. `findMemoryType` and `createBuffer` are deleted — no `vkAllocateMemory` calls remain in project code.
4. Validation layers report no errors.
5. `VmaAllocation` does not appear in `vkobjects.h` public types (forward-declared only).
