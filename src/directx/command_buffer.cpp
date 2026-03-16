#include "common.hpp"
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void* pd) : native(pd) {}

CommandBuffer::~CommandBuffer() {
    if (!native) return;
    auto* h = static_cast<CommandBufferHandle*>(native);
    if (h->cmdList)   h->cmdList->Release();
    if (h->allocator) h->allocator->Release();
    delete h;
}
