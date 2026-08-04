// Platform integration tests for Linux/Vulkan dma-buf import and its
// supporting DRM capability queries: Device::createTextureFromDmaBuf(),
// Device::getSupportedDmaBufModifiers(), Device::getDrmDeviceNode().
//
// These tests require a real GPU device. They are compiled only when
// BUILD_INTEGRATION_TESTS=ON, and only on Linux (see tests/CMakeLists.txt) --
// this API doesn't exist as symbols on other backends.
//
// CI runs against Mesa lavapipe (software Vulkan, no real GPU/DRM device
// behind it) -- confirmed directly (vulkaninfo) that lavapipe supports
// VK_KHR_external_memory_fd/VK_EXT_external_memory_dma_buf but NOT
// VK_EXT_image_drm_format_modifier or VK_EXT_physical_device_drm, and that
// no /dev/dri node exists in that environment at all. So on CI today:
//   - createTextureFromDmaBuf() always fails (extension gate), before ever
//     needing a real dma-buf -- its *input validation* is still testable.
//   - getSupportedDmaBufModifiers() always returns empty.
//   - getDrmDeviceNode() always returns valid == false.
// The tests below assert on that gracefully-degraded behavior rather than
// assuming a capability lavapipe doesn't have. A real dma-buf round-trip
// test (import something actually GBM-allocated and sample it) needs a
// real GPU and isn't included yet -- deferred until there's real hardware
// to run it against, per the same reasoning as the console's other
// hardware-dependent work.

#if defined(__linux__) && !defined(__ANDROID__)

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/platform/linux_dmabuf.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// createTextureFromDmaBuf -- input validation.
//
// Each case below is rejected before the implementation ever reads a plane
// fd or touches the driver (see the planeCount/width/height check, then the
// fd check, at the top of createTextureFromDmaBuf() in device.cpp) -- so
// these hold regardless of whether the environment has real dma-bufs.
// ---------------------------------------------------------------------------

TEST(DmaBuf, CreateTextureFromDmaBufRejectsMultiPlane) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    DmaBufTextureDescriptor descriptor{};
    descriptor.width = 64;
    descriptor.height = 64;
    descriptor.planeCount = 2; // not yet implemented -- see the descriptor's doc comment
    descriptor.planes[0].fd = 3; // placeholder; rejected before ever being read
    descriptor.planes[1].fd = 4;

    EXPECT_EQ(device->createTextureFromDmaBuf(descriptor), nullptr);
}

TEST(DmaBuf, CreateTextureFromDmaBufRejectsNegativeFd) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    DmaBufTextureDescriptor descriptor{};
    descriptor.width = 64;
    descriptor.height = 64;
    descriptor.planeCount = 1;
    descriptor.planes[0].fd = -1;

    EXPECT_EQ(device->createTextureFromDmaBuf(descriptor), nullptr);
}

TEST(DmaBuf, CreateTextureFromDmaBufRejectsZeroWidth) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    DmaBufTextureDescriptor descriptor{};
    descriptor.width = 0;
    descriptor.height = 64;
    descriptor.planeCount = 1;
    descriptor.planes[0].fd = 3;

    EXPECT_EQ(device->createTextureFromDmaBuf(descriptor), nullptr);
}

TEST(DmaBuf, CreateTextureFromDmaBufRejectsZeroHeight) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    DmaBufTextureDescriptor descriptor{};
    descriptor.width = 64;
    descriptor.height = 0;
    descriptor.planeCount = 1;
    descriptor.planes[0].fd = 3;

    EXPECT_EQ(device->createTextureFromDmaBuf(descriptor), nullptr);
}

// ---------------------------------------------------------------------------
// getSupportedDmaBufModifiers -- pure capability query, no real dma-buf
// needed. Assertions hold whether or not the driver actually implements
// VK_EXT_image_drm_format_modifier (vacuously, when the list is empty).
// ---------------------------------------------------------------------------

TEST(DmaBuf, GetSupportedDmaBufModifiersDoesNotThrow) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    EXPECT_NO_THROW({
        auto modifiers = device->getSupportedDmaBufModifiers(
            PixelFormat::bgra8unorm, TextureUsage::textureBinding);
        (void)modifiers;
    });
}

TEST(DmaBuf, GetSupportedDmaBufModifiersEntriesHaveSanePlaneCounts) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    auto modifiers = device->getSupportedDmaBufModifiers(
        PixelFormat::bgra8unorm, TextureUsage::renderTarget);
    for (const auto &m : modifiers) {
        EXPECT_GE(m.planeCount, 1u);
        EXPECT_LE(m.planeCount, kMaxDmaBufPlanes);
    }
}

TEST(DmaBuf, GetSupportedDmaBufModifiersEmptyWhenNoDrmDevice) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    // Documents the actual current CI environment (lavapipe has no real
    // DRM device, and doesn't implement VK_EXT_image_drm_format_modifier)
    // rather than hardcoding that assumption -- this fails loudly instead
    // of silently passing wrong the day CI's software Vulkan implementation
    // gains modifier support.
    auto node = device->getDrmDeviceNode();
    if (node.valid) {
        GTEST_SKIP() << "device reports a real DRM node -- the "
                         "no-DRM-device assumption below no longer applies "
                         "to this environment";
    }
    auto modifiers = device->getSupportedDmaBufModifiers(
        PixelFormat::bgra8unorm, TextureUsage::textureBinding);
    EXPECT_TRUE(modifiers.empty());
}

// ---------------------------------------------------------------------------
// getDrmDeviceNode -- pure capability query, no real dma-buf needed.
// ---------------------------------------------------------------------------

TEST(DmaBuf, GetDrmDeviceNodeDoesNotThrow) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    EXPECT_NO_THROW({
        auto node = device->getDrmDeviceNode();
        (void)node;
    });
}

TEST(DmaBuf, GetDrmDeviceNodeFieldsConsistentWhenValid) {
    auto device = Device::createDefaultDevice(nullptr);
    ASSERT_NE(device, nullptr);

    auto node = device->getDrmDeviceNode();
    if (!node.valid) {
        GTEST_SKIP() << "VK_EXT_physical_device_drm not supported by this "
                         "device (expected on software renderers like lavapipe, "
                         "which has no real DRM device to report)";
    }
    if (node.hasPrimary) {
        EXPECT_GE(node.primaryMajor, 0);
        EXPECT_GE(node.primaryMinor, 0);
    }
    if (node.hasRender) {
        EXPECT_GE(node.renderMajor, 0);
        EXPECT_GE(node.renderMinor, 0);
    }
}

#endif // defined(__linux__) && !defined(__ANDROID__)
