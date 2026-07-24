// Platform integration tests for the Vulkan frames-in-flight ring and the
// resources tied to its lifetime: per-generation descriptor pool recycling,
// and the upload/render command-pool separation. These run headlessly
// (Device::createDefaultDevice(nullptr)) so no window surface/swapchain is
// involved -- the frame-ring, descriptor pools, and fences are all created
// unconditionally in Device::createDevice() regardless of whether a real
// surface was provided, so they're fully exercisable this way. See
// DeviceData::beginFrameRing() and DeviceData::descriptorPools' doc
// comments in src/vulkan/common.hpp for the design these tests exercise.
//
// Only Vulkan has resource pools sized/recycled specifically around this
// ring; on other backends these tests still run (same public API) but
// aren't regression tests for anything backend-specific there.

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/shader_stage.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
    return Device::createDefaultDevice(nullptr);
#elif defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#elif defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Frame-ring reuse
// ---------------------------------------------------------------------------

// Runs well past kFramesInFlight (2 on Vulkan) so every ring slot gets
// waited-on, reset, and reused several times over -- not just once. Device::
// submit() no longer blocks (see DeviceData::beginFrameRing()'s doc
// comment), so a regression here would show up as either a crash/validation
// error or this test hanging until the internal 3s wait timeout is hit
// repeatedly, rather than a silent pass.
TEST(FrameRing, ManyConsecutiveFrameCyclesDoNotHang) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto buffer = device->createBuffer(1024, BufferUsage::storage);
    ASSERT_NE(buffer, nullptr);

    constexpr int kFrames = 50;
    for (int i = 0; i < kFrames; ++i) {
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr) << "frame " << i;
        encoder->clearBuffer(buffer, 0, 1024);
        auto cmdBuffer = encoder->finish();
        ASSERT_NE(cmdBuffer, nullptr) << "frame " << i;
        device->submit(cmdBuffer);
    }
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Descriptor pool sizing
// ---------------------------------------------------------------------------

// Regression test for the "black screen, zero CPU, zero errors" bug: a
// single fixed 100-set VkDescriptorPool, created once and never reset,
// silently failed every createBindGroup() call past the first ~100 -- see
// DeviceData::descriptorPools' doc comment. All allocations here happen
// within the same frame-ring generation (no createCommandEncoder() call in
// between), matching the real-world trigger noted in that comment: a busy
// UI frame with 150+ draw calls, each allocating its own descriptor set.
TEST(FrameRing, ManyBindGroupsInASingleGenerationAllSucceed) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    EntryObject entry{};
    entry.binding = 0;
    entry.visibility = ShaderStage::vertex;
    entry.type = EntryObjectType::buffer;
    entry.data.buffer.type = EntryObjectBufferType::uniform;
    entry.data.buffer.hasDinamicOffaset = false;
    entry.data.buffer.minBindingSize = 256;
    layoutDesc.entries.push_back(entry);
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    // The old fixed pool capped at 100 sets total; this comfortably exceeds
    // it while staying well under the new per-generation pool's 4096.
    constexpr int kBindGroups = 250;
    for (int i = 0; i < kBindGroups; ++i) {
        auto buffer = device->createBuffer(256, BufferUsage::uniform);
        ASSERT_NE(buffer, nullptr) << "bind group " << i;

        BindGroupDescriptor bgDesc{};
        bgDesc.layout = layout;
        bgDesc.entries = { {0, BufferBinding{buffer, 0, 256}} };
        auto bindGroup = device->createBindGroup(bgDesc);
        ASSERT_NE(bindGroup, nullptr) << "bind group " << i << " failed to allocate";
    }
}

// ---------------------------------------------------------------------------
// Upload / render thread separation
// ---------------------------------------------------------------------------

// Regression test for a VkCommandPool data race: Texture::upload() used to
// share the main render thread's VkCommandPool, and recording into a pool
// concurrently from two threads is undefined behavior in Vulkan (caught via
// the validation layer as UNASSIGNED-Threading-MultipleThreads-Write) --
// see DeviceData::uploadCommandPool's doc comment. This hammers upload()
// from a background thread while the main thread simultaneously submits
// ordinary frames. With CAMPELLO_GPU_VALIDATION=ON and the Khronos
// validation layer installed, a regression here is expected to surface as a
// validation error rather than a silent pass -- lavapipe alone is not
// guaranteed to reproduce the race deterministically.
TEST(FrameRing, ConcurrentTextureUploadDuringFrameSubmissionDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 4, H = 4;
    constexpr uint64_t byteSize = W * H * 4; // RGBA8
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);

    auto buffer = device->createBuffer(1024, BufferUsage::storage);
    ASSERT_NE(buffer, nullptr);

    constexpr int kIterations = 30;
    std::atomic<bool> uploadFailed{false};

    std::thread uploader([&] {
        uint8_t pixels[byteSize] = {};
        for (int i = 0; i < kIterations; ++i) {
            pixels[0] = static_cast<uint8_t>(i);
            if (!tex->upload(0, byteSize, pixels)) uploadFailed = true;
        }
    });

    for (int i = 0; i < kIterations; ++i) {
        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr) << "frame " << i;
        encoder->clearBuffer(buffer, 0, 1024);
        auto cmdBuffer = encoder->finish();
        ASSERT_NE(cmdBuffer, nullptr) << "frame " << i;
        device->submit(cmdBuffer);
    }

    uploader.join();
    EXPECT_FALSE(uploadFailed.load());
}
