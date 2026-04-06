#include <android/log.h>
#include <campello_gpu/texture_view.hpp>
#include "texture_view_handle.hpp"

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void *pd) {
    this->native = pd;
}

TextureView::~TextureView() {
    auto data = (TextureViewHandle *)native;
    if (data->owned && data->imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(data->device, data->imageView, nullptr);
    }
    delete data;
}

std::shared_ptr<TextureView> TextureView::fromNative(void *nativeTex) {
    // On Vulkan, nativeTex is a VkImageView cast to void*.
    // The caller retains ownership; we do NOT destroy it on cleanup.
    auto handle = new TextureViewHandle();
    handle->device    = VK_NULL_HANDLE;
    handle->imageView = reinterpret_cast<VkImageView>(nativeTex);
    handle->owned     = false;
    return std::shared_ptr<TextureView>(new TextureView(handle));
}
