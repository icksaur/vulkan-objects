#include "vkinternal.h"

// --- TimestampQuery ---

TimestampQuery::TimestampQuery(uint32_t queryCount, uint32_t frameCount)
    : queryCount(queryCount), frameCount(frameCount),
      nanosPerTick_(g_context().limits.timestampPeriod)
{
    pools.resize(frameCount);
    VkQueryPoolCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = queryCount;
    for (uint32_t i = 0; i < frameCount; ++i) {
        if (vkCreateQueryPool(g_context().device, &ci, nullptr, &pools[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create timestamp query pool");
        }
    }
}

TimestampQuery::~TimestampQuery() {
    for (auto pool : pools) {
        if (pool != VK_NULL_HANDLE)
            vkDestroyQueryPool(g_context().device, pool, nullptr);
    }
}

void TimestampQuery::reset(Commands & cmd, uint32_t frameIndex) {
    vkCmdResetQueryPool(cmd, pools[frameIndex % frameCount], 0, queryCount);
    framesElapsed++;
}

void TimestampQuery::write(Commands & cmd, uint32_t frameIndex, uint32_t queryIndex) {
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         pools[frameIndex % frameCount], queryIndex);
}

bool TimestampQuery::read(uint32_t frameIndex, uint64_t * timestamps, uint32_t count) {
    // Don't read until all pools have been written at least once
    if (framesElapsed < frameCount) return false;

    VkResult result = vkGetQueryPoolResults(
        g_context().device,
        pools[frameIndex % frameCount],
        0, count,
        count * sizeof(uint64_t), timestamps, sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);
    return result == VK_SUCCESS;
}

float TimestampQuery::nanosPerTick() const {
    return nanosPerTick_;
}
