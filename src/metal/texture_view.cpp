#include "Metal.hpp"
#include <campello_gpu/texture_view.hpp>

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void *pd) : native(pd) {}

TextureView::~TextureView() {
    if (native != nullptr) {
        static_cast<MTL::Texture *>(native)->release();
    }
}

std::shared_ptr<TextureView> TextureView::fromNative(void *nativeTex) {
    auto *tex = static_cast<MTL::Texture *>(nativeTex);
    tex->retain();
    return std::shared_ptr<TextureView>(new TextureView(tex));
}
