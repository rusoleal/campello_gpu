// Platform integration tests for CommandEncoder.
//
// Requires a real GPU device (BUILD_INTEGRATION_TESTS=ON).
// Tests guard against unavailable device creation with GTEST_SKIP().

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>

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
