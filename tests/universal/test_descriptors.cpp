#include <gtest/gtest.h>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/constants/shader_stage.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// SamplerDescriptor
// ---------------------------------------------------------------------------

TEST(SamplerDescriptor, CanBeConstructed) {
    SamplerDescriptor desc{
        .addressModeU = WrapMode::clampToEdge,
        .addressModeV = WrapMode::clampToEdge,
        .addressModeW = WrapMode::clampToEdge,
        .lodMinClamp  = 0.0,
        .lodMaxClamp  = 32.0,
        .maxAnisotropy = 1.0,
        .magFilter    = FilterMode::fmNearest,
        .minFilter    = FilterMode::fmNearest,
    };
    EXPECT_EQ(desc.addressModeU, WrapMode::clampToEdge);
    EXPECT_EQ(desc.addressModeV, WrapMode::clampToEdge);
    EXPECT_EQ(desc.addressModeW, WrapMode::clampToEdge);
    EXPECT_EQ(desc.magFilter, FilterMode::fmNearest);
    EXPECT_EQ(desc.minFilter, FilterMode::fmNearest);
    EXPECT_DOUBLE_EQ(desc.lodMinClamp,   0.0);
    EXPECT_DOUBLE_EQ(desc.lodMaxClamp,  32.0);
    EXPECT_DOUBLE_EQ(desc.maxAnisotropy, 1.0);
}

TEST(SamplerDescriptor, CompareFieldIsOptional) {
    SamplerDescriptor desc{
        .addressModeU  = WrapMode::repeat,
        .addressModeV  = WrapMode::repeat,
        .addressModeW  = WrapMode::repeat,
        .lodMinClamp   = 0.0,
        .lodMaxClamp   = 1.0,
        .maxAnisotropy = 1.0,
        .magFilter     = FilterMode::fmLinear,
        .minFilter     = FilterMode::fmLinear,
    };
    // compare field must be absent by default (no value set).
    EXPECT_FALSE(desc.compare.has_value());
}

TEST(SamplerDescriptor, CompareFieldCanBeSet) {
    SamplerDescriptor desc{
        .addressModeU  = WrapMode::clampToEdge,
        .addressModeV  = WrapMode::clampToEdge,
        .addressModeW  = WrapMode::clampToEdge,
        .compare       = CompareOp::less,
        .lodMinClamp   = 0.0,
        .lodMaxClamp   = 1.0,
        .maxAnisotropy = 1.0,
        .magFilter     = FilterMode::fmLinear,
        .minFilter     = FilterMode::fmLinear,
    };
    ASSERT_TRUE(desc.compare.has_value());
    EXPECT_EQ(desc.compare.value(), CompareOp::less);
}

TEST(SamplerDescriptor, WrapModeVariants) {
    auto make = [](WrapMode m) -> SamplerDescriptor {
        return {
            .addressModeU  = m,
            .addressModeV  = m,
            .addressModeW  = m,
            .lodMinClamp   = 0.0,
            .lodMaxClamp   = 1.0,
            .maxAnisotropy = 1.0,
            .magFilter     = FilterMode::fmNearest,
            .minFilter     = FilterMode::fmNearest,
        };
    };
    EXPECT_EQ(make(WrapMode::clampToEdge).addressModeU,  WrapMode::clampToEdge);
    EXPECT_EQ(make(WrapMode::repeat).addressModeU,       WrapMode::repeat);
    EXPECT_EQ(make(WrapMode::mirrorRepeat).addressModeU, WrapMode::mirrorRepeat);
}

// ---------------------------------------------------------------------------
// QuerySetDescriptor
// ---------------------------------------------------------------------------

TEST(QuerySetDescriptor, OcclusionQuery) {
    QuerySetDescriptor desc{.count = 32, .type = QuerySetType::occlusion};
    EXPECT_EQ(desc.count, 32u);
    EXPECT_EQ(desc.type, QuerySetType::occlusion);
}

TEST(QuerySetDescriptor, TimestampQuery) {
    QuerySetDescriptor desc{.count = 8, .type = QuerySetType::timestamp};
    EXPECT_EQ(desc.count, 8u);
    EXPECT_EQ(desc.type, QuerySetType::timestamp);
}

TEST(QuerySetDescriptor, ZeroCountIsValid) {
    // The descriptor itself imposes no minimum — GPU driver validates at runtime.
    QuerySetDescriptor desc{.count = 0, .type = QuerySetType::occlusion};
    EXPECT_EQ(desc.count, 0u);
}

// ---------------------------------------------------------------------------
// PushConstantRange / PipelineLayoutDescriptor
// ---------------------------------------------------------------------------

TEST(PushConstantRange, CanBeConstructed) {
    PushConstantRange range{
        .stages = ShaderStage::vertex,
        .offset = 16,
        .size   = 32,
    };
    EXPECT_EQ(range.stages, ShaderStage::vertex);
    EXPECT_EQ(range.offset, 16u);
    EXPECT_EQ(range.size, 32u);
}

TEST(PushConstantRange, OffsetAndSizeDefaultToZero) {
    PushConstantRange range{.stages = ShaderStage::fragment};
    EXPECT_EQ(range.offset, 0u);
    EXPECT_EQ(range.size, 0u);
}

TEST(PipelineLayoutDescriptor, DefaultHasNoPushConstantRanges) {
    PipelineLayoutDescriptor desc{};
    EXPECT_TRUE(desc.bindGroupLayouts.empty());
    EXPECT_TRUE(desc.pushConstantRanges.empty());
}

TEST(PipelineLayoutDescriptor, CanAddPushConstantRange) {
    PipelineLayoutDescriptor desc{};
    desc.pushConstantRanges.push_back({
        .stages = ShaderStage::vertex,
        .offset = 0,
        .size   = 64,
    });
    ASSERT_EQ(desc.pushConstantRanges.size(), 1u);
    EXPECT_EQ(desc.pushConstantRanges[0].stages, ShaderStage::vertex);
    EXPECT_EQ(desc.pushConstantRanges[0].offset, 0u);
    EXPECT_EQ(desc.pushConstantRanges[0].size, 64u);
}

TEST(PipelineLayoutDescriptor, CanAddMultipleNonOverlappingRanges) {
    // A common pattern: one range per stage, back to back.
    PipelineLayoutDescriptor desc{};
    desc.pushConstantRanges.push_back({.stages = ShaderStage::vertex,   .offset = 0,  .size = 16});
    desc.pushConstantRanges.push_back({.stages = ShaderStage::fragment, .offset = 16, .size = 16});
    ASSERT_EQ(desc.pushConstantRanges.size(), 2u);
    EXPECT_EQ(desc.pushConstantRanges[0].stages, ShaderStage::vertex);
    EXPECT_EQ(desc.pushConstantRanges[1].stages, ShaderStage::fragment);
    EXPECT_EQ(desc.pushConstantRanges[1].offset, 16u);
}
