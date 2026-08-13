// Platform integration tests for Texture and TextureView.
//
// Requires a real GPU device (BUILD_INTEGRATION_TESTS=ON).
// Tests guard against unavailable device creation with GTEST_SKIP().

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/validation_diagnostics.hpp>

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

static std::shared_ptr<Texture> make2DTexture(std::shared_ptr<Device> &device,
                                               uint32_t w, uint32_t h,
                                               PixelFormat fmt = PixelFormat::rgba8unorm,
                                               TextureUsage usage = TextureUsage::textureBinding) {
    return device->createTexture(TextureType::tt2d, fmt, w, h, 1, 1, 1, usage);
}

// ---------------------------------------------------------------------------
// Getter correctness
// ---------------------------------------------------------------------------

TEST(Texture, GetWidthAndHeight) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 256, 128);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getWidth(),  256u);
    EXPECT_EQ(tex->getHeight(), 128u);
}

TEST(Texture, GetFormat) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64, PixelFormat::rgba8unorm);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getFormat(), PixelFormat::rgba8unorm);
}

TEST(Texture, GetDimension) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getDimension(), TextureType::tt2d);
}

TEST(Texture, GetMipLevelCount) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getMipLevelCount(), 1u);
}

TEST(Texture, GetSampleCount) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getSampleCount(), 1u);
}

TEST(Texture, GetUsage) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64, PixelFormat::rgba8unorm,
                             TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getUsage(), TextureUsage::textureBinding);
}

TEST(Texture, GetDepthOrArrayLayers) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);
    // A plain 2-D texture created with depth=1 should report 1.
    EXPECT_GE(tex->getDepthOrarrayLayers(), 1u);
}

// ---------------------------------------------------------------------------
// createView
// ---------------------------------------------------------------------------

TEST(Texture, CreateViewReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);

    // Pass explicit layer count (1) — the -1 sentinel is only safe on Vulkan.
    auto view = tex->createView(PixelFormat::rgba8unorm, 1);
    EXPECT_NE(view, nullptr);
}

TEST(Texture, CreateViewWithExplicitArgs) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 128, 128);
    ASSERT_NE(tex, nullptr);

    auto view = tex->createView(PixelFormat::rgba8unorm,
                                1,           // arrayLayerCount
                                Aspect::all,
                                0, 0,        // baseArrayLayer, baseMipLevel
                                TextureType::tt2d);
    EXPECT_NE(view, nullptr);
}

TEST(Texture, MultipleViewsCanBeCreated) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = make2DTexture(device, 64, 64);
    ASSERT_NE(tex, nullptr);

    auto view1 = tex->createView(PixelFormat::rgba8unorm, 1);
    auto view2 = tex->createView(PixelFormat::rgba8unorm, 1);
    EXPECT_NE(view1, nullptr);
    EXPECT_NE(view2, nullptr);
    // Two separate view objects.
    EXPECT_NE(view1.get(), view2.get());
}

// ---------------------------------------------------------------------------
// Cubemap creation & views
// ---------------------------------------------------------------------------

TEST(Texture, CreateCubeTextureDirectly) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = device->createTexture(
        TextureType::ttCube, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1,
        TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getDimension(), TextureType::ttCube);
    EXPECT_EQ(tex->getWidth(), 64u);
    EXPECT_EQ(tex->getHeight(), 64u);
}

TEST(Texture, CreateCubeArrayTextureDirectly) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = device->createTexture(
        TextureType::ttCubeArray, PixelFormat::rgba8unorm,
        64, 64, 12, 1, 1,
        TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);
    EXPECT_EQ(tex->getDimension(), TextureType::ttCubeArray);
}

TEST(Texture, CreateCubeViewFrom2DArray) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // WebGPU-style: create a 2D texture with 6 array layers, then view as cube.
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 6, 1, 1,
        TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);

    auto view = tex->createView(
        PixelFormat::rgba8unorm,
        6,              // arrayLayerCount
        Aspect::all,
        0, 0,           // baseArrayLayer, baseMipLevel
        TextureType::ttCube);
    EXPECT_NE(view, nullptr);
}

TEST(Texture, CreateCubeArrayViewFrom2DArray) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // WebGPU-style: create a 2D texture with 12 array layers, then view as cube-array.
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 12, 1, 1,
        TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);

    auto view = tex->createView(
        PixelFormat::rgba8unorm,
        12,             // arrayLayerCount
        Aspect::all,
        0, 0,           // baseArrayLayer, baseMipLevel
        TextureType::ttCubeArray);
    EXPECT_NE(view, nullptr);
}

