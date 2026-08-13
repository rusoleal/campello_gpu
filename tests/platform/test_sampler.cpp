// Platform integration tests for Sampler.
//
// test_device.cpp already covers the basic "createSampler returns non-null
// with a fully-specified clampToEdge descriptor" case. These tests instead
// exercise the parts of SamplerDescriptor that basic test doesn't: every
// WrapMode/FilterMode/CompareOp combination, the zero-initialized descriptor
// (regression coverage for the WrapMode(0)-is-not-a-valid-enumerator bug that
// used to crash getAddressMode() with SIGILL), and LOD/anisotropy edges.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>

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
// Default-constructed descriptor — regression coverage.
// ---------------------------------------------------------------------------

TEST(Sampler, DefaultConstructedDescriptorDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Previously, addressModeU/V/W had no default member initializer, so
    // this zero-initialized to WrapMode(0) — not a valid enumerator (real
    // values are GL constants 10497/33071/33648) — and crashed the Vulkan
    // backend's getAddressMode() with SIGILL. addressModeU/V/W now default
    // to WrapMode::clampToEdge.
    SamplerDescriptor desc{};
    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// WrapMode coverage — every enumerator, individually and mixed per axis.
// ---------------------------------------------------------------------------

TEST(Sampler, CreateWithRepeatWrapModeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::repeat;
    desc.addressModeV = WrapMode::repeat;
    desc.addressModeW = WrapMode::repeat;
    desc.minFilter     = FilterMode::fmLinear;
    desc.magFilter     = FilterMode::fmLinear;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithMirrorRepeatWrapModeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::mirrorRepeat;
    desc.addressModeV = WrapMode::mirrorRepeat;
    desc.addressModeW = WrapMode::mirrorRepeat;
    desc.minFilter     = FilterMode::fmLinear;
    desc.magFilter     = FilterMode::fmLinear;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithMixedWrapModesPerAxisReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::repeat;
    desc.addressModeV = WrapMode::clampToEdge;
    desc.addressModeW = WrapMode::mirrorRepeat;
    desc.minFilter     = FilterMode::fmNearest;
    desc.magFilter     = FilterMode::fmNearest;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// FilterMode coverage — nearest, linear, and every mipmap combination.
// ---------------------------------------------------------------------------

TEST(Sampler, CreateWithNearestFilterReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter = FilterMode::fmNearest;
    desc.magFilter = FilterMode::fmNearest;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithLinearFilterReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter = FilterMode::fmLinear;
    desc.magFilter = FilterMode::fmLinear;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithEveryMipmapFilterModeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const FilterMode mipmapModes[] = {
        FilterMode::fmNearestMipmapNearest,
        FilterMode::fmLinearMipmapNearest,
        FilterMode::fmNearestMipmapLinear,
        FilterMode::fmLinearMipmapLinear,
    };

    for (auto mode : mipmapModes) {
        SamplerDescriptor desc{};
        desc.minFilter    = mode;
        desc.magFilter    = mode;
        desc.lodMinClamp  = 0.0;
        desc.lodMaxClamp  = 32.0;

        auto sampler = device->createSampler(desc);
        EXPECT_NE(sampler, nullptr) << "mode = " << static_cast<int>(mode);
    }
}

// ---------------------------------------------------------------------------
// Comparison samplers.
// ---------------------------------------------------------------------------

TEST(Sampler, CreateWithComparisonFunctionReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter = FilterMode::fmLinear;
    desc.magFilter = FilterMode::fmLinear;
    desc.compare   = CompareOp::less;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithEveryCompareOpReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const CompareOp ops[] = {
        CompareOp::never,     CompareOp::less,        CompareOp::equal,
        CompareOp::lessEqual, CompareOp::greater,      CompareOp::notEqual,
        CompareOp::greaterEqual, CompareOp::always,
    };

    for (auto op : ops) {
        SamplerDescriptor desc{};
        desc.minFilter = FilterMode::fmLinear;
        desc.magFilter = FilterMode::fmLinear;
        desc.compare   = op;

        auto sampler = device->createSampler(desc);
        EXPECT_NE(sampler, nullptr) << "op = " << static_cast<int>(op);
    }
}

TEST(Sampler, CreateWithoutComparisonFunctionReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter = FilterMode::fmLinear;
    desc.magFilter = FilterMode::fmLinear;
    desc.compare   = std::nullopt;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// LOD range and anisotropy edges.
// ---------------------------------------------------------------------------

TEST(Sampler, CreateWithZeroLodRangeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter    = FilterMode::fmLinear;
    desc.magFilter    = FilterMode::fmLinear;
    desc.lodMinClamp  = 0.0;
    desc.lodMaxClamp  = 0.0;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithHighAnisotropyReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter     = FilterMode::fmLinear;
    desc.magFilter     = FilterMode::fmLinear;
    desc.maxAnisotropy = 16.0;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

TEST(Sampler, CreateWithAnisotropyDisabledReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor desc{};
    desc.minFilter     = FilterMode::fmLinear;
    desc.magFilter     = FilterMode::fmLinear;
    desc.maxAnisotropy = 1.0;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// Multiple independent samplers.
// ---------------------------------------------------------------------------

TEST(Sampler, MultipleSamplersCanBeCreatedIndependently) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    SamplerDescriptor descA{};
    descA.minFilter = FilterMode::fmNearest;
    descA.magFilter = FilterMode::fmNearest;

    SamplerDescriptor descB{};
    descB.minFilter = FilterMode::fmLinear;
    descB.magFilter = FilterMode::fmLinear;

    auto samplerA = device->createSampler(descA);
    auto samplerB = device->createSampler(descB);

    ASSERT_NE(samplerA, nullptr);
    ASSERT_NE(samplerB, nullptr);
    EXPECT_NE(samplerA.get(), samplerB.get());
}
