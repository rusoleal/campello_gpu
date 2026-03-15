#include "Metal.hpp"
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

CommandBuffer::CommandBuffer(void *pd) : native(pd) {}

CommandBuffer::~CommandBuffer() {
    if (native != nullptr) {
        static_cast<MTL::CommandBuffer *>(native)->release();
    }
}
