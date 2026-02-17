# vulkan-objects specification

A personal Vulkan wrapper library for C++ projects. Automatic lifetime management, bindless descriptors, and a minimal API surface that makes common rendering patterns easy and hard to get wrong.

Target: a 3D game with TF2-level geometry complexity, ~500 entities, directional shadow maps, and multi-pass rendering. Not a AAA engine — no render graph, no scene graph, no ECS. Just correct Vulkan with good defaults.

Always follow and refer to code-quality.md.

## concepts

- **VulkanContext** — Singleton owning the Vulkan instance, device, swapchain, bindless descriptor table, and all shared state. RAII. Construction initializes Vulkan; destruction tears everything down.
- **Frame** — Scoped guard representing one frame of GPU work. Constructor waits for the oldest in-flight frame's fence, cleans deferred resources, acquires the next swapchain image. Destructor advances the frame index. Only one can exist at a time (runtime enforced).
- **Commands** — Scoped command buffer recording. Constructor begins recording and binds the global bindless descriptor set. Provides typed methods for compute, rendering, barriers, and push constants. Move-only. Also used for one-shot setup work via `Commands::oneShot()`.
- **Buffer** — GPU buffer with automatic bindless registration. Every buffer gets a resource ID (RID) on construction. Deferred destruction via DestroyGeneration.
- **Image** — GPU image with view and optional sampler. Automatic bindless registration (sampled images, storage images, or depth-sampled images). Deferred destruction via DestroyGeneration.
- **Pipeline** — RAII wrapper over VkPipeline. Move-only. Destructor defers pipeline destruction via DestroyGeneration. Implicitly converts to VkPipeline for bind calls.
- **Barrier** — Synchronization2-based barrier builder for buffer and image memory barriers.
- **DestroyGeneration** — Per-frame-slot collection of Vulkan handles awaiting deferred destruction. Cleaned when the fence proves the GPU is done with that frame slot.
- **BindlessTable** — Single global descriptor set with three bindings (storage buffers, combined image samplers, storage images). Resources register/unregister automatically. See doc/bindless.md for details.

## target API usage

From main.cpp — this is a working example with shadow mapping:

```cpp
int main() {
    SDLWindow window("App", 1280, 720);
    VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

    ShaderModule fragShader(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
    ShaderModule compShader(ShaderBuilder().compute().fromFile("vertices.comp.spv"));
    ShaderModule meshShader(ShaderBuilder().mesh().fromFile("cube.mesh.spv"));
    ShaderModule shadowMeshShader(ShaderBuilder().mesh().fromFile("shadow.mesh.spv"));

    auto setupCmd = Commands::oneShot();
    Image texture = createImageFromTGAFile(setupCmd, "texture.tga");

    // Per-swapchain-image render targets (one per frame in flight)
    std::vector<Image> depthImages, shadowMaps;
    for (size_t i = 0; i < context.swapchainImageCount; ++i) {
        depthImages.emplace_back(ImageBuilder().depth(), setupCmd);
        shadowMaps.emplace_back(ImageBuilder().depthSampled(1024, 1024), setupCmd);
    }
    setupCmd.submitAndWait();

    context.onSwapchainResize([&](Commands & cmd, VkExtent2D extent) {
        (void)extent;
        depthImages.clear();
        for (size_t i = 0; i < context.swapchainImageCount; ++i)
            depthImages.emplace_back(ImageBuilder().depth(), cmd);
        // shadowMaps are fixed resolution — no rebuild needed
    });

    Buffer storageBuffer(BufferBuilder(vertexDataSize).storage());

    Pipeline graphicsPipeline = GraphicsPipelineBuilder()
        .meshShader(meshShader)
        .fragmentShader(fragShader)
        .build();

    Pipeline shadowPipeline = GraphicsPipelineBuilder()
        .meshShader(shadowMeshShader)
        .depthOnly()
        .build();

    Pipeline computePipeline = createComputePipeline(compShader);

    while (!done) {
        Frame frame;
        uint32_t idx = frame.swapchainImageIndex();

        auto cmd = frame.beginCommands();

        // Compute pass
        cmd.bindCompute(computePipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.dispatch(1, 1, 1);
        cmd.bufferBarrier(storageBuffer, Stage::Compute, Stage::MeshShader);

        // Shadow pass — depth-only, renders scene from light's POV
        cmd.beginRendering(shadowMaps[idx].imageView, {1024, 1024});
        cmd.bindGraphics(shadowPipeline);
        cmd.pushConstants(&lightPush, sizeof(lightPush));
        cmd.drawMeshTasks(cubeCount, 1, 1);
        cmd.endRendering();

        // Barrier: shadow map depth write → fragment shader read
        cmd.imageBarrier(shadowMaps[idx],
            Stage::LateFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment,
            Stage::Fragment, Access::ShaderRead, Layout::DepthReadOnly);

        // Main pass — samples shadow map via RID
        push.shadowMapRID = shadowMaps[idx].rid();
        cmd.beginRendering(depthImages[idx].imageView);
        cmd.bindGraphics(graphicsPipeline);
        cmd.pushConstants(&push, sizeof(push));
        cmd.drawMeshTasks(cubeCount, 1, 1);
        cmd.endRendering();

        frame.submit(cmd);
    }
}
```

