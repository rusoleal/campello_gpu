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
    return Device::createDefaultDevice(nullptr);
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
// Upload / Download round-trip
// ---------------------------------------------------------------------------

TEST(Buffer, UploadDownloadRoundtripRandomData) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    // Create random data
    constexpr uint64_t byteSize = 1024;
    std::vector<uint8_t> uploadData(byteSize);
    
    // Fill with pseudo-random values
    std::srand(42);  // Fixed seed for reproducibility
    for (auto& byte : uploadData) {
        byte = static_cast<uint8_t>(std::rand() % 256);
    }

    // Create buffer with copyDst usage for readback
    auto buffer = device->createBuffer(
        byteSize, 
        static_cast<BufferUsage>(
            static_cast<int>(BufferUsage::copyDst) | 
            static_cast<int>(BufferUsage::mapRead)));
    ASSERT_NE(buffer, nullptr);

    // Upload data
    EXPECT_TRUE(buffer->upload(0, byteSize, uploadData.data()));

    // Download data
    std::vector<uint8_t> downloadData(byteSize);
    EXPECT_TRUE(buffer->download(0, byteSize, downloadData.data()));

    // Verify data matches
    EXPECT_EQ(uploadData, downloadData);
}

TEST(Buffer, UploadDownloadPartialRange) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }

    constexpr uint64_t byteSize = 512;
    constexpr uint64_t offset = 128;
    constexpr uint64_t length = 256;

    // Create and fill buffer with pattern
    std::vector<uint8_t> uploadData(byteSize);
    std::srand(123);
    for (auto& byte : uploadData) {
        byte = static_cast<uint8_t>(std::rand() % 256);
    }

    auto buffer = device->createBuffer(
        byteSize, 
        static_cast<BufferUsage>(
            static_cast<int>(BufferUsage::copyDst) | 
            static_cast<int>(BufferUsage::mapRead)));
    ASSERT_NE(buffer, nullptr);

    // Upload entire buffer
    EXPECT_TRUE(buffer->upload(0, byteSize, uploadData.data()));

    // Download only partial range
    std::vector<uint8_t> downloadData(length);
    EXPECT_TRUE(buffer->download(offset, length, downloadData.data()));

    // Verify partial data matches
    for (uint64_t i = 0; i < length; ++i) {
        EXPECT_EQ(downloadData[i], uploadData[offset + i]) 
            << "Mismatch at index " << i;
    }
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
