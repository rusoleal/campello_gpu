// Platform integration tests for Fence.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

using namespace systems::leal::campello_gpu;

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

// didFail()/failureReason() are new (device-loss detection). This exercises
// the ordinary, non-failing path: a real submission that completes normally
// must report didFail() == false and an empty failureReason() on every
// backend that implements them (Metal, Vulkan, DirectX) as well as the
// backend that doesn't (WebGPU, which always returns false/empty).
TEST(Fence, DidFailIsFalseAfterSuccessfulSubmission) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "Device not available";
    }

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto buffer = device->createBuffer(1024, BufferUsage::storage);
    if (buffer) {
        encoder->clearBuffer(buffer, 0, 1024);
    }

    auto cmdBuffer = encoder->finish();
    ASSERT_NE(cmdBuffer, nullptr);

    auto fence = device->createFence();
    ASSERT_NE(fence, nullptr);

    device->submit(cmdBuffer, fence);
    EXPECT_TRUE(fence->wait());

    EXPECT_FALSE(fence->didFail());
    EXPECT_TRUE(fence->failureReason().empty());
}
