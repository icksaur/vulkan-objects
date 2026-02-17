# vulkan-objects specification

A personal Vulkan wrapper library for C++ projects. Automatic lifetime management, bindless descriptors, and a minimal API surface that makes common rendering patterns easy and hard to get wrong.

Always follow and refer to code-quality.md.

## concepts

- **VulkanContext** — Singleton owning the Vulkan instance, device, swapchain, bindless descriptor table, and all shared state. RAII. Construction initializes Vulkan; destruction tears everything down.
- **Frame** — Scoped guard representing one frame of GPU work. Constructor waits for the oldest in-flight frame's fence, cleans deferred resources, acquires the next swapchain image. Destructor advances the frame index. Only one can exist at a time (runtime enforced).
- **Commands** — Scoped command buffer recording. Constructor begins recording and binds the global bindless descriptor set. Provides typed methods for compute, rendering, barriers, and push constants. Move-only. Also used for one-shot setup work via `Commands::oneShot()`.
- **Buffer** — GPU buffer with automatic bindless registration. Every buffer gets a resource ID (RID) on construction. Deferred destruction via DestroyGeneration.
- **Image** — GPU image with view and sampler. Automatic bindless registration (sampled images or storage images). Deferred destruction via DestroyGeneration.
- **Barrier** — Synchronization2-based barrier builder for buffer and image memory barriers.
- **DestroyGeneration** — Per-frame-slot collection of Vulkan handles awaiting deferred destruction. Cleaned when the fence proves the GPU is done with that frame slot.
- **BindlessTable** — Single global descriptor set with three bindings (storage buffers, combined image samplers, storage images). Resources register/unregister automatically. See doc/bindless.md for details.

## target API usage

From main.cpp — this is a working example:

```cpp
int main() {
    SDLWindow window("App", 1280, 720);
    VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

    ShaderModule fragShader(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
    ShaderModule compShader(ShaderBuilder().compute().fromFile("vertices.comp.spv"));
    ShaderModule meshShader(ShaderBuilder().mesh().fromFile("quad.mesh.spv"));

    // One-shot setup commands
    auto setupCmd = Commands::oneShot();
    Image texture = createImageFromTGAFile(setupCmd, "texture.tga");
    std::vector<Image> depthImages;
    for (size_t i = 0; i < context.swapchainImageCount; ++i)
        depthImages.emplace_back(ImageBuilder().depth(), setupCmd);
    setupCmd.submitAndWait();

    // Resize callback — invoked by Frame when swapchain is rebuilt
    context.onSwapchainResize([&](Commands & cmd, VkExtent2D extent) {
        (void)extent;
        depthImages.clear();
        for (size_t i = 0; i < context.swapchainImageCount; ++i)
            depthImages.emplace_back(ImageBuilder().depth(), cmd);
    });

    Buffer storageBuffer(BufferBuilder(vertexDataSize).storage());

    VkPipeline graphicsPipeline = GraphicsPipelineBuilder()
        .meshShader(meshShader)
        .fragmentShader(fragShader)
        .build();

    VkPipeline computePipeline = createComputePipeline(compShader);

    struct Push { mat4 vp; float z; uint32_t bufRID, texRID; };

    while (!done) {
        Frame frame;

        Push push = { camera.vp(), 0.2f, storageBuffer.rid(), texture.rid() };

        auto cmd = frame.beginCommands();

        cmd.bindCompute(computePipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.dispatch(100, 1, 1);

        cmd.bufferBarrier(storageBuffer, Stage::Compute, Stage::MeshShader);

        cmd.beginRendering(depthImages[frame.swapchainImageIndex()].imageView);
        cmd.bindGraphics(graphicsPipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.drawMeshTasks(100, 1, 1);
        cmd.endRendering();

        frame.submit(cmd);
    }
}
```

## synchronization model

### frame lifecycle

The frame-in-flight system manages all GPU/CPU synchronization:

1. `Frame()` constructor waits on the fence for the oldest in-flight frame
2. `DestroyGeneration` for that frame slot is cleaned (deferred resources freed)
3. Next swapchain image is acquired
4. `frame.beginCommands()` resets the frame's command buffer, begins recording, transitions swapchain image Undefined→ColorAttachment
5. User records commands (compute, barriers, rendering)
6. `frame.submit(cmd)` transitions swapchain image ColorAttachment→PresentSrc, ends recording, submits via `vkQueueSubmit2`, presents
7. `~Frame()` advances `frameInFlightIndex`

