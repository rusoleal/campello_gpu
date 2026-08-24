// Regression test for the pendingPresentDrawable data race fixed alongside
// this file -- see MetalDeviceData::pendingPresentDrawable's doc comment in
// src/metal/common.hpp for the full explanation.
//
// Device::scheduleNextPresent() is called concurrently in production from
// two different threads: the UI thread (drawInMTKView:'s "buildFrame
// skipped" path calls scheduleNextPresent(nullptr) to avoid a dangling
// pointer) and the raster thread (a real per-frame drawable, followed by
// Device::submit() which consumes it). Without synchronization, the two
// threads' unsynchronized read-release-retain-store sequences on the same
// raw pointer can double-release the same drawable.
//
// This test exercises both code paths concurrently at a high iteration
// count. A plain run may pass even with the bug present -- races are
// timing-dependent, not guaranteed to manifest as a crash on any given
// run -- so the reliable way to evaluate this test is under
// ThreadSanitizer (-fsanitize=thread / the TSAN build type), which performs
// deterministic happens-before analysis and flags the unsynchronized
// access directly, independent of whether it happens to crash this run.
#if defined(__APPLE__)

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>

// Internal metal-cpp headers (not part of the public inc/campello_gpu API)
// -- needed to obtain real, retainable/releasable CAMetalDrawable objects
// via an off-screen CAMetalLayer, exactly like MTKView hands to
// scheduleNextPresent() in production. Safe to include here without
// defining MTL_PRIVATE_IMPLEMENTATION/CA_PRIVATE_IMPLEMENTATION: those
// symbols are defined exactly once, in src/metal/context.cpp, and this
// test links against campello_gpu which already contains that definition.
#include "../../src/metal/Metal.hpp"
#include "../../src/metal/common.hpp"

#include <atomic>
#include <thread>

using namespace systems::leal::campello_gpu;

TEST(Device, ScheduleNextPresentConcurrentWithNullClearDoesNotRace) {
    auto device = Device::createDefaultDevice(nullptr);
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform";
    }

    // Off-screen CAMetalLayer -- no window/view needed, just a source of
    // real CAMetalDrawable objects with genuine Objective-C retain/release
    // semantics, matching what run_app.mm hands to scheduleNextPresent().
    MetalAutoreleasePool setupPool;
    auto *layer = CA::MetalLayer::layer();
    ASSERT_NE(layer, nullptr);
    layer->setDevice(MTL::CreateSystemDefaultDevice());
    layer->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    layer->setDrawableSize(CGSizeMake(4, 4));

    constexpr int kIterations = 2000;
    std::atomic<bool> setupFailed{false};

    // "UI thread" stand-in: mimics drawInMTKView:'s skipped-frame path.
    std::thread uiThread([&] {
        for (int i = 0; i < kIterations; ++i) {
            device->scheduleNextPresent(nullptr);
        }
    });

    // "Raster thread" stand-in: mimics Renderer::rasterFrame(), which
    // schedules a real drawable then submits a command buffer that
    // consumes it via Device::submit()'s presentDrawable/commit.
    std::thread rasterThread([&] {
        for (int i = 0; i < kIterations; ++i) {
            MetalAutoreleasePool framePool;
            auto *drawable = layer->nextDrawable();
            if (!drawable) continue;
            device->scheduleNextPresent(static_cast<void *>(drawable));

            auto encoder = device->createCommandEncoder();
            if (!encoder) { setupFailed = true; return; }
            auto cmdBuffer = encoder->finish();
            if (!cmdBuffer) { setupFailed = true; return; }
            device->submit(cmdBuffer);
        }
    });

    uiThread.join();
    rasterThread.join();

    EXPECT_FALSE(setupFailed)
        << "command buffer/encoder creation failed mid-race";
}

#endif // __APPLE__
