// Surface: one presentable swapchain per native window, many per Device.
//
// On Metal, a Surface wraps a CAMetalLayer. Off-screen layers (no window or
// view) are enough to exercise the full acquire, render, schedulePresent,
// submit cycle with real CAMetalDrawable objects, the same way
// test_device_present_race.cpp does.
//
// Vulkan (Android), DirectX 12 and WebGPU have their own sections below,
// each driving real native windows / canvases.

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
#include <cstring>
#include <algorithm>
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

// ---------------------------------------------------------------------------
// Several raster threads on one device (campello_widgets Phase 17, M1.9).
// The Vulkan backend serializes *recording* per device (DeviceData::
// acquireFrameLock()): an encoder holds the lock from createCommandEncoder()
// until its command buffer is submitted or dropped.
// ---------------------------------------------------------------------------

namespace {
using SteadyClock = std::chrono::steady_clock;
long long elapsedMs(SteadyClock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count();
}
} // namespace

TEST(Surface, TwoRasterThreadsDriveTwoWindowsConcurrently) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    ImageReaderWindow winA(32, 32), winB(48, 24);
    auto surfA = device->createSurface(winA.window);
    auto surfB = device->createSurface(winB.window);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    constexpr int kFrames = 300;
    std::atomic<int> failuresA{0}, failuresB{0};
    auto raster = [&](Surface& surface, ImageReaderWindow& win, float r, float b,
                      std::atomic<int>& failures) {
        for (int i = 0; i < kFrames; ++i) {
            if (!presentClear(*device, surface, r, 0, b)) ++failures;
            win.drain();
        }
    };
    auto start = SteadyClock::now();
    std::thread a([&] { raster(*surfA, winA, 1, 0, failuresA); });
    std::thread b([&] { raster(*surfB, winB, 0, 1, failuresB); });
    a.join();
    b.join();
    device->waitForIdle();

    EXPECT_EQ(failuresA.load(), 0);
    EXPECT_EQ(failuresB.load(), 0);
    EXPECT_LT(elapsedMs(start), 20000) << "raster threads stalled each other";
    // Each window still shows only its own content.
    ASSERT_TRUE(presentClear(*device, *surfA, 1, 0, 0));
    ASSERT_TRUE(presentClear(*device, *surfB, 0, 0, 1));
    EXPECT_EQ(winA.latestCentrePixel(), kRed);
    EXPECT_EQ(winB.latestCentrePixel(), kBlue);
}

TEST(Surface, RecordingIsSerializedAcrossThreads) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto encoderA = device->createCommandEncoder();
    ASSERT_NE(encoderA, nullptr);

    std::atomic<bool> aSubmitted{false};
    std::atomic<bool> bGotEncoderBeforeASubmitted{false};
    std::thread b([&] {
        auto encoderB = device->createCommandEncoder();  // must wait for A
        if (encoderB && !aSubmitted.load()) bGotEncoderBeforeASubmitted = true;
        if (encoderB) device->submit(encoderB->finish());
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    aSubmitted = true;
    device->submit(encoderA->finish());
    b.join();
    EXPECT_FALSE(bGotEncoderBeforeASubmitted.load());
}

TEST(Surface, AbandonedEncoderReleasesRecordingSlot) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    { auto dropped = device->createCommandEncoder(); ASSERT_NE(dropped, nullptr); }
    auto start = SteadyClock::now();
    std::shared_ptr<CommandEncoder> other;
    std::thread b([&] { other = device->createCommandEncoder(); if (other) device->submit(other->finish()); });
    b.join();
    EXPECT_NE(other, nullptr);
    EXPECT_LT(elapsedMs(start), 1000) << "dropped encoder kept the lock until the timeout";
}

TEST(Surface, UnsubmittedCommandBufferReleasesRecordingSlot) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    {
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        auto cmd = encoder->finish();  // finished, never submitted, then dropped
        ASSERT_NE(cmd, nullptr);
    }
    auto start = SteadyClock::now();
    std::shared_ptr<CommandEncoder> other;
    std::thread b([&] { other = device->createCommandEncoder(); if (other) device->submit(other->finish()); });
    b.join();
    EXPECT_NE(other, nullptr);
    EXPECT_LT(elapsedMs(start), 1000) << "unsubmitted command buffer kept the lock";
}

TEST(Surface, SameThreadMayHoldTwoEncoders) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    auto start = SteadyClock::now();
    auto first  = device->createCommandEncoder();
    auto second = device->createCommandEncoder();  // same thread: no deadlock
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_LT(elapsedMs(start), 1000);
    device->submit(first->finish());
    device->submit(second->finish());
    // Fully released afterwards: another thread gets in promptly.
    std::shared_ptr<CommandEncoder> other;
    start = SteadyClock::now();
    std::thread b([&] { other = device->createCommandEncoder(); if (other) device->submit(other->finish()); });
    b.join();
    EXPECT_NE(other, nullptr);
    EXPECT_LT(elapsedMs(start), 1000);
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

