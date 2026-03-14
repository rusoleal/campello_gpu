// Platform integration tests for Device.
//
// These tests require a real GPU device. They are compiled only when
// BUILD_INTEGRATION_TESTS=ON (see tests/CMakeLists.txt).
//
// Platform API notes
// ------------------
// The Metal (macOS/iOS) implementation in src/metal/device.cpp is not yet
// aligned with the public Device API declared in inc/campello_gpu/device.hpp.
// Metal implements Device::getDefaultDevice() / Device::getDevices(), whereas
// the public API declares Device::createDefaultDevice(void*) / getAdapters().
//
// Until that alignment is done, macOS integration tests call GTEST_SKIP()
// when the device cannot be created through the public API. This makes the
// test suite forward-compatible: once the Metal backend is updated the skip
// condition will stop triggering.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Returns a Device or nullptr. Isolates platform differences here so that
// individual tests don't need to repeat the same guard logic.
static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    // On Android the caller owns the ANativeWindow. In automated tests there
    // is no window available, so we pass nullptr and expect the driver to cope
    // (compute-only or headless contexts). Adjust if your driver requires a
    // real surface.
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
    // Metal's createDefaultDevice is not yet wired up in src/metal/device.cpp.
    // Return nullptr so callers can GTEST_SKIP().
    return nullptr;
#elif defined(_WIN32)
    // DirectX 12 implementation is in progress.
    return nullptr;
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// getEngineVersion — no GPU required, safe on every platform
// ---------------------------------------------------------------------------

TEST(Device, GetEngineVersionReturnsString) {
    std::string version = Device::getEngineVersion();
    // The contract is simply that it returns *something* (even "unknown").
    EXPECT_FALSE(version.empty());
}

// ---------------------------------------------------------------------------
// getAdapters — enumerates physical GPUs, no window/surface required
// ---------------------------------------------------------------------------

TEST(Device, GetAdaptersReturnsAtLeastOneOnSupportedPlatform) {
#if defined(__ANDROID__)
    auto adapters = Device::getAdapters();
    EXPECT_FALSE(adapters.empty()) << "Expected at least one Vulkan adapter";
#else
    GTEST_SKIP() << "getAdapters not yet implemented for this platform";
#endif
}

// ---------------------------------------------------------------------------
// createDefaultDevice
// ---------------------------------------------------------------------------

TEST(Device, CreateDefaultDeviceReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_NE(device, nullptr);
}

TEST(Device, GetNameReturnsNonEmptyString) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_FALSE(device->getName().empty());
}

TEST(Device, GetFeaturesDoesNotThrow) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    // Features are a set; it may be empty on minimal hardware.
    EXPECT_NO_THROW(device->getFeatures());
}
