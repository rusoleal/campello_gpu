// Surface: one presentable swapchain per native window, many per Device.
//
// On Metal, a Surface wraps a CAMetalLayer. Off-screen layers (no window or
// view) are enough to exercise the full acquire, render, schedulePresent,
// submit cycle with real CAMetalDrawable objects, the same way
// test_device_present_race.cpp does.
//
// Other backends return nullptr from Device::createSurface() until their
// implementations land (campello_widgets TODO.md, Phase 17 M0.2-M0.4). Those
// tests check that contract and skip the rest.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/surface.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>

#if defined(__APPLE__)
#include "../../src/metal/Metal.hpp"
#include "../../src/metal/common.hpp"
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

using namespace systems::leal::campello_gpu;

namespace {

std::shared_ptr<Device> makeDevice() {
    return Device::createDefaultDevice(nullptr);
}

#if defined(__APPLE__)
// Off-screen layer, sized and formatted like an MTKView's.
CA::MetalLayer *makeLayer(uint32_t w, uint32_t h) {
    auto *layer = CA::MetalLayer::layer();
    layer->retain();
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    layer->setDrawableSize(CGSizeMake(w, h));
    return layer;
}

// Clear `view` to `rgba` and present it through `surface` on a fresh
// command buffer. Returns false if any step fails.
bool clearAndPresent(Device &device, Surface &surface,
                     const std::shared_ptr<TextureView> &view,
                     std::shared_ptr<Fence> fence, const float rgba[4]) {
    auto encoder = device.createCommandEncoder();
    if (!encoder) return false;

    BeginRenderPassDescriptor rp{};
    ColorAttachment ca{};
    ca.view    = view;
    ca.loadOp  = LoadOp::clear;
    ca.storeOp = StoreOp::store;
    for (int i = 0; i < 4; ++i) ca.clearValue[i] = rgba[i];
    rp.colorAttachments.push_back(ca);

    auto pass = encoder->beginRenderPass(rp);
    if (!pass) return false;
    pass->end();

    auto cmd = encoder->finish();
    if (!cmd) return false;
    if (!surface.schedulePresent(cmd)) return false;
    device.submit(cmd, fence);
    return true;
}
#endif

} // namespace

TEST(Surface, CreateWithNullHandleReturnsNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    EXPECT_EQ(device->createSurface(nullptr), nullptr);
}

#if defined(__APPLE__)

TEST(Surface, ReportsLayerSizeAndFormat) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer = makeLayer(64, 32);

    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 32u);
    EXPECT_EQ(surface->getPixelFormat(), PixelFormat::bgra8unorm);

    surface->resize(128, 96);
    EXPECT_EQ(surface->getWidth(), 128u);
    EXPECT_EQ(surface->getHeight(), 96u);

    surface->resize(0, 50);  // ignored
    EXPECT_EQ(surface->getWidth(), 128u);

    surface.reset();
    layer->release();
}

TEST(Surface, ExplicitSizeAtCreationOverridesLayer) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer = makeLayer(8, 8);
    auto surface = device->createSurface(layer, 200, 100);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 200u);
    EXPECT_EQ(surface->getHeight(), 100u);
    surface.reset();
    layer->release();
}

TEST(Surface, SchedulePresentWithoutAcquireIsNoop) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer  = makeLayer(16, 16);
    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);

    auto encoder = device->createCommandEncoder();
    auto cmd     = encoder->finish();
    EXPECT_FALSE(surface->schedulePresent(cmd));
    device->submit(cmd);

    surface.reset();
    layer->release();
}

TEST(Surface, AcquireRenderPresentCycle) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer  = makeLayer(32, 32);
    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);

    auto fence = device->createFence();
    const float red[4] = {1, 0, 0, 1};
    // More frames than the layer's drawable pool (3) holds. Note that an
    // off-screen layer doesn't enforce its pool (nextDrawable() never blocks
    // there), so this checks the protocol, not drawable recycling. Leaks are
    // covered by the weak-reference tests below.
    for (int frame = 0; frame < 10; ++frame) {
        MetalAutoreleasePool framePool;
        auto view = surface->acquireNextTexture();
        ASSERT_NE(view, nullptr) << "frame " << frame;
        ASSERT_TRUE(clearAndPresent(*device, *surface, view, fence, red));
        fence->wait();
        // The present was consumed: a second schedulePresent has nothing to present.
        auto enc = device->createCommandEncoder();
        auto cmd = enc->finish();
        EXPECT_FALSE(surface->schedulePresent(cmd));
        device->submit(cmd);
    }

    surface.reset();
    layer->release();
}

