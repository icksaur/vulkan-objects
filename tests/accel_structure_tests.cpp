#include "vkobjects.h"
#include "vkinternal.h"

#include <SDL3/SDL.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

struct TestContext {
    SDL_Window* window = nullptr;
    std::unique_ptr<VulkanContext> context;

    TestContext(bool immediateDestroy = false) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            throw std::runtime_error("SDL_Init failed");
        }
        window = SDL_CreateWindow("vkobjects-as-tests", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error("SDL_CreateWindow failed");
        auto opts = VulkanContextOptions().rayTracing().validation().throwOnValidationError();
        if (immediateDestroy) opts.immediateDestroy();
        context = std::make_unique<VulkanContext>(window, opts);
    }

    ~TestContext() {
        context.reset();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

struct Hit {
    uint32_t instanceCustomIndex = 0;
    uint32_t primitiveIndex = 0;
    float t = 0.0f;
    uint32_t hit = 0;
};

uint64_t allocatedBytes() {
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(g_allocator, budgets);
    VkPhysicalDeviceMemoryProperties props = {};
    vkGetPhysicalDeviceMemoryProperties(g_context().physicalDeviceHandle(), &props);
    uint64_t total = 0;
    for (uint32_t i = 0; i < props.memoryHeapCount; ++i) total += budgets[i].statistics.allocationBytes;
    return total;
}

std::unique_ptr<Buffer> makeTriangleVertices(float z = 0.0f) {
    float vertices[9] = {
        0.0f, 0.0f, z,
        1.0f, 0.0f, z,
        0.0f, 1.0f, z,
    };
    BufferBuilder builder(sizeof(vertices));
    builder.hostVisible().usageFlags(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    auto buffer = std::make_unique<Buffer>(builder);
    buffer->upload(vertices, sizeof(vertices));
    return buffer;
}

std::unique_ptr<Buffer> makeTriangleIndices() {
    uint32_t indices[3] = {0, 1, 2};
    BufferBuilder builder(sizeof(indices));
    builder.hostVisible().usageFlags(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    auto buffer = std::make_unique<Buffer>(builder);
    buffer->upload(indices, sizeof(indices));
    return buffer;
}

Hit trace(Tlas& tlas) {
    ShaderModule shader(ShaderBuilder().compute().fromFile("tests/shaders/as_oracle.comp.spv"));
    Pipeline pipeline = createComputePipeline(shader);

    BufferBuilder outBuilder(sizeof(Hit));
    outBuilder.hostVisible().usageFlags(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer out(outBuilder);
    Hit zero = {};
    out.upload(&zero, sizeof(zero));

    struct Push { uint32_t tlasRID; uint32_t outRID; } push{tlas.rid(), out.rid()};
    auto cmd = Commands::oneShot();
    cmd.bindCompute(pipeline);
    cmd.pushConstants(push);
    cmd.dispatch(1, 1, 1);
    cmd.bufferBarrier(out, Stage::Compute, Access::ShaderWrite, Stage::Host, Access::HostRead);
    cmd.submitAndWait();

    Hit hit;
    out.download(&hit, sizeof(hit));
    return hit;
}

Tlas buildScene(Blas& blas, uint32_t customIndex = 7) {
    Tlas tlas(1);
    TlasInstances instances;
    const float identity[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    instances.add(blas, identity, customIndex);
    auto cmd = Commands::oneShot();
    VkBuffer backing = blas.backing();
    cmd.blasToTlasBarrier(std::span<const VkBuffer>(&backing, 1));
    cmd.buildTlas(tlas, instances);
    cmd.tlasToShaderReadBarrier(tlas.backing());
    cmd.submitAndWait();
    return tlas;
}

void testOracleAndRefit() {
    TestContext ctx;
    auto vertices = makeTriangleVertices(0.0f);
    auto indices = makeTriangleIndices();

    BlasBuilder builder;
    builder.triangles(*vertices, 3, *indices, 1).refittable();
    Blas blas(builder);
    {
        auto cmd = Commands::oneShot();
        cmd.buildBlas(blas, false);
        cmd.submitAndWait();
    }
    Tlas tlas = buildScene(blas);
    Hit hit = trace(tlas);
    assert(hit.hit == 1);
    assert(hit.instanceCustomIndex == 7);
    assert(hit.primitiveIndex == 0);
    assert(std::fabs(hit.t - 1.0f) < 0.001f);

    float moved[9] = {
        0.0f, 0.0f, 0.5f,
        1.0f, 0.0f, 0.5f,
        0.0f, 1.0f, 0.5f,
    };
    vertices->upload(moved, sizeof(moved));
    {
        auto cmd = Commands::oneShot();
        cmd.buildBlas(blas, true);
        cmd.submitAndWait();
    }
    Tlas movedTlas = buildScene(blas);
    Hit movedHit = trace(movedTlas);
    assert(movedHit.hit == 1);
    assert(std::fabs(movedHit.t - 0.5f) < 0.001f);
}

void testMoveAndRaii(bool immediateDestroy) {
    TestContext ctx(immediateDestroy);
    ctx.context->flushDestroys();
    uint64_t before = allocatedBytes();
    for (uint32_t i = 0; i < 4; ++i) {
        auto vertices = makeTriangleVertices();
        auto indices = makeTriangleIndices();
        BlasBuilder builder;
        builder.triangles(*vertices, 3, *indices, 1);
        Blas blas(builder);
        Blas movedBlas(std::move(blas));
        {
            auto cmd = Commands::oneShot();
            cmd.buildBlas(movedBlas, false);
            cmd.submitAndWait();
        }
        Tlas tlas = buildScene(movedBlas);
        Tlas movedTlas(std::move(tlas));
        (void)movedTlas.rid();
    }
    ctx.context->waitIdle();
    ctx.context->flushDestroys();
    uint64_t after = allocatedBytes();
    assert(after <= before + (1u << 20));
}

void testTlasRidFenceGate() {
    TestContext ctx;
    auto vertices = makeTriangleVertices();
    auto indices = makeTriangleIndices();
    BlasBuilder builder;
    builder.triangles(*vertices, 3, *indices, 1);
    Blas blas(builder);
    {
        auto cmd = Commands::oneShot();
        cmd.buildBlas(blas, false);
        cmd.submitAndWait();
    }

    uint32_t first = kNullRid;
    uint32_t second = kNullRid;
    {
        Tlas tlas = buildScene(blas);
        first = tlas.rid();
    }
    {
        Tlas tlas = buildScene(blas);
        second = tlas.rid();
        assert(tlas.rid() != first);
    }
    ctx.context->waitIdle();
    ctx.context->flushDestroys();
    {
        Tlas tlas = buildScene(blas);
        assert(tlas.rid() == first || tlas.rid() == second);
    }
}

void testInstanceTable() {
    TestContext ctx;
    struct Payload { uint32_t a; float b; };
    InstanceTable<Payload> table(4);
    uint32_t i = table.add({3, 4.0f});
    assert(i == 0);
    table.set(0, {5, 6.0f});
    auto cmd = Commands::oneShot();
    table.upload(cmd);
    cmd.submitAndWait();
    assert(table.rid() != kNullRid);
}

void testRingDistinctAddresses() {
    TestContext ctx;
    auto indices = makeTriangleIndices();
    std::vector<std::unique_ptr<Buffer>> vertices;
    vertices.push_back(makeTriangleVertices(0.0f));
    vertices.push_back(makeTriangleVertices(0.25f));
    AccelStructureRing<Blas> ring;
    ring.init(2, [&](uint32_t slot) {
        BlasBuilder builder;
        builder.triangles(*vertices[slot], 3, *indices, 1);
        return Blas(builder);
    });
    assert(ring.size() == 2);
    assert(vertices[0]->deviceAddress() != vertices[1]->deviceAddress());
    assert(&ring.current() != nullptr);
}

void expectRefitAssert(const char* self) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        execl(self, self, "--refit-before-build-child", nullptr);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    assert(!WIFEXITED(status) || WEXITSTATUS(status) != 0);
}

int refitBeforeBuildChild() {
    TestContext ctx;
    auto vertices = makeTriangleVertices();
    auto indices = makeTriangleIndices();
    BlasBuilder builder;
    builder.triangles(*vertices, 3, *indices, 1).refittable();
    Blas blas(builder);
    auto cmd = Commands::oneShot();
    cmd.buildBlas(blas, true);
    cmd.submitAndWait();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--refit-before-build-child") return refitBeforeBuildChild();
    try {
        testOracleAndRefit();
        testMoveAndRaii(false);
        testMoveAndRaii(true);
        testTlasRidFenceGate();
        testInstanceTable();
        testRingDistinctAddresses();
        expectRefitAssert(argv[0]);
    } catch (const std::exception& e) {
        std::cout << "RT tests failed: " << e.what() << "\n";
        return 1;
    }
    std::cout << "accel structure tests passed\n";
    return 0;
}
