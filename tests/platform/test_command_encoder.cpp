// Platform integration tests for CommandEncoder.
//
// Requires a real GPU device (BUILD_INTEGRATION_TESTS=ON).
// Tests guard against unavailable device creation with GTEST_SKIP().

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/types/offset_3d.hpp>
#include <campello_gpu/types/extent_3d.hpp>

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
// finish() without a render/compute pass
// ---------------------------------------------------------------------------

TEST(CommandEncoder, FinishWithoutPassReturnsCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(CommandEncoder, MultipleEncodersCanBeCreated) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto enc1 = device->createCommandEncoder();
    auto enc2 = device->createCommandEncoder();
    ASSERT_NE(enc1, nullptr);
    ASSERT_NE(enc2, nullptr);
    EXPECT_NE(enc1.get(), enc2.get());

    auto cb1 = enc1->finish();
    auto cb2 = enc2->finish();
    EXPECT_NE(cb1, nullptr);
    EXPECT_NE(cb2, nullptr);
}

// ---------------------------------------------------------------------------
// clearBuffer
// ---------------------------------------------------------------------------

TEST(CommandEncoder, ClearBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 256;
    auto buffer = device->createBuffer(size, BufferUsage::storage);
    ASSERT_NE(buffer, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    encoder->clearBuffer(buffer, 0, size);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(CommandEncoder, ClearBufferPartialRange) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 512;
    auto buffer = device->createBuffer(size, BufferUsage::storage);
    ASSERT_NE(buffer, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    // Clear only the second 128 bytes.
    encoder->clearBuffer(buffer, 128, 128);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

// ---------------------------------------------------------------------------
// copyBufferToBuffer
// ---------------------------------------------------------------------------

TEST(CommandEncoder, CopyBufferToBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 256;
    auto src = device->createBuffer(size, BufferUsage::storage);
    auto dst = device->createBuffer(size, BufferUsage::storage);
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    encoder->copyBufferToBuffer(src, 0, dst, 0, size);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(CommandEncoder, CopyBufferToBufferWithOffsets) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint64_t size = 512;
    auto src = device->createBuffer(size, BufferUsage::storage);
    auto dst = device->createBuffer(size, BufferUsage::storage);
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    // Copy 128 bytes from src[64] → dst[256].
    encoder->copyBufferToBuffer(src, 64, dst, 256, 128);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

// ---------------------------------------------------------------------------
// beginComputePass
// ---------------------------------------------------------------------------

TEST(CommandEncoder, BeginComputePassReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    EXPECT_NE(pass, nullptr);
}

// ---------------------------------------------------------------------------
// writeTimestamp / resolveQuerySet
// ---------------------------------------------------------------------------

TEST(CommandEncoder, WriteTimestampDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.count = 4;
    desc.type  = QuerySetType::timestamp;
    auto querySet = device->createQuerySet(desc);
    if (!querySet) GTEST_SKIP() << "Timestamp queries not supported";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    encoder->writeTimestamp(querySet, 0);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

// ---------------------------------------------------------------------------
// Device::submit
// ---------------------------------------------------------------------------

TEST(CommandEncoder, SubmitEmptyCommandBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);

    // An empty command list is valid to submit; the GPU should complete
    // the fence wait without error.
    EXPECT_NO_THROW(device->submit(cmdBuf));
}

TEST(CommandEncoder, ResolveQuerySetDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    QuerySetDescriptor desc{};
    desc.count = 4;
    desc.type  = QuerySetType::timestamp;
    auto querySet = device->createQuerySet(desc);
    if (!querySet) GTEST_SKIP() << "Timestamp queries not supported";

    // Destination buffer: 4 queries × 8 bytes each.
    constexpr uint64_t resolveSize = 4 * sizeof(uint64_t);
    auto dst = device->createBuffer(resolveSize, BufferUsage::storage);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    encoder->writeTimestamp(querySet, 0);
    encoder->resolveQuerySet(querySet, 0, 1, dst, 0);

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

// ---------------------------------------------------------------------------
// copyTextureToTexture
// ---------------------------------------------------------------------------

TEST(CommandEncoder, CopyTextureToTextureDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 64, H = 64;
    auto src = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copySrc)));
    auto dst = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copyDst)));
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    encoder->copyTextureToTexture(src, 0, {}, dst, 0, {}, Extent3D(W, H, 1));

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(CommandEncoder, CopyTextureToTextureWithOffsets) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 64, H = 64;
    auto src = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copySrc)));
    auto dst = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copyDst)));
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    // Copy 32x32 region from src[16, 16] to dst[32, 32]
    encoder->copyTextureToTexture(
        src, 0, Offset3D(16, 16, 0),
        dst, 0, Offset3D(32, 32, 0),
        Extent3D(32, 32, 1));

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(CommandEncoder, CopyTextureToTexturePartialRegion) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Create larger destination, smaller source
    auto src = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        32, 32, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copySrc)));
    auto dst = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        128, 128, 1, 1, 1, 
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) | 
            static_cast<int>(TextureUsage::copyDst)));
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    // Copy entire source to middle of destination
    encoder->copyTextureToTexture(
        src, 0, Offset3D(0, 0, 0),
        dst, 0, Offset3D(48, 48, 0),
        Extent3D(32, 32, 1));

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}


