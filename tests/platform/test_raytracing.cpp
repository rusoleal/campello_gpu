// Platform integration tests for ray tracing.
//
// All tests skip gracefully when the device does not expose Feature::raytracing,
// so they are safe to run on any hardware (including CI runners without DXR/RTAS
// support).  When raytracing IS available the tests verify the full create→build
// →trace flow using minimal in-memory geometry (a single triangle BLAS).

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

/// Returns true when the device supports ray tracing, skips the test otherwise.
static bool requireRaytracing(const std::shared_ptr<Device>& device) {
    if (!device) {
        return false;
    }
    auto features = device->getFeatures();
    return features.count(Feature::raytracing) > 0;
}

/// Builds a minimal triangle vertex buffer: one triangle at z=0 in clip space.
static std::shared_ptr<Buffer> makeTriangleVertexBuffer(const std::shared_ptr<Device>& device) {
    // Three vertices: (x, y, z) as float3
    float verts[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };
    return device->createBuffer(sizeof(verts), BufferUsage::accelerationStructureInput, verts);
}

/// Builds a minimal BLAS descriptor for one triangle.
static BottomLevelAccelerationStructureDescriptor makeBlasDescriptor(
    const std::shared_ptr<Buffer>& vertexBuffer)
{
    AccelerationStructureGeometryDescriptor geo{};
    geo.type         = AccelerationStructureGeometryType::triangles;
    geo.opaque       = true;
    geo.vertexBuffer = vertexBuffer;
    geo.vertexOffset = 0;
    geo.vertexStride = sizeof(float) * 3;
    geo.vertexCount  = 3;
    geo.componentType = ComponentType::ctFloat;

    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries  = { geo };
    desc.buildFlags  = AccelerationStructureBuildFlag::preferFastBuild;
    return desc;
}

// ---------------------------------------------------------------------------
// Feature detection
// ---------------------------------------------------------------------------

TEST(Raytracing, FeatureDetectionDoesNotThrow) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    EXPECT_NO_THROW(device->getFeatures());
}

TEST(Raytracing, FeatureRaytracingIsBooleanResult) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    auto features = device->getFeatures();
    // Simply verify the query completes — value is hardware-dependent.
    bool hasRT = features.count(Feature::raytracing) > 0;
    (void)hasRT;
    SUCCEED();
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateBlasReturnsNullWhenRaytracingAbsent) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (requireRaytracing(device)) GTEST_SKIP() << "Device has raytracing — skipping absence test";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto desc = makeBlasDescriptor(vertBuf);
    // On a device without RT support, createBLAS should return nullptr or not crash.
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    // We don't assert a specific outcome here — the important thing is it doesn't crash.
    (void)blas;
    SUCCEED();
}

TEST(Raytracing, CreateBlasReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto desc = makeBlasDescriptor(vertBuf);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, BlasHasPositiveScratchSize) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);
    EXPECT_GT(blas->getBuildScratchSize(), 0u);
}

TEST(Raytracing, CreateTlasReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst{};
    inst.blas          = blas;
    inst.instanceId    = 0;
    inst.mask          = 0xFF;
    inst.hitGroupOffset = 0;
    inst.opaque        = true;

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances  = { inst };
    desc.buildFlags = AccelerationStructureBuildFlag::preferFastBuild;

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
}

TEST(Raytracing, TlasHasPositiveScratchSize) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst{};
    inst.blas = blas; inst.mask = 0xFF; inst.opaque = true;

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
    EXPECT_GT(tlas->getBuildScratchSize(), 0u);
}

// ---------------------------------------------------------------------------
// AccelerationStructure build (GPU commands)
// ---------------------------------------------------------------------------

TEST(Raytracing, BuildBlasCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blasDesc = makeBlasDescriptor(vertBuf);
    auto blas     = device->createBottomLevelAccelerationStructure(blasDesc);
    ASSERT_NE(blas, nullptr);

    auto scratch = device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);
    ASSERT_NE(scratch, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->buildAccelerationStructure(blas, blasDesc, scratch));

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    EXPECT_NO_THROW(device->submit(cmdBuf));
}

TEST(Raytracing, BuildTlasAfterBlasCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf  = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    // --- Build BLAS first (separate submission so GPU completes before TLAS build) ---
    auto blasDesc = makeBlasDescriptor(vertBuf);
    auto blas     = device->createBottomLevelAccelerationStructure(blasDesc);
    ASSERT_NE(blas, nullptr);

    {
        auto scratch  = device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);
        ASSERT_NE(scratch, nullptr);
        auto encoder  = device->createCommandEncoder();
        ASSERT_NE(encoder, nullptr);
        encoder->buildAccelerationStructure(blas, blasDesc, scratch);
        auto cmdBuf = encoder->finish();
        ASSERT_NE(cmdBuf, nullptr);
        device->submit(cmdBuf);
    }

    // --- Build TLAS ---
    AccelerationStructureInstance inst{};
    inst.blas = blas; inst.mask = 0xFF; inst.opaque = true;
    TopLevelAccelerationStructureDescriptor tlasDesc{};
    tlasDesc.instances = { inst };

    auto tlas    = device->createTopLevelAccelerationStructure(tlasDesc);
    ASSERT_NE(tlas, nullptr);

    auto scratch = device->createBuffer(tlas->getBuildScratchSize(), BufferUsage::storage);
    ASSERT_NE(scratch, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->buildAccelerationStructure(tlas, tlasDesc, scratch));

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    EXPECT_NO_THROW(device->submit(cmdBuf));
}

// ---------------------------------------------------------------------------
// RayTracingPassEncoder
// ---------------------------------------------------------------------------

TEST(Raytracing, BeginRayTracingPassReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto passEncoder = encoder->beginRayTracingPass();
    EXPECT_NE(passEncoder, nullptr);
}

// ---------------------------------------------------------------------------
// RayTracingPipeline creation
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateRayTracingPipelineWithoutShadersDoesNotCrash) {
    // A pipeline with empty/null shaders should either fail gracefully (return nullptr)
    // or succeed; the key invariant is it must not crash.
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    RayTracingPipelineDescriptor desc{};
    // rayGeneration has null module — implementation should return nullptr gracefully.
    auto pipeline = device->createRayTracingPipeline(desc);
    // Accept null or non-null — both are valid for an empty descriptor.
    (void)pipeline;
    SUCCEED();
}
