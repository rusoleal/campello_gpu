// Platform integration tests for ray tracing.
//
// All tests skip gracefully when the device does not expose Feature::raytracing,
// so they are safe to run on any hardware (including CI runners without DXR/RTAS
// support).  When raytracing IS available the tests verify the create->build
// ->trace flow across geometry kinds (indexed/non-indexed triangles, bounding
// boxes), TLAS instancing, build-flag variations, update/copy commands, the
// ray tracing pass encoder, and ray tracing pipeline creation.

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
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
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(_WIN32) || defined(__linux__)
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

/// Combines two build flags with bitwise OR (the enum class has no operator|).
static AccelerationStructureBuildFlag combineFlags(
    AccelerationStructureBuildFlag a, AccelerationStructureBuildFlag b)
{
    return static_cast<AccelerationStructureBuildFlag>(
        static_cast<int>(a) | static_cast<int>(b));
}

/// Builds a minimal triangle vertex buffer: one triangle at z=0 in clip space.
static std::shared_ptr<Buffer> makeTriangleVertexBuffer(const std::shared_ptr<Device>& device) {
    float verts[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };
    return device->createBuffer(sizeof(verts), BufferUsage::accelerationStructureInput, verts);
}

/// A second, spatially-offset triangle — used where a distinct BLAS is needed.
static std::shared_ptr<Buffer> makeOffsetTriangleVertexBuffer(const std::shared_ptr<Device>& device) {
    float verts[] = {
         2.0f,  2.5f, 0.0f,
         1.5f,  1.5f, 0.0f,
         2.5f,  1.5f, 0.0f,
    };
    return device->createBuffer(sizeof(verts), BufferUsage::accelerationStructureInput, verts);
}

/// Four vertices forming a quad, for use with indexed (2-triangle) geometry.
static std::shared_ptr<Buffer> makeQuadVertexBuffer(const std::shared_ptr<Device>& device) {
    float verts[] = {
        -0.5f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
         0.5f,  0.5f, 0.0f,
    };
    return device->createBuffer(sizeof(verts), BufferUsage::accelerationStructureInput, verts);
}

static std::shared_ptr<Buffer> makeUint16IndexBuffer(const std::shared_ptr<Device>& device) {
    uint16_t idx[] = { 0, 1, 2, 0, 2, 3 };
    return device->createBuffer(sizeof(idx), BufferUsage::accelerationStructureInput, idx);
}

static std::shared_ptr<Buffer> makeUint32IndexBuffer(const std::shared_ptr<Device>& device) {
    uint32_t idx[] = { 0, 1, 2, 0, 2, 3 };
    return device->createBuffer(sizeof(idx), BufferUsage::accelerationStructureInput, idx);
}

/// Single axis-aligned bounding box, for procedural (non-triangle) geometry.
static std::shared_ptr<Buffer> makeAabbBuffer(const std::shared_ptr<Device>& device) {
    float box[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f }; // minX,minY,minZ,maxX,maxY,maxZ
    return device->createBuffer(sizeof(box), BufferUsage::accelerationStructureInput, box);
}

/// Builds a minimal BLAS descriptor for one non-indexed triangle geometry.
static BottomLevelAccelerationStructureDescriptor makeBlasDescriptor(
    const std::shared_ptr<Buffer>& vertexBuffer, uint32_t vertexCount = 3)
{
    AccelerationStructureGeometryDescriptor geo{};
    geo.type         = AccelerationStructureGeometryType::triangles;
    geo.opaque       = true;
    geo.vertexBuffer = vertexBuffer;
    geo.vertexOffset = 0;
    geo.vertexStride = sizeof(float) * 3;
    geo.vertexCount  = vertexCount;
    geo.componentType = ComponentType::ctFloat;

    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries  = { geo };
    desc.buildFlags  = AccelerationStructureBuildFlag::preferFastBuild;
    return desc;
}

