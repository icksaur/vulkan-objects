# backlog

Deferred work, ideas, and low-priority improvements. Do not start without asking.

## Indirect mesh dispatch — MEDIUM

GPU-driven draw calls via `vkCmdDrawMeshTasksIndirectEXT` / `vkCmdDrawMeshTasksIndirectCountEXT`. Currently the CPU hardcodes `drawMeshTasks(cubeCount, 1, 1)` — the GPU already generates the geometry (cubes.comp) but the CPU still decides *how many* workgroups to launch. With indirect dispatch, a compute shader writes draw parameters into a buffer and one indirect call executes them all. The GPU controls what gets drawn.

Why it matters: frustum/occlusion culling on the GPU. A compute culling pass decides which objects are visible and emits only those draw commands. The CPU records a single indirect call regardless of scene complexity. This is the standard pattern for GPU-driven rendering in modern engines.

Requires: `VK_KHR_draw_indirect_count` (core 1.2) for count variant. RTX 3080 supports `VK_EXT_mesh_shader` indirect entry points.

Library additions: `Commands::drawMeshTasksIndirect(Buffer, offset, drawCount, stride)` and the count variant. Possibly a helper for the indirect command struct layout.

## Buffer references (BDA) — LOW

`VK_KHR_buffer_device_address` (core 1.2). Instead of binding storage buffers through the descriptor set and accessing via RID, pass a raw GPU pointer (`uint64_t`) in push constants or another buffer. Eliminates the descriptor set indirection for buffer access entirely. We already use bindless descriptors for images/samplers (which still need descriptors), but buffer access could be pointer-based. Trade-off: slightly harder to debug, enables more flexible data structures (GPU-side linked lists, BVH traversal). Low priority because our current RID approach works well and is more debuggable.

## Timeline semaphores for async compute — LOW

`VK_KHR_timeline_semaphore` (core 1.2). Run compute work on a dedicated async compute queue overlapped with graphics. Example: next frame's cubes.comp runs on the async compute queue while the current frame's rendering is still in flight. Requires careful synchronization via timeline semaphore values instead of pipeline barriers. Meaningful for heavy compute workloads; our current single-queue approach is simpler and sufficient for the demo.

## VMA — MEDIUM

Vulkan Memory Allocator integration. Current approach: one `vkAllocateMemory` per Buffer/Image. Works within the ~4096 allocation limit for the target scope (~100 textures, pooled geometry buffers). Revisit if allocation count becomes measurable pressure or if sub-allocation / memory aliasing is needed.

## unified PipelineBuilder — LOW

Single builder for both graphics and compute pipelines (currently split into `GraphicsPipelineBuilder` + `createComputePipeline` free function). Low value — the two paths share almost no configuration.

## GPU-compressed textures — LOW

BC1–BC7 via KTX2 or DDS loaders. The library API (`ImageBuilder().fromStagingBuffer(...)`) already accepts any VkFormat and pre-uploaded pixel data, so this is an application-level loader addition, not a library architecture change. TGA is sufficient for development.
