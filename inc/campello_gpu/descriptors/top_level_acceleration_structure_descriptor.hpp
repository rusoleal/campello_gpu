#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Per-instance data for a top-level acceleration structure.
     *
     * Each instance references one BLAS and places it in world space via a 3×4
     * row-major affine transform. The instance ID and visibility mask let shaders
     * and traversal discriminate between instances at trace time.
     *
     * **Identity-transform instance:**
     * @code
     * AccelerationStructureInstance inst{};
     * inst.blas          = myBlas;
     * inst.instanceId    = 0;
     * inst.mask          = 0xFF;
     * inst.hitGroupOffset = 0;
     * inst.opaque        = true;
     * // transform defaults to identity
     * @endcode
     */
    struct AccelerationStructureInstance {
        /**
         * @brief 3×4 row-major affine transform placing the BLAS in world space.
         *
         * Stored as three rows of four floats. The identity transform is:
         * `{ {1,0,0,0}, {0,1,0,0}, {0,0,1,0} }`.
         */
        float transform[3][4] = {
            { 1.f, 0.f, 0.f, 0.f },
            { 0.f, 1.f, 0.f, 0.f },
            { 0.f, 0.f, 1.f, 0.f }
        };

        /**
         * @brief User-defined 24-bit instance identifier.
         *
         * Accessible in shaders as `InstanceID()` / `gl_InstanceCustomIndexEXT`.
         * Only the lower 24 bits are used; values above 0xFFFFFF are clamped.
         */
        uint32_t instanceId = 0;

        /**
         * @brief 8-bit visibility mask.
         *
         * The instance is intersected only when `(rayMask & mask) != 0`.
         * Set to `0xFF` to make the instance always visible.
         */
        uint8_t mask = 0xFF;

        /**
         * @brief Index of the first hit group in the SBT for this instance.
         *
         * Combined with the geometry index and the trace call's `sbtRecordOffset`
         * to locate the correct hit group shader record.
         */
        uint32_t hitGroupOffset = 0;

        /**
         * @brief When true, any-hit shaders are skipped for all geometry in this instance.
         *
         * Equivalent to setting `VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR` or
         * `MTLAccelerationStructureInstanceOptionOpaque`.
         */
        bool opaque = true;

        /** @brief The bottom-level acceleration structure to instantiate. */
        std::shared_ptr<AccelerationStructure> blas;
    };

    /**
     * @brief Full configuration for creating and building a top-level acceleration
     *        structure (TLAS).
     *
     * A TLAS holds a flat list of `AccelerationStructureInstance` entries, each
     * referencing a BLAS with a transform and metadata. It is created by
     * `Device::createTopLevelAccelerationStructure()` and built by
     * `CommandEncoder::buildAccelerationStructure()`.
     *
     * All BLAS handles referenced by instances must have been built (GPU build
     * commands submitted and completed) before the TLAS build is submitted.
     *
     * @code
     * TopLevelAccelerationStructureDescriptor desc{};
     * desc.instances  = { inst0, inst1 };
     * desc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;
     *
     * auto tlas    = device->createTopLevelAccelerationStructure(desc);
     * auto scratch = device->createBuffer(tlas->getBuildScratchSize(), BufferUsage::storage);
     * encoder->buildAccelerationStructure(tlas, desc, scratch);
     * @endcode
     */
    struct TopLevelAccelerationStructureDescriptor {
        /** @brief List of BLAS instances to include in this TLAS. */
        std::vector<AccelerationStructureInstance> instances;

        /**
         * @brief Build-time hints for the BVH construction.
         *
         * Combine `AccelerationStructureBuildFlag` values with bitwise OR.
         * Defaults to `AccelerationStructureBuildFlag::none`.
         */
        AccelerationStructureBuildFlag buildFlags = AccelerationStructureBuildFlag::none;
    };

}
