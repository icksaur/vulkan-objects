// Vertex-shader pipeline path.
//
// WHY THIS TEST EXISTS. Before this path existed, GraphicsPipelineBuilder had exactly one
// pre-rasterization stage -- mesh -- so nothing could be drawn without VK_EXT_mesh_shader.
// The property under test is therefore not "a triangle appears". It is that a full draw
// succeeds on a context built WITHOUT meshShaders(), which is the portability claim.
//
// It also pins the two failure modes the compiler cannot catch:
//   - mesh + vertex in one builder    -> a pipeline with two pre-rasterization stages
//   - a vertex shader with attribute inputs -> reads undefined data, since this library
//     supplies no vertex input bindings by design

#include "vkobjects.h"
#include "vkinternal.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestContext {
    SDL_Window* window = nullptr;
    std::unique_ptr<VulkanContext> context;

    TestContext() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error("SDL_Init failed");
        window = SDL_CreateWindow("vkobjects-vertex-pipeline-tests", 64, 64,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error("SDL_CreateWindow failed");
        // No meshShaders(): the vertex path must work with VK_EXT_mesh_shader absent.
        auto opts = VulkanContextOptions().validation().throwOnValidationError();
        context = std::make_unique<VulkanContext>(window, opts);
    }
    ~TestContext() {
        context.reset();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

constexpr uint32_t kW = 64, kH = 64;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (ok) { std::cout << "  PASS " << what << "\n"; return; }
    std::cout << "  FAIL " << what << "\n";
    ++failures;
}

struct Push {
    uint32_t vertexBufferRID;
    uint32_t pad0 = 0, pad1 = 0, pad2 = 0;
};

std::vector<uint32_t> readbackTexels(Image& img, Layout from) {
    const size_t bytes = size_t(kW) * kH * sizeof(uint32_t);
    Buffer staging(BufferBuilder(bytes).transferDestination().hostVisible());
    auto cmd = Commands::oneShot();
    cmd.imageBarrier(img, Stage::ColorOutput, Access::ColorAttachmentWrite, from,
                     Stage::Transfer, Access::TransferRead, Layout::TransferSrc);
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {kW, kH, 1};
    vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &region);
    cmd.submitAndWait();
    std::vector<uint32_t> out(size_t(kW) * kH);
    staging.download(out.data(), bytes);
    return out;
}

// A green triangle covering the image center, pulled from a storage buffer, on a
// mesh-shader-free context.
void testVertexPullingDraw() {
    ShaderModule vertModule(ShaderBuilder().vertex().fromFile("tests/shaders/pull.vert.spv"));
    ShaderModule fragModule(ShaderBuilder().fragment().fromFile("tests/shaders/pull.frag.spv"));

    check(vertModule.reflection.executionModel == VK_SHADER_STAGE_VERTEX_BIT,
          "reflection identifies the Vertex execution model");

    Pipeline pipeline = GraphicsPipelineBuilder()
        .vertexShader(vertModule)
        .fragmentShader(fragModule)
        .colorFormats({VK_FORMAT_R8G8B8A8_UNORM})
        .noDepth()
        .build();

    const float positions[6] = { -0.9f, 0.9f,  0.9f, 0.9f,  0.0f, -0.9f };
    Buffer vertices(BufferBuilder(sizeof(positions)).storage().hostVisible());
    vertices.upload((void*)positions, sizeof(positions));

    auto init = Commands::oneShot();
    auto ib = ImageBuilder().colorTarget(kW, kH, VK_FORMAT_R8G8B8A8_UNORM);
    ib.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    Image target(ib, init);
    init.submitAndWait();

    {
        auto cmd = Commands::oneShot();
        cmd.imageBarrier(target, Stage::None, Access::None, Layout::Undefined,
                         Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment);
        cmd.beginRenderingOffscreen(target.imageView, VkExtent2D{kW, kH});
        cmd.bindGraphics(pipeline);
        Push push{ vertices.rid() };
        cmd.pushConstants(push);
        cmd.draw(3);
        cmd.endRendering();
        cmd.submitAndWait();
    }

    auto texels = readbackTexels(target, Layout::ColorAttachment);
    const uint32_t center = texels[size_t(kH / 2) * kW + kW / 2];
    const uint32_t corner = texels[0];
    check((center & 0xffu) == 0u && ((center >> 8) & 0xffu) == 255u,
          "vertex-pulled triangle covers the center in green, with no mesh shader extension");
    check(((corner >> 8) & 0xffu) == 0u,
          "a corner outside the triangle is untouched (the draw was really rasterized)");
}

void testRejectsMeshAndVertexTogether() {
    // Deliberately does NOT require a mesh-capable device: the builder must reject the
    // combination from reflection alone, before any Vulkan call. A second module is loaded
    // from the vertex SPIR-V and relabelled as a mesh stage purely to exercise that check.
    ShaderModule vertModule(ShaderBuilder().vertex().fromFile("tests/shaders/pull.vert.spv"));
    ShaderModule fragModule(ShaderBuilder().fragment().fromFile("tests/shaders/pull.frag.spv"));
    ShaderModule meshLike(ShaderBuilder().vertex().fromFile("tests/shaders/pull.vert.spv"));
    meshLike.reflection.executionModel = VK_SHADER_STAGE_MESH_BIT_EXT;

    bool threw = false;
    try {
        GraphicsPipelineBuilder()
            .vertexShader(vertModule)
            .meshShader(meshLike)
            .fragmentShader(fragModule)
            .build();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "build() rejects a builder holding both a mesh and a vertex stage");
}

void testRejectsVertexAttributeInputs() {
    ShaderModule attrModule(ShaderBuilder().vertex().fromFile("tests/shaders/attributes.vert.spv"));
    bool threw = false;
    try {
        GraphicsPipelineBuilder().vertexShader(attrModule).noDepth().build();
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "build() rejects a vertex shader declaring attribute input locations");
}

} // namespace

int main() {
    try {
        TestContext ctx;
        testVertexPullingDraw();
        testRejectsMeshAndVertexTogether();
        testRejectsVertexAttributeInputs();
    } catch (const std::exception& e) {
        std::cout << "vertex-pipeline tests threw: " << e.what() << "\n";
        return 1;
    }
    if (failures) {
        std::cout << failures << " vertex-pipeline assertion(s) failed\n";
        return 1;
    }
    std::cout << "vertex pipeline tests passed\n";
    return 0;
}
