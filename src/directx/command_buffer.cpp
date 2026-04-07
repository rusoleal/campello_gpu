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
    delete h;
}
