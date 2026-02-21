#include <iostream>
#include <istream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_main.h>
#include "vkobjects.h"

#include "tga.h"
#include "math.h"
#include "camera.h"

// Global Settings
int windowWidth = 1280;
int windowHeight = 720;
const uint32_t cubeCount = 12;
const uint32_t shadowMapRes = 2048;

struct SDLWindow {
    SDL_Window *window;
    SDLWindow(const char * title, int w, int h) {
        if (false == SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error("Failed to initialize SDL");
        }
        window = SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN);
        if (window == nullptr) {
            throw std::runtime_error("Failed to create SDL window");
        }
    }
    ~SDLWindow() {
        SDL_DestroyWindow(window);
        SDL_Quit();
    }
    operator SDL_Window*() {
        return window;
    }
};

struct Timer {
    size_t lastTicks;
    Timer():lastTicks(SDL_GetTicks()){ }
    size_t elapsed() {
        size_t now = SDL_GetTicks();
        size_t elapsed = now - lastTicks;
        lastTicks = now;
        return elapsed;
    }
};

Image createImageFromTGAFile(Commands & commands, const char * filename) {
    std::ifstream file(filename, std::ios::binary);
    std::vector<uint8_t> fileBytes = std::vector<uint8_t>(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());

    file.close();
    unsigned width, height;
    int bpp;
    void* bytes = read_tga(fileBytes, width, height, bpp);
    if (bytes == nullptr) {
        throw std::runtime_error("failed to read file as TGA");
    }

    unsigned byteCount = width*height*(bpp/8);
    VkFormat format = (bpp == 32) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8_SRGB;

    Buffer stagingBuffer(BufferBuilder(byteCount).transferSource().hostVisible());
    stagingBuffer.upload(bytes, byteCount);
    free(bytes);

    Image image(ImageBuilder().fromStagingBuffer(stagingBuffer, width, height, format), commands);
    return image;
}

// Push constants — must match all shaders
struct PushConstants : PushConstantBase<PushConstants> {
    float viewProjection[16]; // mat4
    uint32_t vertexBufferRID;
    uint32_t textureRID;
    uint32_t shadowMapRID;
    uint32_t lightBufferRID;
    float rotationAngle;
};

// Light data stored in a storage buffer (accessed by RID)
struct LightData {
    float lightViewProjection[16]; // mat4
};

