#include "common.hpp"
#include <campello_gpu/texture_view.hpp>

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void* pd) : native(pd) {}

TextureView::~TextureView() {
    if (!native) return;
    auto* h = static_cast<TextureViewHandle*>(native);
    if (h->rtvExtraIndex != static_cast<UINT>(-1) && h->deviceData)
        h->deviceData->freeRtvExtraSlots({ h->rtvExtraIndex });
    delete h;
}

std::shared_ptr<TextureView> TextureView::fromNative(void* nativeTex) {
    // On DirectX the caller passes an ID3D12Resource*. Wrap it in a minimal
    // TextureViewHandle with no descriptor (the caller is responsible for
    // creating descriptors via Device::createTexture / Texture::createView).
    // A null nativeTex is a degenerate but legal input -- matching Vulkan/
    // Metal/WebGPU's fromNative(), the wrapper is still constructed; the
    // caller is responsible for not using it as a real attachment.
    auto* h    = new TextureViewHandle();
    h->format  = DXGI_FORMAT_UNKNOWN;
    // Store the raw resource pointer so callers can retrieve it if needed.
    // No cpu/gpu handles are filled — this is an interop-only view.
    return std::shared_ptr<TextureView>(new TextureView(h));
}
