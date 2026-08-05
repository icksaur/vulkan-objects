// spec-gbuffer-attachments step 3: typed clear values + load-op control on beginRendering.
//
// WHY THIS TEST EXISTS. The pre-existing overloads hardcode LOAD_OP_CLEAR with a FLOAT (0,0,0,1).
// VkClearColorValue is a UNION, so for an integer attachment the float bits are reinterpreted:
// clearing a UINT target to float 0.0 happens to give 0 (all-zero bits), and ANY other value gives
// garbage. That is the worst kind of bug -- it works in the one case people try first, and the
// failure is silent. Hull needs a UINT G-buffer target cleared to a SENTINEL (temporalId = 0xffff
// means "background"), which the old API cannot express at all.
//
// So the property under test is not "the struct compiles". It is: a NON-TRIVIAL uint clear value
// SURVIVES to memory, and would NOT survive if the value were routed through the float path. The
// test asserts both directions:
//   - clearUint(0xffff, ...)  reads back exactly 0xffff       (the capability hull needs)
//   - the same bits interpreted as a float would be ~9.18e-41, so a float-path implementation
//     writing 0xffff would round-trip to something else entirely; the test pins the exact bits.
//   - clearFloat on a UNORM attachment still round-trips           (no regression)
//   - LOAD_OP_LOAD preserves prior contents instead of clearing    (the other half of step 3)
//
// Nothing is drawn: beginRendering + endRendering alone must perform the clear, because that is
// precisely what hull relies on when it deletes clear.comp's G-buffer clears (plan step 14).

#include "vkobjects.h"
#include "vkinternal.h"

