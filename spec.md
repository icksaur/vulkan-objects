# vulkan-objects v2 specification

A personal Vulkan wrapper library for "situated software" C++ projects. Designed for correctness at compile time, automatic lifetime management, and a minimal API surface that makes common rendering patterns easy and hard to get wrong.

Always follow and refer to code-quality.md

## concepts

- **VulkanContext** — Singleton owning the Vulkan instance, device, swapchain, and all shared state. RAII. Construction initializes Vulkan; destruction tears everything down.
- **Frame** — Scoped guard representing one frame of GPU work. Manages sync primitives internally. Only one can exist at a time (enforced at compile/runtime).
- **Buffer** — GPU memory with typed usage. Deferred destruction (survives until the GPU is done with it).
- **Image** — GPU image with view. Deferred destruction like Buffer.
- **Pipeline** — Configured rendering or compute pipeline. Context-owned lifetime.
- **Bindless Descriptor Table** — Single global descriptor set. Resources get an integer index on creation. Shaders address resources by index via push constants. No per-draw descriptor binding.

## use cases

- **Interactive visualizations**: Camera orbiting a procedurally generated mesh, updated each frame via compute shader, rendered with mesh shaders. Typical "creative coding" loop.
- **Compute-only processing**: Load data into buffers, dispatch compute, read results back. No rendering pass needed.
- **Textured scenes**: Load TGA/image files, sample them in fragment shaders. Bindless means any number of textures accessible by index without rebinding.
- **Multi-pass rendering**: Render to offscreen image, then sample it in a later pass (e.g. post-processing). Image barriers handled automatically.

### target API usage

```cpp
int main() {
    SDLWindow window("App", 1280, 720);
    VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

    ShaderModule fragShader(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
    ShaderModule meshShader(ShaderBuilder().mesh().fromFile("quad.mesh.spv"));
    ShaderModule compShader(ShaderBuilder().compute().fromFile("vertices.comp.spv"));

    // Buffers and images self-register in the global bindless table.
    // Their resource ID (RID) is used in push constants to address them in shaders.
    Buffer uniformBuffer(BufferBuilder(sizeof(UniformData)).uniform());
    Buffer storageBuffer(BufferBuilder(vertexDataSize).storage());

    // One-shot setup commands (replaces ScopedCommandBuffer)
    auto setupCmd = Commands::oneShot();
    Image texture = createImageFromTGAFile(setupCmd, "texture.tga");
    std::vector<Image> depthBuffers;
    for (size_t i = 0; i < context.swapchainImageCount; ++i)
        depthBuffers.emplace_back(ImageBuilder().depth(), setupCmd);
    setupCmd.submitAndWait();

    // Resize callback — invoked by Frame when swapchain is rebuilt
    context.onSwapchainResize([&](Commands & cmd, VkExtent2D extent) {
        depthBuffers.clear();
        for (size_t i = 0; i < context.swapchainImageCount; ++i)
            depthBuffers.emplace_back(ImageBuilder().depth(), cmd);
    });

    Pipeline graphicsPipeline = PipelineBuilder()
        .meshShader(meshShader)
        .fragmentShader(fragShader)
        .build();

    Pipeline computePipeline = PipelineBuilder()
        .computeShader(compShader)
        .build();

    while (!done) {
        // Frame handles all sync: fence wait, image acquire, semaphore management.
        Frame frame;

        uniformBuffer.upload(&uniformData, sizeof(uniformData));

        // Push constants carry resource indices — no descriptor binding needed.
        struct PushData { uint32_t uniformRID, storageRID, textureRID; };
        PushData push = { uniformBuffer.rid(), storageBuffer.rid(), texture.rid() };

        auto cmd = frame.beginCommands();

        cmd.bindCompute(computePipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.dispatch(groupCount, 1, 1);

        cmd.bufferBarrier(storageBuffer, Stage::Compute, Stage::MeshShader);

        cmd.beginRendering(depthBuffers[frame.swapchainImageIndex()]);
        cmd.bindGraphics(graphicsPipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.drawMeshTasks(quadCount, 1, 1);
        cmd.endRendering();

        frame.submit(cmd);
    }
}
```

