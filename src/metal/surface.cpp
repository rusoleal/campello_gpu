#include "Metal.hpp"
#include "common.hpp"
#include <campello_gpu/surface.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>

#include <mutex>

using namespace systems::leal::campello_gpu;

namespace {

void releaseDrawableLocked(MetalSurfaceData *data) {
    if (data->drawable) {
        data->drawable->release();
        data->drawable = nullptr;
    }
}

} // namespace

Surface::Surface(void *pd) : native(pd) {}

Surface::~Surface() {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data) return;
    {
        std::lock_guard<std::mutex> lock(data->mutex);
        releaseDrawableLocked(data);
        if (data->layer) data->layer->release();
    }
    delete data;
}

std::shared_ptr<Surface> Device::createSurface(void *nativeWindow, uint32_t width, uint32_t height) {
    if (!nativeWindow || !native) return nullptr;
    MetalAutoreleasePool pool;

    auto *deviceData = static_cast<MetalDeviceData *>(native);
    auto *layer      = static_cast<CA::MetalLayer *>(nativeWindow);

    // Drawables must come from this device, or render passes on our queue
    // can't target them. MTKView normally sets the system default device,
    // which is the same one, but don't rely on that.
    if (layer->device() != deviceData->device) {
        layer->setDevice(deviceData->device);
    }
    if (width > 0 && height > 0) {
        layer->setDrawableSize(CGSizeMake(width, height));
    }
    layer->retain();

    auto *data  = new MetalSurfaceData();
    data->layer = layer;
    return std::shared_ptr<Surface>(new Surface(data));
}

std::shared_ptr<TextureView> Surface::acquireNextTexture() {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !data->layer) return nullptr;
    MetalAutoreleasePool pool;

    std::lock_guard<std::mutex> lock(data->mutex);
    // A drawable acquired last frame but never presented (the frame was
    // abandoned) goes back to the pool here. Holding it would starve the
    // layer's small drawable pool (3 by default).
    releaseDrawableLocked(data);

    const CGSize size = data->layer->drawableSize();
    if (size.width < 1.0 || size.height < 1.0) return nullptr;

    auto *drawable = data->layer->nextDrawable();
    if (!drawable) return nullptr;
    drawable->retain();
    data->drawable = drawable;

    auto *texture = drawable->texture();
    if (!texture) {
        releaseDrawableLocked(data);
        return nullptr;
    }
    return TextureView::fromNative(texture);
}

bool Surface::schedulePresent(const std::shared_ptr<CommandBuffer> &commandBuffer) {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !commandBuffer || !commandBuffer->native) return false;
    MetalAutoreleasePool pool;

    std::lock_guard<std::mutex> lock(data->mutex);
    if (!data->drawable) return false;

    // presentDrawable: on the command buffer (before commit) presents at the
    // first vsync after the GPU finishes this buffer, the same guarantee
    // Device::submit()'s scheduleNextPresent path gives. The command buffer
    // retains the drawable, so we drop our reference now.
    auto *cmdBuf = static_cast<MTL::CommandBuffer *>(commandBuffer->native);
    cmdBuf->presentDrawable(data->drawable);
    releaseDrawableLocked(data);
    return true;
}

void Surface::resize(uint32_t width, uint32_t height) {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !data->layer || width == 0 || height == 0) return;
    MetalAutoreleasePool pool;
    std::lock_guard<std::mutex> lock(data->mutex);
    data->layer->setDrawableSize(CGSizeMake(width, height));
}

uint32_t Surface::getWidth() const noexcept {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !data->layer) return 0;
    return static_cast<uint32_t>(data->layer->drawableSize().width);
}

uint32_t Surface::getHeight() const noexcept {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !data->layer) return 0;
    return static_cast<uint32_t>(data->layer->drawableSize().height);
}

PixelFormat Surface::getPixelFormat() const noexcept {
    auto *data = static_cast<MetalSurfaceData *>(native);
    if (!data || !data->layer) return PixelFormat::invalid;
    // campello_gpu's PixelFormat values are MTLPixelFormat values
    // (see toMTLPixelFormat in common.hpp).
    return static_cast<PixelFormat>(data->layer->pixelFormat());
}
