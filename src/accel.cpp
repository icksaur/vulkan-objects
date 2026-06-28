#include "vkinternal.h"

#include <algorithm>

namespace {

constexpr VkBufferUsageFlags kAsInputUsage =
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr VkBufferUsageFlags kAsStorageUsage =
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr VkBufferUsageFlags kScratchUsage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

VkDeviceAddress alignUp(VkDeviceAddress value, uint32_t alignment) {
    return (value + alignment - 1) & ~VkDeviceAddress(alignment - 1);
}

std::unique_ptr<Buffer> makeBuffer(size_t bytes, VkBufferUsageFlags usage, bool hostVisible = false) {
    BufferBuilder builder(std::max<size_t>(bytes, 4));
    builder.storage().usageFlags(usage);
    if (hostVisible) builder.hostVisible();
    return std::make_unique<Buffer>(builder);
}

std::unique_ptr<Buffer> makeScratch(VkDeviceSize bytes, VkDeviceAddress& outAddress) {
    uint32_t align = g_context().accelerationStructureScratchAlignment();
    auto buffer = makeBuffer(bytes + align, kScratchUsage);
    outAddress = alignUp(buffer->deviceAddress(), align);
    return buffer;
}

std::vector<VkAccelerationStructureGeometryKHR> triangleGeometry(const std::vector<BlasTriangles>& triangles) {
    std::vector<VkAccelerationStructureGeometryKHR> out(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        out[i] = {};
        out[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        out[i].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        out[i].flags = triangles[i].opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
        auto& tri = out[i].geometry.triangles;
        tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tri.vertexFormat = triangles[i].vertexFormat;
        tri.vertexData.deviceAddress = triangles[i].vertices;
        tri.vertexStride = triangles[i].vertexStride;
        tri.maxVertex = triangles[i].vertexCount ? triangles[i].vertexCount - 1 : 0;
        tri.indexType = triangles[i].indices ? triangles[i].indexType : VK_INDEX_TYPE_NONE_KHR;
        tri.indexData.deviceAddress = triangles[i].indices;
    }
    return out;
}

std::vector<uint32_t> primitiveCounts(const std::vector<BlasTriangles>& triangles) {
    std::vector<uint32_t> out(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) out[i] = triangles[i].triangleCount;
    return out;
}

VkBuildAccelerationStructureFlagsKHR buildFlags(bool fastTrace, bool allowUpdate) {
    VkBuildAccelerationStructureFlagsKHR flags =
        fastTrace ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                  : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    if (allowUpdate) flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    return flags;
}

} // namespace

BlasBuilder& BlasBuilder::triangles(Buffer& vtx, uint32_t vtxCount, Buffer& idx, uint32_t triCount) {
    BlasTriangles t;
    t.vertices = vtx.deviceAddress();
    t.vertexCount = vtxCount;
    t.indices = idx.deviceAddress();
    t.triangleCount = triCount;
    geoms.push_back(t);
    return *this;
}

BlasBuilder& BlasBuilder::triangles(const BlasTriangles& t) {
    geoms.push_back(t);
    return *this;
}

BlasBuilder& BlasBuilder::refittable() {
    allowUpdate = true;
    return *this;
}

BlasBuilder& BlasBuilder::fastBuild() {
    fastTrace = false;
    return *this;
}

Blas::Blas(BlasBuilder& builder)
    : geometry_(builder.geoms), updatable_(builder.allowUpdate),
      flags_(buildFlags(builder.fastTrace, builder.allowUpdate)) {
    assert(!geometry_.empty());
    auto geoms = triangleGeometry(geometry_);
    auto counts = primitiveCounts(geometry_);
    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = flags_;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = static_cast<uint32_t>(geoms.size());
    build.pGeometries = geoms.data();

    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    rtGetAccelerationStructureBuildSizes(g_context().deviceHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                         &build, counts.data(), &sizes);
    backing_ = makeBuffer(sizes.accelerationStructureSize, kAsStorageUsage);
    scratch_ = makeScratch(std::max<VkDeviceSize>(sizes.buildScratchSize, 4), scratchAddress_);
    if (updatable_) updateScratch_ = makeScratch(std::max<VkDeviceSize>(sizes.updateScratchSize, 4), updateScratchAddress_);

    VkAccelerationStructureCreateInfoKHR create = {};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.buffer = *backing_;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if (rtCreateAccelerationStructure(g_context().deviceHandle(), &create, nullptr, &handle_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create BLAS");
    }

    VkAccelerationStructureDeviceAddressInfoKHR addr = {};
    addr.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addr.accelerationStructure = handle_;
    address_ = rtGetAccelerationStructureDeviceAddress(g_context().deviceHandle(), &addr);
}

Blas::Blas(Blas&& other) noexcept
    : backing_(std::move(other.backing_)), scratch_(std::move(other.scratch_)),
      updateScratch_(std::move(other.updateScratch_)), geometry_(std::move(other.geometry_)),
      handle_(other.handle_), address_(other.address_), scratchAddress_(other.scratchAddress_),
      updateScratchAddress_(other.updateScratchAddress_), updatable_(other.updatable_),
      built_(other.built_), flags_(other.flags_) {
    other.handle_ = VK_NULL_HANDLE;
    other.address_ = 0;
}

Blas& Blas::operator=(Blas&& other) noexcept {
    if (this == &other) return *this;
    destroyHandle();
    backing_ = std::move(other.backing_);
    scratch_ = std::move(other.scratch_);
    updateScratch_ = std::move(other.updateScratch_);
    geometry_ = std::move(other.geometry_);
    handle_ = other.handle_;
    address_ = other.address_;
    scratchAddress_ = other.scratchAddress_;
    updateScratchAddress_ = other.updateScratchAddress_;
    updatable_ = other.updatable_;
    built_ = other.built_;
    flags_ = other.flags_;
    other.handle_ = VK_NULL_HANDLE;
    other.address_ = 0;
    return *this;
}

Blas::~Blas() { destroyHandle(); }

void Blas::destroyHandle() {
    if (handle_ == VK_NULL_HANDLE) return;
    VulkanContext& context = g_context();
    if (context.options.enableImmediateDestroy) {
        rtDestroyAccelerationStructure(context.deviceHandle(), handle_, nullptr);
    } else {
        context.destroyGenerations[context.frameInFlightIndex].accelStructures.push_back(handle_);
    }
    handle_ = VK_NULL_HANDLE;
}

VkDeviceAddress Blas::address() const { return address_; }
Buffer& Blas::backing() { return *backing_; }
bool Blas::updatable() const { return updatable_; }
Blas::operator VkAccelerationStructureKHR() const { return handle_; }

TlasInstances& TlasInstances::add(const Blas& blas, const float (&xform3x4)[12], uint32_t customIndex, uint8_t mask) {
    assert(customIndex < (1u << 24));
    VkAccelerationStructureInstanceKHR inst = {};
    for (uint32_t r = 0; r < 3; ++r)
        for (uint32_t c = 0; c < 4; ++c)
            inst.transform.matrix[r][c] = xform3x4[r * 4 + c];
    inst.instanceCustomIndex = customIndex;
    inst.mask = mask;
    inst.instanceShaderBindingTableRecordOffset = 0;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = blas.address();
    raw.push_back(inst);
    return *this;
}

void TlasInstances::clear() { raw.clear(); }

Tlas::Tlas(uint32_t maxInstances) : maxInstances_(maxInstances) {
    assert(maxInstances > 0);
    instanceBuf_ = makeBuffer(sizeof(VkAccelerationStructureInstanceKHR) * maxInstances, kAsInputUsage, true);

    VkAccelerationStructureGeometryKHR geom = {};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.data.deviceAddress = instanceBuf_->deviceAddress();

    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    rtGetAccelerationStructureBuildSizes(g_context().deviceHandle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                         &build, &maxInstances, &sizes);
    backing_ = makeBuffer(sizes.accelerationStructureSize, kAsStorageUsage);
    scratch_ = makeScratch(std::max<VkDeviceSize>(sizes.buildScratchSize, 4), scratchAddress_);

    VkAccelerationStructureCreateInfoKHR create = {};
    create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create.buffer = *backing_;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if (rtCreateAccelerationStructure(g_context().deviceHandle(), &create, nullptr, &handle_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create TLAS");
    }
    rid_ = g_context().bindlessTable.registerTlas(g_context().deviceHandle(), handle_);
}

Tlas::Tlas(Tlas&& other) noexcept
    : backing_(std::move(other.backing_)), scratch_(std::move(other.scratch_)),
      instanceBuf_(std::move(other.instanceBuf_)), handle_(other.handle_),
      scratchAddress_(other.scratchAddress_), rid_(other.rid_), maxInstances_(other.maxInstances_) {
    other.handle_ = VK_NULL_HANDLE;
    other.rid_ = kNullRid;
}

Tlas& Tlas::operator=(Tlas&& other) noexcept {
    if (this == &other) return *this;
    destroyHandle();
    backing_ = std::move(other.backing_);
    scratch_ = std::move(other.scratch_);
    instanceBuf_ = std::move(other.instanceBuf_);
    handle_ = other.handle_;
    scratchAddress_ = other.scratchAddress_;
    rid_ = other.rid_;
    maxInstances_ = other.maxInstances_;
    other.handle_ = VK_NULL_HANDLE;
    other.rid_ = kNullRid;
    return *this;
}

Tlas::~Tlas() { destroyHandle(); }

void Tlas::destroyHandle() {
    if (handle_ == VK_NULL_HANDLE) return;
    VulkanContext& context = g_context();
    if (context.options.enableImmediateDestroy) {
        if (rid_ != kNullRid) context.bindlessTable.releaseTlas(rid_);
        rtDestroyAccelerationStructure(context.deviceHandle(), handle_, nullptr);
    } else {
        auto& gen = context.destroyGenerations[context.frameInFlightIndex];
        if (rid_ != kNullRid) gen.tlasRIDs.push_back(rid_);
        gen.accelStructures.push_back(handle_);
    }
    handle_ = VK_NULL_HANDLE;
    rid_ = kNullRid;
}

VkAccelerationStructureKHR Tlas::handle() const { return handle_; }
Buffer& Tlas::backing() { return *backing_; }
uint32_t Tlas::rid() const { return rid_; }
uint32_t Tlas::instanceBufferRid() const { return instanceBuf_->rid(); }
Tlas::operator VkAccelerationStructureKHR() const { return handle_; }

void Commands::buildBlas(Blas& blas, bool refit) {
    if (refit) {
        assert(blas.updatable_ && blas.built_);
        if (!blas.updatable_ || !blas.built_) throw std::runtime_error("invalid BLAS refit before build");
    }
    auto geoms = triangleGeometry(blas.geometry_);
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(blas.geometry_.size());
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> rangePtrs(blas.geometry_.size());
    for (size_t i = 0; i < blas.geometry_.size(); ++i) {
        ranges[i].primitiveCount = blas.geometry_[i].triangleCount;
        rangePtrs[i] = &ranges[i];
    }

    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = blas.flags_;
    build.mode = refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                       : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.srcAccelerationStructure = refit ? blas.handle_ : VK_NULL_HANDLE;
    build.dstAccelerationStructure = blas.handle_;
    build.geometryCount = static_cast<uint32_t>(geoms.size());
    build.pGeometries = geoms.data();
    build.scratchData.deviceAddress = refit ? blas.updateScratchAddress_ : blas.scratchAddress_;
    rtCmdBuildAccelerationStructures(*this, 1, &build, rangePtrs.data());
    if (!refit) blas.built_ = true;
}

void Commands::buildTlas(Tlas& tlas, const TlasInstances& instances) {
    assert(!instances.raw.empty());
    assert(instances.raw.size() <= tlas.maxInstances_);
    tlas.instanceBuf_->upload(const_cast<VkAccelerationStructureInstanceKHR*>(instances.raw.data()),
                              instances.raw.size() * sizeof(VkAccelerationStructureInstanceKHR));
    bufferBarrier(*tlas.instanceBuf_, Stage::Host, Access::HostWrite,
                  Stage::AccelStructureBuild, Access::AccelStructureRead);

    VkAccelerationStructureGeometryKHR geom = {};
    geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geom.geometry.instances.data.deviceAddress = tlas.instanceBuf_->deviceAddress();

    VkAccelerationStructureBuildGeometryInfoKHR build = {};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.dstAccelerationStructure = tlas.handle_;
    build.geometryCount = 1;
    build.pGeometries = &geom;
    build.scratchData.deviceAddress = tlas.scratchAddress_;

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = static_cast<uint32_t>(instances.raw.size());
    const VkAccelerationStructureBuildRangeInfoKHR* rangePtr = &range;
    rtCmdBuildAccelerationStructures(*this, 1, &build, &rangePtr);
}