// An off-screen CAMetalLayer doesn't enforce its drawable pool, so a leaked
// drawable never shows up as nextDrawable() stalling. The test watches the
// drawable object directly instead, with an Objective-C weak reference: it
// must be deallocated once the surface lets go of it. (The drawable's
// texture can't be used for this, because the layer pools textures and they
// outlive their drawables.)
namespace {
extern "C" void *objc_initWeak(void **location, void *obj);
extern "C" void *objc_loadWeakRetained(void **location);
extern "C" void  objc_destroyWeak(void **location);

// Weak handle to the drawable a surface currently holds.
struct WeakDrawable {
    void *slot = nullptr;
    explicit WeakDrawable(Surface &surface) {
        // Surface is non-polymorphic with a single `void *native` member
        // (MetalSurfaceData* on Metal). There is no public accessor; test-only.
        auto *data = *reinterpret_cast<MetalSurfaceData **>(&surface);
        std::lock_guard<std::mutex> lock(data->mutex);
        objc_initWeak(&slot, data->drawable);
    }
    ~WeakDrawable() { objc_destroyWeak(&slot); }
    bool alive() {
        MetalAutoreleasePool p;
        void *strong = objc_loadWeakRetained(&slot);
        if (!strong) return false;
        static_cast<NS::Object *>(strong)->release();
        return true;
    }
};
} // namespace

TEST(Surface, AbandonedAcquireIsReleasedOnNextAcquire) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer  = makeLayer(16, 16);
    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);

    std::unique_ptr<WeakDrawable> first;
    {
        MetalAutoreleasePool framePool;
        auto view = surface->acquireNextTexture();
        ASSERT_NE(view, nullptr);
        first = std::make_unique<WeakDrawable>(*surface);
    }
    EXPECT_TRUE(first->alive()) << "surface should hold the acquired drawable";

    {
        MetalAutoreleasePool framePool;
        auto view = surface->acquireNextTexture();  // abandons the first one
        ASSERT_NE(view, nullptr);
    }
    EXPECT_FALSE(first->alive()) << "abandoned drawable leaked";

    first.reset();
    surface.reset();
    layer->release();
}

TEST(Surface, DestructorReleasesHeldDrawable) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer  = makeLayer(16, 16);
    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);

    std::unique_ptr<WeakDrawable> held;
    {
        MetalAutoreleasePool framePool;
        auto view = surface->acquireNextTexture();
        ASSERT_NE(view, nullptr);
        held = std::make_unique<WeakDrawable>(*surface);
    }
    ASSERT_TRUE(held->alive());
    surface.reset();  // window closed mid-frame
    EXPECT_FALSE(held->alive()) << "drawable leaked by ~Surface";

    held.reset();
    layer->release();
}

TEST(Surface, ZeroSizedLayerAcquiresNothing) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;
    auto *layer = CA::MetalLayer::layer();
    layer->retain();
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    // No setDrawableSize: a fresh layer with no bounds has a 0x0 drawable
    // size, like a minimised window. (CAMetalLayer ignores an explicit
    // setDrawableSize(0, 0), so it can't be used to model that.)

    auto surface = device->createSurface(layer);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->acquireNextTexture(), nullptr);

    surface.reset();
    layer->release();
}

