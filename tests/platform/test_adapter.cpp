// Platform integration tests for Adapter.
//
// Device::getAdapters() is implemented unconditionally in the Vulkan backend
// (see src/vulkan/device.cpp) but test_device.cpp's
// GetAdaptersReturnsAtLeastOneOnSupportedPlatform test gates it behind
// __ANDROID__/__APPLE__/_WIN32 and skips on Linux — leaving this working
// codepath completely untested there. These tests exercise it directly on
// every platform that has a real Vulkan/Metal/DirectX backend.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/adapter.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// getAdapters
// ---------------------------------------------------------------------------

TEST(Adapter, GetAdaptersReturnsAtLeastOneAdapter) {
#if defined(__EMSCRIPTEN__)
    GTEST_SKIP() << "getAdapters not implemented on this platform yet";
#else
    auto adapters = Device::getAdapters();
    EXPECT_FALSE(adapters.empty()) << "Expected at least one GPU adapter";
#endif
}

TEST(Adapter, GetAdaptersIsStableAcrossCalls) {
#if defined(__EMSCRIPTEN__)
    GTEST_SKIP() << "getAdapters not implemented on this platform yet";
#else
    auto first  = Device::getAdapters();
    auto second = Device::getAdapters();
    EXPECT_EQ(first.size(), second.size());
#endif
}

// ---------------------------------------------------------------------------
// Adapter::getFeatures
// ---------------------------------------------------------------------------

TEST(Adapter, GetFeaturesOnEveryAdapterDoesNotThrow) {
#if defined(__EMSCRIPTEN__)
    GTEST_SKIP() << "getAdapters not implemented on this platform yet";
#else
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    for (auto& adapter : adapters) {
        ASSERT_NE(adapter, nullptr);
        EXPECT_NO_THROW(adapter->getFeatures());
    }
#endif
}

// ---------------------------------------------------------------------------
// Device::createDevice(adapter, pd) — building a device from an explicit
// adapter, as opposed to createDefaultDevice()'s implicit adapter selection.
// ---------------------------------------------------------------------------

static void* platformWindowHandle() {
#if defined(__EMSCRIPTEN__)
    return (void*)"#canvas";
#else
    return nullptr;
#endif
}

TEST(Adapter, CreateDeviceFromExplicitFirstAdapterReturnsNonNull) {
#if defined(__EMSCRIPTEN__)
    GTEST_SKIP() << "getAdapters not implemented on this platform yet";
#else
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    auto device = Device::createDevice(adapters[0], platformWindowHandle());
    EXPECT_NE(device, nullptr);
#endif
}

TEST(Adapter, DeviceCreatedFromExplicitAdapterHasNonEmptyName) {
#if defined(__EMSCRIPTEN__)
    GTEST_SKIP() << "getAdapters not implemented on this platform yet";
#else
    auto adapters = Device::getAdapters();
    if (adapters.empty()) GTEST_SKIP() << "No adapters available";

    auto device = Device::createDevice(adapters[0], platformWindowHandle());
    ASSERT_NE(device, nullptr);
    EXPECT_FALSE(device->getName().empty());
#endif
}
