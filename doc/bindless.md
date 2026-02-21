# bindless descriptor strategy

## overview

All GPU resources are addressed by integer index through a single global descriptor set. No per-draw descriptor binding. No descriptor layout/pool/set management in user code.

## the three bindings

The bindless table has exactly three bindings. These cover every resource type a GPU shader needs:

| Binding | Type | Max | Purpose |
|---------|------|-----|---------|
| 0 | Storage buffer | 16384 | All buffer data: vertices, indices, uniforms, structured data |
| 1 | Combined image sampler | 16384 | All sampled textures with their samplers |
| 2 | Storage image | 4096 | Read/write image access (compute stores, imageLoad/imageStore) |

This is the canonical bindless layout used by modern engines. Three bindings is not a limitation — it maps to the three fundamental GPU resource categories.

### why not more bindings?

Descriptor types NOT included, and why:

- **Uniform buffers** — replaced by storage buffers. Storage buffers are more flexible (variable size, read-write, no 64KB limit). Performance difference is negligible on desktop GPUs.
- **Uniform/storage texel buffers** — rare. Use storage buffers instead.
- **Input attachments** — only for subpasses. We use dynamic rendering (no subpasses).
- **Separate sampler + sampled image** — combined image sampler covers this with simpler API.
- **Acceleration structures** — ray tracing, not in scope.

## resource lifecycle

Resources self-register on construction and self-release on destruction:

```
Buffer constructor  → BindlessTable::registerStorageBuffer() → rid assigned
Buffer destructor   → BindlessTable::releaseStorageBuffer()  → rid recycled
Image constructor   → BindlessTable::registerSampler()       → rid assigned
Image destructor    → BindlessTable::releaseSampler()        → rid recycled
```

A free-list per binding type recycles indices. Descriptors are partially-bound and update-after-bind, so unused slots and concurrent updates are safe.

## shader access

Shaders declare unbounded descriptor arrays and index by push-constant RID:

```glsl
layout(set=0, binding=0) buffer StorageBuffers { float data[]; } storageBuffers[];
layout(set=0, binding=1) uniform sampler2D samplers[];
layout(set=0, binding=2, rgba8) uniform image2D storageImages[];

layout(push_constant) uniform Push {
    // ... per-draw data ...
    uint vertexBufferRID;
    uint textureRID;
};

// access:
vec4 color = texture(samplers[nonuniformEXT(textureRID)], uv);
float v = storageBuffers[nonuniformEXT(vertexBufferRID)].data[i];
```

`nonuniformEXT` is required when the index may vary across invocations in a subgroup. Always use it for RIDs from push constants.

## push constants

128 bytes of push constants (Vulkan guaranteed minimum), all stages. This carries:

- Per-frame data (mat4 viewProjection = 64 bytes)
- Per-draw scalars (floats, ints)
- Resource IDs (uint32 each)

128 bytes = 32 uint32s. If a shader needs more data, one of those uint32s can be an RID pointing to a storage buffer containing the full dataset.

### compile-time size validation

Push constant structs inherit from `PushConstantBase<T>` to enforce the 128-byte limit at compile time:

```cpp
struct MyPush : PushConstantBase<MyPush> {
    float mvp[16];        // 64 bytes
    uint32_t verticesRID; //  4 bytes
    uint32_t textureRID;  //  4 bytes
};                        // 72 bytes — ok
```

The typed `pushConstants(const T&)` overload also static-asserts at the call site and eliminates the `sizeof` argument:

```cpp
cmd.pushConstants(push);   // compile-time checked, no sizeof
```

### different structs for different pipelines

The pipeline layout declares a single 128-byte push constant *range*, not a fixed struct. Each pipeline's shaders interpret those bytes however they want. Different pipelines can (and should) use different C++ push constant structs:

```cpp
struct ComputePush : PushConstantBase<ComputePush> {
    uint32_t opsRID;
    uint32_t hitsRID;
    // ... compute-only RIDs ...
};

struct RenderPush : PushConstantBase<RenderPush> {
    float mvp[16];
    uint32_t verticesRID;
    uint32_t triangleIndicesRID;
};
```

This avoids packing unrelated RIDs into a single monolithic struct. Compute and render passes each carry only what their shaders need.

## what this eliminates from user code

The old 7-step descriptor setup (layout builder → pool builder → pool allocate → set binder → update sets → bind sets → manage dynamic offsets) is completely gone. User code never touches descriptors. The entire descriptor lifecycle is internal to vkobjects.

## Vulkan features required

All core in Vulkan 1.2 (we require 1.3):

- `descriptorIndexing`
- `runtimeDescriptorArray`
- `descriptorBindingPartiallyBound`
- `descriptorBindingUpdateUnusedWhilePending`
- `shaderStorageBufferArrayNonUniformIndexing`
- `shaderSampledImageArrayNonUniformIndexing`
- `descriptorBindingStorageBufferUpdateAfterBind`
- `descriptorBindingSampledImageUpdateAfterBind`
- `descriptorBindingStorageImageUpdateAfterBind`