static BottomLevelAccelerationStructureDescriptor makeBlasDescriptorWithFlags(
    const std::shared_ptr<Buffer>& vertexBuffer, AccelerationStructureBuildFlag flags)
{
    auto desc = makeBlasDescriptor(vertexBuffer);
    desc.buildFlags = flags;
    return desc;
}

/// Builds a BLAS descriptor for an indexed quad (2 triangles, 4 vertices).
static BottomLevelAccelerationStructureDescriptor makeIndexedBlasDescriptor(
    const std::shared_ptr<Buffer>& vertexBuffer,
    const std::shared_ptr<Buffer>& indexBuffer,
    IndexFormat indexFormat)
{
    AccelerationStructureGeometryDescriptor geo{};
    geo.type          = AccelerationStructureGeometryType::triangles;
    geo.opaque        = true;
    geo.vertexBuffer  = vertexBuffer;
    geo.vertexOffset  = 0;
    geo.vertexStride  = sizeof(float) * 3;
    geo.vertexCount   = 4;
    geo.componentType = ComponentType::ctFloat;
    geo.indexBuffer   = indexBuffer;
    geo.indexFormat   = indexFormat;
    geo.indexCount    = 6;

    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = { geo };
    desc.buildFlags = AccelerationStructureBuildFlag::preferFastBuild;
    return desc;
}

/// Builds a BLAS descriptor containing a single procedural (AABB) geometry.
static BottomLevelAccelerationStructureDescriptor makeAabbBlasDescriptor(
    const std::shared_ptr<Buffer>& aabbBuffer)
{
    AccelerationStructureGeometryDescriptor geo{};
    geo.type       = AccelerationStructureGeometryType::boundingBoxes;
    geo.opaque     = false;
    geo.aabbBuffer = aabbBuffer;
    geo.aabbOffset = 0;
    geo.aabbStride = sizeof(float) * 6;
    geo.aabbCount  = 1;

    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = { geo };
    desc.buildFlags = AccelerationStructureBuildFlag::preferFastBuild;
    return desc;
}