#elif defined(_WIN32)

// DirectX 12. Each "window" is a real, visible, borderless HWND (so its client
// rect is its whole size) that never takes focus. Presented pixels are read
// back through DWM with PrintWindow(PW_RENDERFULLCONTENT), which captures
// flip-model swapchain content: the tests check what the window shows, not
// just return codes.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>

namespace {

LRESULT CALLBACK testWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;  // don't let GDI paint over the swapchain
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void pumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

struct TestWindow {
    HWND hwnd = nullptr;

    TestWindow(int w, int h, int x = 40, int y = 40) {
        static bool registered = false;
        if (!registered) {
            // Physical pixels everywhere: client sizes and PrintWindow agree.
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            WNDCLASSW wc = {};
            wc.lpfnWndProc   = testWndProc;
            wc.hInstance     = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"CampelloGpuSurfaceTest";
            RegisterClassW(&wc);
            registered = true;
        }
        hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                               L"CampelloGpuSurfaceTest", L"surface test", WS_POPUP,
                               x, y, w, h, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (hwnd) ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        pumpMessages();
    }
    ~TestWindow() {
        if (hwnd) DestroyWindow(hwnd);
        pumpMessages();
    }

    // The window's centre pixel as DWM shows it, 0xRRGGBB, or 0xFFFFFFFF on failure.
    uint32_t centrePixelNow() const {
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const int w = rc.right, h = rc.bottom;
        if (w <= 0 || h <= 0) return 0xFFFFFFFFu;
        HDC screen = GetDC(nullptr);
        HDC mem    = CreateCompatibleDC(screen);
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;  // top-down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, bmp);
        uint32_t result = 0xFFFFFFFFu;
        if (PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT) && bits) {
            const uint8_t *px = static_cast<const uint8_t *>(bits) + ((h / 2) * w + (w / 2)) * 4;
            result = (uint32_t(px[2]) << 16) | (uint32_t(px[1]) << 8) | uint32_t(px[0]);  // BGRA
        }
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return result;
    }

    // Waits (pumping messages) until DWM shows `expected` at the centre.
    // Returns the last pixel seen.
    uint32_t waitForCentrePixel(uint32_t expected, int timeoutMs = 2000) const {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        uint32_t px = 0xFFFFFFFFu;
        do {
            pumpMessages();
            px = centrePixelNow();
            if (px == expected) return px;
            Sleep(10);
        } while (std::chrono::steady_clock::now() < deadline);
        return px;
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

constexpr uint32_t kRed   = 0xFF0000u;
constexpr uint32_t kGreen = 0x00FF00u;
constexpr uint32_t kBlue  = 0x0000FFu;

} // namespace

TEST(Surface, CreateFromHwndReportsClientSizeAndFormat) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(64, 48);
    ASSERT_NE(win.hwnd, nullptr);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 48u);
    EXPECT_EQ(surface->getPixelFormat(), PixelFormat::rgba8unorm);
}

TEST(Surface, ExplicitSizeAtCreationOverridesClientRect) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(64, 48);
    auto surface = device->createSurface(win.hwnd, 200, 100);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 200u);
    EXPECT_EQ(surface->getHeight(), 100u);
    // And it stays at that size, rather than following the window.
    auto view = surface->acquireNextTexture();
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(surface->getWidth(), 200u);
}

TEST(Surface, SecondSurfaceOnTheSameWindowFails) {
    // DXGI allows one flip-model swapchain per HWND.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto first = device->createSurface(win.hwnd);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(device->createSurface(win.hwnd), nullptr);
}

TEST(Surface, InvalidWindowReturnsNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    HWND gone = nullptr;
    { TestWindow win(16, 16); gone = win.hwnd; }
    EXPECT_EQ(device->createSurface(gone), nullptr);
}

TEST(Surface, SchedulePresentWithoutAcquireIsNoop) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(16, 16);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    auto encoder = device->createCommandEncoder();
    auto cmd = encoder->finish();
    EXPECT_FALSE(surface->schedulePresent(cmd));
    device->submit(cmd);
}

TEST(Surface, PresentDeliversRenderedPixels) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(64, 64);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);

    auto fence = device->createFence();
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0, fence));
    fence->wait();
    EXPECT_EQ(win.waitForCentrePixel(kRed), kRed);

    // And the fence-less submit() overload.
    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(win.waitForCentrePixel(kGreen), kGreen);
}

TEST(Surface, AbandonedAcquireIsReturnedAgain) {
    // The back buffer index only advances on Present(), so an image acquired
    // but never presented is simply handed out again.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    auto first = surface->acquireNextTexture();
    ASSERT_NE(first, nullptr);
    auto again = surface->acquireNextTexture();
    EXPECT_EQ(first, again);
    first.reset();
    again.reset();
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(win.waitForCentrePixel(kBlue), kBlue);
}