// The core multi-window case: two windows on one Device, each driven by its
// own raster thread. With the device-wide scheduleNextPresent() slot, one
// thread's submit() could present the other window's drawable (or none at
// all). With per-surface presents, each window always presents exactly its
// own drawables. Like test_device_present_race.cpp, this is most conclusive
// under ThreadSanitizer.
TEST(Surface, TwoSurfacesOnTwoThreadsPresentIndependently) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    MetalAutoreleasePool pool;

    auto *layerA = makeLayer(48, 48);
    auto *layerB = makeLayer(24, 64);
    auto surfaceA = device->createSurface(layerA);
    auto surfaceB = device->createSurface(layerB);
    ASSERT_NE(surfaceA, nullptr);
    ASSERT_NE(surfaceB, nullptr);

    constexpr int kFrames = 200;
    std::atomic<int> presentedA{0}, presentedB{0};
    std::atomic<bool> failed{false};

    auto rasterLoop = [&](Surface &surface, CA::MetalLayer *layer,
                          std::atomic<int> &presented, const float rgba[4]) {
        auto fence = device->createFence();
        for (int i = 0; i < kFrames && !failed; ++i) {
            MetalAutoreleasePool framePool;
            auto view = surface.acquireNextTexture();
            if (!view) { failed = true; return; }
            // The acquired image really is this window's: it matches this
            // layer's drawable size, not the other window's.
            // TextureView is a non-polymorphic class whose only member is
            // `void *native` (the MTL::Texture* on Metal). There is no public
            // accessor, so read it directly; test-only.
            auto *tex = static_cast<MTL::Texture *>(*reinterpret_cast<void **>(view.get()));
            if (tex->width()  != static_cast<NS::UInteger>(layer->drawableSize().width) ||
                tex->height() != static_cast<NS::UInteger>(layer->drawableSize().height)) {
                failed = true; return;
            }
            if (!clearAndPresent(*device, surface, view, fence, rgba)) { failed = true; return; }
            fence->wait();
            ++presented;
        }
    };

    const float red[4]  = {1, 0, 0, 1};
    const float blue[4] = {0, 0, 1, 1};
    std::thread a([&] { rasterLoop(*surfaceA, layerA, presentedA, red); });
    std::thread b([&] { rasterLoop(*surfaceB, layerB, presentedB, blue); });
    a.join();
    b.join();

    EXPECT_FALSE(failed);
    EXPECT_EQ(presentedA.load(), kFrames);
    EXPECT_EQ(presentedB.load(), kFrames);

    surfaceA.reset();
    surfaceB.reset();
    layerA->release();
    layerB->release();
}

#elif defined(__ANDROID__)

// Vulkan on Android. Each "window" is an AImageReader's ANativeWindow: a real
// BufferQueue producer, so the swapchain, acquire and present go through the
// platform's actual WSI path, with no Activity needed. Every presented frame
// arrives in the reader, so these tests check real pixels, not just return
// codes.

#include <media/NdkImageReader.h>
#include <android/hardware_buffer.h>
#include <chrono>

namespace {

struct ImageReaderWindow {
    AImageReader  *reader = nullptr;
    ANativeWindow *window = nullptr;  // owned by reader

    ImageReaderWindow(int32_t w, int32_t h) {
        // GPU_COLOR_OUTPUT so the WSI can render into the buffers;
        // CPU_READ_OFTEN so the test can lock and read them.
        if (AImageReader_newWithUsage(w, h, AIMAGE_FORMAT_RGBA_8888,
                AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT | AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                /*maxImages=*/4, &reader) == AMEDIA_OK) {
            AImageReader_getWindow(reader, &window);
        }
    }
    ~ImageReaderWindow() { if (reader) AImageReader_delete(reader); }