// ---------------------------------------------------------------------------
// upload
// ---------------------------------------------------------------------------

TEST(Texture, UploadReturnsTrueOnSuccess) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 4, H = 4;
    constexpr uint64_t byteSize = W * H * 4; // RGBA8
    uint8_t pixels[byteSize] = {};

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(tex, nullptr);
    EXPECT_TRUE(tex->upload(0, byteSize, pixels));
}

// ---------------------------------------------------------------------------
// Upload / Download round-trip
// ---------------------------------------------------------------------------

TEST(Texture, UploadDownloadRoundtripRandomData) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 64, H = 64;
    constexpr uint64_t byteSize = W * H * 4; // RGBA8

    // Create random pixel data
    std::vector<uint8_t> uploadData(byteSize);
    std::srand(42);  // Fixed seed for reproducibility
    for (auto& byte : uploadData) {
        byte = static_cast<uint8_t>(std::rand() % 256);
    }

    // Create texture with copySrc usage for readback
    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    // Upload data
    EXPECT_TRUE(tex->upload(0, byteSize, uploadData.data()));

    // Download data
    std::vector<uint8_t> downloadData(byteSize);
    EXPECT_TRUE(tex->download(0, 0, downloadData.data(), byteSize));

    // Verify data matches
    EXPECT_EQ(uploadData, downloadData);
}

// ---------------------------------------------------------------------------
// Async download
// ---------------------------------------------------------------------------

TEST(Texture, AsyncDownloadRoundtrip) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 8, H = 8;
    constexpr uint64_t byteSize = W * H * 4; // RGBA8

    std::vector<uint8_t> uploadData(byteSize);
    std::srand(88);
    for (auto& byte : uploadData) {
        byte = static_cast<uint8_t>(std::rand() % 256);
    }

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    EXPECT_TRUE(tex->upload(0, byteSize, uploadData.data()));

    std::vector<uint8_t> downloadData(byteSize);
    bool callbackOk = false;
    bool callbackResult = false;

    tex->downloadAsync(0, 0, downloadData.data(), byteSize,
        [&](bool success) {
            callbackOk = true;
            callbackResult = success;
        });

    EXPECT_TRUE(callbackOk);
    EXPECT_TRUE(callbackResult);
    EXPECT_EQ(uploadData, downloadData);
}

TEST(Texture, UploadDownloadSmallTexture) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Test with a small 2x2 texture
    constexpr uint32_t W = 2, H = 2;
    constexpr uint64_t byteSize = W * H * 4; // RGBA8

    // Create test pattern
    std::vector<uint8_t> uploadData = {
        255, 0,   0,   255,   // Red
        0,   255, 0,   255,   // Green
        0,   0,   255, 255,   // Blue
        255, 255, 0,   255    // Yellow
    };

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    // Upload data
    EXPECT_TRUE(tex->upload(0, byteSize, uploadData.data()));

    // Download data
    std::vector<uint8_t> downloadData(byteSize);
    EXPECT_TRUE(tex->download(0, 0, downloadData.data(), byteSize));

    // Verify data matches
    EXPECT_EQ(uploadData, downloadData);
}

TEST(Texture, UploadDownloadGrayscaleTexture) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 16, H = 16;
    constexpr uint64_t byteSize = W * H; // R8

    // Create random grayscale data
    std::vector<uint8_t> uploadData(byteSize);
    std::srand(99);
    for (auto& byte : uploadData) {
        byte = static_cast<uint8_t>(std::rand() % 256);
    }

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::r8unorm,
        W, H, 1, 1, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc)));
    ASSERT_NE(tex, nullptr);

    // Upload data
    EXPECT_TRUE(tex->upload(0, byteSize, uploadData.data()));

    // Download data
    std::vector<uint8_t> downloadData(byteSize);
    EXPECT_TRUE(tex->download(0, 0, downloadData.data(), byteSize));

    // Verify data matches
    EXPECT_EQ(uploadData, downloadData);
}