TEST(Surface, DroppedEncoderLeavesTheImageUsable) {
    // An encoder that began a pass on the surface image and was then dropped
    // never executes its barriers: the image must still be tracked as
    // PRESENT, so the next frame transitions it again (the D3D12 debug layer
    // reports a state mismatch otherwise) and renders normally.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    {
        auto view = surface->acquireNextTexture();
        ASSERT_NE(view, nullptr);
        auto encoder = device->createCommandEncoder();
        BeginRenderPassDescriptor rp{};
        ColorAttachment ca{};
        ca.view = view; ca.loadOp = LoadOp::clear; ca.storeOp = StoreOp::store;
        rp.colorAttachments.push_back(ca);
        auto pass = encoder->beginRenderPass(rp);
        ASSERT_NE(pass, nullptr);
        pass->end();
    }  // encoder dropped without finish()
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(win.waitForCentrePixel(kRed), kRed);
}

TEST(Surface, ManyFramesCycleBuffersWithoutStalling) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120; ++i) {
        ASSERT_TRUE(presentClear(*device, *surface, (i & 1) ? 1.f : 0.f, 0, (i & 1) ? 0.f : 1.f))
            << "frame " << i;
        pumpMessages();
    }
    device->waitForIdle();
    // Vsync'd: ~2 s at 60 Hz. Far longer means presents are blocking.
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - start).count(), 10000);
    EXPECT_EQ(win.waitForCentrePixel(kRed), kRed);  // frame 119 is odd: red
}

TEST(Surface, FollowsWindowResizeAndKeepsPresenting) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));

    SetWindowPos(win.hwnd, nullptr, 0, 0, 96, 64, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pumpMessages();
    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(surface->getWidth(), 96u);
    EXPECT_EQ(surface->getHeight(), 64u);
    EXPECT_EQ(win.waitForCentrePixel(kGreen), kGreen);
}

TEST(Surface, ExplicitResizeIsHonoured) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    surface->resize(0, 10);  // ignored
    surface->resize(80, 40);
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(surface->getWidth(), 80u);
    EXPECT_EQ(surface->getHeight(), 40u);
    EXPECT_EQ(win.waitForCentrePixel(kBlue), kBlue);  // stretched to the window
}

TEST(Surface, MinimizedWindowAcquiresNothing) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    ShowWindow(win.hwnd, SW_SHOWMINNOACTIVE);
    pumpMessages();
    EXPECT_EQ(surface->acquireNextTexture(), nullptr);
    ShowWindow(win.hwnd, SW_SHOWNOACTIVATE);
    pumpMessages();
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(win.waitForCentrePixel(kRed), kRed);
}

TEST(Surface, TwoSurfacesPresentTheirOwnContent) {
    // Two windows on one device, interleaved frame by frame: each window must
    // only ever show its own colour.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow winA(48, 48, 40, 40), winB(64, 32, 120, 40);
    auto surfA = device->createSurface(winA.hwnd);
    auto surfB = device->createSurface(winB.hwnd);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(presentClear(*device, *surfA, 1, 0, 0)) << "A frame " << i;
        ASSERT_TRUE(presentClear(*device, *surfB, 0, 0, 1)) << "B frame " << i;
        pumpMessages();
    }
    EXPECT_EQ(winA.waitForCentrePixel(kRed), kRed);
    EXPECT_EQ(winB.waitForCentrePixel(kBlue), kBlue);
}

TEST(Surface, OneCommandBufferPresentsTwoSurfaces) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow winA(48, 48, 40, 40), winB(48, 48, 120, 40);
    auto surfA = device->createSurface(winA.hwnd);
    auto surfB = device->createSurface(winB.hwnd);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    for (int i = 0; i < 5; ++i) {
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
        pumpMessages();
    }
    EXPECT_EQ(winA.waitForCentrePixel(kRed), kRed);
    EXPECT_EQ(winB.waitForCentrePixel(kBlue), kBlue);
}

TEST(Surface, DestroyingSurfaceWithPendingPresentIsSafe) {
    // A window's Surface released between schedulePresent() and submit(): the
    // command buffer keeps the swapchain alive until it has been presented.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    auto surface = device->createSurface(win.hwnd);
    ASSERT_NE(surface, nullptr);
    auto view = surface->acquireNextTexture();
    ASSERT_NE(view, nullptr);
    auto cmd = clearPass(*device, view, 1, 0, 0);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    view.reset();
    surface.reset();
    device->submit(cmd);
    device->waitForIdle();
    EXPECT_EQ(win.waitForCentrePixel(kRed), kRed);
}

TEST(Surface, SurfaceRetainedByTheFrameRingIsReleasedWithTheDevice) {
    // The last reference to a surface's swapchain can be a command buffer the
    // frame ring still retains when the Device goes away.
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    TestWindow win(32, 32);
    {
        auto surface = device->createSurface(win.hwnd);
        ASSERT_NE(surface, nullptr);
        ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    }
    device.reset();  // must neither crash nor wait forever
    SUCCEED();
}

#elif defined(__linux__)