    // Waits for the newest presented frame and returns its centre pixel as
    // 0xRRGGBBAA, or 0 on timeout. Also drains the queue, so the swapchain
    // never stalls waiting for free buffers.
    uint32_t latestCentrePixel(int timeoutMs = 2000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            AImage *img = nullptr;
            if (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
                int32_t w = 0, h = 0, stride = 0, len = 0;
                uint8_t *data = nullptr;
                AImage_getWidth(img, &w);
                AImage_getHeight(img, &h);
                AImage_getPlaneRowStride(img, 0, &stride);
                AImage_getPlaneData(img, 0, &data, &len);
                const uint8_t *px = data + (h / 2) * stride + (w / 2) * 4;
                uint32_t rgba = (uint32_t(px[0]) << 24) | (uint32_t(px[1]) << 16) |
                                (uint32_t(px[2]) << 8)  |  uint32_t(px[3]);
                AImage_delete(img);
                return rgba;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return 0;
    }

    void drain() {
        AImage *img = nullptr;
        while (AImageReader_acquireLatestImage(reader, &img) == AMEDIA_OK && img) {
            AImage_delete(img);
            img = nullptr;
        }
    }
};

std::shared_ptr<CommandBuffer> clearPass(Device &device, const std::shared_ptr<TextureView> &view,
                                         float r, float g, float b) {
    auto encoder = device.createCommandEncoder();
    if (!encoder) return nullptr;
    BeginRenderPassDescriptor rp{};
    ColorAttachment ca{};
    ca.view    = view;
    ca.loadOp  = LoadOp::clear;
    ca.storeOp = StoreOp::store;
    ca.clearValue[0] = r; ca.clearValue[1] = g; ca.clearValue[2] = b; ca.clearValue[3] = 1;
    rp.colorAttachments.push_back(ca);
    auto pass = encoder->beginRenderPass(rp);
    if (!pass) return nullptr;
    pass->end();
    return encoder->finish();
}

bool presentClear(Device &device, Surface &surface, float r, float g, float b,
                  std::shared_ptr<Fence> fence = nullptr) {
    auto view = surface.acquireNextTexture();
    if (!view) return false;
    auto cmd = clearPass(device, view, r, g, b);
    if (!cmd || !surface.schedulePresent(cmd)) return false;
    if (fence) device.submit(cmd, fence); else device.submit(cmd);
    return true;
}

constexpr uint32_t kRed  = 0xFF0000FFu;
constexpr uint32_t kBlue = 0x0000FFFFu;
constexpr uint32_t kGreen = 0x00FF00FFu;

} // namespace

TEST(Surface, CreateFromNativeWindowReportsWindowSize) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(64, 48);
    ASSERT_NE(win.window, nullptr);

    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 48u);
    auto fmt = surface->getPixelFormat();
    EXPECT_TRUE(fmt == PixelFormat::rgba8unorm || fmt == PixelFormat::bgra8unorm)
        << "format " << (int)fmt;
}

TEST(Surface, PresentDeliversRenderedPixels) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(32, 32);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);

    auto fence = device->createFence();
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0, fence));
    fence->wait();
    EXPECT_EQ(win.latestCentrePixel(), kRed);

    // And the fence-less submit() overload.
    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(win.latestCentrePixel(), kGreen);
}

TEST(Surface, SchedulePresentWithoutAcquireIsNoop) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(16, 16);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);
    auto encoder = device->createCommandEncoder();
    auto cmd = encoder->finish();
    EXPECT_FALSE(surface->schedulePresent(cmd));
    device->submit(cmd);
}

TEST(Surface, AbandonedAcquireIsReturnedAgain) {
    // Vulkan can't hand back an acquired image without presenting it, so an
    // abandoned acquisition is reused by the next acquireNextTexture().
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(16, 16);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);

    auto first  = surface->acquireNextTexture();
    ASSERT_NE(first, nullptr);
    auto second = surface->acquireNextTexture();
    EXPECT_EQ(first.get(), second.get());

    // ...and it still presents normally afterwards.
    auto cmd = clearPass(*device, second, 0, 0, 1);
    ASSERT_NE(cmd, nullptr);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    device->submit(cmd);
    EXPECT_EQ(win.latestCentrePixel(), kBlue);
}

TEST(Surface, ManyFramesCycleSlotsWithoutStalling) {
    // Far more frames than acquire slots (3) and swapchain images: exercises
    // slot-fence reuse and semaphore recycling. A sync bug shows up as a
    // 3-second acquire timeout (nullptr) or a validation error.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(32, 32);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i) {
        const bool red = (i % 2) == 0;
        ASSERT_TRUE(presentClear(*device, *surface, red ? 1 : 0, 0, red ? 0 : 1)) << "frame " << i;
        win.drain();
    }
    device->waitForIdle();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(ms, 5000) << "frames are stalling";
    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(win.latestCentrePixel(), kGreen);
}

TEST(Surface, ResizeRebuildsSwapchainAndKeepsPresenting) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(32, 32);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(win.latestCentrePixel(), kRed);

    // An ImageReader window's buffer size is fixed, so the extent stays
    // 32x32 (Android reports currentExtent), but the swapchain is rebuilt.
    surface->resize(64, 64);
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(win.latestCentrePixel(), kBlue);
    EXPECT_EQ(surface->getWidth(), 32u);
}

