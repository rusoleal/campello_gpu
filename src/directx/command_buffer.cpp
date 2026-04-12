#include "common.hpp"
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void* pd) : native(pd) {}

CommandBuffer::~CommandBuffer() {
    if (!native) return;
    auto* h = static_cast<CommandBufferHandle*>(native);
    // Release upload-heap staging buffers created during command recording (e.g. TLAS instance data).
    // Device::submit() calls waitForGpu() before returning, so GPU work is complete at this point.
    for (auto* res : h->stagingResources)
        if (res) res->Release();
    if (h->cmdList)   h->cmdList->Release();
    if (h->allocator) h->allocator->Release();
    // Release timing resources
    if (h->timestampQueryHeap) h->timestampQueryHeap->Release();
    if (h->timestampReadbackBuffer) h->timestampReadbackBuffer->Release();
    delete h;
}

uint64_t CommandBuffer::getGPUExecutionTime() {
    auto* h = static_cast<CommandBufferHandle*>(native);
    if (!h || !h->hasTimingData || !h->timestampReadbackBuffer || h->timestampFrequency == 0.0) {
        return 0;
    }
    
    // Map the readback buffer to read the timestamps
    void* mappedData = nullptr;
    if (FAILED(h->timestampReadbackBuffer->Map(0, nullptr, &mappedData))) {
        return 0;
    }
    
    uint64_t* timestamps = static_cast<uint64_t*>(mappedData);
    uint64_t startTick = timestamps[0];
    uint64_t endTick = timestamps[1];
    
    h->timestampReadbackBuffer->Unmap(0, nullptr);
    
    if (endTick <= startTick) {
        return 0;
    }
    
    // Convert ticks to nanoseconds
    // frequency is ticks per second, so duration in seconds = (end - start) / frequency
    // duration in nanoseconds = (end - start) / frequency * 1e9
    uint64_t tickDelta = endTick - startTick;
    return static_cast<uint64_t>(static_cast<double>(tickDelta) / h->timestampFrequency * 1e9);
}