// Vulkan on Linux desktop, through both window systems campello_widgets
// uses. Each section skips when its display server isn't reachable, so the
// suite runs unchanged on a headless CI box (Xvfb gives the X11 half).
//
// X11: real windows; the presented pixels are read back with XGetImage
// (works on Xvfb, with Mesa's software WSI, and on XWayland).
// Wayland: real xdg_toplevels. A client can't read back what the compositor
// shows, so these check sizes, return codes, frame callbacks (the compositor
// received a new buffer) and that a hidden window never stalls another.

#include <campello_gpu/platform/linux_surface.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <wayland-client.h>
#include "protocols/xdg-shell-client-protocol.h"
#include <poll.h>
#include <chrono>
#include <cstdlib>

namespace {

using SteadyClock = std::chrono::steady_clock;
long long elapsedMs(SteadyClock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - since).count();
}

constexpr uint32_t kRed   = 0xFF0000FFu;
constexpr uint32_t kGreen = 0x00FF00FFu;
constexpr uint32_t kBlue  = 0x0000FFFFu;

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

// ---------------------------------------------------------------------------
// X11
// ---------------------------------------------------------------------------

// One connection for the whole suite. Opening and closing a Display per test
// is slow on XWayland, and Mesa's X11 WSI keeps per-connection state.
Display *x11Display() {
    static Display *dpy = [] {
        XInitThreads();  // raster threads present while the test thread reads back
        return XOpenDisplay(nullptr);
    }();
    return dpy;
}

struct X11Window {
    Display         *dpy = nullptr;
    Window           win = 0;
    int              width = 0, height = 0;
    LinuxSurfaceInfo info{};

    X11Window(int w, int h) : dpy(x11Display()), width(w), height(h) {
        if (!dpy) return;
        XSetWindowAttributes swa{};
        swa.event_mask       = StructureNotifyMask | ExposureMask;
        swa.background_pixel = BlackPixel(dpy, DefaultScreen(dpy));
        // Side by side: without a window manager (Xvfb) every window would
        // open at 0,0, and XGetImage on a covered window returns the pixels
        // of the window on top of it.
        static int next = 0;
        const int x = (next++ % 8) * 120;
        win = XCreateWindow(dpy, DefaultRootWindow(dpy), x, 0, w, h, 0, CopyFromParent,
                            InputOutput, CopyFromParent, CWEventMask | CWBackPixel, &swa);
        XStoreName(dpy, win, "campello_gpu Surface test");
        pinSize(w, h);
        XMapWindow(dpy, win);
        waitFor(MapNotify);
        info.display = dpy;
        info.window  = reinterpret_cast<void *>(static_cast<uintptr_t>(win));
        info.api     = LinuxWindowApi::x11;
    }
    ~X11Window() {
        if (dpy && win) { XDestroyWindow(dpy, win); XSync(dpy, False); }
    }

    // A window manager may resize a new window (minimum sizes, tiling):
    // pin it to exactly the size the test asked for.
    void pinSize(int w, int h) {
        XSizeHints *hints = XAllocSizeHints();
        hints->flags      = PMinSize | PMaxSize;
        hints->min_width  = hints->max_width  = w;
        hints->min_height = hints->max_height = h;
        XSetWMNormalHints(dpy, win, hints);
        XFree(hints);
    }

    // Waits for an event of `type` (and, if given, matching `accept`).
    template <typename Accept>
    bool waitFor(int type, Accept accept, int timeoutMs = 2000) {
        auto start = SteadyClock::now();
        XEvent ev;
        while (elapsedMs(start) < timeoutMs) {
            if (XCheckTypedWindowEvent(dpy, win, type, &ev)) {
                if (accept(ev)) return true;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    }
    bool waitFor(int type) { return waitFor(type, [](const XEvent &) { return true; }); }

    // Resizes and waits until the window manager has applied it: the first
    // ConfigureNotify after the request can still carry the old size.
    bool resize(int w, int h) {
        width = w; height = h;
        pinSize(w, h);
        XResizeWindow(dpy, win, w, h);
        XSync(dpy, False);
        return waitFor(ConfigureNotify, [&](const XEvent &ev) {
            return ev.xconfigure.width == w && ev.xconfigure.height == h;
        });
    }

    // A pixel well inside the window as 0xRRGGBBAA (opaque), or 0 if it
    // can't be read.
    uint32_t centrePixel() {
        XImage *img = XGetImage(dpy, win, width / 3, height / 3, 1, 1, AllPlanes, ZPixmap);
        if (!img) return 0;
        const unsigned long p = XGetPixel(img, 0, 0);
        XDestroyImage(img);
        return (uint32_t(p & 0xFFFFFFu) << 8) | 0xFFu;
    }

    // Presentation completes asynchronously (the X server or compositor
    // copies the image after the GPU is done): wait for `expected`, and
    // return whatever was last read if it never shows.
    uint32_t waitForPixel(uint32_t expected, int timeoutMs = 2000) {
        auto start = SteadyClock::now();
        uint32_t last = 0;
        while (elapsedMs(start) < timeoutMs) {
            last = centrePixel();
            if (last == expected) return last;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return last;
    }
};

#define REQUIRE_X11()                                                        \
    if (!x11Display()) GTEST_SKIP() << "no X display (DISPLAY unset?)";     \
    auto device = Device::createDefaultDevice(nullptr);                      \
    if (!device) GTEST_SKIP() << "no GPU device"

} // namespace

TEST(Surface, X11CreateReportsWindowSizeAndFormat) {
    REQUIRE_X11();
    X11Window win(64, 48);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 48u);
    auto fmt = surface->getPixelFormat();
    EXPECT_TRUE(fmt == PixelFormat::bgra8unorm || fmt == PixelFormat::rgba8unorm)
        << "format " << (int)fmt;
}

TEST(Surface, X11PresentDeliversRenderedPixels) {
    REQUIRE_X11();
    X11Window win(64, 64);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);

