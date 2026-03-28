#include "Metal.hpp"
#include <campello_gpu/buffer.hpp>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void *pd) {
    native = pd;
}

Buffer::~Buffer() {
    if (native != nullptr) {
        ((MTL::Buffer *)native)->release();
    }
}

uint64_t Buffer::getLength() {
    return ((MTL::Buffer *)native)->length();
}

bool Buffer::upload(uint64_t offset, uint64_t size, void *data) {

    uint8_t *dst = (uint8_t *)((MTL::Buffer *)native)->contents();
    memcpy(dst+offset,data, size);

    ((MTL::Buffer *)native)->didModifyRange(NS::Range::Make(offset, size));
    return true;
}

bool Buffer::download(uint64_t offset, uint64_t length, void *data) {
    if (!native || !data) return false;
    auto *buf = static_cast<MTL::Buffer *>(native);
    
    // Get buffer contents
    uint8_t *src = static_cast<uint8_t *>(buf->contents());
    if (!src) return false;
    
    // Check bounds
    if (offset + length > buf->length()) return false;
    
    // Copy data
    memcpy(data, src + offset, length);
    return true;
}