The number of frames in flight equals `swapchainImageCount` (typically 3).

### deferred destruction

Buffer and Image destructors don't destroy Vulkan handles immediately. Instead, handles are pushed into the current frame slot's `DestroyGeneration`. When the fence for that slot is signaled (step 1 above), the handles are destroyed. This ensures the GPU is finished with resources before they are freed.

DestroyGeneration holds: VkBuffer, VkDeviceMemory, VkCommandBuffer, VkImage, VkImageView, VkSampler.

### barriers

`Commands` provides two barrier APIs:

- `cmd.bufferBarrier(buffer, srcStage, dstStage)` — convenience for shader-write → shader-read buffer barriers
- `cmd.imageBarrier(image, srcStage, srcAccess, oldLayout, dstStage, dstAccess, newLayout)` — full image barrier

Both use `vkCmdPipelineBarrier2` (synchronization2). For complex cases, use `Barrier(cmd)` builder directly:

```cpp
Barrier(cmd).image(img, mipLevels)
    .from(Stage::Transfer, Access::TransferWrite, Layout::TransferDst)
    .to(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
    .record();
```

`Stage`, `Access`, `Layout` are typed wrappers for `VK_PIPELINE_STAGE_2_*`, `VK_ACCESS_2_*`, `VK_IMAGE_LAYOUT_*`.

## dynamic content requirements

Interactive applications need to create, destroy, and modify resources at any time. All scenarios are handled:

### swapping pipelines ✓

`cmd.bindCompute(pipeline)` and `cmd.bindGraphics(pipeline)` accept any `VkPipeline`. Multiple bind calls per frame work. Pipelines are immutable once created — no sync concern.

### per-frame buffer writes ✓

CPU: `buffer.upload(data, size)` maps, copies, and unmaps. Safe because `Frame()` waits on the fence for this frame slot.

GPU: Compute shaders write to storage buffers, then `cmd.bufferBarrier()` makes the writes visible to subsequent stages.

### adding and removing resources at runtime ✓

Buffer and Image constructors register with the bindless table and assign an RID. Destructors defer both Vulkan handle destruction and RID release into the current frame's `DestroyGeneration`, ensuring the GPU is finished before the descriptor slot is recycled.

### render to offscreen image ✓

Two overloads of `Commands::beginRendering()`:
- `beginRendering(depthImageView)` — renders to the swapchain color attachment (requires frame-bound Commands)
- `beginRendering(colorImageView, depthImageView, extent)` — renders to any color target

### indirect dispatch/draw ✓

`cmd.dispatchIndirect(buffer, offset)` and `cmd.drawMeshTasksIndirect(buffer, drawCount, offset, stride)` for GPU-driven rendering workflows.

### dynamic viewport/scissor ✓

Viewport and scissor are dynamic pipeline state. `Frame::beginCommands()` sets them to window dimensions by default. `cmd.setViewport()` and `cmd.setScissor()` change them per-draw.

### pipeline destruction ✓

`destroyPipeline(pipeline)` defers destruction via DestroyGeneration, safe for mid-session pipeline replacement.

## Vulkan requirements

- **Vulkan 1.3** — dynamic rendering, synchronization2
- **Vulkan 1.2 features** — descriptor indexing (all nine feature flags, see doc/bindless.md)
- **VK_EXT_mesh_shader** — optional, enabled via `VulkanContextOptions::meshShaders()`
- **SDL3 3.4.0** — window management and Vulkan surface

## shader introspection

SPIR-V binaries are self-describing. The library should parse SPIR-V at shader load time to extract metadata that enables compile-time correctness checks during pipeline building.

### what SPIR-V exposes

The SPIR-V module contains all of the following, extractable by walking the instruction stream:

- **Entry point** — name and execution model (Fragment, GLCompute, MeshEXT)
- **Push constant block** — total size, member offsets and types
- **Descriptor set/binding usage** — which (set, binding) pairs the shader references
- **Compute local_size** — workgroup dimensions (local_size_x/y/z)
- **Mesh shader output geometry** — max_vertices, max_primitives, output primitive topology (triangles/lines/points)
- **Input/output locations** — location numbers for inter-stage variables (mesh→fragment)