## current state and defects

### what works

- VulkanContext singleton with RAII construction/destruction
- Mesh shader pipeline (no vertex shaders — good, keep this)
- Compute shaders with storage buffers
- Dynamic rendering (no render passes — good, keep this)
- Image loading from TGA files via staging buffer
- Mipmap generation
- Multisampling support
- Frame-based sync with fences and semaphores
- Deferred buffer destruction via generational system
- Builder patterns for most objects

### what is complex or error-prone

**1. Descriptor management is the biggest source of complexity**

Creating a working descriptor setup requires 7 coordinated steps in main.cpp:
1. Create a `DescriptorLayoutBuilder`, add bindings with correct types, stages, and binding indices
2. Call `.build()` to get a `VkDescriptorSetLayout`
3. Create a `DescriptorPoolBuilder`, add pool sizes that must match the layout
4. Call `.build()` to get a `DescriptorPool`
5. Allocate a `VkDescriptorSet` from the pool using the layout
6. Create a `DescriptorSetBinder`, bind each resource with the correct binding index and type
7. Call `.updateSets()`
8. At draw time, bind the descriptor set with correct pipeline bind point and dynamic offsets

This is 8 steps where any mismatch (wrong binding index, wrong type, wrong pool size, wrong stage flags) produces a validation error at runtime, not a compile error. Adding a new buffer or texture means updating 4 places.

**2. Synchronization is exposed and manual**

- User must understand semaphores, fences, and barriers
- `Frame` methods must be called in exact order: `prepareOldestFrameResources()` → `acquireNextImageIndex()` → `submitCommandBuffer()` → `tryPresentQueue()`
- Frame class self-documents as "does too much and methods MUST be called in order"
- Barrier builders (`BufferBarrier`, `ImageBarrier`, `ImageTransition`) are three overlapping APIs for similar things
- `recordTransitionImageLayout()` still exists and is self-described as "misguided"
- Dynamic uniform buffer offset calculation is manual (alignment math in main.cpp)

**3. Image lifetime is inconsistent with Buffer lifetime**

- `Buffer::~Buffer()` defers destruction (safe)
- `Image::~Image()` destroys immediately (unsafe if GPU is still using it)
- This asymmetry is a trap

**4. Resource ownership is split and unclear**

- Some resources are context-owned (pipeline layouts, pipelines, semaphores, fences, descriptor set layouts)
- Some are RAII-owned (Buffer, Image, ShaderModule, TextureSampler, DescriptorPool)
- Some are manually owned (VkDescriptorSet — allocated from pool, freed when pool resets)
- No consistent pattern; user must know which is which

**5. Frame requires returning values the user doesn't want**

- `acquireNextImageIndex(nextImage, renderFinishedSemaphore)` forces the user to provide storage for a semaphore they label "unused"
- `tryPresentQueue()` returning false triggers a complex swapchain rebuild sequence the user must implement

### code quality violations

| Violation | Evidence |
|-----------|----------|
| **Complexity** | 7-step descriptor setup, 4-step frame loop with ordering constraints, 3 overlapping barrier APIs, manual uniform buffer alignment |
| **Code must be kept in sync** | Descriptor layout bindings, pool sizes, set bindings, and shader bindings must all agree. Adding a resource touches 4 places. |
| **Wrong abstraction** | `ImageTransition` and `ImageBarrier` do the same thing with different APIs. `recordTransitionImageLayout()` is self-described as "misguided" but still used. |
| **Relying on side effects** | Frame methods must be called in order. No compile-time enforcement. |
| **Coupling** | main.cpp must understand descriptor types, binding indices, pool sizes, dynamic offsets, swapchain indices, semaphore semantics |
| **Mutable shared state** | `nextResourceIndex`, `frameInFlightIndex` mutated by Frame, read by user for manual offset calculation |
| **Global state** | `g_context` singleton accessed by everything |

## goals

### P0 — Bindless descriptors (eliminate descriptor complexity)