// Build an orthographic light VP matrix for a directional light
mat16f buildLightVP(vec3f lightDir) {
    // Light looks at origin from some distance along -lightDir
    vec3f lightPos = lightDir * -12.0f;

    Camera lightCam;
    lightCam.orthographic(8.0f, 8.0f, 0.1f, 30.0f);
    lightCam.moveTo(lightPos.x, lightPos.y, lightPos.z);
    lightCam.lookAt(0.0f, 0.0f, 0.0f);
    return lightCam.getViewProjection();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    SDLWindow window("VulkanApp - Shadow Maps", windowWidth, windowHeight);

    VulkanContext context(window, VulkanContextOptions().validation().meshShaders());

    // Shaders
    ShaderModule cubeMeshModule(ShaderBuilder().mesh().fromFile("demo/shaders/cube.mesh.spv"));
    ShaderModule shadowMeshModule(ShaderBuilder().mesh().fromFile("demo/shaders/shadow.mesh.spv"));
    ShaderModule shadowFragModule(ShaderBuilder().fragment().fromFile("demo/shaders/shadow.frag.spv"));
    ShaderModule cubeCompModule(ShaderBuilder().compute().fromFile("demo/shaders/cubes.comp.spv"));
    ShaderModule fullscreenMeshModule(ShaderBuilder().mesh().fromFile("demo/shaders/fullscreen.mesh.spv"));
    ShaderModule blitFragModule(ShaderBuilder().fragment().fromFile("demo/shaders/blit.frag.spv"));

    // One-shot setup
    auto setupCmd = Commands::oneShot();
    Image textureImage = createImageFromTGAFile(setupCmd, "vulkan.tga");

    // Per-swapchain render targets
    std::vector<Image> depthImages;
    std::vector<Image> shadowMaps;
    std::vector<Image> offscreenColors;
    for (size_t i = 0; i < context.swapchainImageCount; ++i) {
        depthImages.emplace_back(ImageBuilder().depth(), setupCmd);
        shadowMaps.emplace_back(ImageBuilder().depthSampled(shadowMapRes, shadowMapRes), setupCmd);
        offscreenColors.emplace_back(ImageBuilder().colorTarget(windowWidth, windowHeight), setupCmd);
    }
    setupCmd.submitAndWait();

    // Resize callback — recreate depth images and offscreen colors; shadow maps are fixed resolution
    context.onSwapchainResize([&](Commands & cmd, VkExtent2D extent) {
        depthImages.clear();
        offscreenColors.clear();
        for (size_t i = 0; i < context.swapchainImageCount; ++i) {
            depthImages.emplace_back(ImageBuilder().depth(), cmd);
            offscreenColors.emplace_back(ImageBuilder().colorTarget(extent.width, extent.height), cmd);
        }
    });

    // Vertex buffer: cubeCount cubes × 36 verts × 8 floats (pos3, normal3, uv2)
    const size_t vertexBufferSize = sizeof(float) * 8 * 36 * cubeCount;
    Buffer vertexBuffer(BufferBuilder(vertexBufferSize).storage());

    // Light data buffer (light VP matrix, accessed via RID)
    Buffer lightBuffer(BufferBuilder(sizeof(LightData)).storage().hostVisible());
    vec3f lightDir = vec3f(1.0f, -2.0f, 1.0f).normalized();
    mat16f lightVP = buildLightVP(lightDir);
    LightData lightData;
    memcpy(lightData.lightViewProjection, &lightVP, sizeof(mat16f));
    lightBuffer.upload(&lightData, sizeof(LightData));

    // Pipelines
    Pipeline graphicsPipeline = GraphicsPipelineBuilder()
        .meshShader(cubeMeshModule)
        .fragmentShader(shadowFragModule)
        .build();

    Pipeline shadowPipeline = GraphicsPipelineBuilder()
        .meshShader(shadowMeshModule)
        .depthOnly()
        .build();

    Pipeline computePipeline = createComputePipeline(cubeCompModule);

    Pipeline blitPipeline = GraphicsPipelineBuilder()
        .meshShader(fullscreenMeshModule)
        .fragmentShader(blitFragModule)
        .build();

    // Camera: looking down at the scene from above and to the side
    Camera camera;
    camera.perspective(0.5f * M_PI, windowWidth, windowHeight, 0.1f, 100.0f)
        .moveTo(6.0f, 5.0f, 6.0f)
        .lookAt(0.0f, 0.0f, 0.0f)
        .setDistance(0.0f);

    PushConstants push = {};
    float totalTime = 0.0f;

    Timer timer;
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                done = true;
            }
        }

        Frame frame;
        uint32_t idx = frame.swapchainImageIndex();

        // Accumulate rotation angle for cubes
        float seconds = (float)timer.elapsed() / 1000.0f;
        totalTime += seconds;

        // Update push constants
        mat16f vp = camera.getViewProjection();
        memcpy(push.viewProjection, &vp, sizeof(mat16f));
        push.vertexBufferRID = vertexBuffer.rid();
        push.textureRID = textureImage.rid();
        push.shadowMapRID = shadowMaps[idx].rid();
        push.lightBufferRID = lightBuffer.rid();
        push.rotationAngle = totalTime * (float)M_PI / 6.0f;

        auto cmd = frame.beginCommands();

        // 1. Compute pass: generate cube geometry
        cmd.bindCompute(computePipeline);
        cmd.pushConstants(push);
        cmd.dispatch(cubeCount, 1, 1);

        // Barrier: compute writes → mesh shader reads
        cmd.bufferBarrier(vertexBuffer, Stage::Compute, Stage::MeshShader);

        // 2. Shadow pass: render depth from light's perspective
        cmd.beginRendering(shadowMaps[idx].imageView, {shadowMapRes, shadowMapRes});
        cmd.bindGraphics(shadowPipeline);
        cmd.pushConstants(push);
        cmd.drawMeshTasks(cubeCount, 1, 1);
        cmd.endRendering();

        // Barrier: shadow map depth write → fragment shader read
        Barrier(cmd).image(shadowMaps[idx], 1)
            .from(Stage::LateFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
            .to(Stage::Fragment, Access::ShaderRead, Layout::DepthReadOnly)
            .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
            .record();

        // 3. Main pass: render scene to offscreen color target (not swapchain)
        VkExtent2D offscreenExtent = {(uint32_t)context.windowWidth, (uint32_t)context.windowHeight};
        cmd.beginRendering(offscreenColors[idx].imageView, depthImages[idx].imageView, offscreenExtent);
        cmd.bindGraphics(graphicsPipeline);
        cmd.pushConstants(push);
        cmd.drawMeshTasks(cubeCount, 1, 1);
        cmd.endRendering();

        // Barrier: offscreen color → shader readable for blit
        Barrier(cmd).image(offscreenColors[idx], 1)
            .from(Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment)
            .to(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
            .record();

        // Barrier: shadow map back for next frame
        Barrier(cmd).image(shadowMaps[idx], 1)
            .from(Stage::Fragment, Access::ShaderRead, Layout::DepthReadOnly)
            .to(Stage::EarlyFragment, Access::DepthStencilWrite, Layout::DepthStencilAttachment)
            .aspectMask(VK_IMAGE_ASPECT_DEPTH_BIT)
            .record();

        // 4. Blit pass: sample offscreen texture, draw to swapchain (no depth needed)
        cmd.beginRendering();
        cmd.bindGraphics(blitPipeline);
        uint32_t blitRID = offscreenColors[idx].rid();
        cmd.pushConstants(&blitRID, sizeof(blitRID));
        cmd.drawMeshTasks(1, 1, 1);
        cmd.endRendering();

        // Barrier: offscreen color back to ColorAttachment for next frame
        Barrier(cmd).image(offscreenColors[idx], 1)
            .from(Stage::Fragment, Access::ShaderRead, Layout::ShaderReadOnly)
            .to(Stage::ColorOutput, Access::ColorAttachmentWrite, Layout::ColorAttachment)
            .record();

        frame.submit(cmd);
    }

    if (VK_SUCCESS != vkQueueWaitIdle(context.graphicsQueue)) {
        throw std::runtime_error("failed to wait for the graphics queue to be idle");
    }
}
