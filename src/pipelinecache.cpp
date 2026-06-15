#include "pipelinecache.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vkobjects {

namespace {

void put32(std::vector<uint8_t> & b, size_t off, uint32_t v) {
    b[off + 0] = uint8_t(v);
    b[off + 1] = uint8_t(v >> 8);
    b[off + 2] = uint8_t(v >> 16);
    b[off + 3] = uint8_t(v >> 24);
}
void put64(std::vector<uint8_t> & b, size_t off, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[off + i] = uint8_t(v >> (8 * i));
}
uint32_t get32(const uint8_t * b) {
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}
uint64_t get64(const uint8_t * b) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(b[i]) << (8 * i);
    return v;
}

// Offsets into the trailing VkPipelineCacheHeaderVersionOne (little-endian per spec).
constexpr size_t kDriverHeaderSize = 32;

} // namespace

uint64_t fnv1a64(const uint8_t * data, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::vector<uint8_t> serializeCacheFile(const DeviceCacheId & id,
                                        const uint8_t * blob, size_t blobSize) {
    std::vector<uint8_t> file(kPrefixSize + blobSize);
    put32(file, 0, kCacheMagic);
    put32(file, 4, kCacheVersion);
    put64(file, 8, uint64_t(blobSize));
    put64(file, 16, fnv1a64(blob, blobSize));
    put32(file, 24, id.vendorID);
    put32(file, 28, id.deviceID);
    put32(file, 32, id.driverVersion);
    put32(file, 36, id.abiBits);
    std::memcpy(file.data() + 40, id.uuid, VK_UUID_SIZE);
    if (blobSize) std::memcpy(file.data() + kPrefixSize, blob, blobSize);
    return file;
}

std::vector<uint8_t> validateCacheFile(const std::vector<uint8_t> & file,
                                       const DeviceCacheId & id) {
    if (file.size() < kPrefixSize) return {};
    const uint8_t * p = file.data();
    if (get32(p + 0) != kCacheMagic) return {};
    if (get32(p + 4) != kCacheVersion) return {};

    uint64_t dataSize = get64(p + 8);
    if (dataSize == 0) return {};
    // Overflow-safe length check (also bounds dataSize to size_t, making the
    // blob span below safe on 32-bit). Do NOT use kPrefixSize + dataSize.
    if (file.size() < kPrefixSize || file.size() - kPrefixSize != dataSize) return {};

    const uint8_t * blob = p + kPrefixSize;
    if (fnv1a64(blob, size_t(dataSize)) != get64(p + 16)) return {};

    if (get32(p + 24) != id.vendorID) return {};
    if (get32(p + 28) != id.deviceID) return {};
    if (get32(p + 32) != id.driverVersion) return {};
    if (get32(p + 36) != id.abiBits) return {};
    if (std::memcmp(p + 40, id.uuid, VK_UUID_SIZE) != 0) return {};

    // Trailing driver header — a valid prefix can still wrap a corrupt blob.
    size_t blobSize = size_t(dataSize); // safe: bounded to file.size() above
    if (blobSize < kDriverHeaderSize) return {};
    if (get32(blob + 0) != kDriverHeaderSize) return {};                  // headerSize
    if (get32(blob + 4) != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) return {}; // headerVersion
    if (get32(blob + 8) != id.vendorID) return {};
    if (get32(blob + 12) != id.deviceID) return {};
    if (std::memcmp(blob + 16, id.uuid, VK_UUID_SIZE) != 0) return {};

    return std::vector<uint8_t>(blob, blob + blobSize);
}

std::string cacheFileName(const DeviceCacheId & id) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "vkpc_%08x_%08x_%u.bin",
                  id.vendorID, id.deviceID, id.abiBits);
    return buf;
}

DeviceCacheId deviceCacheId(const VkPhysicalDeviceProperties & props) {
    DeviceCacheId id;
    id.vendorID = props.vendorID;
    id.deviceID = props.deviceID;
    id.driverVersion = props.driverVersion;
    id.abiBits = uint32_t(sizeof(void *) * 8);
    std::memcpy(id.uuid, props.pipelineCacheUUID, VK_UUID_SIZE);
    return id;
}

// --- Device-touching glue ---

