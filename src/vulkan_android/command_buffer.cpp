#include <android/log.h>
#include <campello_gpu/command_buffer.hpp>
#include "command_buffer_handle.hpp"

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void *pd) {
    this->native = pd;
}

CommandBuffer::~CommandBuffer() {
    auto data = (CommandBufferHandle *)native;
    for (size_t i = 0; i < data->stagingBuffers.size(); i++) {
        vkFreeMemory(data->device, data->stagingMemories[i], nullptr);
        vkDestroyBuffer(data->device, data->stagingBuffers[i], nullptr);
    }
    vkFreeCommandBuffers(data->device, data->commandPool, 1, &data->commandBuffer);
    delete data;
    __android_log_print(ANDROID_LOG_DEBUG, "campello_gpu", "CommandBuffer::~CommandBuffer()");
}
