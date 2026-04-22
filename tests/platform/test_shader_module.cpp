// Platform integration tests for ShaderModule.
//
// ShaderModule is a thin wrapper around pre-compiled bytecode — no GPU
// execution is needed, so these tests only verify that the object is
// created successfully and holds the bytes it was given.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/shader_module.hpp>

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
// ShaderModule creation
// ---------------------------------------------------------------------------

TEST(ShaderModule, CreateWithArbitraryBytesReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Any sequence of bytes is accepted; validity is only checked at
    // pipeline-creation time when the bytes are handed to the driver.
    const uint8_t bytes[] = { 0x01, 0x02, 0x03, 0x04, 0xFF, 0xFE };
    auto module = device->createShaderModule(bytes, sizeof(bytes));
    EXPECT_NE(module, nullptr);
}

TEST(ShaderModule, CreateWithEmptyBytesReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Empty bytecode is stored as-is; pipeline creation will reject it later.
    const uint8_t dummy = 0;
    auto module = device->createShaderModule(&dummy, 0);
    EXPECT_NE(module, nullptr);
}

TEST(ShaderModule, MultipleModulesCanBeCreated) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const uint8_t bytesA[] = { 0xAA, 0xBB };
    const uint8_t bytesB[] = { 0x11, 0x22, 0x33 };

    auto modA = device->createShaderModule(bytesA, sizeof(bytesA));
    auto modB = device->createShaderModule(bytesB, sizeof(bytesB));

    ASSERT_NE(modA, nullptr);
    ASSERT_NE(modB, nullptr);
    EXPECT_NE(modA.get(), modB.get());
}