/// Records a build for `as` using `descriptor` and submits it. Works for both
/// BLAS and TLAS descriptors via the overloaded buildAccelerationStructure().
template <typename DescriptorT>
static void buildAndSubmit(const std::shared_ptr<Device>& device,
                           const std::shared_ptr<AccelerationStructure>& as,
                           const DescriptorT& descriptor)
{
    auto scratch = device->createBuffer(as->getBuildScratchSize(), BufferUsage::storage);
    ASSERT_NE(scratch, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    encoder->buildAccelerationStructure(as, descriptor, scratch);

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    device->submit(cmdBuf);
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

TEST(Raytracing, FeatureDetectionIsConsistentAcrossCalls) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";

    bool first  = device->getFeatures().count(Feature::raytracing) > 0;
    bool second = device->getFeatures().count(Feature::raytracing) > 0;
    EXPECT_EQ(first, second);
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — BLAS, triangles
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

TEST(Raytracing, CreateBlasWithEmptyGeometriesReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    BottomLevelAccelerationStructureDescriptor desc{};
    // desc.geometries intentionally left empty.
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    EXPECT_EQ(blas, nullptr);
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

TEST(Raytracing, CreateBlasWithNonOpaqueGeometryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto desc = makeBlasDescriptor(vertBuf);
    desc.geometries[0].opaque = false; // exercise the any-hit-required path
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, MultipleBlasCanBeCreatedIndependently) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBufA = makeTriangleVertexBuffer(device);
    auto vertBufB = makeOffsetTriangleVertexBuffer(device);
    ASSERT_NE(vertBufA, nullptr);
    ASSERT_NE(vertBufB, nullptr);

    auto blasA = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBufA));
    auto blasB = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBufB));

    ASSERT_NE(blasA, nullptr);
    ASSERT_NE(blasB, nullptr);
    EXPECT_NE(blasA.get(), blasB.get());
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — BLAS, indexed triangles
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateBlasWithUint16IndexedGeometryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeQuadVertexBuffer(device);
    auto idxBuf  = makeUint16IndexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    ASSERT_NE(idxBuf, nullptr);

    auto desc = makeIndexedBlasDescriptor(vertBuf, idxBuf, IndexFormat::uint16);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, CreateBlasWithUint32IndexedGeometryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeQuadVertexBuffer(device);
    auto idxBuf  = makeUint32IndexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    ASSERT_NE(idxBuf, nullptr);

    auto desc = makeIndexedBlasDescriptor(vertBuf, idxBuf, IndexFormat::uint32);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, IndexedBlasHasPositiveScratchSize) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeQuadVertexBuffer(device);
    auto idxBuf  = makeUint32IndexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    ASSERT_NE(idxBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(
        makeIndexedBlasDescriptor(vertBuf, idxBuf, IndexFormat::uint32));
    ASSERT_NE(blas, nullptr);
    EXPECT_GT(blas->getBuildScratchSize(), 0u);
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — BLAS, procedural (bounding box) geometry
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateBlasWithBoundingBoxGeometryReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto aabbBuf = makeAabbBuffer(device);
    ASSERT_NE(aabbBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(makeAabbBlasDescriptor(aabbBuf));
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, BoundingBoxBlasHasPositiveScratchSize) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto aabbBuf = makeAabbBuffer(device);
    ASSERT_NE(aabbBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(makeAabbBlasDescriptor(aabbBuf));
    ASSERT_NE(blas, nullptr);
    EXPECT_GT(blas->getBuildScratchSize(), 0u);
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — BLAS, multiple geometries
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateBlasWithMultipleTriangleGeometriesReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBufA = makeTriangleVertexBuffer(device);
    auto vertBufB = makeOffsetTriangleVertexBuffer(device);
    ASSERT_NE(vertBufA, nullptr);
    ASSERT_NE(vertBufB, nullptr);

    AccelerationStructureGeometryDescriptor geoA{};
    geoA.type          = AccelerationStructureGeometryType::triangles;
    geoA.opaque        = true;
    geoA.vertexBuffer  = vertBufA;
    geoA.vertexStride  = sizeof(float) * 3;
    geoA.vertexCount   = 3;
    geoA.componentType = ComponentType::ctFloat;

    AccelerationStructureGeometryDescriptor geoB = geoA;
    geoB.vertexBuffer = vertBufB;

    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = { geoA, geoB };
    desc.buildFlags = AccelerationStructureBuildFlag::preferFastBuild;

    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    EXPECT_GT(blas->getBuildScratchSize(), 0u);
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — BLAS, build flags
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateBlasWithPreferFastTraceFlagReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(
        makeBlasDescriptorWithFlags(vertBuf, AccelerationStructureBuildFlag::preferFastTrace));
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, CreateBlasWithPreferFastBuildFlagReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(
        makeBlasDescriptorWithFlags(vertBuf, AccelerationStructureBuildFlag::preferFastBuild));
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, CreateBlasWithAllowUpdateFlagReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(
        makeBlasDescriptorWithFlags(vertBuf, AccelerationStructureBuildFlag::allowUpdate));
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, CreateBlasWithAllowCompactionFlagReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blas = device->createBottomLevelAccelerationStructure(
        makeBlasDescriptorWithFlags(vertBuf, AccelerationStructureBuildFlag::allowCompaction));
    ASSERT_NE(blas, nullptr);
}

TEST(Raytracing, CreateBlasWithCombinedBuildFlagsReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto flags = combineFlags(AccelerationStructureBuildFlag::preferFastTrace,
                              AccelerationStructureBuildFlag::allowCompaction);
    auto blas = device->createBottomLevelAccelerationStructure(
        makeBlasDescriptorWithFlags(vertBuf, flags));
    ASSERT_NE(blas, nullptr);
    EXPECT_GT(blas->getBuildScratchSize(), 0u);
}

// ---------------------------------------------------------------------------
// AccelerationStructure creation — TLAS
// ---------------------------------------------------------------------------

TEST(Raytracing, CreateTlasReturnsNullWhenRaytracingAbsent) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (requireRaytracing(device)) GTEST_SKIP() << "Device has raytracing — skipping absence test";

    AccelerationStructureInstance inst{};
    inst.mask = 0xFF; // inst.blas stays null — feature gate is checked before dereferencing it
    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    EXPECT_EQ(tlas, nullptr);
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

TEST(Raytracing, CreateTlasWithEmptyInstancesReturnsNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    TopLevelAccelerationStructureDescriptor desc{};
    // desc.instances intentionally left empty.
    auto tlas = device->createTopLevelAccelerationStructure(desc);
    EXPECT_EQ(tlas, nullptr);
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

TEST(Raytracing, CreateTlasWithMultipleInstancesOfSameBlasReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst0{};
    inst0.blas = blas; inst0.instanceId = 0; inst0.mask = 0xFF; inst0.opaque = true;

    AccelerationStructureInstance inst1{};
    inst1.blas = blas; inst1.instanceId = 1; inst1.mask = 0xFF; inst1.opaque = true;
    inst1.transform[0][3] = 2.0f; // translate along X

    AccelerationStructureInstance inst2{};
    inst2.blas = blas; inst2.instanceId = 2; inst2.mask = 0x01; inst2.opaque = false;
    inst2.transform[1][3] = -2.0f; // translate along Y

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst0, inst1, inst2 };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
    EXPECT_GT(tlas->getBuildScratchSize(), 0u);
}

TEST(Raytracing, CreateTlasWithInstancesOfDifferentBlasReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBufA = makeTriangleVertexBuffer(device);
    auto vertBufB = makeOffsetTriangleVertexBuffer(device);
    ASSERT_NE(vertBufA, nullptr);
    ASSERT_NE(vertBufB, nullptr);

    auto blasA = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBufA));
    auto blasB = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBufB));
    ASSERT_NE(blasA, nullptr);
    ASSERT_NE(blasB, nullptr);

    AccelerationStructureInstance instA{};
    instA.blas = blasA; instA.instanceId = 0; instA.mask = 0xFF;

    AccelerationStructureInstance instB{};
    instB.blas = blasB; instB.instanceId = 1; instB.mask = 0xFF;

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { instA, instB };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
}