Replace the 7-step descriptor setup with a single global bindless descriptor table managed by the context. Every Buffer and Image gets a resource ID (RID) on creation. Shaders access resources by integer index via push constants.

#### design

**Bindless descriptor table** owned by VulkanContext:
- One `VkDescriptorSetLayout` with 3 bindings: storage buffers, combined image samplers, storage images
- Each binding is a large, partially-bound, update-after-bind array (descriptor indexing, core in Vulkan 1.2)
- One `VkDescriptorPool` and one `VkDescriptorSet` allocated at context creation
- One `VkPipelineLayout` with this layout + 128 bytes of push constants (guaranteed minimum)

**Resource registration** is automatic:
- `Buffer` constructor writes its descriptor to the global set at an auto-assigned array index
- `Image` constructor writes its descriptor (with sampler) at an auto-assigned array index
- Destructor marks the index as free for reuse
- RID is stored on the object and returned via `.rid()`

**Push constants** replace per-resource descriptor binding:
- User packs RIDs into a struct and calls `cmd.pushConstants(&data, sizeof(data))`
- Shaders declare `layout(push_constant)` and index into descriptor arrays

**What this eliminates from user code:**
- `DescriptorLayoutBuilder` — gone (context creates the one layout)
- `DescriptorPoolBuilder` — gone (context creates the one pool)
- `DescriptorPool::allocate()` — gone
- `DescriptorSetBinder` — gone (registration happens in constructors)
- `vkCmdBindDescriptorSets` calls — gone (bound once at command buffer start)
- Dynamic uniform buffer offset math — gone (use push constants for per-frame data, or use storage buffers with frame index)

**What this changes in shader code:**
```glsl
// Before
layout(set=0, binding=0) uniform UBO { mat4 viewProj; float zScale; } ubo;
layout(set=0, binding=1) uniform sampler2D texSampler;
layout(set=0, binding=2) buffer SSBO { float data[]; } ssbo;

// After
layout(set=0, binding=0) buffer StorageBuffers { float data[]; } storageBuffers[];
layout(set=0, binding=1) uniform sampler2D samplers[];
layout(push_constant) uniform PushConstants { uint uboRID; uint ssboRID; uint texRID; };
// access: storageBuffers[nonuniformEXT(ssboRID)].data[i]
// access: texture(samplers[nonuniformEXT(texRID)], uv)
```

**Uniform buffers become storage buffers** in the bindless model. This is standard practice. Storage buffers are more flexible (variable size, read/write) and the performance difference is negligible on desktop GPUs.

#### migration notes

- `DescriptorLayoutBuilder`, `DescriptorPoolBuilder`, `DescriptorPool`, `DescriptorSetBinder` are removed from the public API
- `PushConstantsBuilder` is removed (push constants are always 128 bytes, all stages)
- `createPipelineLayout()` free function is removed (context owns the one layout)
- Existing shaders need updating to use descriptor arrays + push constants

### P1 — Simplified synchronization

Replace the three barrier APIs and manual frame ordering with a simpler model.

#### design: synchronization2

Adopt `VK_KHR_synchronization2` (core in Vulkan 1.3, which we already require):
- Use `vkCmdPipelineBarrier2` instead of `vkCmdPipelineBarrier`
- Use `VkMemoryBarrier2`, `VkImageMemoryBarrier2`, `VkBufferMemoryBarrier2` which pair stage+access together (clearer)
- Use `vkQueueSubmit2` instead of `vkQueueSubmit`
- Use `VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL` / `VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL` (generic layouts)
- Deprecate `TOP_OF_PIPE` / `BOTTOM_OF_PIPE`

#### design: unified barrier API

Replace `BufferBarrier`, `ImageBarrier`, and `ImageTransition` with one `Barrier` type:

```cpp
// Buffer barrier
Barrier(cmd).buffer(storageBuffer)
    .from(Stage::Compute, Access::ShaderWrite)
    .to(Stage::MeshShader, Access::ShaderRead)
    .record();

// Image layout transition
Barrier(cmd).image(texture)
    .from(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
    .to(Stage::FragmentShader, Access::ShaderRead, Layout::ReadOnly)
    .record();
```