### compile-time correctness checks

The pipeline builder should validate shader metadata at `build()` time. These are programmer errors detectable before any GPU work begins.

**Push constant consistency** — All stages in a pipeline must declare the same push constant block size. A mismatch (e.g., fragment shader declares 76 bytes, mesh shader declares 80 bytes) means one shader is using a stale or wrong struct. The builder should throw with both sizes printed.

**Inter-stage location matching** — The mesh shader's output locations must be a superset of the fragment shader's input locations. A fragment shader reading `location=1` that the mesh shader never writes is a silent black-screen bug. The builder should throw listing the unmatched locations.

**Descriptor set compatibility** — Every shader should only reference set=0 (the bindless set). Any reference to set≥1 is a mistake in a bindless architecture. The builder should throw identifying the unexpected set.

**Binding range check** — Bindings used by the shader must be within the bindless table's declared bindings (0=storage buffers, 1=samplers, 2=storage images). An out-of-range binding is a shader bug. The builder should throw with the invalid binding number.

**Execution model vs stage flag** — The execution model declared in SPIR-V (e.g., MeshEXT) must match the `VkShaderStageFlagBits` the builder is using. Passing a compute SPIR-V as a mesh shader stage should throw immediately rather than producing a Vulkan validation error later.

**Push constant size vs Vulkan limit** — If the push constant block exceeds `maxPushConstantsSize` (128 bytes minimum guaranteed), the builder should throw with the declared size and the device limit.

### queryable metadata

ShaderModule (or ShaderBuilder) should expose introspection results so application code can also use them:

```cpp
ShaderModule shader(ShaderBuilder().mesh().fromFile("quad.mesh.spv"));

shader.reflection.pushConstantSize;    // 76
shader.reflection.localSize;           // {1, 1, 1}
shader.reflection.maxVertices;         // 6     (mesh only)
shader.reflection.maxPrimitives;       // 2     (mesh only)
shader.reflection.outputLocations;     // {1}   (set of location numbers)
shader.reflection.inputLocations;      // {}    (mesh has no inputs from prior stage)
shader.reflection.descriptorBindings;  // {{0,0}, {0,1}}  (set, binding pairs)
```

This lets application code compute dispatch counts from `localSize()`, size buffers from `maxVertices()`, or verify assumptions in tests.

### implementation approach

SPIR-V is a simple binary format: a header followed by a stream of variable-length instructions. Parsing the subset needed for introspection (OpEntryPoint, OpExecutionMode, OpDecorate, OpMemberDecorate, OpVariable, OpTypeStruct, OpTypeFloat, OpTypeInt, OpTypeVector, OpTypeMatrix) requires ~200 lines of code with no external dependencies. No need for a full SPIR-V library.

ShaderModule owns a `ShaderReflection` struct parsed at construction time from the SPIR-V bytes. Multiple pipelines sharing the same ShaderModule read the already-parsed metadata — parse once, validate many. ShaderBuilder stores the source filename (from `fromFile()`) so error messages can name the shader. Pipeline builders store `ShaderModule *` (not just `VkShaderModule`) so they can access reflection data at `build()` time. Validation errors print shader filenames and specific mismatches, then throw.

### debug output

When checks detect a problem, the error message must be actionable:

```
pipeline build error: push constant size mismatch
  mesh shader (quad.mesh.spv): 80 bytes
  fragment shader (tri.frag.spv): 76 bytes
```

```
pipeline build error: unmatched fragment input locations
  fragment shader (tri.frag.spv) reads location 2
  mesh shader (quad.mesh.spv) outputs: {1}
```

In debug builds, the builder should also print a summary of what it validated (stages, push sizes, locations matched) to stderr so the programmer can see the checks ran.

## considerations

### push constant size

128 bytes (Vulkan guaranteed minimum), all stages. Enough for mat4 (64 bytes) + several scalars and RIDs. If a shader needs more data, one RID can point to a storage buffer containing the full dataset.

### VMA

Not included. The current `vkAllocateMemory` + `findMemoryType` approach works for the project scope. VMA is a backlog item if allocation patterns become painful.
