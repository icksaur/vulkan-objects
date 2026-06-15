// Internal — pipeline cache file format + persistence glue.
// NOT part of the public API (include/vkobjects.h). The pure file-format
// functions below are the shared source of truth and the unit-test target
// (hull's surfels-test includes this header directly).
//
// File layout: a fixed-offset, little-endian application prefix header
// (kPrefixSize bytes) immediately followed by the raw Vulkan pipeline-cache
// blob. The prefix lets us validate device/ABI identity and data integrity
// *before* the bytes ever reach vkCreatePipelineCache, which several drivers
// crash on for corrupt/mismatched input (see doc/spec-pipeline-cache.md).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace vkobjects {

// Identity a cache blob is bound to. A blob is only reusable on a byte-exact
// match of every field. driverVersion is included deliberately (conservative:
// a driver upgrade that forgets to roll pipelineCacheUUID still invalidates).
struct DeviceCacheId {
    uint32_t vendorID = 0;
    uint32_t deviceID = 0;
    uint32_t driverVersion = 0;
    uint32_t abiBits = 0; // sizeof(void*) * 8 — catches 32/64 mismatch under one UUID
    uint8_t uuid[VK_UUID_SIZE] = {};
};

constexpr uint32_t kCacheMagic = 0x43504B56u; // 'VKPC'
constexpr uint32_t kCacheVersion = 1u;
constexpr size_t kPrefixSize = 56; // 4+4+8+8+4+4+4+4 + VK_UUID_SIZE(16)
static_assert(VK_UUID_SIZE == 16, "prefix layout assumes 16-byte UUID");

// FNV-1a 64-bit. Integrity only (bit-rot / truncation), not cryptographic.
uint64_t fnv1a64(const uint8_t * data, size_t n);

// Build the on-disk file image (prefix header at fixed offsets + blob copy).
std::vector<uint8_t> serializeCacheFile(const DeviceCacheId & id,
                                        const uint8_t * blob, size_t blobSize);

// Validate a file image against the running device. On success returns the
// inner Vulkan blob (ready to hand to vkCreatePipelineCache as pInitialData);
// on ANY failure returns an empty vector. Validates the app prefix
// (magic/version/size/hash/device fields) AND the trailing 32-byte
// VkPipelineCacheHeaderVersionOne the driver wrote into the blob.
std::vector<uint8_t> validateCacheFile(const std::vector<uint8_t> & file,
                                       const DeviceCacheId & id);

// Deterministic, collision-free filename for a device+ABI (no driverVersion —
// a driver upgrade reads, rejects via the header, and overwrites in place).
//   vkpc_<vendorID:08x>_<deviceID:08x>_<abiBits>.bin
std::string cacheFileName(const DeviceCacheId & id);

// --- Device-touching glue (declared here, defined in pipelinecache.cpp) ---

DeviceCacheId deviceCacheId(const VkPhysicalDeviceProperties & props);

// Create a VkPipelineCache, seeding it from <dir>/<cacheFileName> when that
// file exists and validates. Always returns a usable cache (empty on any
// problem, with a final empty-retry if the driver rejects validated data).
// dir empty => in-process-only cache (no file read).
VkPipelineCache loadPipelineCache(VkDevice device, const DeviceCacheId & id,
                                  const std::string & dir, bool verbose);

// Serialize the cache and atomically replace <dir>/<cacheFileName>. No-op when
// dir is empty or the cache yields no data. Best-effort: never throws.
void savePipelineCache(VkDevice device, VkPipelineCache cache,
                       const DeviceCacheId & id, const std::string & dir, bool verbose);

} // namespace vkobjects
