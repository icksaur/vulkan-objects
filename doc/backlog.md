# backlog

Deferred work, ideas, and low-priority improvements. Do not start without asking.

Items that require no API changes are documented as recipes in [doc/cookbook.md](cookbook.md) instead of here.

## Buffer references (BDA) — LOW

`VK_KHR_buffer_device_address` (core 1.2). Pass raw GPU pointers (`uint64_t`) in push constants instead of RID-based descriptor indexing for buffer access. Enables flexible GPU-side data structures (linked lists, BVH traversal). Low priority: the current RID approach is easier to debug, well understood, and sufficient. BDA doesn't make the API harder to misuse — it's an alternative access pattern, not a safety improvement.

## Timeline semaphores for async compute — LOW

`VK_KHR_timeline_semaphore` (core 1.2). Overlap compute and graphics work on separate queues. Meaningful only for heavy compute workloads. The current single-queue model is simpler, correctly synchronized by barriers, and sufficient for the demo scope. Adding async compute would introduce a new synchronization model that's easy to get wrong.

## Unified PipelineBuilder — LOW

Single builder for both graphics and compute pipelines. The two paths share almost no configuration (`GraphicsPipelineBuilder` has mesh/fragment stages, depth-only, color formats, sample count; `createComputePipeline` takes a single shader). Merging them gains little ergonomics and risks a confusing builder where most methods are invalid depending on the pipeline type.
