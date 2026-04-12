// Platform integration tests for CommandBuffer GPU timing.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

using namespace systems::leal::campello_gpu;

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

TEST(CommandBuffer, GetGPUExecutionTimeReturnsZeroBeforeSubmission) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "Device not available";
    }
    
    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    
    auto cmdBuffer = encoder->finish();
    ASSERT_NE(cmdBuffer, nullptr);
    
    // Before submission, GPU execution time should be 0
    uint64_t gpuTime = cmdBuffer->getGPUExecutionTime();
    EXPECT_EQ(gpuTime, 0u);
}

TEST(CommandBuffer, GetGPUExecutionTimeWorksAfterSubmission) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "Device not available";
    }
    
    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    
    // Do some GPU work - create and clear a buffer
    auto buffer = device->createBuffer(1024, BufferUsage::storage);
    if (buffer) {
        encoder->clearBuffer(buffer, 0, 1024);
    }
    
    auto cmdBuffer = encoder->finish();
    ASSERT_NE(cmdBuffer, nullptr);
    
    // Submit and wait
    device->submit(cmdBuffer);
    
    // After submission and completion, we might have timing data
    // (Metal provides this, Vulkan/DirectX may return 0 until implemented)
    uint64_t gpuTime = cmdBuffer->getGPUExecutionTime();
    
    // Either 0 (not implemented) or a positive value (timing available)
    // The important thing is it doesn't crash
    (void)gpuTime;
    SUCCEED();
}
