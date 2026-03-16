#include "common.hpp"
#include <campello_gpu/texture_view.hpp>

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void* pd) : native(pd) {}

TextureView::~TextureView() {
    if (!native) return;
    delete static_cast<TextureViewHandle*>(native);
}

std::shared_ptr<TextureView> TextureView::fromNative(void* nativeTex) {
    if (!nativeTex) return nullptr;
    // On DirectX the caller passes an ID3D12Resource*. Wrap it in a minimal
    // TextureViewHandle with no descriptor (the caller is responsible for
    // creating descriptors via Device::createTexture / Texture::createView).
    auto* h    = new TextureViewHandle();
    h->format  = DXGI_FORMAT_UNKNOWN;
    // Store the raw resource pointer so callers can retrieve it if needed.
    // No cpu/gpu handles are filled — this is an interop-only view.
    return std::shared_ptr<TextureView>(new TextureView(h));
}
