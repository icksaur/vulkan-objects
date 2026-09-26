#include "vkinternal.h"

#include <cassert>

size_t nextFrameInFlightSlot(size_t currentSlot, size_t frameSlotCount) {
    assert(frameSlotCount > 0);
    return (currentSlot + 1) % frameSlotCount;
}

bool swapchainImageSlotsConsistent(
    size_t imageCount,
    size_t imageViewCount,
    size_t renderFinishedSemaphoreCount)
{
    return imageCount == imageViewCount && imageCount == renderFinishedSemaphoreCount;
}
