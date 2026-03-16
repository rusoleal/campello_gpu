// Platform integration tests for RenderPipeline and ComputePipeline creation.
//
// Pipeline creation requires valid driver-compiled shader bytecode to succeed.
// These tests cover the creation API surface including expected-failure paths
// (null/empty shader modules) that must return nullptr without crashing.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/pixel_format.hpp>

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
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// RenderPipeline — null / empty shader module (expected-failure paths)
// ---------------------------------------------------------------------------

TEST(RenderPipeline, NullVertexModuleReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // Vertex shader is required by D3D12; null module → PSO creation fails.
    RenderPipelineDescriptor desc{};
    // desc.vertex.module is nullptr by default
    desc.topology  = PrimitiveTopology::triangleList;
    desc.cullMode  = CullMode::none;
    desc.frontFace = FrontFace::ccw;

    auto pipeline = device->createRenderPipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}

TEST(RenderPipeline, EmptyShaderBytecodeReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // A module created with zero bytes produces an empty DXBC/DXIL blob.
    // D3D12 rejects a PSO with empty VS bytecode.
    const uint8_t emptyVS = 0;
    auto vsModule = device->createShaderModule(&emptyVS, 0);
    ASSERT_NE(vsModule, nullptr);

    RenderPipelineDescriptor desc{};
    desc.vertex.module     = vsModule;
    desc.vertex.entryPoint = "VSMain";
    desc.topology          = PrimitiveTopology::triangleList;
    desc.cullMode          = CullMode::none;
    desc.frontFace         = FrontFace::ccw;

    auto pipeline = device->createRenderPipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}

TEST(RenderPipeline, EmptyBytecodeWithFragmentReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const uint8_t emptyVS = 0, emptyPS = 0;
    auto vsModule = device->createShaderModule(&emptyVS, 0);
    auto psModule = device->createShaderModule(&emptyPS, 0);
    ASSERT_NE(vsModule, nullptr);
    ASSERT_NE(psModule, nullptr);

    FragmentDescriptor frag{};
    frag.module     = psModule;
    frag.entryPoint = "PSMain";
    frag.targets    = { ColorState{ PixelFormat::rgba8unorm, ColorWrite::all } };

    RenderPipelineDescriptor desc{};
    desc.vertex.module     = vsModule;
    desc.vertex.entryPoint = "VSMain";
    desc.fragment          = frag;
    desc.topology          = PrimitiveTopology::triangleList;
    desc.cullMode          = CullMode::none;
    desc.frontFace         = FrontFace::ccw;

    auto pipeline = device->createRenderPipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}

// ---------------------------------------------------------------------------
// ComputePipeline — null / empty shader module (expected-failure paths)
// ---------------------------------------------------------------------------

TEST(ComputePipeline, NullComputeModuleReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    // createComputePipeline checks for a null module and returns nullptr
    // before touching D3D12.
    ComputePipelineDescriptor desc{};
    // desc.compute.module is nullptr by default

    auto pipeline = device->createComputePipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}

TEST(ComputePipeline, EmptyBytecodeReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const uint8_t emptyCS = 0;
    auto csModule = device->createShaderModule(&emptyCS, 0);
    ASSERT_NE(csModule, nullptr);

    ComputePipelineDescriptor desc{};
    desc.compute.module     = csModule;
    desc.compute.entryPoint = "CSMain";

    auto pipeline = device->createComputePipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}

TEST(ComputePipeline, EmptyBytecodeWithLayoutReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device on this platform";

    const uint8_t emptyCS = 0;
    auto csModule = device->createShaderModule(&emptyCS, 0);
    ASSERT_NE(csModule, nullptr);

    PipelineLayoutDescriptor layoutDesc{};
    auto layout = device->createPipelineLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    ComputePipelineDescriptor desc{};
    desc.compute.module     = csModule;
    desc.compute.entryPoint = "CSMain";
    desc.layout             = layout;

    auto pipeline = device->createComputePipeline(desc);
    EXPECT_EQ(pipeline, nullptr);
}
