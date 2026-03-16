#define NOMINMAX
#include "common.hpp"
#include <campello_gpu/buffer.hpp>
#include <cstring>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void* pd) : native(pd) {}

Buffer::~Buffer() {
    if (!native) return;
    auto* h = static_cast<BufferHandle*>(native);
    if (h->resource) {
        if (h->mapped) h->resource->Unmap(0, nullptr);
        h->resource->Release();
    }
    delete h;
}

uint64_t Buffer::getLength() {
    if (!native) return 0;
    return static_cast<BufferHandle*>(native)->size;
}

bool Buffer::upload(uint64_t offset, uint64_t length, void* data) {
    if (!native || !data) return false;
    auto* h = static_cast<BufferHandle*>(native);
    if (!h->mapped) return false;
    if (offset + length > h->size) return false;
    std::memcpy(static_cast<uint8_t*>(h->mapped) + offset, data, length);
    return true;
}
