# Cookbook

Recipes for modern Vulkan techniques using the `vkobjects.h` API.
Each recipe shows the minimal code to get a technique working correctly.

---

## Compute shader writing geometry

A compute shader generates vertex data into a storage buffer.
Mesh shaders read it back via RID.

**Setup:**

```cpp
const uint32_t cubeCount = 12;
const size_t vertexBufferSize = sizeof(float) * 8 * 36 * cubeCount;
Buffer vertexBuffer(BufferBuilder(vertexBufferSize).storage());

ShaderModule cubeCompModule(ShaderBuilder().compute().fromFile("src/shaders/cubes.comp.spv"));
Pipeline computePipeline = createComputePipeline(cubeCompModule);
```

**Per frame:**

```cpp
cmd.bindCompute(computePipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.dispatch(cubeCount, 1, 1);

// Barrier: compute writes → mesh shader reads
cmd.bufferBarrier(vertexBuffer, Stage::Compute, Stage::MeshShader);
```

The shader accesses the buffer via its RID in push constants:

```glsl
storageBuffers[nonuniformEXT(vertexBufferRID)].data[i] = pos.x;
```

---

## Indirect mesh dispatch (GPU-driven draw calls)

Let the GPU decide how many mesh workgroups to launch.  The CPU records a
single indirect call; a compute shader writes the draw parameters.

**No library changes required** — `drawMeshTasksIndirect`, `BufferBuilder::indirect()`,
`Stage::DrawIndirect`, and `Access::IndirectCommandRead` all exist.

**The indirect command struct** (12 bytes, matches the default stride):

```c
struct DrawIndirectCmd {
    uint32_t groupCountX;   // number of mesh workgroups
    uint32_t groupCountY;   // 1
    uint32_t groupCountZ;   // 1
};
```

**Setup:**

```cpp
// .storage() for compute write via RID, .indirect() for indirect consumption
Buffer indirectBuffer(BufferBuilder(sizeof(uint32_t) * 3).storage().indirect());
```

**Cull shader** (`cull.comp` — writes the indirect struct):

```glsl
layout(push_constant) uniform PushConstants {
    // ... existing fields ...
    uint indirectBufferRID;
};

void main() {
    uint visibleCount = 0;

    // TODO: per-cube frustum test against viewProjection
    visibleCount = 12;   // passthrough — identical to current behavior

    storageBuffers[indirectBufferRID].data[0] = visibleCount;  // groupCountX
    storageBuffers[indirectBufferRID].data[1] = 1;             // groupCountY
    storageBuffers[indirectBufferRID].data[2] = 1;             // groupCountZ
}
```

**Per frame:**

```cpp
// 1. Generate geometry (unchanged)
cmd.bindCompute(computePipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.dispatch(cubeCount, 1, 1);
cmd.bufferBarrier(vertexBuffer, Stage::Compute, Stage::MeshShader);

// 2. Cull pass — write indirect buffer
cmd.bindCompute(cullPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.dispatch(1, 1, 1);

// 3. Barrier: compute writes → indirect read
cmd.bufferBarrier(indirectBuffer, Stage::Compute, Stage::DrawIndirect);

// 4. Draw — GPU controls workgroup count
cmd.beginRendering(shadowMaps[idx].imageView, {shadowMapRes, shadowMapRes});
cmd.bindGraphics(shadowPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.drawMeshTasksIndirect(indirectBuffer, 1);   // 1 draw command
cmd.endRendering();
```

**Incremental approach:**

1. Start with `visibleCount = cubeCount` (passthrough). Confirms wiring — rendering is identical.
2. Add frustum culling in `cull.comp`.
3. Later, `drawMeshTasksIndirectCount` for multi-draw when the scene grows.

**Hard to mess up:** validation layer catches missing `.indirect()`, default stride matches the spec struct, and passthrough cull shader is visually identical to the direct path.

---

## Shadow mapping

Render depth from the light's perspective into a sampled depth image, then
read it in the main pass's fragment shader via RID.

**Setup:**

```cpp
// Depth image that can also be sampled
Image shadowMap(ImageBuilder().depthSampled(2048, 2048), setupCmd);

// Shadow pipeline — depth-only, no color attachment
Pipeline shadowPipeline = GraphicsPipelineBuilder()
    .meshShader(shadowMeshModule)
    .depthOnly()
    .build();
```

**Per frame:**

```cpp
// Render shadow depth
cmd.beginRendering(shadowMaps[idx].imageView, {shadowMapRes, shadowMapRes});
cmd.bindGraphics(shadowPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.drawMeshTasks(cubeCount, 1, 1);
cmd.endRendering();

// Barrier: depth write → fragment shader read
Barrier(cmd).image(shadowMaps[idx], 1)
    .from(Stage::LateFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
    .to(Stage::Fragment, Access::ShaderRead, Layout::DepthReadOnly)
    .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
    .record();

// Main pass reads shadow map via its RID in push constants
push.shadowMapRID = shadowMaps[idx].rid();
```