Enums `Stage`, `Access`, `Layout` wrap the Vulkan 1.3 `VK_PIPELINE_STAGE_2_*`, `VK_ACCESS_2_*`, `VK_IMAGE_LAYOUT_*` constants with short names. This is a thin wrapper, not a new abstraction — the user still thinks in Vulkan barrier terms but with less boilerplate.

#### design: Frame handles all frame sync internally

```cpp
// Before (4 ordered calls, user manages semaphore storage)
Frame frame;
frame.prepareOldestFrameResources();
frame.acquireNextImageIndex(nextImage, unusedSemaphore);
// ... record commands ...
frame.submitCommandBuffer(commandBuffer);
if (!frame.tryPresentQueue()) { /* rebuild swapchain */ }

// After (2 calls, Frame handles everything)
Frame frame; // constructor: waits fence, cleans old resources, acquires image
auto cmd = frame.beginCommands(); // returns a recording command buffer
// ... record commands ...
frame.submit(cmd); // submits + presents, rebuilds swapchain if needed
```

- `Frame()` constructor does what `prepareOldestFrameResources()` + `acquireNextImageIndex()` did
- `frame.submit(cmd)` does what `submitCommandBuffer()` + `tryPresentQueue()` + swapchain rebuild did
- Swapchain rebuild is internal — frame invokes a user-provided resize callback (lambda) to recreate size-dependent resources (depth buffers)
- Semaphores and fences are fully hidden from the user
- `frame.swapchainImageIndex()` is available if the user needs the image index
- `frame.swapchainImageView()` returns the current swapchain image view for rendering

#### what is removed

- `BufferBarrier` struct
- `ImageBarrier` struct
- `ImageTransition` struct
- `recordTransitionImageLayout()` function
- `prepareOldestFrameResources()` method
- `acquireNextImageIndex()` method
- `tryPresentQueue()` method
- `rebuildPresentationResources()` as a user-facing method

### P2 — Consistent RAII lifetime

Make all resource types use deferred destruction, matching how Buffer already works.

#### design

- `Image::~Image()` defers destruction into the current frame's `DestroyGeneration`, same as `Buffer`
- `ShaderModule`, `TextureSampler`, `DescriptorPool` also defer if they could be in-flight
- Context-owned resources (pipelines, pipeline layouts) continue to be destroyed at context teardown
- Rule: **if the GPU might be using it, destruction is deferred. If it's only used at setup time, destruction is immediate.**

This eliminates the current trap where destroying an Image while the GPU uses it causes undefined behavior.

### P3 — Scoped command recording

Make command buffer recording safer with RAII scoping.

#### design

`frame.beginCommands()` returns a `Commands` object that:
- Internally allocates (or resets) a command buffer for the current frame
- Begins recording in constructor
- Binds the global bindless descriptor set for both compute and graphics bind points
- Provides typed methods: `bindCompute()`, `bindGraphics()`, `dispatch()`, `drawMeshTasks()`, `pushConstants()`, `beginRendering()`, `endRendering()`, `bufferBarrier()`, `imageBarrier()`
- Ends recording when it goes out of scope or when passed to `frame.submit()`
- Is move-only (no copy)

This replaces `CommandBuffer`, `CommandBufferRecording`, and direct `vkCmd*` calls in user code. The user never calls raw Vulkan commands.

```cpp
auto cmd = frame.beginCommands();
cmd.bindCompute(computePipeline);
cmd.pushConstants(&pushData, sizeof(pushData));
cmd.dispatch(groups, 1, 1);
cmd.bufferBarrier(storageBuffer, Stage::Compute, Stage::MeshShader);
{
    auto pass = cmd.beginRendering(depthImage);
    cmd.bindGraphics(graphicsPipeline);
    cmd.pushConstants(&pushData, sizeof(pushData));
    cmd.drawMeshTasks(count, 1, 1);
} // pass ends rendering
frame.submit(cmd); // cmd ends recording, submits, presents
```

## considerations

