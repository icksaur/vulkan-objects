# backlog

Deferred work, ideas, and low-priority improvements. Do not start without asking.

## VMA — MEDIUM

Vulkan Memory Allocator integration. Current approach: one `vkAllocateMemory` per Buffer/Image. Works within the ~4096 allocation limit for the target scope (~100 textures, pooled geometry buffers). Revisit if allocation count becomes measurable pressure or if sub-allocation / memory aliasing is needed.

## unified PipelineBuilder — LOW

Single builder for both graphics and compute pipelines (currently split into `GraphicsPipelineBuilder` + `createComputePipeline` free function). Low value — the two paths share almost no configuration.

## GPU-compressed textures — LOW

BC1–BC7 via KTX2 or DDS loaders. The library API (`ImageBuilder().fromStagingBuffer(...)`) already accepts any VkFormat and pre-uploaded pixel data, so this is an application-level loader addition, not a library architecture change. TGA is sufficient for development.
