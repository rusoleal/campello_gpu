#pragma once

#include "Metal.hpp"
#include "acceleration_structure_handle.hpp"
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <unordered_map>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Build-flag mapping
// ---------------------------------------------------------------------------

static inline MTL::AccelerationStructureUsage toMTLASUsage(AccelerationStructureBuildFlag flags)
{
    MTL::AccelerationStructureUsage usage = MTL::AccelerationStructureUsageNone;
    const int f = static_cast<int>(flags);
    if (f & static_cast<int>(AccelerationStructureBuildFlag::allowUpdate))
        usage |= MTL::AccelerationStructureUsageRefit;
    if (f & static_cast<int>(AccelerationStructureBuildFlag::preferFastBuild))
        usage |= MTL::AccelerationStructureUsagePreferFastBuild;
    return usage;
}

// ---------------------------------------------------------------------------
// BLAS descriptor builder
// ---------------------------------------------------------------------------

// Returns a retained MTL::PrimitiveAccelerationStructureDescriptor built from
// the abstract descriptor. Caller is responsible for releasing it.
static inline MTL::PrimitiveAccelerationStructureDescriptor *
makePrimASDescriptor(const BottomLevelAccelerationStructureDescriptor &desc)
{
    auto *primDesc = MTL::PrimitiveAccelerationStructureDescriptor::alloc()->init();
    primDesc->setUsage(toMTLASUsage(desc.buildFlags));

    auto *geoArray = NS::MutableArray::alloc()->init();

    for (const auto &geo : desc.geometries) {
        if (geo.type == AccelerationStructureGeometryType::triangles) {
            auto *triDesc = MTL::AccelerationStructureTriangleGeometryDescriptor::alloc()->init();

            if (geo.vertexBuffer)
                triDesc->setVertexBuffer(static_cast<MTL::Buffer *>(geo.vertexBuffer->native));
            triDesc->setVertexBufferOffset(geo.vertexOffset);
            triDesc->setVertexStride(geo.vertexStride);

            if (geo.indexBuffer) {
                triDesc->setIndexBuffer(static_cast<MTL::Buffer *>(geo.indexBuffer->native));
                triDesc->setIndexBufferOffset(0);
                triDesc->setIndexType(geo.indexFormat == IndexFormat::uint16
                    ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32);
                triDesc->setTriangleCount(geo.indexCount / 3);
            } else {
                triDesc->setTriangleCount(geo.vertexCount / 3);
            }

            if (geo.transformBuffer) {
                triDesc->setTransformationMatrixBuffer(
                    static_cast<MTL::Buffer *>(geo.transformBuffer->native));
                triDesc->setTransformationMatrixBufferOffset(geo.transformOffset);
                // Our public API stores transforms as row-major float[3][4]
                triDesc->setTransformationMatrixLayout(MTL::MatrixLayoutRowMajor);
            }

            triDesc->setOpaque(geo.opaque);
            geoArray->addObject(triDesc);
            triDesc->release();
        } else {
            auto *aabbDesc =
                MTL::AccelerationStructureBoundingBoxGeometryDescriptor::alloc()->init();

            if (geo.aabbBuffer)
                aabbDesc->setBoundingBoxBuffer(static_cast<MTL::Buffer *>(geo.aabbBuffer->native));
            aabbDesc->setBoundingBoxBufferOffset(geo.aabbOffset);
            aabbDesc->setBoundingBoxStride(geo.aabbStride);
            aabbDesc->setBoundingBoxCount(geo.aabbCount);
            aabbDesc->setOpaque(geo.opaque);
            geoArray->addObject(aabbDesc);
            aabbDesc->release();
        }
    }

    primDesc->setGeometryDescriptors(static_cast<NS::Array *>(geoArray));
    geoArray->release();
    return primDesc;
}

// ---------------------------------------------------------------------------
// TLAS descriptor builder
// ---------------------------------------------------------------------------

// Creates a retained MTL::InstanceAccelerationStructureDescriptor and fills a
// host-visible shared instance buffer. Returns {descriptor, instanceBuffer} —
// both are retained; caller is responsible for releasing both.
static inline std::pair<MTL::InstanceAccelerationStructureDescriptor *, MTL::Buffer *>
makeInstanceASDescriptor(MTL::Device *dev,
                         const TopLevelAccelerationStructureDescriptor &desc)
{
    if (desc.instances.empty())
        return { nullptr, nullptr };

    // Build a deduplicated array of BLAS objects and an index map.
    auto *blasArray = NS::MutableArray::alloc()->init();
    std::unordered_map<const AccelerationStructure *, NS::UInteger> blasIndex;

    for (const auto &inst : desc.instances) {
        if (!inst.blas) continue;
        if (blasIndex.find(inst.blas.get()) == blasIndex.end()) {
            NS::UInteger idx = blasArray->count();
            auto *mtlAs = static_cast<MetalAccelerationStructureData *>(
                inst.blas->native)->accelerationStructure;
            blasArray->addObject(mtlAs);
            blasIndex[inst.blas.get()] = idx;
        }
    }

    // Fill host-visible instance descriptor buffer (UserID variant for instanceId).
    const size_t stride = sizeof(MTL::AccelerationStructureUserIDInstanceDescriptor);
    auto *instanceBuf = dev->newBuffer(desc.instances.size() * stride,
                                       MTL::ResourceStorageModeShared);
    auto *instData = static_cast<MTL::AccelerationStructureUserIDInstanceDescriptor *>(
        instanceBuf->contents());

    for (size_t i = 0; i < desc.instances.size(); i++) {
        const auto &inst = desc.instances[i];
        auto &dst = instData[i];

        // Convert from our row-major float[3][4] to Metal's column-major PackedFloat4x3.
        const float (*t)[4] = inst.transform;
        dst.transformationMatrix[0] = { t[0][0], t[1][0], t[2][0] };
        dst.transformationMatrix[1] = { t[0][1], t[1][1], t[2][1] };
        dst.transformationMatrix[2] = { t[0][2], t[1][2], t[2][2] };
        dst.transformationMatrix[3] = { t[0][3], t[1][3], t[2][3] };

        dst.options = inst.opaque
            ? MTL::AccelerationStructureInstanceOptionOpaque
            : MTL::AccelerationStructureInstanceOptionNone;
        dst.mask                          = inst.mask;
        dst.intersectionFunctionTableOffset = inst.hitGroupOffset;
        dst.accelerationStructureIndex    =
            inst.blas ? blasIndex[inst.blas.get()] : 0;
        dst.userID = inst.instanceId;
    }

    auto *instanceDesc = MTL::InstanceAccelerationStructureDescriptor::alloc()->init();
    instanceDesc->setUsage(toMTLASUsage(desc.buildFlags));
    instanceDesc->setInstancedAccelerationStructures(static_cast<NS::Array *>(blasArray));
    instanceDesc->setInstanceCount(desc.instances.size());
    instanceDesc->setInstanceDescriptorBuffer(instanceBuf);
    instanceDesc->setInstanceDescriptorBufferOffset(0);
    instanceDesc->setInstanceDescriptorStride(stride);
    instanceDesc->setInstanceDescriptorType(
        MTL::AccelerationStructureInstanceDescriptorTypeUserID);

    blasArray->release();
    return { instanceDesc, instanceBuf };
}
