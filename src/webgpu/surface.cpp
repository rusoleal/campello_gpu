#include <webgpu/webgpu.h>
#include <emscripten/html5.h>

#include <campello_gpu/surface.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>
#include "common.hpp"

#include <string>

using namespace systems::leal::campello_gpu;

namespace {

// One per Surface: a WGPUSurface (GPUCanvasContext) on its own <canvas>,
// configured with the shared device. Any number of canvases can share one
// device; that is the web's version of several windows.
//
// Presentation is implicit on the web: the browser shows a canvas's current
// texture when the task that rendered it returns. So schedulePresent() is
// pure bookkeeping here; every submitted frame is presented, and a canvas
// never shows another canvas's texture, because each GPUCanvasContext only
// ever hands out its own.
struct WebGPUSurfaceData {
    WebGPUDeviceData* deviceData = nullptr;
    WGPUSurface       surface    = nullptr;
    std::string       selector;  // CSS selector, e.g. "#canvas2"
    WGPUTextureFormat format     = WGPUTextureFormat_Undefined;
    bool              configured = false;
    uint32_t          width      = 0;  // configured drawing-buffer size
    uint32_t          height     = 0;
    bool              acquired   = false;
};

// The canvas drawing-buffer size (canvas.width/height), or 0x0 if the
// selector doesn't resolve.
void canvasSize(const std::string& selector, uint32_t& w, uint32_t& h) {
    int iw = 0, ih = 0;
    if (emscripten_get_canvas_element_size(selector.c_str(), &iw, &ih) != EMSCRIPTEN_RESULT_SUCCESS) {
        iw = ih = 0;
    }
    w = static_cast<uint32_t>(iw > 0 ? iw : 0);
    h = static_cast<uint32_t>(ih > 0 ? ih : 0);
}

void configure(WebGPUSurfaceData* s, uint32_t w, uint32_t h) {
    WGPUSurfaceConfiguration config{};
    config.device      = s->deviceData->device;
    config.format      = s->format;
    config.usage       = WGPUTextureUsage_RenderAttachment;
    config.width       = w;
    config.height      = h;
    config.alphaMode   = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(s->surface, &config);
    s->configured = true;
    s->width      = w;
    s->height     = h;
}

} // namespace

Surface::Surface(void* pd) : native(pd) {}

Surface::~Surface() {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s) return;
    if (s->surface) {
        if (s->configured) wgpuSurfaceUnconfigure(s->surface);
        wgpuSurfaceRelease(s->surface);
    }
    delete s;
}

// `nativeWindow` is a CSS selector for the target <canvas> (e.g. "#canvas2").
// width/height > 0 also set the canvas drawing-buffer size; 0 keeps the
// canvas's current width/height attributes.
std::shared_ptr<Surface> Device::createSurface(void* nativeWindow, uint32_t width, uint32_t height) {
    auto* dd = static_cast<WebGPUDeviceData*>(native);
    if (!nativeWindow || !dd || !dd->device) return nullptr;
    const std::string selector = static_cast<const char*>(nativeWindow);

    // Fail cleanly for a selector that matches no canvas: the WebGPU surface
    // would be created anyway and only fail later, at configure time.
    int iw = 0, ih = 0;
    if (emscripten_get_canvas_element_size(selector.c_str(), &iw, &ih) != EMSCRIPTEN_RESULT_SUCCESS) {
        return nullptr;
    }
    if (width > 0 && height > 0) {
        emscripten_set_canvas_element_size(selector.c_str(), (int)width, (int)height);
    }

    auto* s       = new WebGPUSurfaceData();
    s->deviceData = dd;
    s->selector   = selector;

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source{};
    source.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    source.selector    = toStringView(s->selector);
    WGPUSurfaceDescriptor desc{};
    desc.nextInChain = &source.chain;
    s->surface = wgpuInstanceCreateSurface(dd->instance, &desc);
    if (!s->surface) {
        delete s;
        return nullptr;
    }

    // The browser's preferred canvas format comes first
    // (navigator.gpu.getPreferredCanvasFormat()), as for the device's own
    // canvas in Device::createDevice().
    WGPUSurfaceCapabilities caps{};
    if (dd->adapter &&
        wgpuSurfaceGetCapabilities(s->surface, dd->adapter, &caps) == WGPUStatus_Success &&
        caps.formatCount > 0) {
        s->format = caps.formats[0];
    } else {
        s->format = WGPUTextureFormat_BGRA8Unorm;
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    uint32_t w = 0, h = 0;
    canvasSize(s->selector, w, h);
    if (w > 0 && h > 0) configure(s, w, h);
    return std::shared_ptr<Surface>(new Surface(s));
}

std::shared_ptr<TextureView> Surface::acquireNextTexture() {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s || !s->surface) return nullptr;

    // Follow the canvas: page code (CSS layout, devicePixelRatio handling)
    // may have changed canvas.width/height since the last frame.
    uint32_t w = 0, h = 0;
    canvasSize(s->selector, w, h);
    if (w == 0 || h == 0) return nullptr;  // zero-sized / detached canvas
    if (!s->configured || w != s->width || h != s->height) configure(s, w, h);

    // Within one task this returns the same texture again, so an abandoned
    // acquisition is naturally reused; in a later task it's a fresh one.
    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(s->surface, &st);
    if (!st.texture) return nullptr;
    WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
    wgpuTextureRelease(st.texture);  // the view keeps it alive
    if (!view) return nullptr;

    auto* handle     = new TextureViewHandle();
    handle->view     = view;
    handle->ownsView = true;
    handle->format   = s->format;
    s->acquired = true;
    return std::shared_ptr<TextureView>(new TextureView(handle));
}

bool Surface::schedulePresent(const std::shared_ptr<CommandBuffer>& commandBuffer) {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s || !commandBuffer || !s->acquired) return false;
    // Nothing to attach: the browser presents the canvas after this task.
    s->acquired = false;
    return true;
}

void Surface::resize(uint32_t width, uint32_t height) {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s || width == 0 || height == 0) return;
    // On the web the canvas drawing buffer *is* the swapchain size; the next
    // acquireNextTexture() sees the new size and reconfigures.
    emscripten_set_canvas_element_size(s->selector.c_str(), (int)width, (int)height);
}

uint32_t Surface::getWidth() const noexcept {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s) return 0;
    if (s->configured) return s->width;
    uint32_t w = 0, h = 0;
    canvasSize(s->selector, w, h);
    return w;
}

uint32_t Surface::getHeight() const noexcept {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    if (!s) return 0;
    if (s->configured) return s->height;
    uint32_t w = 0, h = 0;
    canvasSize(s->selector, w, h);
    return h;
}

PixelFormat Surface::getPixelFormat() const noexcept {
    auto* s = static_cast<WebGPUSurfaceData*>(native);
    return s ? wgpuFormatToPixelFormat(s->format) : PixelFormat::invalid;
}