TEST(Surface, TwoSurfacesPresentTheirOwnContent) {
    // Two windows on one device, interleaved frame by frame: each window must
    // only ever receive its own colour.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow winA(32, 32), winB(48, 24);
    auto surfA = device->createSurface(winA.window);
    auto surfB = device->createSurface(winB.window);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    for (int i = 0; i < 30; ++i) {
        ASSERT_TRUE(presentClear(*device, *surfA, 1, 0, 0)) << "A frame " << i;
        ASSERT_TRUE(presentClear(*device, *surfB, 0, 0, 1)) << "B frame " << i;
        EXPECT_EQ(winA.latestCentrePixel(), kRed)  << "A frame " << i;
        EXPECT_EQ(winB.latestCentrePixel(), kBlue) << "B frame " << i;
    }
}

TEST(Surface, OneCommandBufferPresentsTwoSurfaces) {
    // Both windows drawn in the same command buffer: one submit, one
    // vkQueuePresentKHR covering both swapchains.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow winA(32, 32), winB(32, 32);
    auto surfA = device->createSurface(winA.window);
    auto surfB = device->createSurface(winB.window);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    for (int i = 0; i < 10; ++i) {
        auto viewA = surfA->acquireNextTexture();
        auto viewB = surfB->acquireNextTexture();
        ASSERT_NE(viewA, nullptr);
        ASSERT_NE(viewB, nullptr);
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        for (auto [view, r, b] : {std::tuple{viewA, 1.f, 0.f}, std::tuple{viewB, 0.f, 1.f}}) {
            BeginRenderPassDescriptor rp{};
            ColorAttachment ca{};
            ca.view = view; ca.loadOp = LoadOp::clear; ca.storeOp = StoreOp::store;
            ca.clearValue[0] = r; ca.clearValue[1] = 0; ca.clearValue[2] = b; ca.clearValue[3] = 1;
            rp.colorAttachments.push_back(ca);
            auto pass = encoder->beginRenderPass(rp);
            ASSERT_NE(pass, nullptr);
            pass->end();
        }
        auto cmd = encoder->finish();
        ASSERT_TRUE(surfA->schedulePresent(cmd));
        ASSERT_TRUE(surfB->schedulePresent(cmd));
        device->submit(cmd);
        EXPECT_EQ(winA.latestCentrePixel(), kRed)  << "frame " << i;
        EXPECT_EQ(winB.latestCentrePixel(), kBlue) << "frame " << i;
    }
}

TEST(Surface, DestroyingSurfaceWithPendingPresentIsSafe) {
    // A window closed between schedulePresent() and submit(): the command
    // buffer keeps the swapchain alive until it's been presented.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow win(16, 16);
    auto surface = device->createSurface(win.window);
    ASSERT_NE(surface, nullptr);
    auto view = surface->acquireNextTexture();
    ASSERT_NE(view, nullptr);
    auto cmd = clearPass(*device, view, 1, 0, 0);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    view.reset();
    surface.reset();
    device->submit(cmd);
    device->waitForIdle();
    EXPECT_EQ(win.latestCentrePixel(), kRed);
}

#elif defined(__EMSCRIPTEN__)

// WebGPU: a Surface is a GPUCanvasContext on its own <canvas>, and several
// canvases share one device (the web's version of several windows). The
// browser presents each canvas when the task returns.
//
// Pixel checks draw the WebGPU canvas into a 2D canvas *in the same task as
// the submit*: that samples exactly the texture this canvas is about to
// present. They can't run after the yield: once presented, a WebGPU canvas's
// drawing buffer reads back as transparent black (like WebGL without
// preserveDrawingBuffer; verified in headless Chrome: ff0000ff before the
// yield, 00000000 after). The tests still yield between frames, so every
// frame gets a fresh canvas texture.

#include <emscripten.h>
#include <string>