    auto fence = device->createFence();
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0, fence));
    fence->wait();
    EXPECT_EQ(win.waitForPixel(kRed), kRed);

    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));  // fence-less overload
    EXPECT_EQ(win.waitForPixel(kGreen), kGreen);
}

TEST(Surface, X11SchedulePresentWithoutAcquireIsNoop) {
    REQUIRE_X11();
    X11Window win(16, 16);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    auto encoder = device->createCommandEncoder();
    auto cmd = encoder->finish();
    EXPECT_FALSE(surface->schedulePresent(cmd));
    device->submit(cmd);
}

TEST(Surface, X11AbandonedAcquireIsReturnedAgain) {
    REQUIRE_X11();
    X11Window win(32, 32);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    auto first  = surface->acquireNextTexture();
    ASSERT_NE(first, nullptr);
    auto second = surface->acquireNextTexture();
    EXPECT_EQ(first.get(), second.get());
    auto cmd = clearPass(*device, second, 0, 0, 1);
    ASSERT_NE(cmd, nullptr);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    device->submit(cmd);
    EXPECT_EQ(win.waitForPixel(kBlue), kBlue);
}

TEST(Surface, X11ManyFramesCycleSlotsWithoutStalling) {
    REQUIRE_X11();
    X11Window win(32, 32);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    auto start = SteadyClock::now();
    for (int i = 0; i < 240; ++i) {
        const bool red = (i % 2) == 0;
        ASSERT_TRUE(presentClear(*device, *surface, red ? 1 : 0, 0, red ? 0 : 1)) << "frame " << i;
    }
    device->waitForIdle();
    EXPECT_LT(elapsedMs(start), 10000) << "frames are stalling";
    ASSERT_TRUE(presentClear(*device, *surface, 0, 1, 0));
    EXPECT_EQ(win.waitForPixel(kGreen), kGreen);
}

TEST(Surface, X11WindowResizeIsFollowed) {
    // X11 reports currentExtent, so the swapchain follows the window once
    // the app calls resize() (or the present reports OUT_OF_DATE).
    REQUIRE_X11();
    X11Window win(32, 32);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    EXPECT_EQ(win.waitForPixel(kRed), kRed);

    ASSERT_TRUE(win.resize(96, 80)) << "the window manager didn't apply the resize";
    surface->resize(96, 80);
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(surface->getWidth(), 96u);
    EXPECT_EQ(surface->getHeight(), 80u);
    EXPECT_EQ(win.waitForPixel(kBlue), kBlue);
}

TEST(Surface, X11TwoSurfacesPresentTheirOwnContent) {
    REQUIRE_X11();
    X11Window winA(32, 32), winB(48, 24);
    auto surfA = device->createSurface(&winA.info);
    auto surfB = device->createSurface(&winB.info);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);
    for (int i = 0; i < 20; ++i) {
        const bool flip = (i % 2) == 1;  // swap colours every frame
        ASSERT_TRUE(presentClear(*device, *surfA, flip ? 0 : 1, 0, flip ? 1 : 0)) << "A frame " << i;
        ASSERT_TRUE(presentClear(*device, *surfB, flip ? 1 : 0, 0, flip ? 0 : 1)) << "B frame " << i;
        const uint32_t a = flip ? kBlue : kRed, b = flip ? kRed : kBlue;
        EXPECT_EQ(winA.waitForPixel(a), a) << "A frame " << i;
        EXPECT_EQ(winB.waitForPixel(b), b) << "B frame " << i;
    }
}

TEST(Surface, X11OneCommandBufferPresentsTwoSurfaces) {
    REQUIRE_X11();
    X11Window winA(32, 32), winB(32, 32);
    auto surfA = device->createSurface(&winA.info);
    auto surfB = device->createSurface(&winB.info);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);
    for (int i = 0; i < 6; ++i) {
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
        EXPECT_EQ(winA.waitForPixel(kRed), kRed)   << "frame " << i;
        EXPECT_EQ(winB.waitForPixel(kBlue), kBlue) << "frame " << i;
    }
}

