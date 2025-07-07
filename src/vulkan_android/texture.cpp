#include <campello_gpu/texture.hpp>
#include "texture_handle.hpp"

using namespace systems::leal::campello_gpu;

Texture::Texture(void *pd) {
    this->native = pd;
}

Texture::~Texture() {
    auto handle = (TextureHandle *)native;
    handle->buffer = nullptr;

    vkDestroyImage(handle->device, handle->image, nullptr);

    delete handle;
}

bool Texture::upload(uint64_t offset, uint64_t length, void *data) {
    auto handle = (TextureHandle *)native;

    return handle->buffer->upload(offset, length, data);
}