#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
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
        window = SDL_CreateWindow("vkobjects-clear-tests", 64, 64,
                                  SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error("SDL_CreateWindow failed");
        // throwOnValidationError: a wrong clear type or a missing layout transition should fail the
        // test loudly rather than be diagnosed from an image diff.
        auto opts = VulkanContextOptions().validation().throwOnValidationError();
        context = std::make_unique<VulkanContext>(window, opts);
    }
    ~TestContext() {
        context.reset();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

constexpr uint32_t kW = 16, kH = 16;

int failures = 0;
void check(bool ok, const std::string& what) {
    if (ok) { std::cout << "  PASS " << what << "\n"; return; }
    std::cout << "  FAIL " << what << "\n";
    ++failures;
}

Image makeImage(VkFormat format, Commands& cmd) {
    // colorAttachment + transferSource: written by the ROP, read back by copy.
    // colorTarget gives COLOR_ATTACHMENT usage; the readback path needs TRANSFER_SRC too.
    auto b = ImageBuilder().colorTarget(kW, kH, format);
    b.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return Image(b, cmd);
}

template <typename T>
std::vector<T> readbackTexels(Image& img, Layout from) {
    const size_t bytes = size_t(kW) * kH * sizeof(T);
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
    std::vector<T> out(size_t(kW) * kH);
    staging.download(out.data(), bytes);
    return out;
}

// A UINT attachment cleared to a real sentinel -- the capability that does not exist today.
void testUintSentinelClear() {
    auto init = Commands::oneShot();
    Image img = makeImage(VK_FORMAT_R32_UINT, init);
    init.submitAndWait();

    constexpr uint32_t kSentinel = 0x0000ffffu;   // hull's "no temporal id"
    {
        auto cmd = Commands::oneShot();
        cmd.imageBarrier(img, Stage::None, Access::None, Layout::Undefined,
                         Stage::ColorOutput, Access::ColorAttachmentWrite,
                         Layout::ColorAttachment);
        Commands::ColorAttachment att =
            Commands::ColorAttachment::clearUint(img.imageView, kSentinel, 0u, 0u, 0u);
        cmd.beginRendering(std::span<const Commands::ColorAttachment>(&att, 1),
                           Commands::DepthAttachment::none(), VkExtent2D{kW, kH});
        cmd.endRendering();
        cmd.submitAndWait();
    }
    auto texels = readbackTexels<uint32_t>(img, Layout::ColorAttachment);
    bool all = true;
    for (uint32_t v : texels) if (v != kSentinel) { all = false; break; }
    check(all, "clearUint(0xffff) round-trips exactly (float path would not)");

    // Pin the failure mode explicitly: 0xffff reinterpreted as float is a denormal ~9.18e-41, so an
    // implementation that routed this through VkClearColorValue::float32 could not produce these
    // bits. Asserting the exact value IS the discriminator between the two code paths.
    check(texels.empty() ? false : texels[0] == 0x0000ffffu,
          "the exact bit pattern is 0x0000ffff, not a float reinterpretation");
}

// Regression guard: the float path must still behave for UNORM targets.
void testFloatClearStillWorks() {
    auto init = Commands::oneShot();
    Image img = makeImage(VK_FORMAT_R8G8B8A8_UNORM, init);
    init.submitAndWait();
    {
        auto cmd = Commands::oneShot();
        cmd.imageBarrier(img, Stage::None, Access::None, Layout::Undefined,
                         Stage::ColorOutput, Access::ColorAttachmentWrite,
                         Layout::ColorAttachment);
        Commands::ColorAttachment att =
            Commands::ColorAttachment::clearFloat(img.imageView, 1.0f, 0.0f, 0.5f, 1.0f);
        cmd.beginRendering(std::span<const Commands::ColorAttachment>(&att, 1),
                           Commands::DepthAttachment::none(), VkExtent2D{kW, kH});
        cmd.endRendering();
        cmd.submitAndWait();
    }
    auto texels = readbackTexels<uint32_t>(img, Layout::ColorAttachment);
    const uint32_t px = texels.empty() ? 0u : texels[0];
    const uint32_t r = px & 0xffu, g = (px >> 8) & 0xffu, b = (px >> 16) & 0xffu;
    check(r == 255u && g == 0u && (b == 127u || b == 128u),
          "clearFloat on UNORM round-trips (1.0, 0.0, 0.5)");
}

// The other half of step 3: an attachment can be PRESERVED rather than always cleared.
void testLoadOpPreserves() {
    auto init = Commands::oneShot();
    Image img = makeImage(VK_FORMAT_R32_UINT, init);
    init.submitAndWait();

    constexpr uint32_t kFirst = 0x0000ffffu, kSecondIfCleared = 0x00001234u;
    {
        auto cmd = Commands::oneShot();
        cmd.imageBarrier(img, Stage::None, Access::None, Layout::Undefined,
                         Stage::ColorOutput, Access::ColorAttachmentWrite,
                         Layout::ColorAttachment);
        Commands::ColorAttachment first =
            Commands::ColorAttachment::clearUint(img.imageView, kFirst, 0u, 0u, 0u);
        cmd.beginRendering(std::span<const Commands::ColorAttachment>(&first, 1),
                           Commands::DepthAttachment::none(), VkExtent2D{kW, kH});
        cmd.endRendering();
        // Dynamic rendering provides NO implicit dependency between two rendering instances, so
        // without this barrier the second pass could observe the first pass's writes only by luck --
        // the preservation test would be non-deterministic rather than wrong, which is worse.
        cmd.imageBarrier(img, Stage::ColorOutput, Access::ColorAttachmentWrite,
                         Layout::ColorAttachment,
                         Stage::ColorOutput, Access::ColorAttachmentRead | Access::ColorAttachmentWrite,
                         Layout::ColorAttachment);
        // Second pass LOADS. If load-op control were broken and it cleared instead, the sentinel
        // would be replaced -- so this distinguishes LOAD from CLEAR, rather than merely observing
        // that the value is still there after doing nothing.
        Commands::ColorAttachment second = Commands::ColorAttachment::load(img.imageView);
        second.clearValue.color.uint32[0] = kSecondIfCleared;   // must be IGNORED under LOAD_OP_LOAD
        cmd.beginRendering(std::span<const Commands::ColorAttachment>(&second, 1),
                           Commands::DepthAttachment::none(), VkExtent2D{kW, kH});
        cmd.endRendering();
        cmd.submitAndWait();
    }
    auto texels = readbackTexels<uint32_t>(img, Layout::ColorAttachment);
    const uint32_t px = texels.empty() ? 0u : texels[0];
    check(px == kFirst, "LOAD_OP_LOAD preserves prior contents (ignores the clear value)");
    check(px != kSecondIfCleared, "the second pass did not clear");
}

// Two attachments of DIFFERENT type in one pass, each with its own clear -- the actual hull shape
// (RGBA8 albedo + R32_UINT temporal), and the case a single shared clear value cannot serve.
void testMixedTypeMrt() {
    auto init = Commands::oneShot();
    Image colorImg = makeImage(VK_FORMAT_R8G8B8A8_UNORM, init);
    Image uintImg = makeImage(VK_FORMAT_R32_UINT, init);
    init.submitAndWait();
    {
        auto cmd = Commands::oneShot();
        for (Image* i : {&colorImg, &uintImg})
            cmd.imageBarrier(*i, Stage::None, Access::None, Layout::Undefined,
                             Stage::ColorOutput, Access::ColorAttachmentWrite,
                             Layout::ColorAttachment);
        Commands::ColorAttachment atts[2] = {
            Commands::ColorAttachment::clearFloat(colorImg.imageView, 0.0f, 1.0f, 0.0f, 1.0f),
            Commands::ColorAttachment::clearUint(uintImg.imageView, 0x0000ffffu, 0u, 0u, 0u),
        };
        cmd.beginRendering(std::span<const Commands::ColorAttachment>(atts, 2),
                           Commands::DepthAttachment::none(), VkExtent2D{kW, kH});
        cmd.endRendering();
        cmd.submitAndWait();
    }
    auto colorTexels = readbackTexels<uint32_t>(colorImg, Layout::ColorAttachment);
    auto uintTexels = readbackTexels<uint32_t>(uintImg, Layout::ColorAttachment);
    const uint32_t c = colorTexels.empty() ? 0u : colorTexels[0];
    check((c & 0xffu) == 0u && ((c >> 8) & 0xffu) == 255u,
          "MRT: float attachment cleared to green");
    check(!uintTexels.empty() && uintTexels[0] == 0x0000ffffu,
          "MRT: uint attachment cleared to its own sentinel in the SAME pass");
}

} // namespace

int main() {
    try {
        TestContext ctx;
        testUintSentinelClear();
        testFloatClearStillWorks();
        testLoadOpPreserves();
        testMixedTypeMrt();
    } catch (const std::exception& e) {
        std::cout << "clear-value tests threw: " << e.what() << "\n";
        return 1;
    }
    if (failures) {
        std::cout << failures << " clear-value assertion(s) failed\n";
        return 1;
    }
    std::cout << "clear value tests passed\n";
    return 0;
}
