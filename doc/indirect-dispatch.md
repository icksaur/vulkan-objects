# Indirect Mesh Dispatch

## Summary

Replace the hardcoded `cmd.drawMeshTasks(cubeCount, 1, 1)` calls in main.cpp
with `cmd.drawMeshTasksIndirect(...)` so the GPU decides how many workgroups
to launch.  The library already has every piece needed — this is purely a
main.cpp usage change plus one small compute shader addition.

## What already exists in the library

| API | Status |
|---|---|
| `BufferBuilder::indirect()` | ready |
| `Commands::drawMeshTasksIndirect(buffer, drawCount, offset, stride)` | ready (stride defaults to 12) |
| `Commands::dispatchIndirect(buffer, offset)` | ready |
| `Stage::DrawIndirect`, `Access::IndirectCommandRead` | ready |

No new library code is required.

## The indirect command struct

`VkDrawMeshTasksIndirectCommandEXT` is three `uint32_t`s:

```c
struct DrawIndirectCmd {   // 12 bytes, matches default stride
    uint32_t groupCountX;  // number of mesh workgroups
    uint32_t groupCountY;  // 1
    uint32_t groupCountZ;  // 1
};
```

## New buffer required

One small device-local buffer that holds a single `DrawIndirectCmd`:

```cpp
Buffer indirectBuffer(BufferBuilder(sizeof(uint32_t) * 3).storage().indirect());
```

`.storage()` lets a compute shader write to it via RID.
`.indirect()` adds `VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` so it can be
consumed by `drawMeshTasksIndirect`.

## New shader required?

**Yes — but it's tiny.**  The existing `cubes.comp` generates geometry; it
should not also decide the draw count (single responsibility, and the two
dispatches have different workgroup counts).  A separate shader like
`cull.comp` writes the indirect struct:

```glsl
#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(local_size_x = 1) in;

layout(set=0, binding=0) buffer StorageBuffers {
    uint data[];      // uint view for indirect commands
} storageBuffers[];

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    uint vertexBufferRID;
    uint textureRID;
    uint shadowMapRID;
    uint lightBufferRID;
    float rotationAngle;
    uint indirectBufferRID;   // ← new field
};

void main() {
    uint rid = indirectBufferRID;
    uint visibleCount = 0;

    // TODO: per-cube frustum test goes here.
    // For now, pass everything through.
    visibleCount = 12;   // cubeCount

    storageBuffers[rid].data[0] = visibleCount;  // groupCountX
    storageBuffers[rid].data[1] = 1;             // groupCountY
    storageBuffers[rid].data[2] = 1;             // groupCountZ
}
```

This is dispatched with `cmd.dispatch(1, 1, 1)` — a single invocation.
Later, real frustum culling replaces the hardcoded `12` with a loop over
cubes that tests each against the view-projection frustum.

## main.cpp changes (sketch)

```cpp
// --- setup ---
Buffer indirectBuffer(BufferBuilder(sizeof(uint32_t) * 3).storage().indirect());

// Add indirectBufferRID to PushConstants (after rotationAngle)
push.indirectBufferRID = indirectBuffer.rid();

// --- per frame ---

// 1. Compute geometry (unchanged)
cmd.bindCompute(computePipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.dispatch(cubeCount, 1, 1);

cmd.bufferBarrier(vertexBuffer, Stage::Compute, Stage::MeshShader);

// 2. Cull pass — writes indirect buffer
cmd.bindCompute(cullPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.dispatch(1, 1, 1);

// Barrier: cull compute writes → indirect read
cmd.bufferBarrier(indirectBuffer, Stage::Compute, Stage::DrawIndirect);

// 3. Shadow pass — indirect
cmd.beginRendering(shadowMaps[idx].imageView, {shadowMapRes, shadowMapRes});
cmd.bindGraphics(shadowPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.drawMeshTasksIndirect(indirectBuffer, 1);    // 1 draw, reading count from buffer
cmd.endRendering();

// 4. Main pass — indirect
cmd.beginRendering(offscreenColors[idx].imageView, depthImages[idx].imageView, offscreenExtent);
cmd.bindGraphics(graphicsPipeline);
cmd.pushConstants(&push, sizeof(push));
cmd.drawMeshTasksIndirect(indirectBuffer, 1);
cmd.endRendering();
```

## Barrier requirement

The key new barrier is:

```
Stage::Compute  →  Stage::DrawIndirect
```

with implicit `Access::ShaderWrite → Access::IndirectCommandRead`.
`bufferBarrier` already handles this — the only caller-visible change is
picking the right stage enum.

## What this is NOT

- **Not a library change.** `drawMeshTasksIndirect` and `BufferBuilder::indirect()`
  already exist.  No new `Commands` methods, no new builder options.
- **Not `drawMeshTasksIndirectCount`.**  The count variant (GPU also decides
  *how many draws*) is useful for multi-draw batching.  Start with the
  simpler single-draw path first — it covers frustum culling without the
  extra complexity.

## Hard to mess up

| Concern | Mitigation |
|---|---|
| Forgetting `.indirect()` on the buffer | Vulkan validation layer catches it immediately |
| Wrong barrier stage | Use `Stage::DrawIndirect` — the enum is already defined |
| Struct layout mismatch | Default stride (12) matches the spec struct exactly |
| Cull shader writes wrong values | Start with `visibleCount = cubeCount` (passthrough) — visually identical to current behavior, confirms wiring before adding real culling |

## Incremental plan

1. Add `indirectBufferRID` to `PushConstants`.  Create the buffer.  Write
   a passthrough `cull.comp` that sets `groupCountX = cubeCount`.  Replace
   `drawMeshTasks` → `drawMeshTasksIndirect`.  Result: identical rendering,
   proven wiring.
2. Add frustum extraction to `cull.comp` — test each cube center against
   the six frustum planes.  Count only visible cubes.
3. (Optional) Switch to `drawMeshTasksIndirectCount` for multi-draw when
   the scene grows beyond a single draw call.
