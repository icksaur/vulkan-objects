// Swapchain extent selection -- a pure function, tested with synthetic VkSurfaceCapabilitiesKHR
// values rather than a real window/device.
//
// WHY THIS TEST EXISTS. On Wayland, VkSurfaceCapabilitiesKHR::currentExtent is reported as
// {UINT32_MAX, UINT32_MAX}: the compositor is telling Vulkan "you decide", and the caller must
// fall back to its own idea of the window size, clamped to the surface's min/max image extent.
// Other platforms (and Wayland after XDG configure) dictate currentExtent directly and it must be
// used verbatim, even if it disagrees with what the caller asked for. Getting either branch wrong
// either freezes the swapchain at the wrong size (never following a Wayland resize) or creates one
// with an extent the surface rejects. This tests only the selection logic, not vkCreateSwapchainKHR
// itself -- no window or GPU is created.

#include "vkinternal.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    if (ok) { std::cout << "  PASS " << what << "\n"; return; }
    std::cout << "  FAIL " << what << "\n";
    ++failures;
}

VkSurfaceCapabilitiesKHR makeCapabilities(VkExtent2D currentExtent, VkExtent2D minExtent, VkExtent2D maxExtent) {
    VkSurfaceCapabilitiesKHR capabilities{};
    capabilities.currentExtent = currentExtent;
    capabilities.minImageExtent = minExtent;
    capabilities.maxImageExtent = maxExtent;
    return capabilities;
}

bool equal(VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; }

void testCurrentExtentDictatedIsUsedVerbatim() {
    // Non-Wayland (or a Wayland surface mid-configure): the surface reports an exact extent, which
    // must win even when it disagrees with the window's own idea of its pixel size.
    auto capabilities = makeCapabilities({800, 600}, {1, 1}, {4096, 4096});
    VkExtent2D chosen = chooseSwapExtent({1280, 720}, capabilities);
    check(equal(chosen, {800, 600}), "currentExtent is used verbatim, ignoring requestedSize");
}

void testUndefinedCurrentExtentUsesRequestedSize() {
    // The Wayland case: currentExtent == UINT32_MAX means "defer to the caller". Within the
    // surface's [min, max] range, the requested (window pixel) size must be used exactly --
    // this is the fix for a resize never reaching the swapchain on Wayland.
    auto capabilities = makeCapabilities({UINT32_MAX, UINT32_MAX}, {1, 1}, {4096, 4096});
    VkExtent2D chosen = chooseSwapExtent({1920, 1080}, capabilities);
    check(equal(chosen, {1920, 1080}), "undefined currentExtent falls back to the requested size");
}

void testRequestedSizeClampedToMinimum() {
    auto capabilities = makeCapabilities({UINT32_MAX, UINT32_MAX}, {64, 64}, {4096, 4096});
    VkExtent2D chosen = chooseSwapExtent({16, 16}, capabilities);
    check(equal(chosen, {64, 64}), "a requested size below minImageExtent is clamped up to it");
}

void testRequestedSizeClampedToMaximum() {
    auto capabilities = makeCapabilities({UINT32_MAX, UINT32_MAX}, {1, 1}, {2048, 2048});
    VkExtent2D chosen = chooseSwapExtent({4096, 3000}, capabilities);
    check(equal(chosen, {2048, 2048}), "a requested size above maxImageExtent is clamped down to it");
}

void testZeroRequestedSizeClampsToMinimum() {
    // A minimized window's pixel size is {0, 0}; a real surface never advertises a zero
    // minImageExtent, so the clamp alone (not a caller-side zero check) keeps this function's
    // output always valid to create a swapchain with.
    auto capabilities = makeCapabilities({UINT32_MAX, UINT32_MAX}, {1, 1}, {4096, 4096});
    VkExtent2D chosen = chooseSwapExtent({0, 0}, capabilities);
    check(equal(chosen, {1, 1}), "a zero requested size is clamped up to minImageExtent, never zero");
}

void testMatchingPixelSizeNeedsNoRebuild() {
    check(!needsSwapchainRebuild({1280, 720}, {1280, 720}),
        "a window pixel size equal to the requested size needs no rebuild");
}

void testChangedPixelSizeNeedsRebuild() {
    check(needsSwapchainRebuild({1920, 1080}, {1280, 720}),
        "a window pixel size different from the requested size needs a rebuild");
}

void testPixelSizeAboveMaxImageExtentNeedsNoRebuildAfterFirst() {
    // Regression test: a window pixel size outside the surface's [min, max] image extent (e.g. a
    // fullscreen surface larger than maxImageExtent) makes chooseSwapExtent's OUTPUT differ from
    // the window's pixel size on every single call, forever. Comparing Frame's live pixel size
    // against that clamped extent (windowWidth/windowHeight) would therefore never converge and
    // rebuild the swapchain every frame. Comparing against swapchainRequestedSize -- the input to
    // chooseSwapExtent, captured before the clamp -- converges after the first rebuild instead.
    auto capabilities = makeCapabilities({UINT32_MAX, UINT32_MAX}, {1, 1}, {2048, 2048});
    VkExtent2D windowPixelSize{4096, 3000};

    // First frame: nothing has been requested yet, so a rebuild is due.
    VkExtent2D swapchainRequestedSize{0, 0};
    check(needsSwapchainRebuild(windowPixelSize, swapchainRequestedSize),
        "an unset requested size differing from the window needs a rebuild");

    // createSwapChain's sequence: capture the pre-clamp requested size, then clamp for the actual
    // swapchain extent (which the caller must NOT compare against -- see windowWidth/windowHeight's
    // doc comment).
    swapchainRequestedSize = windowPixelSize;
    VkExtent2D actualSwapchainExtent = chooseSwapExtent(windowPixelSize, capabilities);
    check(equal(actualSwapchainExtent, {2048, 2048}),
        "the window pixel size above maxImageExtent clamps the actual swapchain extent");

    // Second (and every subsequent) frame, with the window unchanged: no rebuild, because the
    // comparison is against the requested size rather than the ever-reclamped actual extent.
    check(!needsSwapchainRebuild(windowPixelSize, swapchainRequestedSize),
        "pixel size larger than maxImageExtent -> no rebuild after the first");
}

} // namespace

int main() {
    testCurrentExtentDictatedIsUsedVerbatim();
    testUndefinedCurrentExtentUsesRequestedSize();
    testRequestedSizeClampedToMinimum();
    testRequestedSizeClampedToMaximum();
    testZeroRequestedSizeClampsToMinimum();
    testMatchingPixelSizeNeedsNoRebuild();
    testChangedPixelSizeNeedsRebuild();
    testPixelSizeAboveMaxImageExtentNeedsNoRebuildAfterFirst();

    if (failures) {
        std::cout << failures << " swapchain-extent assertion(s) failed\n";
        return 1;
    }
    std::cout << "swapchain extent tests passed\n";
    return 0;
}
