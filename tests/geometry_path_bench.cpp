// Mesh vs vertex geometry path timing on the current device.
//
// WHY THIS EXISTS. The library offers two pre-rasterization paths for the same draw. The
// choice between them is a performance claim, and a claim that is never measured drifts.
// This renders the SAME compute-generated geometry through both paths, in one process on
// one device, and reports GPU milliseconds per pass.
//
// This is a measurement, not a test: it has no pass/fail and is not registered with ctest.
// Absolute numbers are meaningless across machines; only the ratio between the two paths
// on one machine means anything.

#include "vkobjects.h"
#include "vkinternal.h"
#include "camera.h"
#include "math.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr uint32_t kW = 1280, kH = 720;
constexpr uint32_t kShadowRes = 2048;
constexpr uint32_t kWarmupFrames = 20;
constexpr uint32_t kMeasuredFrames = 200;

struct PushConstants : PushConstantBase<PushConstants> {
    float viewProjection[16];
    uint32_t vertexBufferRID;
    uint32_t textureRID;
    uint32_t shadowMapRID;
    uint32_t lightBufferRID;
    float rotationAngle;
    uint32_t tlasRID;
    uint32_t useRT;
};

struct CubeVertex { float px, py, pz, nx, ny, nz, u, v; };

struct BenchContext {
    SDL_Window* window = nullptr;
    std::unique_ptr<VulkanContext> context;

