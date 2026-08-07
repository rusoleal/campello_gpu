// Platform integration tests for TextureView.
//
// Focuses on Texture::createView()'s parameters that aren't exercised
// elsewhere: explicit array-layer counts, mip subranges, depth/stencil
// aspect selection, array textures, cube textures, and TextureView::fromNative().

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
#elif defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#elif defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Basic view creation.
// ---------------------------------------------------------------------------

TEST(TextureView, CreateViewWithDefaultParamsReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(PixelFormat::rgba8unorm);
    EXPECT_NE(view, nullptr);
}

TEST(TextureView, CreateViewWithExplicitArrayLayerCountReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(PixelFormat::rgba8unorm, /*arrayLayerCount=*/1);
    EXPECT_NE(view, nullptr);
}

// ---------------------------------------------------------------------------
// Mip subranges.
// ---------------------------------------------------------------------------

TEST(TextureView, CreateViewOfMipSubrangeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, /*mipLevels=*/4, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    // A view exposing only mip level 2 onward.
    auto view = texture->createView(
        PixelFormat::rgba8unorm, /*arrayLayerCount=*/1,
        Aspect::all, /*baseArrayLayer=*/0, /*baseMipLevel=*/2);
    EXPECT_NE(view, nullptr);
}

TEST(TextureView, CreateViewOfEachMipLevelIndividuallyReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t mipLevels = 4;
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, mipLevels, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    for (uint32_t mip = 0; mip < mipLevels; ++mip) {
        auto view = texture->createView(
            PixelFormat::rgba8unorm, /*arrayLayerCount=*/1,
            Aspect::all, /*baseArrayLayer=*/0, /*baseMipLevel=*/mip);
        EXPECT_NE(view, nullptr) << "mip = " << mip;
    }
}

// ---------------------------------------------------------------------------
// Array textures.
// ---------------------------------------------------------------------------

TEST(TextureView, CreateViewOfArrayTextureFullRangeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, /*depth(arrayLayers)=*/4, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    // arrayLayerCount = -1 (default) means "all remaining layers".
    auto view = texture->createView(PixelFormat::rgba8unorm);
    EXPECT_NE(view, nullptr);
}

TEST(TextureView, CreateViewOfSingleArrayLayerReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, /*depth(arrayLayers)=*/4, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(
        PixelFormat::rgba8unorm, /*arrayLayerCount=*/1,
        Aspect::all, /*baseArrayLayer=*/2);
    EXPECT_NE(view, nullptr);
}

// ---------------------------------------------------------------------------
// Cube textures.
// ---------------------------------------------------------------------------

TEST(TextureView, CreateViewOfCubeTextureReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::ttCube, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(
        PixelFormat::rgba8unorm, /*arrayLayerCount=*/6,
        Aspect::all, /*baseArrayLayer=*/0, /*baseMipLevel=*/0,
        TextureType::ttCube);
    EXPECT_NE(view, nullptr);
}

// ---------------------------------------------------------------------------
// Depth/stencil aspects.
// ---------------------------------------------------------------------------

TEST(TextureView, CreateViewWithDepthOnlyAspectReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::depth32float,
        64, 64, 1, 1, 1, TextureUsage::renderTarget);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(
        PixelFormat::depth32float, /*arrayLayerCount=*/1, Aspect::depthOnly);
    EXPECT_NE(view, nullptr);
}

TEST(TextureView, CreateViewWithCombinedDepthStencilAllAspectReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::depth24plus_stencil8,
        64, 64, 1, 1, 1, TextureUsage::renderTarget);
    ASSERT_NE(texture, nullptr);

    auto view = texture->createView(
        PixelFormat::depth24plus_stencil8, /*arrayLayerCount=*/1, Aspect::all);
    EXPECT_NE(view, nullptr);
}

// ---------------------------------------------------------------------------
// Multiple views of the same texture.
// ---------------------------------------------------------------------------

TEST(TextureView, MultipleViewsOfSameTextureCanCoexist) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);

    auto viewA = texture->createView(PixelFormat::rgba8unorm);
    auto viewB = texture->createView(PixelFormat::rgba8unorm);

    ASSERT_NE(viewA, nullptr);
    ASSERT_NE(viewB, nullptr);
    EXPECT_NE(viewA.get(), viewB.get());
}

// ---------------------------------------------------------------------------
// TextureView::fromNative — wraps an externally-owned handle.
// ---------------------------------------------------------------------------

TEST(TextureView, FromNativeWithNullHandleReturnsNonNullWrapper) {
    // fromNative() does not require a device or GPU — it just wraps
    // whatever native handle is given. A null handle is a degenerate but
    // legal input: the wrapper object itself must still be constructed
    // (the caller is responsible for not using it as a real attachment).
    auto view = TextureView::fromNative(nullptr);
    EXPECT_NE(view, nullptr);
}