namespace {

VkPipelineCache createCache(VkDevice device, const void * data, size_t size) {
    VkPipelineCacheCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    ci.initialDataSize = size;
    ci.pInitialData = size ? data : nullptr;
    VkPipelineCache cache = VK_NULL_HANDLE;
    if (vkCreatePipelineCache(device, &ci, nullptr, &cache) == VK_SUCCESS) return cache;
    // Driver rejected validated data — fall back to an empty cache.
    ci.initialDataSize = 0;
    ci.pInitialData = nullptr;
    cache = VK_NULL_HANDLE;
    vkCreatePipelineCache(device, &ci, nullptr, &cache);
    return cache;
}

std::vector<uint8_t> readFile(const std::filesystem::path & path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

bool atomicWrite(const std::filesystem::path & finalPath,
                 const std::vector<uint8_t> & bytes, bool verbose) {
    std::error_code ec;
    std::filesystem::path dir = finalPath.parent_path();
    std::filesystem::create_directories(dir, ec);

    static std::atomic<uint32_t> counter{0};
#if defined(_WIN32)
    uint32_t pid = uint32_t(GetCurrentProcessId());
#else
    uint32_t pid = uint32_t(getpid());
#endif
    std::filesystem::path tmp = finalPath;
    tmp += "." + std::to_string(pid) + "." + std::to_string(counter++) + ".tmp";

#if defined(_WIN32)
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
        f.flush();
        if (!f) { std::filesystem::remove(tmp, ec); return false; }
    }
    if (!MoveFileExW(tmp.wstring().c_str(), finalPath.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
#else
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    size_t off = 0;
    while (off < bytes.size()) {
        ssize_t w = ::write(fd, bytes.data() + off, bytes.size() - off);
        if (w < 0) { ::close(fd); std::filesystem::remove(tmp, ec); return false; }
        off += size_t(w);
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(tmp.c_str(), finalPath.c_str()) != 0) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    // fsync the directory so the new entry survives a crash (best-effort).
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    } else if (verbose) {
        std::cerr << "[pipelinecache] dir fsync skipped: " << dir << "\n";
    }
    return true;
#endif
}

} // namespace

VkPipelineCache loadPipelineCache(VkDevice device, const DeviceCacheId & id,
                                  const std::string & dir, bool verbose) {
    if (dir.empty()) return createCache(device, nullptr, 0);
    try {
        std::filesystem::path path = std::filesystem::path(dir) / cacheFileName(id);
        std::vector<uint8_t> file = readFile(path);
        if (file.empty()) {
            if (verbose) std::cerr << "[pipelinecache] no cache at " << path << " (cold)\n";
            return createCache(device, nullptr, 0);
        }
        std::vector<uint8_t> blob = validateCacheFile(file, id);
        if (blob.empty()) {
            if (verbose) std::cerr << "[pipelinecache] discarding incompatible/corrupt " << path << "\n";
            return createCache(device, nullptr, 0);
        }
        if (verbose) std::cerr << "[pipelinecache] seeded from " << path << " (" << blob.size() << " B)\n";
        return createCache(device, blob.data(), blob.size());
    } catch (const std::exception & e) {
        if (verbose) std::cerr << "[pipelinecache] load error (" << e.what() << "); cold start\n";
        return createCache(device, nullptr, 0);
    }
}

void savePipelineCache(VkDevice device, VkPipelineCache cache,
                       const DeviceCacheId & id, const std::string & dir, bool verbose) {
    if (dir.empty() || cache == VK_NULL_HANDLE) return;

    size_t size = 0;
    if (vkGetPipelineCacheData(device, cache, &size, nullptr) != VK_SUCCESS || size == 0) return;

    std::vector<uint8_t> blob(size);
    VkResult r = vkGetPipelineCacheData(device, cache, &size, blob.data());
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return;
    blob.resize(size);
    if (blob.empty()) return;

    try {
        std::vector<uint8_t> file = serializeCacheFile(id, blob.data(), blob.size());
        std::filesystem::path path = std::filesystem::path(dir) / cacheFileName(id);
        if (atomicWrite(path, file, verbose)) {
            if (verbose) std::cerr << "[pipelinecache] wrote " << path << " (" << blob.size() << " B)\n";
        } else if (verbose) {
            std::cerr << "[pipelinecache] write failed: " << path << "\n";
        }
    } catch (const std::exception & e) {
        if (verbose) std::cerr << "[pipelinecache] save error (" << e.what() << ")\n";
    }
}

} // namespace vkobjects

namespace {

std::string sanitizeAppName(const char * appName) {
    if (!appName || !*appName) return {};
    std::string out;
    for (const char * c = appName; *c; ++c) {
        char ch = *c;
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                  (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        out += ok ? ch : '_';
    }
    // Reject pathological names that reduce to traversal.
    if (out.empty() || out == "." || out == "..") return {};
    return out;
}

} // namespace

std::string defaultPipelineCacheDir(const char * appName) {
    std::string app = sanitizeAppName(appName);
    if (app.empty()) return {};

    std::filesystem::path base;
#if defined(_WIN32)
    if (const char * lad = std::getenv("LOCALAPPDATA"); lad && *lad) {
        base = lad;
    } else {
        return {};
    }
#else
    if (const char * xdg = std::getenv("XDG_CACHE_HOME"); xdg && *xdg) {
        base = xdg;
    } else if (const char * home = std::getenv("HOME"); home && *home) {
        base = std::filesystem::path(home) / ".cache";
    } else {
        return {};
    }
#endif

    std::filesystem::path dir = base / app;
    try {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) return {};
        return dir.string();
    } catch (const std::exception &) {
        return {};
    }
}

