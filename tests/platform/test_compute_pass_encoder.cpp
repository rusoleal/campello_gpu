// Platform integration tests for ComputePassEncoder.
//
// Commands are recorded via beginComputePass() and the encoder is closed with
// finish(). The resulting CommandBuffer is not submitted — GPU execution is
// not the concern; we verify the command-recording API does not crash.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>

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
// dispatchWorkgroups
// ---------------------------------------------------------------------------

TEST(ComputePassEncoder, DispatchWorkgroupsDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    // No compute pipeline set — command is recorded but not validated until
    // submission.  We never submit, so this is a safe recording-only test.
    EXPECT_NO_THROW(pass->dispatchWorkgroups(1, 1, 1));
}

TEST(ComputePassEncoder, DispatchWorkgroupsLargeCountDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->dispatchWorkgroups(64, 32, 16));
}

TEST(ComputePassEncoder, DispatchWorkgroupsMultipleTimesDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->dispatchWorkgroups(1, 1, 1));
    EXPECT_NO_THROW(pass->dispatchWorkgroups(8, 4, 2));
    EXPECT_NO_THROW(pass->dispatchWorkgroups(16, 1, 1));
}

// ---------------------------------------------------------------------------
// end
// ---------------------------------------------------------------------------

TEST(ComputePassEncoder, EndDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->end());
}

// ---------------------------------------------------------------------------
// setBindGroup — null-safety
// ---------------------------------------------------------------------------

TEST(ComputePassEncoder, SetBindGroupWithNullDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    // Passing a null BindGroup; the implementation guards against this.
    EXPECT_NO_THROW(pass->setBindGroup(0, nullptr, {}, 0, 0));
}

TEST(ComputePassEncoder, SetBindGroupWithValidGroupDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    BindGroupLayoutDescriptor layoutDesc{};
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    BindGroupDescriptor bgDesc{};
    bgDesc.layout = layout;
    auto bindGroup = device->createBindGroup(bgDesc);
    ASSERT_NE(bindGroup, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);

    EXPECT_NO_THROW(pass->setBindGroup(0, bindGroup, {}, 0, 0));
}

// ---------------------------------------------------------------------------
// finish after compute pass
// ---------------------------------------------------------------------------

TEST(ComputePassEncoder, FinishAfterComputePassReturnsCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);
    pass->end();

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}

TEST(ComputePassEncoder, FinishAfterDispatchReturnsCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass = encoder->beginComputePass();
    ASSERT_NE(pass, nullptr);
    pass->dispatchWorkgroups(1, 1, 1);
    pass->end();

    auto cmdBuf = encoder->finish();
    EXPECT_NE(cmdBuf, nullptr);
}
