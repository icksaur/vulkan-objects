#include <iostream>
#include <istream>
#include <fstream>
#include <vector>
#include <set>
#include <assert.h>
#include <memory>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "vkobjects.h"

#include "tga.h"
#include "math.h"
#include "camera.h"

// Global Settings
int windowWidth = 1280;
int windowHeight = 720;
size_t computedQuadCount = 100;

// You can have multiple vertex bindings in your vertex stage.  
// A single binding is common.  They are zero-indexed so zero here.
const size_t vertexBindingIndex = 0; 

struct SDLWindow {
    SDL_Window *window;
    SDLWindow(const char * title, int w, int h) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            throw std::runtime_error("Failed to initialize SDL");
        }
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_VULKAN);
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

Image createImageFromTGAFile(const char * filename) {
    std::ifstream file(filename);
    std::vector<char> fileBytes = std::vector<char>(
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

    // TGA is BGR order, not RGB
    // Further, TGA does not specify linear or non-linear color component intensity.
    // By convention, TGA values are going to be "gamma corrected" or non-linear.
    // Assuming the bytes are sRGB looks good.  If they are assumed to be linear here, the colors will be washed out.
    // Read more by looking up sRGB to linear Vulkan conversions.
    VkFormat format = (bpp == 32) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8_SRGB;

    ImageBuilder builder;
    builder.fromBytes(bytes, byteCount, width, height, format);

    Image image(builder);
    free(bytes);
    return image;
}

VkDeviceSize vertexOffsets[] = { 0 };

void record(
    VkPipeline computePipeline,
    VkPipeline graphicsPipeline,
    VkCommandBuffer commandBuffer,
    VkImageView colorImage,
    VkImageView depthImage,
    VkBuffer vertexBuffer,
    VkPipelineLayout pipelineLayout,
    VkDescriptorSet descriptorSet
) {
    CommandBufferRecording commandBufferRecording(commandBuffer);

    // bind and dispatch compute
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    // start dynamic rendering to our presentation image and depth buffer
    RenderingRecording renderingRecording(commandBuffer, colorImage, depthImage);

    // Bind the descriptor which contains the shader uniform buffer
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

    vkCmdBindVertexBuffers(commandBuffer, vertexBindingIndex, 1, &vertexBuffer, vertexOffsets);  // bind the vertex buffer
    vkCmdDraw(commandBuffer, 6 * computedQuadCount, 1, 0, 0); // draw the quads
}

int main(int argc, char *argv[]) {
    SDLWindow window(appName, windowWidth, windowHeight);

    // There can only be one context, and creating it is required for other objects to construct.
    VulkanContext context(window, VulkanContextOptions().validation());

    // shaders
    ShaderModule fragShaderModule(ShaderBuilder().fragment().fromFile("tri.frag.spv"));
    ShaderModule vertShaderModule(ShaderBuilder().vertex().fromFile("tri.vert.spv"));
    ShaderModule compShaderModule(ShaderBuilder().compute().fromFile("vertices.comp.spv"));

    // Image and sampler.  The image class encapsulates the image, memory, and imageview.
    Image textureImage = createImageFromTGAFile("vulkan.tga");
    TextureSampler textureSampler;
 
    // uniform buffer data for our view projection matrix and z scale in compute
    struct UniformBufferData {
        mat16f viewProjection;
        float zScale;
    } uniformBufferData;

    Camera camera;
    camera.perspective(0.5f*M_PI, windowWidth, windowHeight, 0.1f, 100.0f)
        .moveTo(1.0f, 0.0f, -0.5f)
        .lookAt(0.0f, 0.0f, 0.0f)
        .moveTo(0.0f, 0.0f, 0.0f)
        .setDistance(1.0f);

    uniformBufferData.viewProjection = camera.getViewProjection();
    uniformBufferData.zScale = 0.2f;

    // dynamic buffer means that it has multiple buffers so that we don't modify data being used by the GPU
    // dynamic buffers have as many buffers as swapchain image count
    // we will set the data above in the render loop
    DynamicBuffer uniformBuffer(BufferBuilder(sizeof(UniformBufferData)).uniform());

    // buffer for writing in compute and reading in vertex
    // This single buffer strategy is incorrect.  If this program runs slow enough, previous frames may be reading the buffer
    // while the current frame is writing to it.  Either dispatch the compute first and never write again, or we need multiple buffers.
    // This buffer is an example to show how we can write to and reuse a buffer.  Typically such a vertex buffer would be static anyhow.
    Buffer shaderStorageVertexBuffer(BufferBuilder(sizeof(float) * 5 * 6 * computedQuadCount).storage().vertex());

    // DESCRIPTOR SETS
    // These things are complex.  They describe what resources are bound to shader invocations.
    // We need layouts, pools matching the layout for allocating them, then to allocate them,
    // then bind the actual resources to the descriptor sets, and use the right descriptor set.
    // This example does all of that with a one dynamic buffer which adds enough complexity for an example.
    // There's a way around a lot of this: vkCmdPushDescriptorSet in the extension VK_KHR_push_descriptor.
    // Be sure to look that up.

    // descriptor layout of uniforms in our pipeline
    // we're going to use a single descriptor set layout that is used by both pipelines
    // you may prefer to use multiple layouts, one for graphics pipeline and another for compute
    DescriptorLayoutBuilder desriptorLayoutBuilder;
    desriptorLayoutBuilder
        .addUniformBuffer(0, 1, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_COMPUTE_BIT)
        .addSampler(1, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .addStorageBuffer(2, 1, VK_SHADER_STAGE_COMPUTE_BIT);
    VkDescriptorSetLayout descriptorSetLayout = desriptorLayoutBuilder.build();

    // descriptor pool for allocating descriptor sets
    // we've only got one pool here that can build the combined descriptor set above
    // you might want to have two pools if you have different sizes of descriptor sets
    DescriptorPoolBuilder poolBuilder;
    poolBuilder
        .addSampler(context.swapchainImageCount)
        .addStorageBuffer(context.swapchainImageCount)
        .addUniformBuffer(context.swapchainImageCount)
        .maxSets(context.swapchainImageCount);
    DescriptorPool descriptorPool(poolBuilder);
    
    // There are multiple uniform buffers in our dynamic buffer.  That means we need multiple descriptor sets
    // where each one refers to a different buffer in the dynamic buffer.
    // If we had multiple dynamic buffers there would be a need for a total of swapchainImageCount^dynamicBufferCount descriptor sets.
    // A general-purpose solution is necessarily complex, and this is a simple solution.
    std::vector<VkDescriptorSet> descriptorSets(context.swapchainImageCount);
    DescriptorSetBinder descriptorSetBinder;
    for (size_t i = 0; i < context.swapchainImageCount; i++) {
        descriptorSets[i] = descriptorPool.allocate(descriptorSetLayout);
        descriptorSetBinder.bindUniformBuffer(descriptorSets[i], 0, uniformBuffer.buffers[i]); // bind each buffer in the dynamic buffer
        descriptorSetBinder.bindSampler(descriptorSets[i], 1, textureSampler, textureImage);
        descriptorSetBinder.bindStorageBuffer(descriptorSets[i], 2, shaderStorageVertexBuffer);
    }
    descriptorSetBinder.updateSets();

    // command buffers are recorded into and submitted to a queue
    // we have one command buffer for each swapchain image, and cycle through them
    // they must be reset before rewritten
    std::vector<CommandBuffer> commandBuffers(context.swapchainImageCount);

    // pipelines
    // Pipelines represent the configurable pipeline stages that define what shaders are used and how their results are combined.
    // Take a look at the build() function to see all the options that are necessary and configurable.
    VkPipelineLayout pipelineLayout = createPipelineLayout({descriptorSetLayout}); // context-owned
    
    // vertex stage pipeline setup
    // You can have multiple vertex bindings for different use cases, and step through per vertex or per instance.
    // We only have one in this example.  See how we use vec3 position and vec2 UV in tri.vert
    // Locations have to be unique across the vertex shader in a pipeline.
    // Mesh shaders much simpler than this, but for this example we're using the traditional vertex stage.
    GraphicsPipelineBuilder graphicsPipelineBuilder(pipelineLayout);
    graphicsPipelineBuilder
        .addVertexShader(vertShaderModule)
        .addFragmentShader(fragShaderModule)
        .vertexBinding(vertexBindingIndex, sizeof(float)*5) // vec3 location and vec2 UV is 5 floats
        .vertexFloats(vertexBindingIndex, 0, 3, 0) // location 0: position vec3
        .vertexFloats(vertexBindingIndex, 1, 2, sizeof(float)*3); // location 1: UV vec2, offset by the previous position vec3

    VkPipeline graphicsPipeline = graphicsPipelineBuilder.build(); // context-owned

    VkPipeline computePipeline = createComputePipeline(pipelineLayout, compShaderModule); // context-owned
    
    // index of the swapchain resources to use for the next frame
    uint nextImage = 0;

    // depth buffer images, used for depth testing
    ImageBuilder depthImagebuilder;
    depthImagebuilder.depth();
    std::vector<Image> depthImages;
    for (size_t i=0; i<context.swapchainImageCount; ++i) {
        depthImages.emplace_back(depthImagebuilder);
    }

    // semaphore signaling when the next image has completed rendering
    // This is not used in this example.
    VkSemaphore unusedRenderFinishedSemaphore;

    Timer timer;
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }
        }

        // Frame objects provide convenient per-frame sync resources and track cleanup of destroyed buffer data.
        // Only one one frame object can exist at a time.
        Frame frame;

        // The previous frame's resources might be in flight.  The oldest frame-in-flight's resources are what we will reuse.
        // Acquire them into the frame object, and also block await that they are 
        frame.prepareOldestFrameResources();

        // Acquire a new image from the swapchain.  The next image index has no guaranteed order.
        // This call will block until the index is identified, but will not block while waiting for the image to be ready!
        // The semaphore is used to ensure that the image at that index is ready.
        // The semaphore is stored in the frame and is awaited when submitting buffers, but returned here if you need it.
        frame.acquireNextImageIndex(nextImage, unusedRenderFinishedSemaphore);

        // Rotate the camera, and update dynamic uniform buffer for the GPU.
        float seconds = (float)timer.elapsed()/1000.0f;
        camera.rotate(0.0f, 1.0f, 0.0f, M_PI*seconds/2.0);
        uniformBufferData.viewProjection = camera.getViewProjection();
        uniformBuffer.setData(&uniformBufferData, sizeof(uniformBufferData));
    
        // get and reset the oldest command buffer
        CommandBuffer & commandBuffer = commandBuffers[context.frameInFlightIndex];
        commandBuffer.reset();
    
        // This program has no dynamic commands, but we record in
        // the loop as an example of how you'd record a dynamic frame.
        record(
            computePipeline,
            graphicsPipeline,
            commandBuffer,
            $context().swapchainImageViews[nextImage], // use the next swapchain image
            depthImages[nextImage].imageView,
            shaderStorageVertexBuffer,
            pipelineLayout,
            descriptorSets[uniformBuffer.lastWriteIndex]); // use the descriptor set associated with the most recent buffer written
            
        // Submit the command buffer to the graphics queue
        frame.submitCommandBuffer(commandBuffer);
       
        // Present the image to the screen.  The internal semaphore is now unsignaled, and the presentation engine will signal it when it's done.
        if (!frame.tryPresentQueue()) {
            // This is a common Vulkan situation handled automatically by OpenGL.
            // We need to remake our swap chain and any images used in presentation, like the depth buffer.
            // This usually happens after the first frame.

            std::cout << "swap chain out of date, trying to remake" << std::endl;
            rebuildPresentationResources();
            depthImages.clear();
            for (size_t i=0; i<context.swapchainImageCount; ++i) {
                depthImages.push_back(depthImagebuilder);
            }
        }

        frame.cleanup(); // automatically called by destructor, but we call it explicitly here for clarity
    }

    // Wait until GPU is done with all work before cleaning up resources which could be in use.

    if (VK_SUCCESS != vkQueueWaitIdle(context.graphicsQueue)) {
        throw std::runtime_error("failed to wait for the graphics queue to be idle");
    }

    return 0;
}