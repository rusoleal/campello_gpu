// Platform integration tests for Device.
//
// These tests require a real GPU device. They are compiled only when
// BUILD_INTEGRATION_TESTS=ON (see tests/CMakeLists.txt).

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
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
// getEngineVersion — no GPU required
// ---------------------------------------------------------------------------

TEST(Device, GetEngineVersionReturnsString) {
    std::string version = Device::getEngineVersion();
    EXPECT_FALSE(version.empty());
}

// ---------------------------------------------------------------------------
// getAdapters — enumerates physical GPUs, no window/surface required
// ---------------------------------------------------------------------------

TEST(Device, GetAdaptersReturnsAtLeastOneOnSupportedPlatform) {
#if defined(__ANDROID__) || defined(__APPLE__)
    auto adapters = Device::getAdapters();
    EXPECT_FALSE(adapters.empty()) << "Expected at least one GPU adapter";
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
    EXPECT_NO_THROW(device->getFeatures());
}

// ---------------------------------------------------------------------------
// createSampler
// ---------------------------------------------------------------------------

TEST(Device, CreateSamplerReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::clampToEdge;
    desc.addressModeV = WrapMode::clampToEdge;
    desc.addressModeW = WrapMode::clampToEdge;
    desc.minFilter    = FilterMode::fmLinear;
    desc.magFilter    = FilterMode::fmLinear;
    desc.lodMinClamp  = 0.0;
    desc.lodMaxClamp  = 32.0;
    desc.maxAnisotropy = 1.0;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// createTexture
// ---------------------------------------------------------------------------

TEST(Device, CreateTexture2DReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    EXPECT_NE(texture, nullptr);
}

TEST(Device, CreateTextureDimensionsMatchRequest) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        128, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidth(),  128u);
    EXPECT_EQ(texture->getHeight(),  64u);
    EXPECT_EQ(texture->getFormat(), PixelFormat::rgba8unorm);
    EXPECT_EQ(texture->getDimension(), TextureType::tt2d);
    EXPECT_EQ(texture->getMipLevelCount(), 1u);
    EXPECT_EQ(texture->getSampleCount(),   1u);
}

// ---------------------------------------------------------------------------
// createQuerySet
// ---------------------------------------------------------------------------

TEST(Device, CreateQuerySetReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    QuerySetDescriptor desc{};
    desc.count = 8;
    desc.type  = QuerySetType::occlusion;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

// ---------------------------------------------------------------------------
// createPipelineLayout / createBindGroupLayout / createBindGroup
// ---------------------------------------------------------------------------

TEST(Device, CreatePipelineLayoutReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    PipelineLayoutDescriptor desc{};
    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(Device, CreateBindGroupLayoutReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    BindGroupLayoutDescriptor desc{};
    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(Device, CreateBindGroupReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    BindGroupLayoutDescriptor layoutDesc{};
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    BindGroupDescriptor desc{};
    desc.layout = layout;
    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

// ---------------------------------------------------------------------------
// createCommandEncoder
// ---------------------------------------------------------------------------

TEST(Device, CreateCommandEncoderReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto encoder = device->createCommandEncoder();
    EXPECT_NE(encoder, nullptr);
}

TEST(Device, CommandEncoderFinishReturnsCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto cmdBuffer = encoder->finish();
    EXPECT_NE(cmdBuffer, nullptr);
}
