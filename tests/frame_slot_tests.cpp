#include "vkinternal.h"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

static_assert(std::same_as<
    decltype(std::declval<const VulkanContext &>().frameInFlightCount()),
    size_t>);
static_assert(std::same_as<
    decltype(std::declval<const VulkanContext &>().swapchainImageCount()),
    size_t>);
static_assert(std::same_as<decltype(SwapchainInfo::extent), VkExtent2D>);
static_assert(std::same_as<decltype(SwapchainInfo::imageCount), size_t>);

int main() {
    const size_t frameSlotCount = 3;
    size_t frameSlot = 0;

    const size_t changingImageCounts[] = { 3, 2, 4, 1, 3, 2, 4 };
    const size_t expectedFrameSlots[] = { 1, 2, 0, 1, 2, 0, 1 };
    for (size_t i = 0; i < std::size(changingImageCounts); ++i) {
        (void)changingImageCounts[i];
        frameSlot = nextFrameInFlightSlot(frameSlot, frameSlotCount);
        assert(frameSlot == expectedFrameSlots[i]);
    }

    assert(swapchainImageSlotsConsistent(3, 3, 3));
    assert(swapchainImageSlotsConsistent(1, 1, 1));
    assert(!swapchainImageSlotsConsistent(3, 2, 3));
    assert(!swapchainImageSlotsConsistent(3, 3, 2));
    assert(!swapchainImageSlotsConsistent(2, 3, 3));
}