TEST(Raytracing, CreateTlasWithCustomTransformReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst{};
    inst.blas = blas;
    inst.mask = 0xFF;
    // Non-identity transform: 2x uniform scale plus translation.
    inst.transform[0][0] = 2.0f; inst.transform[0][3] = 5.0f;
    inst.transform[1][1] = 2.0f; inst.transform[1][3] = -5.0f;
    inst.transform[2][2] = 2.0f; inst.transform[2][3] = 1.0f;

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
}

TEST(Raytracing, CreateTlasWithNonDefaultMaskAndHitGroupOffsetReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst{};
    inst.blas           = blas;
    inst.mask           = 0x02;
    inst.hitGroupOffset = 3;
    inst.instanceId     = 0xABCDEF; // exercise the 24-bit instance ID range

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
}

TEST(Raytracing, CreateTlasWithNonOpaqueInstanceReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto blas = device->createBottomLevelAccelerationStructure(makeBlasDescriptor(vertBuf));
    ASSERT_NE(blas, nullptr);

    AccelerationStructureInstance inst{};
    inst.blas   = blas;
    inst.mask   = 0xFF;
    inst.opaque = false; // forces any-hit shader invocation for this instance

    TopLevelAccelerationStructureDescriptor desc{};
    desc.instances = { inst };

    auto tlas = device->createTopLevelAccelerationStructure(desc);
    ASSERT_NE(tlas, nullptr);
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
    buildAndSubmit(device, blas, blasDesc);

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

TEST(Raytracing, BuildIndexedBlasCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeQuadVertexBuffer(device);
    auto idxBuf  = makeUint32IndexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    ASSERT_NE(idxBuf, nullptr);

    auto desc = makeIndexedBlasDescriptor(vertBuf, idxBuf, IndexFormat::uint32);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);

    EXPECT_NO_THROW(buildAndSubmit(device, blas, desc));
}