TEST(Surface, X11DestroyingSurfaceWithPendingPresentIsSafe) {
    REQUIRE_X11();
    X11Window win(16, 16);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    auto view = surface->acquireNextTexture();
    ASSERT_NE(view, nullptr);
    auto cmd = clearPass(*device, view, 1, 0, 0);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    view.reset();
    surface.reset();
    device->submit(cmd);
    device->waitForIdle();
    EXPECT_EQ(win.waitForPixel(kRed), kRed);
}

TEST(Surface, X11TwoRasterThreadsDriveTwoWindowsConcurrently) {
    REQUIRE_X11();
    X11Window winA(32, 32), winB(48, 24);
    auto surfA = device->createSurface(&winA.info);
    auto surfB = device->createSurface(&winB.info);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);

    constexpr int kFrames = 300;
    std::atomic<int> failuresA{0}, failuresB{0};
    auto raster = [&](Surface &surface, float r, float b, std::atomic<int> &failures) {
        for (int i = 0; i < kFrames; ++i)
            if (!presentClear(*device, surface, r, 0, b)) ++failures;
    };
    auto start = SteadyClock::now();
    std::thread a([&] { raster(*surfA, 1, 0, failuresA); });
    std::thread b([&] { raster(*surfB, 0, 1, failuresB); });
    a.join();
    b.join();
    device->waitForIdle();
    EXPECT_EQ(failuresA.load(), 0);
    EXPECT_EQ(failuresB.load(), 0);
    EXPECT_LT(elapsedMs(start), 20000) << "raster threads stalled each other";
    EXPECT_EQ(winA.waitForPixel(kRed), kRed);
    EXPECT_EQ(winB.waitForPixel(kBlue), kBlue);
}

TEST(Surface, X11UnmappedWindowDoesNotStallAnother) {
    // A minimised (unmapped) window keeps being drawn by an app that hasn't
    // noticed yet. With several raster threads sharing the device's
    // recording lock, a present that blocks would stop every window.
    REQUIRE_X11();
    X11Window shown(32, 32), hidden(32, 32);
    auto surfShown  = device->createSurface(&shown.info);
    auto surfHidden = device->createSurface(&hidden.info);
    ASSERT_NE(surfShown, nullptr);
    ASSERT_NE(surfHidden, nullptr);
    XUnmapWindow(hidden.dpy, hidden.win);
    XSync(hidden.dpy, False);

    std::atomic<bool> stop{false};
    std::thread h([&] {
        while (!stop) presentClear(*device, *surfHidden, 0, 0, 1);
    });
    auto start = SteadyClock::now();
    int failures = 0;
    for (int i = 0; i < 120; ++i)
        if (!presentClear(*device, *surfShown, 1, 0, 0)) ++failures;
    const auto ms = elapsedMs(start);
    stop = true;
    h.join();
    device->waitForIdle();
    EXPECT_EQ(failures, 0);
    EXPECT_LT(ms, 10000) << "the hidden window's presents stalled the visible one";
    EXPECT_EQ(shown.waitForPixel(kRed), kRed);
}

// ---------------------------------------------------------------------------
// Wayland
// ---------------------------------------------------------------------------

namespace {

struct WaylandConnection {
    wl_display    *display    = nullptr;
    wl_registry   *registry   = nullptr;
    wl_compositor *compositor = nullptr;
    xdg_wm_base   *wm_base    = nullptr;

    WaylandConnection() {
        const char *name = std::getenv("WAYLAND_DISPLAY");
        if (!name || !*name) return;
        display = wl_display_connect(nullptr);
        if (!display) return;
        registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            [](void *data, wl_registry *reg, uint32_t id, const char *iface, uint32_t version) {
                auto *self = static_cast<WaylandConnection *>(data);
                if (std::strcmp(iface, wl_compositor_interface.name) == 0)
                    self->compositor = static_cast<wl_compositor *>(
                        wl_registry_bind(reg, id, &wl_compositor_interface, std::min(version, 4u)));
                else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0)
                    self->wm_base = static_cast<xdg_wm_base *>(
                        wl_registry_bind(reg, id, &xdg_wm_base_interface, 1));
            },
            [](void *, wl_registry *, uint32_t) {},
        };
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        if (wm_base) {
            static const xdg_wm_base_listener ping = {
                [](void *, xdg_wm_base *base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
            };
            xdg_wm_base_add_listener(wm_base, &ping, nullptr);
        }
    }
    ~WaylandConnection() {
        if (wm_base)    xdg_wm_base_destroy(wm_base);
        if (compositor) wl_compositor_destroy(compositor);
        if (registry)   wl_registry_destroy(registry);
        if (display)    wl_display_disconnect(display);
    }
    bool ok() const { return display && compositor && wm_base; }