TEST(CommandEncoder, CopyTextureToTextureMipLevels) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 64, H = 64;
    auto src = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 2, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc)));
    auto dst = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, 2, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc) |
            static_cast<int>(TextureUsage::copyDst)));
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    // Upload distinct pattern to src mip 0
    std::vector<uint8_t> mip0Data(W * H * 4, 0xAB);
    EXPECT_TRUE(src->upload(0, mip0Data.size(), mip0Data.data()));

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    // Copy src mip 0 → dst mip 1
    encoder->copyTextureToTexture(
        src, 0, Offset3D(0, 0, 0),
        dst, 1, Offset3D(0, 0, 0),
        Extent3D(W, H, 1));

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);

    auto fence = device->createFence();
    device->submit(cmdBuf, fence);
    fence->wait();

    // Download dst mip 1 and verify
    std::vector<uint8_t> readback(mip0Data.size(), 0);
    EXPECT_TRUE(dst->download(1, 0, readback.data(), readback.size()));

    EXPECT_EQ(readback[0], 0xAB);
    EXPECT_EQ(readback[1], 0xAB);
    EXPECT_EQ(readback[2], 0xAB);
    EXPECT_EQ(readback[3], 0xAB);
}

TEST(CommandEncoder, GenerateMipmapsDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 4, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc) |
            static_cast<int>(TextureUsage::copyDst) |
            static_cast<int>(TextureUsage::renderTarget)));
    ASSERT_NE(tex, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    EXPECT_TRUE(encoder->generateMipmaps(tex));

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
    device->submit(cmdBuf);
}

// ---------------------------------------------------------------------------
// Pixel-correctness mipmap generation
// ---------------------------------------------------------------------------

TEST(CommandEncoder, GenerateMipmapsProducesCorrectContent) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    constexpr uint32_t W = 8, H = 8, MIP_LEVELS = 4;
    constexpr uint64_t MIP0_SIZE = W * H * 4; // RGBA8
    constexpr uint64_t MIP1_SIZE = 4 * 4 * 4;
    constexpr uint64_t MIP2_SIZE = 2 * 2 * 4;
    constexpr uint64_t MIP3_SIZE = 1 * 1 * 4;

    // Solid red — filtering a uniform color should always produce the same color
    std::vector<uint8_t> mip0Data(MIP0_SIZE);
    for (size_t i = 0; i < mip0Data.size(); i += 4) {
        mip0Data[i + 0] = 255; // R
        mip0Data[i + 1] = 0;   // G
        mip0Data[i + 2] = 0;   // B
        mip0Data[i + 3] = 255; // A
    }

    auto tex = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        W, H, 1, MIP_LEVELS, 1,
        static_cast<TextureUsage>(
            static_cast<int>(TextureUsage::textureBinding) |
            static_cast<int>(TextureUsage::copySrc) |
            static_cast<int>(TextureUsage::copyDst) |
            static_cast<int>(TextureUsage::renderTarget)));
    ASSERT_NE(tex, nullptr);

    EXPECT_TRUE(tex->upload(0, MIP0_SIZE, mip0Data.data()));

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    EXPECT_TRUE(encoder->generateMipmaps(tex));

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    device->submit(cmdBuf);
    device->waitForIdle(); // ensure GPU mipmap generation is complete before readback

    // Download and verify each mip level
    std::vector<uint8_t> mip1Data(MIP1_SIZE);
    std::vector<uint8_t> mip2Data(MIP2_SIZE);
    std::vector<uint8_t> mip3Data(MIP3_SIZE);

    EXPECT_TRUE(tex->download(0, 0, mip0Data.data(), MIP0_SIZE));
    EXPECT_TRUE(tex->download(1, 0, mip1Data.data(), MIP1_SIZE));
    EXPECT_TRUE(tex->download(2, 0, mip2Data.data(), MIP2_SIZE));
    EXPECT_TRUE(tex->download(3, 0, mip3Data.data(), MIP3_SIZE));

    // All mip levels should still be solid red
    for (size_t i = 0; i < mip0Data.size(); i += 4) {
        EXPECT_EQ(mip0Data[i + 0], 255) << "Mip 0 R mismatch at byte " << i;
        EXPECT_EQ(mip0Data[i + 1], 0)   << "Mip 0 G mismatch at byte " << i;
        EXPECT_EQ(mip0Data[i + 2], 0)   << "Mip 0 B mismatch at byte " << i;
        EXPECT_EQ(mip0Data[i + 3], 255) << "Mip 0 A mismatch at byte " << i;
    }
    for (size_t i = 0; i < mip1Data.size(); i += 4) {
        EXPECT_EQ(mip1Data[i + 0], 255) << "Mip 1 R mismatch at byte " << i;
        EXPECT_EQ(mip1Data[i + 1], 0)   << "Mip 1 G mismatch at byte " << i;
        EXPECT_EQ(mip1Data[i + 2], 0)   << "Mip 1 B mismatch at byte " << i;
        EXPECT_EQ(mip1Data[i + 3], 255) << "Mip 1 A mismatch at byte " << i;
    }
    for (size_t i = 0; i < mip2Data.size(); i += 4) {
        EXPECT_EQ(mip2Data[i + 0], 255) << "Mip 2 R mismatch at byte " << i;
        EXPECT_EQ(mip2Data[i + 1], 0)   << "Mip 2 G mismatch at byte " << i;
        EXPECT_EQ(mip2Data[i + 2], 0)   << "Mip 2 B mismatch at byte " << i;
        EXPECT_EQ(mip2Data[i + 3], 255) << "Mip 2 A mismatch at byte " << i;
    }
    for (size_t i = 0; i < mip3Data.size(); i += 4) {
        EXPECT_EQ(mip3Data[i + 0], 255) << "Mip 3 R mismatch at byte " << i;
        EXPECT_EQ(mip3Data[i + 1], 0)   << "Mip 3 G mismatch at byte " << i;
        EXPECT_EQ(mip3Data[i + 2], 0)   << "Mip 3 B mismatch at byte " << i;
        EXPECT_EQ(mip3Data[i + 3], 255) << "Mip 3 A mismatch at byte " << i;
    }
}