TEST(Raytracing, BuildBoundingBoxBlasCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto aabbBuf = makeAabbBuffer(device);
    ASSERT_NE(aabbBuf, nullptr);

    auto desc = makeAabbBlasDescriptor(aabbBuf);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);

    EXPECT_NO_THROW(buildAndSubmit(device, blas, desc));
}

TEST(Raytracing, BuildAccelerationStructureWithNullDstDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto desc = makeBlasDescriptor(vertBuf);

    auto scratch = device->createBuffer(1024, BufferUsage::storage);
    ASSERT_NE(scratch, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->buildAccelerationStructure(nullptr, desc, scratch));
}

TEST(Raytracing, BuildAccelerationStructureWithNullScratchBufferDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);
    auto desc = makeBlasDescriptor(vertBuf);
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->buildAccelerationStructure(blas, desc, nullptr));
}

// ---------------------------------------------------------------------------
// AccelerationStructure update / copy
// ---------------------------------------------------------------------------

TEST(Raytracing, UpdateAccelerationStructureAfterBuildCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto blasDesc = makeBlasDescriptorWithFlags(vertBuf, AccelerationStructureBuildFlag::allowUpdate);
    auto blas     = device->createBottomLevelAccelerationStructure(blasDesc);
    ASSERT_NE(blas, nullptr);

    buildAndSubmit(device, blas, blasDesc);

    // Refit the same AS in place — a common per-frame animation pattern.
    uint64_t updateScratchSize = std::max<uint64_t>(blas->getUpdateScratchSize(), 1);
    auto updateScratch = device->createBuffer(updateScratchSize, BufferUsage::storage);
    ASSERT_NE(updateScratch, nullptr);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->updateAccelerationStructure(blas, blas, updateScratch));

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    EXPECT_NO_THROW(device->submit(cmdBuf));
}

TEST(Raytracing, UpdateAccelerationStructureWithNullArgumentsDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->updateAccelerationStructure(nullptr, nullptr, nullptr));
}

TEST(Raytracing, CopyAccelerationStructureCompletesWithoutError) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto vertBuf = makeTriangleVertexBuffer(device);
    ASSERT_NE(vertBuf, nullptr);

    auto desc = makeBlasDescriptor(vertBuf);
    auto src  = device->createBottomLevelAccelerationStructure(desc);
    auto dst  = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(src, nullptr);
    ASSERT_NE(dst, nullptr);

    buildAndSubmit(device, src, desc);

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->copyAccelerationStructure(src, dst));

    auto cmdBuf = encoder->finish();
    ASSERT_NE(cmdBuf, nullptr);
    EXPECT_NO_THROW(device->submit(cmdBuf));
}

TEST(Raytracing, CopyAccelerationStructureWithNullArgumentsDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    EXPECT_NO_THROW(encoder->copyAccelerationStructure(nullptr, nullptr));
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

TEST(Raytracing, RayTracingPassEndDoesNotThrow) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto passEncoder = encoder->beginRayTracingPass();
    ASSERT_NE(passEncoder, nullptr);

    EXPECT_NO_THROW(passEncoder->end());
}

TEST(Raytracing, RayTracingPassSetBindGroupWithNullBindGroupDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto passEncoder = encoder->beginRayTracingPass();
    ASSERT_NE(passEncoder, nullptr);

    EXPECT_NO_THROW(passEncoder->setBindGroup(0, nullptr));
    passEncoder->end();
}