namespace {

// Creates (or recreates) <canvas id=...> of the given drawing-buffer size and
// returns its CSS selector.
std::string makeCanvas(const char* id, int w, int h) {
    EM_ASM({
        var id = UTF8ToString($0);
        var old = document.getElementById(id);
        if (old) old.remove();
        var c = document.createElement('canvas');
        c.id = id; c.width = $1; c.height = $2;
        document.body.appendChild(c);
    }, id, w, h);
    return std::string("#") + id;
}

void removeCanvas(const char* id) {
    EM_ASM({ var c = document.getElementById(UTF8ToString($0)); if (c) c.remove(); }, id);
}

// Centre pixel of what the canvas currently shows, as 0xRRGGBBAA.
uint32_t centrePixel(const std::string& selector) {
    return (uint32_t)EM_ASM_INT({
        var c = document.querySelector(UTF8ToString($0));
        if (!c || !c.width || !c.height) return 0;
        var t = document.createElement('canvas');
        t.width = c.width; t.height = c.height;
        var g = t.getContext('2d');
        g.drawImage(c, 0, 0);
        var d = g.getImageData(c.width >> 1, c.height >> 1, 1, 1).data;
        return ((d[0] << 24) | (d[1] << 16) | (d[2] << 8) | d[3]) >>> 0;
    }, selector.c_str());
}

void setCanvasAttrSize(const std::string& selector, int w, int h) {
    EM_ASM({ var c = document.querySelector(UTF8ToString($0)); c.width = $1; c.height = $2; },
           selector.c_str(), w, h);
}

std::shared_ptr<CommandBuffer> clearPass(Device& device, const std::shared_ptr<TextureView>& view,
                                         float r, float g, float b) {
    auto encoder = device.createCommandEncoder();
    if (!encoder) return nullptr;
    BeginRenderPassDescriptor rp{};
    ColorAttachment ca{};
    ca.view = view; ca.loadOp = LoadOp::clear; ca.storeOp = StoreOp::store;
    ca.clearValue[0] = r; ca.clearValue[1] = g; ca.clearValue[2] = b; ca.clearValue[3] = 1;
    rp.colorAttachments.push_back(ca);
    auto pass = encoder->beginRenderPass(rp);
    if (!pass) return nullptr;
    pass->end();
    return encoder->finish();
}

bool presentClear(Device& device, Surface& surface, float r, float g, float b) {
    auto view = surface.acquireNextTexture();
    if (!view) return false;
    auto cmd = clearPass(device, view, r, g, b);
    if (!cmd || !surface.schedulePresent(cmd)) return false;
    device.submit(cmd);
    return true;
}

// Let the browser finish the task: presents the canvases, and the next
// acquireNextTexture() gets a fresh texture.
void yieldToBrowser() { emscripten_sleep(50); }

constexpr uint32_t kRed   = 0xFF0000FFu;
constexpr uint32_t kGreen = 0x00FF00FFu;
constexpr uint32_t kBlue  = 0x0000FFFFu;

} // namespace

TEST(Surface, CreateOnCanvasReportsCanvasSize) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_size", 64, 48);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()));
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 48u);
    auto fmt = surface->getPixelFormat();
    EXPECT_TRUE(fmt == PixelFormat::bgra8unorm || fmt == PixelFormat::rgba8unorm) << (int)fmt;
    surface.reset();
    removeCanvas("surf_size");
}

TEST(Surface, ExplicitSizeAtCreationSetsCanvasSize) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_explicit", 8, 8);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()), 120, 90);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 120u);
    EXPECT_EQ(surface->getHeight(), 90u);
    EXPECT_EQ(EM_ASM_INT({ return document.querySelector(UTF8ToString($0)).width; }, sel.c_str()), 120);
    surface.reset();
    removeCanvas("surf_explicit");
}

TEST(Surface, UnknownCanvasSelectorReturnsNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    EXPECT_EQ(device->createSurface(const_cast<char*>("#no_such_canvas")), nullptr);
}

TEST(Surface, SchedulePresentWithoutAcquireIsNoop) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_noop", 16, 16);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()));
    ASSERT_NE(surface, nullptr);
    auto encoder = device->createCommandEncoder();
    auto cmd = encoder->finish();
    EXPECT_FALSE(surface->schedulePresent(cmd));
    device->submit(cmd);
    surface.reset();
    removeCanvas("surf_noop");
}

TEST(Surface, PresentDeliversRenderedPixels) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_pixels", 32, 32);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()));
    ASSERT_NE(surface, nullptr);

    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(centrePixel(sel), kRed);
    yieldToBrowser();

    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(centrePixel(sel), kGreen);
    yieldToBrowser();
    surface.reset();
    removeCanvas("surf_pixels");
}