    // Dispatches events on the default queue for up to `ms` milliseconds,
    // or until `done()` is true.
    template <typename Pred>
    bool pumpUntil(Pred done, int ms = 2000) {
        auto start = SteadyClock::now();
        while (!done()) {
            if (elapsedMs(start) > ms) return false;
            while (wl_display_prepare_read(display) != 0) wl_display_dispatch_pending(display);
            wl_display_flush(display);
            pollfd pfd{wl_display_get_fd(display), POLLIN, 0};
            if (poll(&pfd, 1, 10) > 0) wl_display_read_events(display);
            else wl_display_cancel_read(display);
            wl_display_dispatch_pending(display);
        }
        return true;
    }
};

struct WaylandWindow {
    WaylandConnection &conn;
    wl_surface        *surface  = nullptr;
    xdg_surface       *xsurface = nullptr;
    xdg_toplevel      *toplevel = nullptr;
    bool               configured = false;
    int                frames_done = 0;
    LinuxSurfaceInfo   info{};

    // `role` false: a bare wl_surface the compositor never shows (and never
    // sends frame callbacks for), the worst case of a hidden window.
    WaylandWindow(WaylandConnection &c, int w, int h, bool role = true) : conn(c) {
        surface = wl_compositor_create_surface(conn.compositor);
        if (role) {
            xsurface = xdg_wm_base_get_xdg_surface(conn.wm_base, surface);
            static const xdg_surface_listener xl = {
                [](void *data, xdg_surface *xs, uint32_t serial) {
                    xdg_surface_ack_configure(xs, serial);
                    static_cast<WaylandWindow *>(data)->configured = true;
                },
            };
            xdg_surface_add_listener(xsurface, &xl, this);
            toplevel = xdg_surface_get_toplevel(xsurface);
            xdg_toplevel_set_title(toplevel, "campello_gpu Surface test");
            wl_surface_commit(surface);
            conn.pumpUntil([this] { return configured; });
        }
        info.display = conn.display;
        info.window  = surface;
        info.api     = LinuxWindowApi::wayland;
        info.width   = static_cast<uint32_t>(w);
        info.height  = static_cast<uint32_t>(h);
    }
    ~WaylandWindow() {
        if (toplevel) xdg_toplevel_destroy(toplevel);
        if (xsurface) xdg_surface_destroy(xsurface);
        if (surface)  wl_surface_destroy(surface);
        wl_display_roundtrip(conn.display);
    }

    // Asks for a frame callback; the WSI's next commit (in the present)
    // carries it, and the compositor answers once it has used that buffer.
    void requestFrameCallback() {
        static const wl_callback_listener cl = {
            [](void *data, wl_callback *cb, uint32_t) {
                ++static_cast<WaylandWindow *>(data)->frames_done;
                wl_callback_destroy(cb);
            },
        };
        wl_callback_add_listener(wl_surface_frame(surface), &cl, this);
    }
};

#define REQUIRE_WAYLAND()                                                       \
    WaylandConnection conn;                                                     \
    if (!conn.ok()) GTEST_SKIP() << "no Wayland compositor with xdg_wm_base";   \
    auto device = Device::createDefaultDevice(nullptr);                         \
    if (!device) GTEST_SKIP() << "no GPU device"

} // namespace

TEST(Surface, WaylandCreateUsesRequestedSize) {
    // Wayland reports currentExtent = UINT32_MAX: the size the app asks for
    // is the swapchain's size.
    REQUIRE_WAYLAND();
    WaylandWindow win(conn, 64, 48);
    ASSERT_TRUE(win.configured);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->getWidth(), 64u);
    EXPECT_EQ(surface->getHeight(), 48u);
    auto fmt = surface->getPixelFormat();
    EXPECT_TRUE(fmt == PixelFormat::bgra8unorm || fmt == PixelFormat::rgba8unorm)
        << "format " << (int)fmt;
}

TEST(Surface, WaylandPresentReachesTheCompositor) {
    REQUIRE_WAYLAND();
    WaylandWindow win(conn, 64, 64);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    for (int i = 0; i < 3; ++i) {
        const int before = win.frames_done;
        win.requestFrameCallback();
        ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0)) << "frame " << i;
        EXPECT_TRUE(conn.pumpUntil([&] { return win.frames_done > before; }))
            << "no frame callback for frame " << i;
    }
}

TEST(Surface, WaylandResizeChangesSwapchainSize) {
    // What campello_widgets' Wayland runner needs instead of recreating its
    // device on every configure: Surface::resize() picks the new size.
    REQUIRE_WAYLAND();
    WaylandWindow win(conn, 64, 64);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    ASSERT_TRUE(presentClear(*device, *surface, 1, 0, 0));
    surface->resize(160, 120);
    const int before = win.frames_done;
    win.requestFrameCallback();
    ASSERT_TRUE(presentClear(*device, *surface, 0, 0, 1));
    EXPECT_EQ(surface->getWidth(), 160u);
    EXPECT_EQ(surface->getHeight(), 120u);
    EXPECT_TRUE(conn.pumpUntil([&] { return win.frames_done > before; }));
}

