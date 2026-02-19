# Vulkan Objects

Vulkan wrapper library for C++. Automatic lifetime management, bindless descriptors, and a minimal API surface that makes common rendering patterns easy and hard to get wrong.

## Features

- Bindless descriptors — single global descriptor set, resources get an ID on construction
- RAII for all GPU resources — buffers, images, pipelines, command buffers
- Deferred destruction — resources are freed only after the GPU is done with them
- Mesh shader support (VK_EXT_mesh_shader)
- Dynamic rendering (no render passes)
- Synchronization2 barriers with typed `Stage`, `Access`, `Layout` enums
- SPIR-V introspection — shaders are validated at pipeline build time (push constant consistency, inter-stage location matching, descriptor set/binding checks)
- Push constants (128 bytes, all stages)

## Requirements

- Vulkan 1.3 SDK
- SDL3 (3.4.0+)
- glslc (shader compiler)
- CMake 3.14+

## Building

```bash
make            # debug build
make release    # optimized build
make run        # build and run
```

## Usage

From [demo/main.cpp](demo/main.cpp):

```cpp
#include "vkobjects.h"
SDLWindow window("App", 1280, 720);
VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

ShaderModule fragShader(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
ShaderModule compShader(ShaderBuilder().compute().fromFile("vertices.comp.spv"));
ShaderModule meshShader(ShaderBuilder().mesh().fromFile("quad.mesh.spv"));

// one-shot setup
auto setupCmd = Commands::oneShot();
Image texture = createImageFromTGAFile(setupCmd, "texture.tga");
setupCmd.submitAndWait();

Buffer storageBuffer(BufferBuilder(dataSize).storage());

VkPipeline graphicsPipeline = GraphicsPipelineBuilder()
    .meshShader(meshShader)
    .fragmentShader(fragShader)
    .build();

VkPipeline computePipeline = createComputePipeline(compShader);

while (!done) {
    Frame frame;
    auto cmd = frame.beginCommands();

    cmd.bindCompute(computePipeline);
    cmd.pushConstants(&push, sizeof(push));
    cmd.dispatch(100, 1, 1);

    cmd.bufferBarrier(storageBuffer, Stage::Compute, Stage::MeshShader);

    cmd.beginRendering(depthImageView);
    cmd.bindGraphics(graphicsPipeline);
    cmd.pushConstants(&push, sizeof(push));
    cmd.drawMeshTasks(100, 1, 1);
    cmd.endRendering();

    frame.submit(cmd);
}
```

Shaders reference resources by ID via push constants — no per-material descriptor sets:

```glsl
layout(set=0, binding=0) buffer StorageBuffers { float data[]; } storageBuffers[];
layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    float zScale;
    uint vertexBufferRID;
    uint textureRID;
};
```

## API Reference

| Type | Purpose |
|------|---------|
| `VulkanContext` | Singleton owning instance, device, swapchain, bindless table |
| `Frame` | Scoped frame guard — fence wait, image acquire, submit, present |
| `Commands` | Scoped command buffer — compute, rendering, barriers, push constants |
| `Buffer` | GPU buffer with automatic bindless RID |
| `Image` | GPU image with view, sampler, and bindless RID |
| `Barrier` | Synchronization2 barrier builder (`.from()`, `.to()`, `.record()`) |
| `ShaderModule` | SPIR-V shader with reflection data (push constant size, locations, bindings) |
| `GraphicsPipelineBuilder` | Builds a graphics pipeline with shader validation |

## Project Structure

```
include/        # public header (vkobjects.h)
src/            # library implementation (static library)
demo/           # demo application
demo/shaders/   # GLSL shaders (compiled to .spv by CMake)
doc/            # spec.md, bindless.md, backlog.md
plan.md         # active implementation plan
code-quality.md # code review standards
```

## Using in Your Project

Add this repository as a subdirectory (git submodule, FetchContent, or copy) and link against the `vkobjects` target:

```cmake
add_subdirectory(vulkan-objects)
target_link_libraries(my_app PRIVATE vkobjects)
```

This gives your project:
- `#include "vkobjects.h"` — the public API header
- Vulkan and SDL3 include paths and link libraries (propagated automatically)

The library is a static archive (`libvkobjects.a`). No install step needed.

### Minimal example

```cpp
#include "vkobjects.h"

int main() {
    SDLWindow window("App", 1280, 720);
    VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

    Buffer vertexBuffer(BufferBuilder(dataSize).storage());

    while (!done) {
        Frame frame;
        auto cmd = frame.beginCommands();
        // ...
        frame.submit(cmd);
    }
}
```

## Agent Guide

- Read `plan.md` for current work
- Read `doc/spec.md` for project architecture
- Read `doc/bindless.md` for the bindless descriptor strategy
- Read `code-quality.md` before making changes
- Deferred work and bugs go in `doc/backlog.md`
- Do not start backlog items without asking
