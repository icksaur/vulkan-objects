#include "vkinternal.h"

// --- Buffer ---

BufferBuilder::BufferBuilder(size_t byteCount) : usage(0), properties(0), byteCount(byteCount) {}
BufferBuilder & BufferBuilder::index() { usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::uniform() {
    // In bindless model, uniform buffers are storage buffers with host visibility
    properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    return *this;
}
BufferBuilder & BufferBuilder::storage() { usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::indirect() { usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT; return *this; }
BufferBuilder & BufferBuilder::hostCoherent() { properties |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; return *this; }
BufferBuilder & BufferBuilder::hostVisible() { properties |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT; return *this; }
BufferBuilder & BufferBuilder::deviceLocal() { properties |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT; return *this; }
BufferBuilder & BufferBuilder::transferSource() { usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT; return *this; }
BufferBuilder & BufferBuilder::transferDestination() { usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT; return *this; }
BufferBuilder & BufferBuilder::size(size_t byteCount) { this->byteCount = byteCount; return *this; }

Buffer::Buffer(BufferBuilder & builder) : buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(builder.byteCount), rid_(UINT32_MAX) {
    // All buffers get STORAGE_BUFFER_BIT for bindless registration
    VkBufferUsageFlags usage = builder.usage | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    std::tie(buffer, memory) = createBuffer(g_context().physicalDevice, g_context().device, usage, builder.byteCount, builder.properties);
    rid_ = g_context().bindlessTable.registerStorageBuffer(g_context().device, buffer, builder.byteCount);
}
Buffer::Buffer(Buffer && other) : buffer(other.buffer), memory(other.memory), size(other.size), rid_(other.rid_) {
    other.buffer = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.size = 0;
    other.rid_ = UINT32_MAX;
}
uint32_t Buffer::rid() const { return rid_; }
void Buffer::upload(void * bytes, size_t size) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, 0, size, 0, &mapped);
    memcpy(mapped, bytes, size);
    vkUnmapMemory(g_context().device, memory);
}
void Buffer::upload(void * bytes, size_t size, VkDeviceSize offset) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, offset, size, 0, &mapped);
    memcpy(mapped, bytes, size);
    vkUnmapMemory(g_context().device, memory);
}
void Buffer::download(void * bytes, size_t size) {
    if (size > this->size) throw std::runtime_error("buffer size mismatch");
    void* mapped;
    vkMapMemory(g_context().device, memory, 0, size, 0, &mapped);
    memcpy(bytes, mapped, size);
    vkUnmapMemory(g_context().device, memory);
}
Buffer::~Buffer() {
    VulkanContext & context = g_context();
    auto & gen = context.destroyGenerations[context.frameInFlightIndex];
    if (rid_ != UINT32_MAX) {
        gen.storageBufferRIDs.push_back(rid_);
    }
    gen.memories.push_back(memory);
    gen.buffers.push_back(buffer);
}
Buffer::operator VkBuffer() const { return buffer; }
Buffer::operator VkBuffer*() const { return (VkBuffer*)&buffer; }
