#include <gtest/gtest.h>
#include <campello_gpu/descriptors/depth_stencil_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/compute_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// StencilDescriptor
// ---------------------------------------------------------------------------

TEST(StencilDescriptor, CanBeConstructed) {
    StencilDescriptor desc{};
    desc.compare     = CompareOp::always;
    desc.failOp      = StencilOp::keep;
    desc.depthFailOp = StencilOp::keep;
    desc.passOp      = StencilOp::replace;

    EXPECT_EQ(desc.compare,     CompareOp::always);
    EXPECT_EQ(desc.failOp,      StencilOp::keep);
    EXPECT_EQ(desc.depthFailOp, StencilOp::keep);
    EXPECT_EQ(desc.passOp,      StencilOp::replace);
}

// ---------------------------------------------------------------------------
// DepthStencilDescriptor
// ---------------------------------------------------------------------------

TEST(DepthStencilDescriptor, CanBeConstructed) {
    DepthStencilDescriptor desc{};
    desc.format             = PixelFormat::depth32float;
    desc.depthCompare       = CompareOp::less;
    desc.depthWriteEnabled  = true;
    desc.depthBias          = 0.0;
    desc.depthBiasClamp     = 0.0;
    desc.depthBiasSlopeScale = 0.0;
    desc.stencilReadMask    = 0xFFFFFFFF;
    desc.stencilWriteMask   = 0xFFFFFFFF;

    EXPECT_EQ(desc.format,          PixelFormat::depth32float);
    EXPECT_EQ(desc.depthCompare,    CompareOp::less);
    EXPECT_TRUE(desc.depthWriteEnabled);
    EXPECT_EQ(desc.stencilReadMask, 0xFFFFFFFFu);
}

TEST(DepthStencilDescriptor, StencilFacesAreOptional) {
    DepthStencilDescriptor desc{};
    EXPECT_FALSE(desc.stencilFront.has_value());
    EXPECT_FALSE(desc.stencilBack.has_value());
}

TEST(DepthStencilDescriptor, StencilFacesCanBeSet) {
    DepthStencilDescriptor desc{};
    StencilDescriptor face{};
    face.compare = CompareOp::equal;
    face.passOp  = StencilOp::incrementClamp;
    desc.stencilFront = face;

    ASSERT_TRUE(desc.stencilFront.has_value());
    EXPECT_EQ(desc.stencilFront->compare, CompareOp::equal);
    EXPECT_EQ(desc.stencilFront->passOp,  StencilOp::incrementClamp);
    EXPECT_FALSE(desc.stencilBack.has_value());
}

// ---------------------------------------------------------------------------
// VertexAttribute / VertexLayout / VertexDescriptor
// ---------------------------------------------------------------------------

TEST(VertexAttribute, CanBeConstructed) {
    VertexAttribute attr{};
    attr.componentType  = ComponentType::ctFloat;
    attr.accessorType   = AccessorType::acVec3;
    attr.offset         = 0;
    attr.shaderLocation = 0;

    EXPECT_EQ(attr.componentType,  ComponentType::ctFloat);
    EXPECT_EQ(attr.accessorType,   AccessorType::acVec3);
    EXPECT_EQ(attr.offset,         0u);
    EXPECT_EQ(attr.shaderLocation, 0u);
}

TEST(VertexLayout, CanBeConstructed) {
    VertexLayout layout{};
    layout.arrayStride = 12.0; // 3 * float = 12 bytes
    layout.stepMode    = StepMode::vertex;

    VertexAttribute pos{};
    pos.componentType  = ComponentType::ctFloat;
    pos.accessorType   = AccessorType::acVec3;
    pos.offset         = 0;
    pos.shaderLocation = 0;
    layout.attributes.push_back(pos);

    EXPECT_DOUBLE_EQ(layout.arrayStride, 12.0);
    EXPECT_EQ(layout.stepMode, StepMode::vertex);
    EXPECT_EQ(layout.attributes.size(), 1u);
    EXPECT_EQ(layout.attributes[0].accessorType, AccessorType::acVec3);
}

TEST(VertexDescriptor, DefaultHasNoBuffers) {
    VertexDescriptor desc{};
    desc.entryPoint = "vertexMain";
    EXPECT_TRUE(desc.buffers.empty());
    EXPECT_EQ(desc.entryPoint, "vertexMain");
    EXPECT_EQ(desc.module, nullptr);
}

// ---------------------------------------------------------------------------
// ColorState / FragmentDescriptor
// ---------------------------------------------------------------------------

TEST(ColorState, CanBeConstructed) {
    ColorState cs{};
    cs.format    = PixelFormat::bgra8unorm;
    cs.writeMask = ColorWrite::all;

    EXPECT_EQ(cs.format,    PixelFormat::bgra8unorm);
    EXPECT_EQ(cs.writeMask, ColorWrite::all);
}

TEST(ColorWrite, FlagValues) {
    EXPECT_EQ(static_cast<int>(ColorWrite::red),   0x01);
    EXPECT_EQ(static_cast<int>(ColorWrite::green),  0x02);
    EXPECT_EQ(static_cast<int>(ColorWrite::blue),   0x04);
    EXPECT_EQ(static_cast<int>(ColorWrite::alpha),  0x08);
    EXPECT_EQ(static_cast<int>(ColorWrite::all),    0x0f);
}