TEST(Surface, WaylandTwoWindowsPresentIndependently) {
    REQUIRE_WAYLAND();
    WaylandWindow winA(conn, 64, 64), winB(conn, 96, 48);
    auto surfA = device->createSurface(&winA.info);
    auto surfB = device->createSurface(&winB.info);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);
    for (int i = 0; i < 5; ++i) {
        const int a0 = winA.frames_done, b0 = winB.frames_done;
        winA.requestFrameCallback();
        winB.requestFrameCallback();
        ASSERT_TRUE(presentClear(*device, *surfA, 1, 0, 0)) << "A frame " << i;
        ASSERT_TRUE(presentClear(*device, *surfB, 0, 0, 1)) << "B frame " << i;
        EXPECT_TRUE(conn.pumpUntil([&] { return winA.frames_done > a0 && winB.frames_done > b0; }))
            << "frame " << i << ": A " << winA.frames_done - a0 << ", B " << winB.frames_done - b0;
    }
}

TEST(Surface, WaylandTwoRasterThreadsDriveTwoWindowsConcurrently) {
    REQUIRE_WAYLAND();
    WaylandWindow winA(conn, 64, 64), winB(conn, 96, 48);
    auto surfA = device->createSurface(&winA.info);
    auto surfB = device->createSurface(&winB.info);
    ASSERT_NE(surfA, nullptr);
    ASSERT_NE(surfB, nullptr);
    constexpr int kFrames = 300;
    std::atomic<int> failuresA{0}, failuresB{0};
    auto raster = [&](Surface &surface, float r, float b, std::atomic<int> &failures) {
        for (int i = 0; i < kFrames; ++i)
            if (!presentClear(*device, surface, r, 0, b)) ++failures;
    };
    // The compositor's buffer releases arrive on the WSI's own queue, but
    // keep the default queue moving too (pings, configures) as an app would.
    std::atomic<bool> done{false};
    auto start = SteadyClock::now();
    std::thread a([&] { raster(*surfA, 1, 0, failuresA); });
    std::thread b([&] { raster(*surfB, 0, 1, failuresB); });
    std::thread joiner([&] { a.join(); b.join(); done = true; });
    conn.pumpUntil([&] { return done.load(); }, 30000);
    joiner.join();
    device->waitForIdle();
    EXPECT_EQ(failuresA.load(), 0);
    EXPECT_EQ(failuresB.load(), 0);
    EXPECT_LT(elapsedMs(start), 20000) << "raster threads stalled each other";
}

TEST(Surface, WaylandHiddenSurfaceDoesNotStallAnother) {
    // A surface the compositor never shows (no role: same as a minimised or
    // fully covered window, which gets no frame callbacks). Under FIFO,
    // Mesa's Wayland WSI would block its presents; that thread would then
    // hold the device's recording lock and stop every other window.
    REQUIRE_WAYLAND();
    WaylandWindow shown(conn, 64, 64), hidden(conn, 64, 64, /*role=*/false);
    auto surfShown  = device->createSurface(&shown.info);
    auto surfHidden = device->createSurface(&hidden.info);
    ASSERT_NE(surfShown, nullptr);
    ASSERT_NE(surfHidden, nullptr);

    std::atomic<bool> stop{false};
    std::atomic<int>  hiddenFrames{0};
    std::thread h([&] {
        while (!stop) if (presentClear(*device, *surfHidden, 0, 0, 1)) ++hiddenFrames;
    });
    std::atomic<int>  failures{0};
    std::atomic<bool> done{false};
    auto start = SteadyClock::now();
    std::thread s([&] {
        for (int i = 0; i < 120; ++i)
            if (!presentClear(*device, *surfShown, 1, 0, 0)) ++failures;
        done = true;
    });
    conn.pumpUntil([&] { return done.load(); }, 30000);
    const auto ms = elapsedMs(start);
    s.join();
    stop = true;
    h.join();
    device->waitForIdle();
    EXPECT_EQ(failures.load(), 0);
    EXPECT_LT(ms, 10000) << "the hidden surface stalled the visible one (" << hiddenFrames
                         << " hidden frames)";
}

TEST(Surface, WaylandDestroyingSurfaceWithPendingPresentIsSafe) {
    REQUIRE_WAYLAND();
    WaylandWindow win(conn, 32, 32);
    auto surface = device->createSurface(&win.info);
    ASSERT_NE(surface, nullptr);
    auto view = surface->acquireNextTexture();
    ASSERT_NE(view, nullptr);
    auto cmd = clearPass(*device, view, 1, 0, 0);
    ASSERT_TRUE(surface->schedulePresent(cmd));
    view.reset();
    surface.reset();
    device->submit(cmd);
    device->waitForIdle();
    SUCCEED();
}

#else  // no other backend

TEST(Surface, NotYetImplementedBackendsReturnNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    int dummy = 0;
    EXPECT_EQ(device->createSurface(&dummy), nullptr);
}

#endif
