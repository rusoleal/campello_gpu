// Platform integration tests for PipelineLayout.
//
// test_device.cpp already covers "createPipelineLayout with an empty
// descriptor returns non-null". These tests cover the parts that actually
// exercise the layout: bind group layouts (single/multiple), push constant
// ranges (single/multiple/non-overlapping across stages), and both combined.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/constants/shader_stage.hpp>

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

static std::shared_ptr<BindGroupLayout> makeSamplerLayout(std::shared_ptr<Device>& device) {
    BindGroupLayoutDescriptor desc{};
    EntryObject entry{};
    entry.binding    = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type       = EntryObjectType::sampler;
    entry.data.sampler.type = EntryObjectSamplerType::filtering;
    desc.entries.push_back(entry);
    return device->createBindGroupLayout(desc);
}

// ---------------------------------------------------------------------------
// Bind group layouts.
// ---------------------------------------------------------------------------

TEST(PipelineLayout, CreateWithSingleBindGroupLayoutReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto bgLayout = makeSamplerLayout(device);
    ASSERT_NE(bgLayout, nullptr);

    PipelineLayoutDescriptor desc{};
    desc.bindGroupLayouts = { bgLayout };

    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(PipelineLayout, CreateWithMultipleBindGroupLayoutsReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto bgLayoutA = makeSamplerLayout(device);
    auto bgLayoutB = makeSamplerLayout(device);
    ASSERT_NE(bgLayoutA, nullptr);
    ASSERT_NE(bgLayoutB, nullptr);

    PipelineLayoutDescriptor desc{};
    desc.bindGroupLayouts = { bgLayoutA, bgLayoutB };

    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

// ---------------------------------------------------------------------------
// Push constant ranges.
// ---------------------------------------------------------------------------

TEST(PipelineLayout, CreateWithSinglePushConstantRangeReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    PipelineLayoutDescriptor desc{};
    PushConstantRange range{};
    range.stages = ShaderStage::vertex;
    range.offset = 0;
    range.size   = 64;
    desc.pushConstantRanges = { range };

    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(PipelineLayout, CreateWithMultipleNonOverlappingPushConstantRangesReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    PipelineLayoutDescriptor desc{};

    PushConstantRange vertexRange{};
    vertexRange.stages = ShaderStage::vertex;
    vertexRange.offset = 0;
    vertexRange.size   = 32;

    PushConstantRange fragmentRange{};
    fragmentRange.stages = ShaderStage::fragment;
    fragmentRange.offset = 32;
    fragmentRange.size   = 32;

    desc.pushConstantRanges = { vertexRange, fragmentRange };

    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(PipelineLayout, CreateWithBindGroupLayoutsAndPushConstantsReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    auto bgLayout = makeSamplerLayout(device);
    ASSERT_NE(bgLayout, nullptr);

    PipelineLayoutDescriptor desc{};
    desc.bindGroupLayouts = { bgLayout };

    PushConstantRange range{};
    range.stages = ShaderStage::vertex;
    range.offset = 0;
    range.size   = 16;
    desc.pushConstantRanges = { range };

    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

// ---------------------------------------------------------------------------
// Multiple independent pipeline layouts.
// ---------------------------------------------------------------------------

TEST(PipelineLayout, MultiplePipelineLayoutsCanBeCreatedIndependently) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    PipelineLayoutDescriptor descA{};
    PushConstantRange rangeA{};
    rangeA.stages = ShaderStage::vertex;
    rangeA.size   = 16;
    descA.pushConstantRanges = { rangeA };

    PipelineLayoutDescriptor descB{};
    PushConstantRange rangeB{};
    rangeB.stages = ShaderStage::fragment;
    rangeB.size   = 16;
    descB.pushConstantRanges = { rangeB };

    auto layoutA = device->createPipelineLayout(descA);
    auto layoutB = device->createPipelineLayout(descB);

    ASSERT_NE(layoutA, nullptr);
    ASSERT_NE(layoutB, nullptr);
    EXPECT_NE(layoutA.get(), layoutB.get());
}