TEST(FragmentDescriptor, DefaultHasNoTargets) {
    FragmentDescriptor desc{};
    desc.entryPoint = "fragmentMain";
    EXPECT_TRUE(desc.targets.empty());
    EXPECT_EQ(desc.entryPoint, "fragmentMain");
}

TEST(FragmentDescriptor, CanAddTarget) {
    FragmentDescriptor desc{};
    ColorState cs{};
    cs.format    = PixelFormat::rgba8unorm;
    cs.writeMask = ColorWrite::all;
    desc.targets.push_back(cs);

    EXPECT_EQ(desc.targets.size(), 1u);
    EXPECT_EQ(desc.targets[0].format, PixelFormat::rgba8unorm);
}

// ---------------------------------------------------------------------------
// RenderPipelineDescriptor
// ---------------------------------------------------------------------------

TEST(RenderPipelineDescriptor, DefaultOptionalFieldsAreEmpty) {
    RenderPipelineDescriptor desc{};
    desc.topology  = PrimitiveTopology::triangleList;
    desc.cullMode  = CullMode::none;
    desc.frontFace = FrontFace::ccw;

    EXPECT_FALSE(desc.depthStencil.has_value());
    EXPECT_FALSE(desc.fragment.has_value());
    EXPECT_EQ(desc.topology,  PrimitiveTopology::triangleList);
    EXPECT_EQ(desc.cullMode,  CullMode::none);
    EXPECT_EQ(desc.frontFace, FrontFace::ccw);
}

TEST(RenderPipelineDescriptor, DepthStencilCanBeSet) {
    RenderPipelineDescriptor desc{};
    DepthStencilDescriptor ds{};
    ds.format            = PixelFormat::depth32float;
    ds.depthCompare      = CompareOp::less;
    ds.depthWriteEnabled = true;
    desc.depthStencil    = ds;

    ASSERT_TRUE(desc.depthStencil.has_value());
    EXPECT_EQ(desc.depthStencil->format, PixelFormat::depth32float);
}

// ---------------------------------------------------------------------------
// ColorAttachment / BeginRenderPassDescriptor
// ---------------------------------------------------------------------------

TEST(ColorAttachment, ClearValueFields) {
    ColorAttachment ca{};
    ca.clearValue[0] = 0.1f;
    ca.clearValue[1] = 0.2f;
    ca.clearValue[2] = 0.3f;
    ca.clearValue[3] = 1.0f;
    ca.loadOp        = LoadOp::clear;
    ca.storeOp       = StoreOp::store;
    ca.depthSlice    = 0;

    EXPECT_FLOAT_EQ(ca.clearValue[0], 0.1f);
    EXPECT_FLOAT_EQ(ca.clearValue[3], 1.0f);
    EXPECT_EQ(ca.loadOp,  LoadOp::clear);
    EXPECT_EQ(ca.storeOp, StoreOp::store);
}

TEST(LoadOp, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(LoadOp::clear), static_cast<int>(LoadOp::load));
}

TEST(StoreOp, ValuesAreDistinct) {
    EXPECT_NE(static_cast<int>(StoreOp::store), static_cast<int>(StoreOp::discard));
}

TEST(BeginRenderPassDescriptor, DefaultHasNoAttachments) {
    BeginRenderPassDescriptor desc{};
    EXPECT_TRUE(desc.colorAttachments.empty());
    EXPECT_FALSE(desc.depthStencilAttachment.has_value());
    EXPECT_EQ(desc.occlusionQuerySet, nullptr);
    EXPECT_TRUE(desc.timestampWrites.empty());
}

TEST(BeginRenderPassDescriptor, CanAddColorAttachment) {
    ColorAttachment ca{};
    ca.clearValue[0] = 0.0f;
    ca.clearValue[1] = 0.0f;
    ca.clearValue[2] = 0.0f;
    ca.clearValue[3] = 1.0f;
    ca.loadOp  = LoadOp::clear;
    ca.storeOp = StoreOp::store;

    BeginRenderPassDescriptor desc{};
    desc.colorAttachments.push_back(ca);

    EXPECT_EQ(desc.colorAttachments.size(), 1u);
}

// ---------------------------------------------------------------------------
// BindGroupDescriptor
// ---------------------------------------------------------------------------

TEST(BindGroupDescriptor, DefaultHasNoEntries) {
    BindGroupDescriptor desc{};
    EXPECT_TRUE(desc.entries.empty());
    EXPECT_EQ(desc.layout, nullptr);
}

// ---------------------------------------------------------------------------
// ComputeDescriptor / ComputePipelineDescriptor
// ---------------------------------------------------------------------------

TEST(ComputeDescriptor, CanBeConstructed) {
    ComputeDescriptor desc{};
    desc.entryPoint = "computeMain";
    EXPECT_EQ(desc.entryPoint, "computeMain");
    EXPECT_EQ(desc.module, nullptr);
}

TEST(ComputePipelineDescriptor, EntryPointPropagates) {
    ComputePipelineDescriptor desc{};
    desc.compute.entryPoint = "myKernel";
    EXPECT_EQ(desc.compute.entryPoint, "myKernel");
    EXPECT_EQ(desc.layout, nullptr);
}