### bindless descriptor limits

Desktop GPUs support very large descriptor counts (500k+). We should pick conservative defaults (e.g. 16384 storage buffers, 16384 samplers, 4096 storage images) and query actual limits at context creation. For a personal project this is more than enough.

### uniform buffers → storage buffers

Uniform buffers have a size limit (`maxUniformBufferRange`, often 64KB) and alignment requirements that the user currently calculates manually. Storage buffers have much larger limits and simpler alignment. Switching to storage buffers for all data eliminates the dynamic offset complexity. The performance difference is negligible for non-AAA use (storage buffers may be slightly slower for small, frequently-read data on some GPUs, but this is dwarfed by the simplicity gain).

### push constant size

The Vulkan spec guarantees 128 bytes of push constants. This is enough for ~32 uint32 resource indices. If a shader needs more, it can read additional data from a storage buffer (addressed by one of those indices). 128 bytes is sufficient for all foreseeable personal project use cases.

### VMA (Vulkan Memory Allocator)

Not proposed for this iteration. The current manual `vkAllocateMemory` + `findMemoryType` approach works and is simple enough for the project scope. VMA could be considered later if memory management becomes a pain point (many small allocations, memory budget management, defragmentation). Adding VMA is a significant dependency increase.

### swapchain rebuild

Currently the user must handle swapchain rebuild (recreating depth images, etc.) in main.cpp. In the new design, `Frame` handles the swapchain rebuild internally. For resources that depend on swapchain size (like depth buffers), the user provides a callback or checks `frame.swapchainResized()` and recreates them. This is simpler than the current approach but still requires user awareness.

## risks and mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Bindless requires Vulkan 1.2+ features (descriptor indexing) | Won't work on very old drivers | We already require Vulkan 1.3 features (dynamic rendering). Descriptor indexing is core in 1.2. No new hardware requirement. |
| Storage buffers instead of uniform buffers may be slower for small frequent reads | Minor perf regression on some GPUs | Benchmark before/after. For personal projects, simplicity > marginal perf. Can reintroduce uniform buffer path later if measured. |
| Push constant 128-byte limit constrains per-draw data | May need workaround for complex shaders | Use one push constant RID to point at a storage buffer containing the full per-draw data. This is standard practice. |
| Shader changes required (bindless arrays) | All existing shaders break | Only 3 shaders in the project. Rewrite them as part of the migration. |
| `nonuniformEXT` qualifier needed in shaders | Easy to forget, causes GPU hangs on some hardware | Add `#extension GL_EXT_nonuniform_qualifier : require` to a shared GLSL include. Document in README. |
| Hiding sync from user may make debugging harder | Can't easily insert custom barriers | `Commands` object exposes `bufferBarrier()` / `imageBarrier()` for explicit barriers when needed. The common case (compute→graphics) is handled. Advanced users can access the raw `VkCommandBuffer`. |

## plan

### phase 1: bindless descriptors (P0)

