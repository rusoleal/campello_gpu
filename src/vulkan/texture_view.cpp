#include <mutex>
#include <campello_gpu/texture_view.hpp>
#include "texture_view_handle.hpp"
#include "texture_handle.hpp"
#include "common.hpp"

using namespace systems::leal::campello_gpu;

TextureView::TextureView(void *pd) {
    this->native = pd;
}

TextureView::~TextureView() {
    auto data = (TextureViewHandle *)native;
    if (data->owned && data->imageView != VK_NULL_HANDLE) {
        // Deferred, not immediate -- see PendingTextureDestroy's doc
        // comment in common.hpp (same reasoning as Texture::~Texture()).
        // Uses data->deviceData (copied at creation time), NOT
        // data->ownerTexture->deviceData -- a TextureView's lifetime is
        // independent of its owning Texture's, so ownerTexture can already
        // be a dangling pointer (its TextureHandle deleted by
        // Texture::~Texture()) by the time THIS view is destroyed. See
        // deviceData's doc comment in texture_view_handle.hpp.
        if (data->deviceData) {
            DeviceData::PendingTextureDestroy pd;
            pd.view = data->imageView;
            std::lock_guard<std::mutex> lock(data->deviceData->gpu_mutex);
            data->deviceData->pendingTextureDestroys[data->deviceData->currentFrameGen].push_back(pd);
        } else {
            vkDestroyImageView(data->device, data->imageView, nullptr);
        }
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