// ---------------------------------------------------------------------------
// Regression: destroying a Texture/TextureView must not call
// vkDestroyImage/vkDestroyImageView while the GPU may still be executing a
// command buffer that references them
// (VUID-vkDestroyImage-image-01000 / VUID-vkDestroyImageView-imageView-01026).
// See DeviceData::pendingTextureDestroys' doc comment in common.hpp for the
// fix (a deferred-destruction queue, drained once a frame-ring slot's fence
// confirms GPU completion, instead of destroying immediately).
//
// Only meaningful in a CAMPELLO_GPU_VALIDATION=ON build: validationErrorCount()
// is hardcoded to 0 otherwise (see validation_diagnostics.hpp), since there is
// no messenger installed to actually observe the VUID violation. Build/run
// this suite with -DCAMPELLO_GPU_VALIDATION=ON to exercise the real check;
// without it this test still runs (and passes trivially) so it isn't skipped
// by default configs.
// ---------------------------------------------------------------------------

TEST(Texture, DroppingTextureImmediatelyAfterSubmitTriggersNoValidationError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    resetValidationErrorCount();

    // Everything that references the device lives in this nested scope, so
    // it's guaranteed to be destroyed — in reverse declaration order — before
    // `device` goes out of scope below. Without this, BeginRenderPassDescriptor
    // (desc/ca below) silently keeps its own shared_ptr<TextureView> copy
    // alive until the *test function* returns, which is after the explicit
    // `device.reset()` a manually-ordered version of this test used to have —
    // by then DeviceData is already freed, and TextureView::~TextureView()
    // dereferencing it is its own, unrelated use-after-free. A real
    // application is never going to destroy its Device while it still has
    // live Textures/TextureViews (any more than it would with any other
    // graphics API), so the fix belongs in this test's structure, not in the
    // library.
    {
        // renderTarget | textureBinding, not renderTarget alone: unrelated
        // to what this test checks, RenderPassEncoder::end()'s offscreen
        // branch transitions a color attachment to SHADER_READ_ONLY_OPTIMAL
        // when the pass ends, which is only a valid layout for an image
        // actually created with the SAMPLED usage bit (textureBinding
        // here). Without it, that unconditional transition itself trips
        // VUID-VkImageMemoryBarrier-oldLayout-01211 — a real, pre-existing,
        // unrelated issue (reproducible with renderTarget-only textures in
        // RenderPassEncoder's own tests too) that would otherwise
        // contaminate this test's validationErrorCount() assertion below.
        auto tex = device->createTexture(
            TextureType::tt2d, PixelFormat::rgba8unorm,
            256, 256, 1, 1, 1,
            static_cast<TextureUsage>(
                static_cast<int>(TextureUsage::renderTarget) |
                static_cast<int>(TextureUsage::textureBinding)));
        ASSERT_NE(tex, nullptr);

        auto view = tex->createView(PixelFormat::rgba8unorm, 1);
        ASSERT_NE(view, nullptr);

        auto encoder = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);

        ColorAttachment ca{};
        ca.view          = view;
        ca.loadOp        = LoadOp::clear;
        ca.storeOp       = StoreOp::store;
        ca.clearValue[0] = 1.0f;
        ca.clearValue[1] = 0.0f;
        ca.clearValue[2] = 0.0f;
        ca.clearValue[3] = 1.0f;
        BeginRenderPassDescriptor desc{};
        desc.colorAttachments = { ca };

        auto pass = encoder->beginRenderPass(desc);
        ASSERT_NE(pass, nullptr);
        pass->end();

        auto cmdBuf = encoder->finish();
        ASSERT_NE(cmdBuf, nullptr);

        auto fence = device->createFence();
        device->submit(cmdBuf, fence);

        // The point of this test: drop the texture and its view right now,
        // without waiting for `fence` first — the GPU may well still be
        // executing the render pass that referenced them. Before the fix,
        // this triggered vkDestroyImageView/vkDestroyImage immediately,
        // here, synchronously.
        view.reset();
        tex.reset();

        fence->wait();
    } // desc/ca/pass/encoder/cmdBuf/fence — and their shared_ptr<TextureView>
      // copies — are all fully destroyed by here.

    // Force full teardown -- Device::~Device()'s vkDeviceWaitIdle() plus its
    // pendingTextureDestroys flush is what actually issues the deferred
    // vkDestroyImageView/vkDestroyImage/vkFreeMemory calls for a
    // single-frame test like this one (nothing else calls
    // createCommandEncoder() again to cycle the frame ring and drain them
    // sooner). Checking validationErrorCount() before this point wouldn't
    // distinguish old vs. new behavior, since the deferred calls simply
    // wouldn't have run yet either way.
    device.reset();

    EXPECT_EQ(validationErrorCount(), 0u)
        << "vkDestroyImage/vkDestroyImageView fired while the texture may "
           "still have been in use by GPU-submitted work";
}