1. Enable descriptor indexing features in `createLogicalDevice()` (`runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `shaderStorageBufferArrayNonUniformIndexing`, `shaderSampledImageArrayNonUniformIndexing`, update-after-bind flags)
2. Add `BindlessTable` struct to VulkanContext: owns the descriptor set layout, pool, set, and pipeline layout. Manages a free-list of indices per resource type (storage buffer, sampler, storage image).
3. Add `rid()` method to `Buffer` and `Image`. Constructor calls `BindlessTable::registerStorageBuffer()` / `registerSampler()`. Destructor calls `BindlessTable::release()`.
4. Create the single `VkPipelineLayout` with the bindless layout + 128-byte push constant range (all stages) at context creation.
5. Change `GraphicsPipelineBuilder` and `createComputePipeline()` to use the context-owned pipeline layout.
6. Update `Buffer` constructor: all buffers get `VK_BUFFER_USAGE_STORAGE_BUFFER_BIT` added. Buffers with `.uniform()` are treated as storage buffers internally.
7. Update `Image` constructor: sampler is created alongside image, and the combined image sampler descriptor is written to the bindless set.
8. Remove `DescriptorLayoutBuilder`, `DescriptorPoolBuilder`, `DescriptorPool`, `DescriptorSetBinder`, `PushConstantsBuilder` from public API.
9. Remove the `createPipelineLayout()` free functions.
10. Update all 3 shaders to use bindless descriptor arrays + push constants.
11. Update main.cpp to use push constants instead of descriptor setup.

### phase 2: synchronization simplification (P1)

1. Remove `recordTransitionImageLayout()` (the "misguided" function).
2. Create a single `Barrier` builder using synchronization2 types (`VkMemoryBarrier2`, `VkImageMemoryBarrier2`, `VkBufferMemoryBarrier2`).
3. Add `Stage`, `Access`, `Layout` enum wrappers for `VK_PIPELINE_STAGE_2_*`, `VK_ACCESS_2_*`, `VK_IMAGE_LAYOUT_*` constants.
4. Replace all usages of `BufferBarrier`, `ImageBarrier`, `ImageTransition` with `Barrier`.
5. Remove the old `BufferBarrier`, `ImageBarrier`, `ImageTransition` types.
6. Switch queue submission to `vkQueueSubmit2`.
7. Consolidate `Frame` methods: constructor does fence wait + resource cleanup + image acquire. `submit()` does command submit + present + optional swapchain rebuild.
8. Remove `prepareOldestFrameResources()`, `acquireNextImageIndex()`, `tryPresentQueue()`, `rebuildPresentationResources()` as public methods.
9. Add resize callback registration (`context.onSwapchainResize(lambda)`) and `frame.swapchainImageView()` accessor.
10. Update main.cpp frame loop.

### phase 3: consistent RAII (P2)

1. Change `Image::~Image()` to defer destruction via `DestroyGeneration`, adding image, memory, and image view to the destroy list.
2. Extend `DestroyGeneration` to hold `VkImage`, `VkImageView` in addition to current `VkBuffer`, `VkDeviceMemory`, `VkCommandBuffer`.
3. Audit `TextureSampler` / `ShaderModule` — if they can be in-flight, defer their destruction too.
4. Document the rule: "GPU-used resources defer; setup-only resources destroy immediately."

### phase 4: scoped commands (P3)

1. Create `Commands` struct wrapping `VkCommandBuffer` with typed methods.
2. Constructor begins recording and binds the global bindless descriptor set.
3. Add methods: `bindCompute()`, `bindGraphics()`, `dispatch()`, `drawMeshTasks()`, `pushConstants()`, `beginRendering()`, `endRendering()`, `bufferBarrier()`, `imageBarrier()`.
4. `operator VkCommandBuffer()` for escape hatch to raw Vulkan.
5. `Frame::beginCommands()` returns a `Commands` for the current frame's command buffer.
6. `Frame::submit(Commands&)` ends recording and submits.
7. Remove `CommandBuffer`, `CommandBufferRecording`, `ScopedCommandBuffer` from public API.
8. Add `Commands::submitAndWait()` for one-shot setup work (replaces `ScopedCommandBuffer`).
9. Update main.cpp to use `Commands` for both setup and rendering.

## open questions

### resolved

1. **Swapchain resize notification**: Frame provides a resize callback (lambda registered at setup time). More automatic than polling. Frame invokes the callback during `submit()` when swapchain rebuild is needed, passing the new extent. User's callback recreates size-dependent resources (depth buffers).

2. **TextureSampler**: Per-image sampler. Each Image bundles its own sampler in the bindless descriptor table. Simpler API, no separate sampler management. The `TextureSampler` type is removed from the public API.

3. **Image loading**: Stays as a free function (`createImageFromTGAFile()` or similar). Keeps Image decoupled from file format details. TGA loading is a utility, not an Image concern.

4. **ScopedCommandBuffer**: Unified into `Commands`. One command abstraction for both setup-time work and per-frame rendering. `Commands` can be created outside a Frame for setup operations (image loading, initial transitions) via a static factory or context method, and submitted directly. This eliminates the `ScopedCommandBuffer` type.
