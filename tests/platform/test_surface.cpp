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

#else  // !__APPLE__

TEST(Surface, NotYetImplementedBackendsReturnNull) {
    auto device = makeDevice();
    if (!device) GTEST_SKIP() << "no GPU device";
    int dummy = 0;
    // Backend stub: no per-surface swapchain yet (Phase 17 M0.2-M0.4).
    EXPECT_EQ(device->createSurface(&dummy), nullptr);
}

#endif
