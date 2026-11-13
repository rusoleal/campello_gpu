#include "common.hpp"
#include <campello_gpu/surface.hpp>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>

using namespace systems::leal::campello_gpu;

// DirectX 12 Surface: one flip-model IDXGISwapChain3 per HWND on the device's
// shared direct queue. See SurfaceData's doc comment in common.hpp for the
// per-frame protocol.

namespace {

constexpr DXGI_FORMAT kSurfaceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

std::shared_ptr<SurfaceData> &surfaceRef(void *native) {
    return *static_cast<std::shared_ptr<SurfaceData> *>(native);
}

#if !CAMPELLO_DX_UWP
// The size the swapchain should have right now: the explicit one if the
// caller gave one (createSurface()/resize()), else the window's client rect.
// 0x0 when the window is minimised or has no client area.
void targetSize(const SurfaceData *s, UINT &w, UINT &h) {
    if (s->requestedWidth && s->requestedHeight) {
        w = s->requestedWidth;
        h = s->requestedHeight;
        return;
    }
    RECT rect = {};
    if (!GetClientRect(s->hwnd, &rect)) { w = h = 0; return; }
    w = static_cast<UINT>(std::max<LONG>(rect.right - rect.left, 0));
    h = static_cast<UINT>(std::max<LONG>(rect.bottom - rect.top, 0));
}
#endif

} // namespace

bool SurfaceData::createBuffers() {
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBufferCount; ++i) {
        if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&buffers[i])))) {
            releaseBuffers();
            return false;
        }
        deviceData->device->CreateRenderTargetView(buffers[i], nullptr, rtv);

        TextureHandle &th = bufferHandles[i];
        th = TextureHandle{};
        th.resource       = buffers[i];  // not owned by the handle
        th.device         = deviceData->device;
        th.queue          = deviceData->queue;
        th.deviceData     = deviceData;
        th.format         = PixelFormat::rgba8unorm;
        th.width          = width;
        th.height         = height;
        th.usage          = TextureUsage::renderTarget;
        th.rtvHandle      = rtv;
        th.currentState   = D3D12_RESOURCE_STATE_PRESENT;
        th.isSurfaceImage = true;
        rtv.ptr += rtvDescSize;
    }
    return true;
}

void SurfaceData::releaseBuffers() {
    for (UINT i = 0; i < kBufferCount; ++i) {
        if (buffers[i]) { buffers[i]->Release(); buffers[i] = nullptr; }
        bufferHandles[i].resource = nullptr;
    }
}

SurfaceData::~SurfaceData() {
    // The last frame presented here may still be executing (e.g. a window
    // closed right after a frame). Once the frame ring retires the command
    // buffer that presented it, this is a no-op.
    if (deviceData) deviceData->waitForFenceValue(lastSubmitFence);
    acquiredView.reset();
    releaseBuffers();
    if (rtvHeap)   rtvHeap->Release();
    if (swapChain) swapChain->Release();
}

void systems::leal::campello_gpu::presentSurfaces(CommandBufferHandle *cb, UINT64 submitFenceValue) {
    for (auto &s : cb->surfacePresents) {
        if (!s || !s->swapChain) continue;
        std::lock_guard<std::mutex> lock(s->mutex);
        // Vsync'd, like the legacy swapchain. Present() is queued on the same
        // queue, after the command list that rendered the image.
        const HRESULT hr = s->swapChain->Present(1, 0);
        s->occluded        = (hr == DXGI_STATUS_OCCLUDED);
        s->lastSubmitFence = submitFenceValue;
    }
}

Surface::Surface(void *pd) : native(pd) {}

Surface::~Surface() {
    if (!native) return;
    auto *ref = static_cast<std::shared_ptr<SurfaceData> *>(native);
    {
        std::lock_guard<std::mutex> lock((*ref)->mutex);
        (*ref)->acquired = false;
        (*ref)->acquiredView.reset();
    }
    // Command buffers holding a pending present keep the SurfaceData alive.
    delete ref;
}

