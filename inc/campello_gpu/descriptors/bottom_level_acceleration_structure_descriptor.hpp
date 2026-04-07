#pragma once

#include <vector>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/descriptors/acceleration_structure_geometry_descriptor.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Full configuration for creating and building a bottom-level acceleration
     *        structure (BLAS).
     *
     * A BLAS holds one or more pieces of geometry (triangle meshes or bounding-box
     * sets). It is created by `Device::createBottomLevelAccelerationStructure()` which
     * allocates the backing GPU memory and computes the required scratch buffer sizes.
     * The actual BVH build is recorded later via
     * `CommandEncoder::buildAccelerationStructure()`.
     *
     * The same descriptor is passed to both calls so the backend can keep them
     * consistent without duplicating state in the `AccelerationStructure` object.
     *
     * @code
     * BottomLevelAccelerationStructureDescriptor desc{};
     * desc.geometries  = { triangleGeo };
     * desc.buildFlags  = AccelerationStructureBuildFlag::preferFastTrace;
     *
     * auto blas    = device->createBottomLevelAccelerationStructure(desc);
     * auto scratch = device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);
     * encoder->buildAccelerationStructure(blas, desc, scratch);
     * @endcode
     */
    struct BottomLevelAccelerationStructureDescriptor {
        /**
         * @brief One or more geometry pieces to include in this BLAS.
         *
         * All entries must be the same geometry type (triangles or bounding boxes) on
         * some backends; mixing types within a single BLAS is undefined behaviour.
         */
        std::vector<AccelerationStructureGeometryDescriptor> geometries;

        /**
         * @brief Build-time hints for the BVH construction.
         *
         * Combine `AccelerationStructureBuildFlag` values with bitwise OR.
         * Defaults to `AccelerationStructureBuildFlag::none`.
         */
        AccelerationStructureBuildFlag buildFlags = AccelerationStructureBuildFlag::none;
    };

}
