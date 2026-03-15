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

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
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