std::shared_ptr<Surface> Device::createSurface(void *pd, uint32_t width, uint32_t height) {
#if CAMPELLO_DX_UWP
    (void)pd; (void)width; (void)height;
    return nullptr;  // UWP has no HWND; use createDefaultDeviceForCoreWindow()
#else
    if (!pd) return nullptr;
    HWND hwnd = static_cast<HWND>(pd);
    if (!IsWindow(hwnd)) return nullptr;
    auto *d = static_cast<DeviceData *>(native);

    auto s = std::make_shared<SurfaceData>();
    s->deviceData      = d;
    s->hwnd            = hwnd;
    s->requestedWidth  = width;
    s->requestedHeight = height;

    UINT w = 0, h = 0;
    targetSize(s.get(), w, h);

    IDXGIFactory6 *factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return nullptr;

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width            = std::max<UINT>(w, 1u);
    scd.Height           = std::max<UINT>(h, 1u);
    scd.Format           = kSurfaceFormat;
    scd.BufferCount      = SurfaceData::kBufferCount;
    scd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    scd.Scaling          = DXGI_SCALING_STRETCH;
    scd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;

    IDXGISwapChain1 *sc1 = nullptr;
    // Fails (DXGI_ERROR_INVALID_CALL / E_ACCESSDENIED) if this HWND already
    // has a flip-model swapchain, e.g. the legacy createDefaultDevice(hwnd) one.
    HRESULT hr = factory->CreateSwapChainForHwnd(d->queue, hwnd, &scd, nullptr, nullptr, &sc1);
    if (SUCCEEDED(hr)) {
        // Alt+Enter would switch the swapchain to exclusive fullscreen behind
        // the app's back; windows go fullscreen through the app instead.
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    factory->Release();
    if (FAILED(hr) || !sc1) return nullptr;

    hr = sc1->QueryInterface(IID_PPV_ARGS(&s->swapChain));
    sc1->Release();
    if (FAILED(hr)) return nullptr;
    s->width  = scd.Width;
    s->height = scd.Height;

    D3D12_DESCRIPTOR_HEAP_DESC rd = {};
    rd.NumDescriptors = SurfaceData::kBufferCount;
    rd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(d->device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&s->rtvHeap)))) return nullptr;
    s->rtvDescSize = d->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    if (!s->createBuffers()) return nullptr;

    auto *ref = new std::shared_ptr<SurfaceData>(std::move(s));
    return std::shared_ptr<Surface>(new Surface(ref));
#endif
}

std::shared_ptr<TextureView> Surface::acquireNextTexture() {
#if CAMPELLO_DX_UWP
    return nullptr;
#else
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    if (!s->swapChain) return nullptr;
    if (s->acquired) return s->acquiredView;  // abandoned last frame: reuse it

    if (!IsWindow(s->hwnd) || IsIconic(s->hwnd)) return nullptr;
    UINT w = 0, h = 0;
    targetSize(s, w, h);
    if (w == 0 || h == 0) return nullptr;  // minimised / zero-sized: skip

    if (w != s->width || h != s->height) {
        // ResizeBuffers needs every back buffer free of GPU work and of
        // references. Resizes are rare (window drags), so a full idle wait.
        s->deviceData->waitForGpu();
        s->releaseBuffers();
        const HRESULT hr = s->swapChain->ResizeBuffers(SurfaceData::kBufferCount, w, h,
                                                       kSurfaceFormat, 0);
        if (FAILED(hr)) {
            s->createBuffers();  // keep the old size
            return nullptr;
        }
        s->width  = w;
        s->height = h;
        if (!s->createBuffers()) return nullptr;
    }

    const UINT index = s->swapChain->GetCurrentBackBufferIndex();
    if (index >= SurfaceData::kBufferCount || !s->buffers[index]) return nullptr;

    auto *vh         = new TextureViewHandle();
    vh->cpuHandle    = s->bufferHandles[index].rtvHandle;
    vh->format       = kSurfaceFormat;
    vh->viewType     = ViewType::RTV;
    vh->sourceHandle = &s->bufferHandles[index];
    vh->deviceData   = s->deviceData;

    s->acquired      = true;
    s->acquiredIndex = index;
    s->acquiredView  = std::shared_ptr<TextureView>(new TextureView(vh));
    return s->acquiredView;
#endif
}

bool Surface::schedulePresent(const std::shared_ptr<CommandBuffer> &commandBuffer) {
    if (!commandBuffer || !commandBuffer->native) return false;
    auto &ref = surfaceRef(native);
    std::lock_guard<std::mutex> lock(ref->mutex);
    if (!ref->acquired) return false;

    auto *cb = static_cast<CommandBufferHandle *>(commandBuffer->native);
    cb->surfacePresents.push_back(ref);
    ref->acquired = false;
    ref->acquiredView.reset();
    return true;
}

void Surface::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    s->requestedWidth  = width;
    s->requestedHeight = height;
}

uint32_t Surface::getWidth() const noexcept {
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    return s->width;
}

uint32_t Surface::getHeight() const noexcept {
    SurfaceData *s = surfaceRef(native).get();
    std::lock_guard<std::mutex> lock(s->mutex);
    return s->height;
}

PixelFormat Surface::getPixelFormat() const noexcept {
    return PixelFormat::rgba8unorm;
}
