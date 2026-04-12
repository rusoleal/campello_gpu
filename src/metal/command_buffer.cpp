#include "Metal.hpp"
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void *pd) : native(pd) {}

CommandBuffer::~CommandBuffer() {
    if (native != nullptr) {
        static_cast<MTL::CommandBuffer *>(native)->release();
    }
}

uint64_t CommandBuffer::getGPUExecutionTime() {
    if (native == nullptr) return 0;
    auto *cmdBuf = static_cast<MTL::CommandBuffer *>(native);
    
    // GPUStartTime and GPUEndTime are in seconds (CFTimeInterval)
    double startTime = cmdBuf->GPUStartTime();
    double endTime = cmdBuf->GPUEndTime();
    
    if (startTime > 0 && endTime > startTime) {
        // Convert to nanoseconds
        return static_cast<uint64_t>((endTime - startTime) * 1e9);
    }
    return 0;
}
