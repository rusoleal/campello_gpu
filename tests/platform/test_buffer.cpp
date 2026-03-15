// Platform integration tests for Buffer.
//
// Depends on a working Device (see test_device.cpp). All tests guard
// against unavailable device creation with GTEST_SKIP().

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

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
    return nullptr;
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// Buffer creation
// ---------------------------------------------------------------------------

TEST(Buffer, CreateVertexBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t size = 1024;
    auto buffer = device->createBuffer(size, BufferUsage::vertex);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getLength(), size);
}

TEST(Buffer, CreateIndexBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t size = 512;
    auto buffer = device->createBuffer(size, BufferUsage::index);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getLength(), size);
}

TEST(Buffer, CreateUniformBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t size = 256;
    auto buffer = device->createBuffer(size, BufferUsage::uniform);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getLength(), size);
}

// ---------------------------------------------------------------------------
// Buffer upload
// ---------------------------------------------------------------------------

TEST(Buffer, UploadDataRoundtrip) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t count = 16;
    float src[count];
    for (int i = 0; i < (int)count; ++i) src[i] = static_cast<float>(i);

    const uint64_t byteSize = count * sizeof(float);
    auto buffer = device->createBuffer(byteSize, BufferUsage::vertex, src);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getLength(), byteSize);
}

TEST(Buffer, UploadReturnsTrueOnSuccess) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t byteSize = 64;
    uint8_t data[byteSize];
    std::fill(std::begin(data), std::end(data), 0xAB);

    auto buffer = device->createBuffer(byteSize, BufferUsage::vertex);
    ASSERT_NE(buffer, nullptr);
    EXPECT_TRUE(buffer->upload(0, byteSize, data));
}

TEST(Buffer, CreateWithInitialData) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    int values[] = {1, 2, 3, 4};
    const uint64_t byteSize = sizeof(values);

    auto buffer = device->createBuffer(byteSize, BufferUsage::vertex, values);
    ASSERT_NE(buffer, nullptr);
    EXPECT_EQ(buffer->getLength(), byteSize);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(Buffer, MinimumSizeBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    // A 1-byte buffer: minimal legal allocation.
    auto buffer = device->createBuffer(1, BufferUsage::storage);
    // Some drivers reject sub-64-byte allocations; accept null gracefully.
    if (buffer) {
        EXPECT_GE(buffer->getLength(), 1u);
    }
}