After the main pass, transition the shadow map back for the next frame:

```cpp
Barrier(cmd).image(shadowMaps[idx], 1)
    .from(Stage::Fragment, Access::ShaderRead, Layout::DepthReadOnly)
    .to(Stage::EarlyFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
    .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
    .record();
```

---

## Offscreen render-to-texture with blit

Render the scene to an offscreen color target, then sample it in a
fullscreen pass that draws to the swapchain.  Useful for post-processing.

**Setup:**

```cpp
Image offscreenColor(ImageBuilder().colorTarget(windowWidth, windowHeight), setupCmd);

// Fullscreen blit pipeline
Pipeline blitPipeline = GraphicsPipelineBuilder()
    .meshShader(fullscreenMeshModule)
    .fragmentShader(blitFragModule)
    .build();
```

**Per frame:**

```cpp
// Render scene to offscreen target
cmd.beginRendering(offscreenColors[idx].imageView, depthImages[idx].imageView, offscreenExtent);
cmd.bindGraphics(graphicsPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.drawMeshTasks(cubeCount, 1, 1);
cmd.endRendering();

// Barrier: color attachment → shader readable
Barrier(cmd).image(offscreenColors[idx], 1)
    .from(Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment)
    .to(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
    .record();

// Blit to swapchain
cmd.beginRendering();   // default: swapchain color, no depth
cmd.bindGraphics(blitPipeline);
uint32_t blitRID = offscreenColors[idx].rid();
cmd.pushConstants(&blitRID, sizeof(blitRID));
cmd.drawMeshTasks(1, 1, 1);
cmd.endRendering();

// Barrier: back to writable for next frame
Barrier(cmd).image(offscreenColors[idx], 1)
    .from(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
    .to(Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment)
    .record();
```

---

## Staging buffer upload (textures)

Upload CPU-side pixel data into a GPU image via a staging buffer.
The `ImageBuilder` handles layout transitions internally.

```cpp
// Create staging buffer (host-visible, transfer source)
Buffer staging(BufferBuilder(byteCount).transferSource().hostVisible());
staging.upload(pixels, byteCount);

// Create image — builder copies from staging buffer via the command buffer
Image img(ImageBuilder().fromStagingBuffer(staging, width, height, VK_FORMAT_B8G8R8A8_SRGB), cmd);
```

This works for any format — SRGB, UNORM, or GPU-compressed (BC1–BC7).
For compressed formats, just change the `VkFormat` and supply pre-compressed data:

```cpp
// BC7 compressed texture — same API, different format and data
Buffer staging(BufferBuilder(compressedByteCount).transferSource().hostVisible());
staging.upload(bc7Blocks, compressedByteCount);

Image img(ImageBuilder().fromStagingBuffer(staging, width, height, VK_FORMAT_BC7_SRGB_BLOCK), cmd);
```

No library changes needed — this is a loader concern in application code.

---

## Host-visible storage buffer (CPU → GPU per frame)

For small data updated every frame (e.g. light matrices), use a host-visible
storage buffer.  No staging buffer or transfer barrier needed.

```cpp
Buffer lightBuffer(BufferBuilder(sizeof(LightData)).storage().hostVisible());

// Upload directly — buffer is host-coherent
lightBuffer.upload(&lightData, sizeof(LightData));

// Access via RID in shaders
push.lightBufferRID = lightBuffer.rid();
```

---

## Swapchain resize handling

Register a callback that recreates resolution-dependent images.
Fixed-resolution resources (e.g. shadow maps) can stay as-is.

```cpp
context.onSwapchainResize([&](Commands & cmd, VkExtent2D extent) {
    depthImages.clear();
    offscreenColors.clear();
    for (size_t i = 0; i < context.swapchainImageCount; ++i) {
        depthImages.emplace_back(ImageBuilder().depth(), cmd);
        offscreenColors.emplace_back(ImageBuilder().colorTarget(extent.width, extent.height), cmd);
    }
});
```

The callback receives a `Commands` reference for any transitions the new
images need.  RAII cleanup of the old images happens via `clear()`.

---

## One-shot commands

Run GPU work outside the render loop (e.g. texture uploads at startup).

```cpp
auto cmd = Commands::oneShot();

Image tex1 = createImageFromTGAFile(cmd, "albedo.tga");
Image tex2 = createImageFromTGAFile(cmd, "normal.tga");

cmd.submitAndWait();   // blocks until all work completes
```

After `submitAndWait()`, the command buffer is freed automatically.