TEST(Surface, TwoCanvasesPresentTheirOwnContent) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto selA = makeCanvas("surf_a", 32, 32);
    auto selB = makeCanvas("surf_b", 48, 24);
    auto surfA = device->createSurface(const_cast<char*>(selA.c_str()));
    auto surfB = device->createSurface(const_cast<char*>(selB.c_str()));
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    for (int i = 0; i < 5; ++i) {
        ASSERT_TRUE(presentClear(*device, *surfA, 1, 0, 0)) << "A frame " << i;
        ASSERT_TRUE(presentClear(*device, *surfB, 0, 0, 1)) << "B frame " << i;
        EXPECT_EQ(centrePixel(selA), kRed)  << "A frame " << i;
        EXPECT_EQ(centrePixel(selB), kBlue) << "B frame " << i;
        yieldToBrowser();
    }
    surfA.reset(); surfB.reset();
    removeCanvas("surf_a"); removeCanvas("surf_b");
}

TEST(Surface, OneCommandBufferPresentsTwoCanvases) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto selA = makeCanvas("surf_one_a", 32, 32);
    auto selB = makeCanvas("surf_one_b", 32, 32);
    auto surfA = device->createSurface(const_cast<char*>(selA.c_str()));
    auto surfB = device->createSurface(const_cast<char*>(selB.c_str()));
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    auto viewA = surfA->acquireNextTexture();
    auto viewB = surfB->acquireNextTexture();
    ASSERT_NE(viewA, nullptr);
    ASSERT_NE(viewB, nullptr);
    auto encoder = device->createCommandEncoder();
    for (auto [view, r, b] : {std::tuple{viewA, 1.f, 0.f}, std::tuple{viewB, 0.f, 1.f}}) {
        BeginRenderPassDescriptor rp{};
        ColorAttachment ca{};
        ca.view = view; ca.loadOp = LoadOp::clear; ca.storeOp = StoreOp::store;
        ca.clearValue[0] = r; ca.clearValue[1] = 0; ca.clearValue[2] = b; ca.clearValue[3] = 1;
        rp.colorAttachments.push_back(ca);
        auto pass = encoder->beginRenderPass(rp);
        ASSERT_NE(pass, nullptr);
        pass->end();
    }
    auto cmd = encoder->finish();
    ASSERT_TRUE(surfA->schedulePresent(cmd));
    ASSERT_TRUE(surfB->schedulePresent(cmd));
    device->submit(cmd);
    EXPECT_EQ(centrePixel(selA), kRed);
    EXPECT_EQ(centrePixel(selB), kBlue);
    yieldToBrowser();
    surfA.reset(); surfB.reset();
    removeCanvas("surf_one_a"); removeCanvas("surf_one_b");
}

TEST(Surface, ResizeAndCanvasChangesAreFollowed) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_resize", 32, 32);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()));
    ASSERT_NE(surface, nullptr);

    surface->resize(100, 50);  // through the API
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(surface->getWidth(), 100u);
    EXPECT_EQ(surface->getHeight(), 50u);
    EXPECT_EQ(centrePixel(sel), kRed);
    yieldToBrowser();

    setCanvasAttrSize(sel, 40, 70);  // by page code, behind our back
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(surface->getWidth(), 40u);
    EXPECT_EQ(surface->getHeight(), 70u);
    EXPECT_EQ(centrePixel(sel), kBlue);
    yieldToBrowser();
    surface.reset();
    removeCanvas("surf_resize");
}

TEST(Surface, ZeroSizedCanvasAcquiresNothing) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto sel = makeCanvas("surf_zero", 16, 16);
    auto surface = device->createSurface(const_cast<char*>(sel.c_str()));
    ASSERT_NE(surface, nullptr);
    setCanvasAttrSize(sel, 0, 16);
    EXPECT_EQ(surface->acquireNextTexture(), nullptr);
    surface.reset();
    removeCanvas("surf_zero");
}

#else  // neither Apple nor Android

TEST(Surface, NotYetImplementedBackendsReturnNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    int dummy = 0;
    // Backend stub: no per-surface swapchain yet (Phase 17 M0.2-M0.4).
    EXPECT_EQ(device->createSurface(&dummy), nullptr);
}

#endif