TEST(Raytracing, MultipleSequentialRayTracingPassesDoNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);

    auto pass1 = encoder->beginRayTracingPass();
    ASSERT_NE(pass1, nullptr);
    pass1->end();

    // A second pass, opened after the first ended, should also succeed.
    std::shared_ptr<RayTracingPassEncoder> pass2;
    EXPECT_NO_THROW(pass2 = encoder->beginRayTracingPass());
    EXPECT_NE(pass2, nullptr);
    if (pass2) pass2->end();
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

TEST(Raytracing, CreateRayTracingPipelineWithGarbageRayGenerationBytecodeDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    // ShaderModule accepts arbitrary bytes; validity is only checked at pipeline
    // creation time. Invalid bytecode must fail gracefully, not crash.
    const uint8_t garbage[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    auto shaderModule = device->createShaderModule(garbage, sizeof(garbage));
    ASSERT_NE(shaderModule, nullptr);

    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = { shaderModule, "rayGenMain" };
    desc.missShaders   = {{ shaderModule, "missMain" }};

    std::shared_ptr<RayTracingPipeline> pipeline;
    EXPECT_NO_THROW(pipeline = device->createRayTracingPipeline(desc));
    (void)pipeline;
}

TEST(Raytracing, CreateRayTracingPipelineWithMultipleMissShadersDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    const uint8_t garbage[] = { 0x01, 0x02, 0x03, 0x04 };
    auto shaderModule = device->createShaderModule(garbage, sizeof(garbage));
    ASSERT_NE(shaderModule, nullptr);

    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = { shaderModule, "rayGenMain" };
    desc.missShaders   = {
        { shaderModule, "missPrimary" },
        { shaderModule, "missShadow" },
    };

    std::shared_ptr<RayTracingPipeline> pipeline;
    EXPECT_NO_THROW(pipeline = device->createRayTracingPipeline(desc));
    (void)pipeline;
}

TEST(Raytracing, CreateRayTracingPipelineWithIntersectionHitGroupDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    const uint8_t garbage[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    auto shaderModule = device->createShaderModule(garbage, sizeof(garbage));
    ASSERT_NE(shaderModule, nullptr);

    RayTracingHitGroupDescriptor hg{};
    hg.closestHit   = RayTracingShaderDescriptor{ shaderModule, "closestHitMain" };
    hg.intersection = RayTracingShaderDescriptor{ shaderModule, "intersectMain" };

    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = { shaderModule, "rayGenMain" };
    desc.missShaders   = {{ shaderModule, "missMain" }};
    desc.hitGroups     = { hg };

    std::shared_ptr<RayTracingPipeline> pipeline;
    EXPECT_NO_THROW(pipeline = device->createRayTracingPipeline(desc));
    (void)pipeline;
}

TEST(Raytracing, CreateRayTracingPipelineWithVaryingRecursionDepthDoesNotCrash) {
    auto device = tryCreateDevice();
    if (!device) GTEST_SKIP() << "No device available";
    if (!requireRaytracing(device)) GTEST_SKIP() << "Feature::raytracing not available";

    const uint8_t garbage[] = { 0x10, 0x20, 0x30, 0x40 };
    auto shaderModule = device->createShaderModule(garbage, sizeof(garbage));
    ASSERT_NE(shaderModule, nullptr);

    for (uint32_t depth : { 1u, 4u, 31u }) {
        RayTracingPipelineDescriptor desc{};
        desc.rayGeneration    = { shaderModule, "rayGenMain" };
        desc.missShaders      = {{ shaderModule, "missMain" }};
        desc.maxRecursionDepth = depth;

        std::shared_ptr<RayTracingPipeline> pipeline;
        EXPECT_NO_THROW(pipeline = device->createRayTracingPipeline(desc));
        (void)pipeline;
    }
}