    BenchContext() {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error("SDL_Init failed");
        window = SDL_CreateWindow("vkobjects-path-bench", kW, kH, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error("SDL_CreateWindow failed");
        // Mesh shaders are required here specifically so BOTH paths can be measured in one
        // process. Enabling the feature does not affect the vertex path's timings.
        context = std::make_unique<VulkanContext>(window, VulkanContextOptions().meshShaders());
    }
    ~BenchContext() {
        context.reset();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

// Median is reported rather than mean because GPU frame timings have a long right tail
// (scheduling, clock ramping); one stall would move a mean but not a median.
double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

struct PathTimings {
    std::vector<double> shadow;
    std::vector<double> main;
};

} // namespace

int main(int argc, char** argv) {
    try {
        const uint32_t kCubeCount = (argc > 1) ? uint32_t(atoi(argv[1])) : 12u;
        BenchContext bench;

        ShaderModule cubeMesh(ShaderBuilder().mesh().fromFile("demo/shaders/cube.mesh.spv"));
        ShaderModule shadowMesh(ShaderBuilder().mesh().fromFile("demo/shaders/shadow.mesh.spv"));
        ShaderModule cubeVert(ShaderBuilder().vertex().fromFile("demo/shaders/cubes.vert.spv"));
        ShaderModule shadowVert(ShaderBuilder().vertex().fromFile("demo/shaders/shadow.vert.spv"));
        ShaderModule benchFrag(ShaderBuilder().fragment().fromFile("tests/shaders/bench.frag.spv"));
        ShaderModule cubeComp(ShaderBuilder().compute().fromFile("demo/shaders/cubes.comp.spv"));

        const uint32_t sceneVertexCount = 36 * kCubeCount;
        Buffer vertexBuffer(BufferBuilder(sizeof(CubeVertex) * sceneVertexCount).storage());

        Buffer lightBuffer(BufferBuilder(sizeof(float) * 16).storage().hostVisible());
        vec3f lightDir = vec3f(1.0f, -2.0f, 1.0f).normalized();
        vec3f lightPos = lightDir * -12.0f;
        Camera lightCam;
        lightCam.orthographic(8.0f, 8.0f, 0.1f, 30.0f);
        lightCam.moveTo(lightPos.x, lightPos.y, lightPos.z);
        lightCam.lookAt(0.0f, 0.0f, 0.0f);
        mat16f lightVP = lightCam.getViewProjection();
        lightBuffer.upload(&lightVP, sizeof(lightVP));

        auto setup = Commands::oneShot();
        Image depthImage(ImageBuilder().depth(), setup);
        Image shadowMap(ImageBuilder().depthSampled(kShadowRes, kShadowRes), setup);
        Image colorTarget(ImageBuilder().colorTarget(kW, kH), setup);
        setup.submitAndWait();

        Pipeline computePipeline = createComputePipeline(cubeComp);

        Pipeline meshMainPipeline = GraphicsPipelineBuilder()
            .meshShader(cubeMesh).fragmentShader(benchFrag).build();
        Pipeline meshShadowPipeline = GraphicsPipelineBuilder()
            .meshShader(shadowMesh).depthOnly().build();

        Pipeline vertMainPipeline = GraphicsPipelineBuilder()
            .vertexShader(cubeVert).fragmentShader(benchFrag).build();
        Pipeline vertShadowPipeline = GraphicsPipelineBuilder()
            .vertexShader(shadowVert).depthOnly().build();

        PushConstants push = {};
        // A real view matrix matching the demo's camera. An identity matrix here would place
        // the scene outside clip space, and the benchmark would measure clipping rather than
        // rasterization -- the timings would stop scaling with cube count.
        Camera camera;
        camera.perspective(0.5f * 3.14159265f, kW, kH, 0.1f, 200.0f)
            .moveTo(6.0f, 5.0f, 6.0f)
            .lookAt(0.0f, 0.0f, 0.0f)
            .setDistance(0.0f);
        mat16f vp = camera.getViewProjection();
        memcpy(push.viewProjection, &vp, sizeof(mat16f));
        push.vertexBufferRID = vertexBuffer.rid();
        push.textureRID = 0;
        push.shadowMapRID = shadowMap.rid();
        push.lightBufferRID = lightBuffer.rid();
        push.tlasRID = 0;
        push.useRT = 0;

        PathTimings meshTimings, vertTimings;

        // Each pass is timed in its own submission. Timing both passes inside one command
        // buffer lets the GPU overlap them, and the second segment then reports work that
        // already ran during the first -- which showed up as the main pass appearing not to
        // scale with cube count at all.
        auto recordCompute = [&](Commands& cmd) {
            cmd.bindCompute(computePipeline);
            cmd.pushConstants(push);
            cmd.dispatch(kCubeCount, 1, 1);
            cmd.bufferBarrier(vertexBuffer, Stage::Compute, Access::ShaderWrite,
                              Stage::MeshShader | Stage::VertexShader, Access::ShaderRead);
        };
        auto recordShadow = [&](Commands& cmd, bool useMesh) {
            cmd.imageBarrier(shadowMap, Stage::None, Access::None, Layout::Undefined,
                             Stage::EarlyFragment, Access::DepthStencilWrite,
                             Layout::DepthStencilAttachment, VK_IMAGE_ASPECT_DEPTH_BIT);
            cmd.beginRendering(shadowMap.imageView, {kShadowRes, kShadowRes});
            cmd.bindGraphics(useMesh ? meshShadowPipeline : vertShadowPipeline);
            cmd.pushConstants(push);
            if (useMesh) cmd.drawMeshTasks(kCubeCount, 1, 1);
            else cmd.draw(sceneVertexCount, 1, 0, 0);
            cmd.endRendering();
        };
        auto recordMain = [&](Commands& cmd, bool useMesh) {
            cmd.imageBarrier(colorTarget, Stage::None, Access::None, Layout::Undefined,
                             Stage::ColorOutput, Access::ColorAttachmentWrite,
                             Layout::ColorAttachment);
            cmd.beginRendering(colorTarget.imageView, depthImage.imageView, {kW, kH});
            cmd.bindGraphics(useMesh ? meshMainPipeline : vertMainPipeline);
            cmd.pushConstants(push);
            if (useMesh) cmd.drawMeshTasks(kCubeCount, 1, 1);
            else cmd.draw(sceneVertexCount, 1, 0, 0);
            cmd.endRendering();
        };

        // Correctness gate: a pass that draws nothing would look infinitely fast, so both
        // paths' covered-pixel counts are compared before any timing is believed.
        auto coveredPixels = [&](bool useMesh) {
            const size_t bytes = size_t(kW) * kH * 4;
            Buffer staging(BufferBuilder(bytes).transferDestination().hostVisible());
            auto cmd = Commands::oneShot();
            recordCompute(cmd);
            recordMain(cmd, useMesh);
            cmd.imageBarrier(colorTarget, Stage::ColorOutput, Access::ColorAttachmentWrite,
                             Layout::ColorAttachment, Stage::Transfer, Access::TransferRead,
                             Layout::TransferSrc);
            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {kW, kH, 1};
            vkCmdCopyImageToBuffer(cmd, colorTarget, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   staging, 1, &region);
            cmd.submitAndWait();
            std::vector<uint32_t> texels(size_t(kW) * kH);
            staging.download(texels.data(), bytes);
            size_t covered = 0;
            for (uint32_t t : texels) if ((t & 0x00ffffffu) != 0u) ++covered;
            return covered;
        };

        const size_t meshCovered = coveredPixels(true);
        const size_t vertCovered = coveredPixels(false);

        // Times one pass, isolated in its own submission.
        auto timePass = [&](bool useMesh, bool shadowPass) {
            GpuTimer timer(1);
            auto cmd = Commands::oneShot();
            recordCompute(cmd);
            timer.begin(cmd);
            if (shadowPass) recordShadow(cmd, useMesh);
            else recordMain(cmd, useMesh);
            timer.mark(cmd, "pass");
            cmd.submitAndWait();
            auto results = timer.resolve();
            return results.empty() ? 0.0 : results[0].second;
        };

        // Both paths run interleaved frame by frame rather than in two separate blocks, so a
        // GPU clock ramp partway through biases both equally instead of only the second one.
        for (uint32_t frameIdx = 0; frameIdx < kWarmupFrames + kMeasuredFrames; ++frameIdx) {
            const bool measured = frameIdx >= kWarmupFrames;
            push.rotationAngle = float(frameIdx) * 0.01f;

            for (int pathIdx = 0; pathIdx < 2; ++pathIdx) {
                const bool useMesh = (pathIdx == 0);
                double shadowMs = timePass(useMesh, true);
                double mainMs = timePass(useMesh, false);
                if (!measured) continue;
                PathTimings& into = useMesh ? meshTimings : vertTimings;
                into.shadow.push_back(shadowMs);
                into.main.push_back(mainMs);
            }
        }

        std::cout << "covered pixels: mesh " << meshCovered << " vert " << vertCovered
                  << (meshCovered == vertCovered ? "  (identical)" : "  MISMATCH") << "\n";
        std::cout << kCubeCount << " cubes, " << sceneVertexCount << " vertices, "
                  << kMeasuredFrames << " measured frames\n\n";

        std::cout << std::fixed << std::setprecision(4);
        auto row = [](const char* name, std::vector<double> mesh, std::vector<double> vert) {
            double meshMs = median(mesh), vertMs = median(vert);
            std::sort(mesh.begin(), mesh.end());
            std::sort(vert.begin(), vert.end());
            size_t meshDistinct = size_t(std::unique(mesh.begin(), mesh.end()) - mesh.begin());
            size_t vertDistinct = size_t(std::unique(vert.begin(), vert.end()) - vert.begin());
            std::cout << std::left << std::setw(8) << name
                      << " mesh " << std::setw(9) << meshMs
                      << " vert " << std::setw(9) << vertMs
                      << " ratio " << std::setw(8) << (meshMs > 0.0 ? vertMs / meshMs : 0.0)
                      << " (distinct mesh " << meshDistinct << " vert " << vertDistinct << ")\n";
        };
        row("shadow", meshTimings.shadow, vertTimings.shadow);
        row("main", meshTimings.main, vertTimings.main);
        std::cout << "\nratio < 1.0 means the vertex path is faster.\n";
    } catch (const std::exception& e) {
        std::cout << "path benchmark threw: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