## render target model

### per-frame vs static render targets

With N frames in flight (equals `swapchainImageCount`, typically 3), frames N and N-1 can execute on the GPU simultaneously. Any image written during the render loop needs **one copy per swapchain image count** to avoid cross-frame hazards.

**Per-frame targets** (one per `swapchainImageCount`):
- Main-pass depth buffers — different frames use different swapchain images
- Shadow maps re-rendered each frame — frame N's write would stomp frame N-1's read
- G-buffer targets (future) — written and consumed within each frame

**Single-copy targets** (rendered once at setup or infrequently):
- Baked environment maps
- Static shadow maps (if the light never moves)
- Lookup textures (BRDF LUT, noise, etc.)

The existing `depthImages` vector establishes the per-frame pattern. Shadow maps and any future per-frame render targets follow the same pattern.

### rendering overloads

`Commands::beginRendering` has three overloads covering the common cases:

| Overload | Color target | Depth target | Use case |
|----------|-------------|-------------|----------|
| `(VkImageView depth)` | Swapchain (from frame) | Provided depth view | Main scene pass |
| `(VkImageView depth, VkExtent2D extent)` | None | Provided depth view | Shadow map pass |
| `(VkImageView color, VkImageView depth, VkExtent2D extent)` | Provided color view | Provided depth view | Offscreen color+depth |

All use `VK_ATTACHMENT_LOAD_OP_CLEAR` and `VK_ATTACHMENT_STORE_OP_STORE`. The depth-only overload (no color attachment) is essential for shadow map passes where no fragment shader runs.

### depth-sampled images

Regular depth images (`ImageBuilder().depth()`) are render-only — they serve as depth attachments but cannot be sampled by shaders. They have no sampler and no RID.

Depth-sampled images (`ImageBuilder().depthSampled(w, h)`) serve double duty: depth attachment during the shadow pass, then sampled texture during the lighting pass. They:

- Use `VK_FORMAT_D32_SFLOAT` (no stencil — more efficient for shadow maps)
- Have usage flags `DEPTH_STENCIL_ATTACHMENT | SAMPLED`
- Create a **comparison sampler** (`compareEnable = VK_TRUE`, `compareOp = VK_COMPARE_OP_LESS`)
- Register in the bindless table as a combined image sampler and return an RID
- Use `VK_IMAGE_ASPECT_DEPTH_BIT` only (no stencil aspect)
- Initial layout: `DepthStencilAttachment` (ready for shadow pass)

The shadow pass renders to the depth-sampled image, a barrier transitions it to `DepthReadOnly`, and the main pass's fragment shader samples it via RID using `texture(sampler2DShadow(...), ...)` in GLSL.

### depth-only pipelines

`GraphicsPipelineBuilder::depthOnly()` configures the pipeline for depth-only rendering:

- `colorAttachmentCount = 0` in `VkPipelineRenderingCreateInfo`
- Uses the shadow map's depth format (`VK_FORMAT_D32_SFLOAT`)
- No fragment shader required (mesh shader outputs clip-space positions, rasterizer writes depth)
- Color blend state still provided but with 0 attachments
- Depth bias can optionally be enabled to reduce shadow acne

```cpp
Pipeline shadowPipeline = GraphicsPipelineBuilder()
    .meshShader(shadowMeshShader)
    .depthOnly()   // 0 color attachments, D32_SFLOAT depth
    .build();
```

### future: multi-color-attachment rendering

G-buffer / deferred rendering requires multiple color attachments (position, normal, albedo). This needs:
- A `beginRendering` overload accepting a span of color image views
- Pipeline builder support for multiple color attachment formats

This is not needed for shadow maps and is deferred to the backlog.

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

Buffer, Image, and Pipeline destructors don't destroy Vulkan handles immediately. Instead, handles are pushed into the current frame slot's `DestroyGeneration`. When the fence for that slot is signaled (step 1 above), the handles are destroyed. This ensures the GPU is finished with resources before they are freed.

DestroyGeneration holds: VkBuffer, VkDeviceMemory, VkCommandBuffer, VkImage, VkImageView, VkSampler, VkPipeline.

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

`cmd.bindCompute(pipeline)` and `cmd.bindGraphics(pipeline)` accept any `VkPipeline` (Pipeline converts implicitly). Multiple bind calls per frame work. Pipelines are immutable once created — no sync concern. Pipeline is RAII — destruction is deferred automatically.

### per-frame buffer writes ✓

CPU: `buffer.upload(data, size)` maps, copies, and unmaps. Safe because `Frame()` waits on the fence for this frame slot.

GPU: Compute shaders write to storage buffers, then `cmd.bufferBarrier()` makes the writes visible to subsequent stages.

### adding and removing resources at runtime ✓

Buffer and Image constructors register with the bindless table and assign an RID. Destructors defer both Vulkan handle destruction and RID release into the current frame's `DestroyGeneration`, ensuring the GPU is finished before the descriptor slot is recycled.

### multi-pass rendering ✓

Shadow map pass → barrier → main pass, all in a single command buffer per frame. Each pass uses `beginRendering` / `endRendering` with the appropriate overload:

1. `beginRendering(shadowMap.imageView, {w, h})` — depth-only shadow pass
2. `imageBarrier(...)` — transition shadow map to shader-readable
3. `beginRendering(depthImage.imageView)` — main pass, fragment shader samples shadow map via RID

Render targets that are written each frame need one per `swapchainImageCount` (see render target model above).

### render to offscreen image ✓

Two offscreen overloads of `Commands::beginRendering()`:
- `beginRendering(depthView, extent)` — depth-only offscreen (shadow maps)
- `beginRendering(colorView, depthView, extent)` — color + depth offscreen

### indirect dispatch/draw ✓

`cmd.dispatchIndirect(buffer, offset)` and `cmd.drawMeshTasksIndirect(buffer, drawCount, offset, stride)` for GPU-driven rendering workflows.

### dynamic viewport/scissor ✓

Viewport and scissor are dynamic pipeline state. `Frame::beginCommands()` sets them to window dimensions by default. `cmd.setViewport()` and `cmd.setScissor()` change them per-draw.

## Vulkan requirements

- **Vulkan 1.3** — dynamic rendering, synchronization2
- **Vulkan 1.2 features** — descriptor indexing (all nine feature flags, see doc/bindless.md)
- **VK_EXT_mesh_shader** — optional, enabled via `VulkanContextOptions::meshShaders()`
- **SDL3 3.4.0** — window management and Vulkan surface

## shader introspection

SPIR-V binaries are self-describing. The library parses SPIR-V at shader load time to extract metadata that enables compile-time correctness checks during pipeline building.

### what SPIR-V exposes

The SPIR-V module contains all of the following, extractable by walking the instruction stream:

- **Entry point** — name and execution model (Fragment, GLCompute, MeshEXT)
- **Push constant block** — total size, member offsets and types
- **Descriptor set/binding usage** — which (set, binding) pairs the shader references
- **Compute local_size** — workgroup dimensions (local_size_x/y/z)
- **Mesh shader output geometry** — max_vertices, max_primitives, output primitive topology (triangles/lines/points)
- **Input/output locations** — location numbers for inter-stage variables (mesh→fragment)

### compile-time correctness checks

The pipeline builder validates shader metadata at `build()` time. These are programmer errors detectable before any GPU work begins.

**Push constant consistency** — All stages in a pipeline must declare the same push constant block size. A mismatch (e.g., fragment shader declares 76 bytes, mesh shader declares 80 bytes) means one shader is using a stale or wrong struct. The builder throws with both sizes printed.

**Inter-stage location matching** — The mesh shader's output locations must be a superset of the fragment shader's input locations. A fragment shader reading `location=1` that the mesh shader never writes is a silent black-screen bug. The builder throws listing the unmatched locations.

**Descriptor set compatibility** — Every shader should only reference set=0 (the bindless set). Any reference to set≥1 is a mistake in a bindless architecture. The builder throws identifying the unexpected set.

**Binding range check** — Bindings used by the shader must be within the bindless table's declared bindings (0=storage buffers, 1=samplers, 2=storage images). An out-of-range binding is a shader bug. The builder throws with the invalid binding number.

**Execution model vs stage flag** — The execution model declared in SPIR-V (e.g., MeshEXT) must match the `VkShaderStageFlagBits` the builder is using. Passing a compute SPIR-V as a mesh shader stage throws immediately rather than producing a Vulkan validation error later.

**Push constant size vs Vulkan limit** — If the push constant block exceeds `maxPushConstantsSize` (128 bytes minimum guaranteed), the builder throws with the declared size and the device limit.

### queryable metadata

ShaderModule exposes introspection results so application code can use them:

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

### implementation approach

SPIR-V is a simple binary format: a header followed by a stream of variable-length instructions. Parsing the subset needed for introspection (OpEntryPoint, OpExecutionMode, OpDecorate, OpMemberDecorate, OpVariable, OpTypeStruct, OpTypeFloat, OpTypeInt, OpTypeVector, OpTypeMatrix) requires ~200 lines of code with no external dependencies. No need for a full SPIR-V library.

ShaderModule owns a `ShaderReflection` struct parsed at construction time from the SPIR-V bytes. Multiple pipelines sharing the same ShaderModule read the already-parsed metadata — parse once, validate many. ShaderBuilder stores the source filename (from `fromFile()`) so error messages can name the shader. Pipeline builders store `ShaderModule *` (not just `VkShaderModule`) so they can access reflection data at `build()` time. Validation errors print shader filenames and specific mismatches, then throw.

### debug output

When checks detect a problem, the error message is actionable:

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

In debug builds, the builder prints a summary of what it validated (stages, push sizes, locations matched) to stderr so the programmer can see the checks ran.

## considerations

### push constant size

128 bytes (Vulkan guaranteed minimum), all stages. Enough for mat4 (64 bytes) + several scalars and RIDs. If a shader needs more data, one RID can point to a storage buffer containing the full dataset. 500 entities × 128 bytes = 64 KB in a single storage buffer, trivial.

### VMA

Not included. The current `vkAllocateMemory` + `findMemoryType` approach works for the current project scope. Each Buffer and Image is a separate allocation. Vulkan implementations typically allow ~4096 allocations. A game with pooled geometry buffers and ~100 unique textures stays well within this limit. VMA is a backlog item if allocation counts become a problem.

### texture formats

TGA-only loading is sufficient for development. GPU-compressed formats (BC1–BC7 via KTX2 or DDS) are application-level loader additions — the library API (`ImageBuilder().fromStagingBuffer(...)`) already accepts any VkFormat and pre-uploaded data. Not a library architecture concern.
