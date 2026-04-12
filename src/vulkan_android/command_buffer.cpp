#include <android/log.h>
#include <campello_gpu/command_buffer.hpp>
#include "command_buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void *pd) {
    this->native = pd;
}

CommandBuffer::~CommandBuffer() {
    auto data = (CommandBufferHandle *)native;
    // Release query pool if we allocated one for timing
    if (data->queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(data->device, data->queryPool, nullptr);
    }
    for (size_t i = 0; i < data->stagingBuffers.size(); i++) {
        vkFreeMemory(data->device, data->stagingMemories[i], nullptr);
        vkDestroyBuffer(data->device, data->stagingBuffers[i], nullptr);
    }
    vkFreeCommandBuffers(data->device, data->commandPool, 1, &data->commandBuffer);
    delete data;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "CommandBuffer::~CommandBuffer()");
}

uint64_t CommandBuffer::getGPUExecutionTime() {
    auto data = (CommandBufferHandle *)native;
    
    if (!data->hasTimingData || data->queryPool == VK_NULL_HANDLE) {
        return 0;
    }
    
    // Read back the timestamp query results
    uint64_t timestamps[2] = {0, 0};
    VkResult result = vkGetQueryPoolResults(
        data->device,
        data->queryPool,
        data->queryStartIndex, 2,  // start query, count
        sizeof(timestamps), timestamps,
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );
    
    if (result != VK_SUCCESS) {
        return 0;
    }
    
    // Calculate duration in nanoseconds
    // timestampPeriod is in nanoseconds per tick from VkPhysicalDeviceLimits
    uint64_t tickDelta = timestamps[1] - timestamps[0];
    return static_cast<uint64_t>(static_cast<double>(tickDelta) * data->timestampPeriod);
}
