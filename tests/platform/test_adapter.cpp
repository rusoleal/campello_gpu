// Platform integration tests for Adapter.
//
// Device::getAdapters() is implemented unconditionally in the Vulkan backend
// (see src/vulkan/device.cpp) but test_device.cpp's
// GetAdaptersReturnsAtLeastOneOnSupportedPlatform test gates it behind
// __ANDROID__/__APPLE__/_WIN32 and skips on Linux — leaving this working
// codepath completely untested there. These tests exercise it directly on
// every platform that has a real Vulkan/Metal/DirectX/WebGPU backend.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// getAdapters
// ---------------------------------------------------------------------------

TEST(Adapter, GetAdaptersReturnsAtLeastOneAdapter) {
    auto adapters = Device::getAdapters();
    EXPECT_FALSE(adapters.empty()) << "Expected at least one GPU adapter";
}

TEST(Adapter, GetAdaptersIsStableAcrossCalls) {
    auto first  = Device::getAdapters();
    auto second = Device::getAdapters();
    EXPECT_EQ(first.size(), second.size());
}

// ---------------------------------------------------------------------------
// Adapter::getFeatures
// ---------------------------------------------------------------------------

TEST(Adapter, GetFeaturesOnEveryAdapterDoesNotThrow) {
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    for (auto& adapter : adapters) {
        ASSERT_NE(adapter, nullptr);
        EXPECT_NO_THROW(adapter->getFeatures());
    }
}

// ---------------------------------------------------------------------------
// Device::createDevice(adapter, pd) — building a device from an explicit
// adapter, as opposed to createDefaultDevice()'s implicit adapter selection.
// ---------------------------------------------------------------------------

static void* platformWindowHandle() {
#if defined(__EMSCRIPTEN__)
    return const_cast<char*>("#canvas");
#else
    return nullptr;
#endif
}

TEST(Adapter, CreateDeviceFromExplicitFirstAdapterReturnsNonNull) {
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    auto device = Device::createDevice(adapters[0], platformWindowHandle());
    EXPECT_NE(device, nullptr);
}

TEST(Adapter, DeviceCreatedFromExplicitAdapterHasNonEmptyName) {
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    auto device = Device::createDevice(adapters[0], platformWindowHandle());
    ASSERT_NE(device, nullptr);
    EXPECT_FALSE(device->getName().empty());
}
