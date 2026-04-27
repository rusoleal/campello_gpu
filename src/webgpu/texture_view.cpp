#include <campello_gpu/texture_view.hpp>
#include "texture_view_handle.hpp"

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void* pd) {
    native = pd;
}

TextureView::~TextureView() {
    auto* handle = static_cast<TextureViewHandle*>(native);
    if (handle->ownsView && handle->view) {
        wgpuTextureViewRelease(handle->view);
    }
    delete handle;
}

std::shared_ptr<TextureView> TextureView::fromNative(void* nativeTex) {
    auto* handle = new TextureViewHandle();
    handle->view = static_cast<WGPUTextureView>(nativeTex);
    handle->ownsView = false;
    return std::shared_ptr<TextureView>(new TextureView(handle));
}
