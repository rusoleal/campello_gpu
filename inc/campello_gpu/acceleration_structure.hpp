#pragma once

#include <stdint.h>

namespace systems::leal::campello_gpu {

    class Device;
    class CommandEncoder;

    /**
     * @brief An opaque handle to a built GPU acceleration structure (BLAS or TLAS).
     *
     * Acceleration structures are the spatial data structures the GPU traverses during
     * ray tracing. A bottom-level AS (BLAS) holds triangle or bounding-box geometry;
     * a top-level AS (TLAS) holds a set of BLAS instances with per-instance transforms
     * and masks. Both are represented by this class.
     *
     * **Typical workflow:**
     * @code
     * // 1. Describe and create the BLAS handle (backing memory allocated by the device).
     * auto blas = device->createBottomLevelAccelerationStructure(blasDesc);
     *
     * // 2. Allocate a scratch buffer large enough for the build.
     * auto scratch = device->createBuffer(blas->getBuildScratchSize(),
     *                                     BufferUsage::storage);
     *
     * // 3. Record the build into a command encoder.
     * encoder->buildAccelerationStructure(blas, blasDesc, scratch);
     *
     * // 4. Submit — the AS is ready once the GPU finishes.
     * device->submit(encoder->finish());
     * @endcode
     *
     * Acceleration structures are created by:
     * - `Device::createBottomLevelAccelerationStructure()`
     * - `Device::createTopLevelAccelerationStructure()`
     */
    class AccelerationStructure {
        friend class Device;
        friend class CommandEncoder;
        friend class RayTracingPassEncoder;
        friend class ComputePassEncoder;
        void *native;

        AccelerationStructure(void *data);

    public:
        ~AccelerationStructure();

        /**
         * @brief Returns the minimum scratch buffer size required to build this AS.
         *
         * Allocate a buffer with at least this many bytes (using
         * `BufferUsage::storage`) and pass it to
         * `CommandEncoder::buildAccelerationStructure()`.
         *
         * @return Required scratch size in bytes.
         */
        uint64_t getBuildScratchSize() const;

        /**
         * @brief Returns the minimum scratch buffer size required for an incremental update.
         *
         * Only valid if the AS was created with
         * `AccelerationStructureBuildFlag::allowUpdate`. The update scratch size is
         * typically smaller than the full build scratch size.
         *
         * @return Required update-scratch size in bytes.
         */
        uint64_t getUpdateScratchSize() const;

        /**
         * @brief Returns the size this acceleration structure compacts to.
         *
         * Available once the GPU has finished a build of this structure with
         * `AccelerationStructureBuildFlag::allowCompaction` (the build records
         * the size query itself). Until then, and for structures built without
         * that flag, it returns 0. It never blocks: wait for the build's
         * command buffer (e.g. `Device::waitForIdle()` or a fence) first.
         *
         * **Compaction workflow:**
         * @code
         * desc.buildFlags = AccelerationStructureBuildFlag::allowCompaction;
         * auto blas = device->createBottomLevelAccelerationStructure(desc);
         * encoder->buildAccelerationStructure(blas, desc, scratch);
         * device->submit(encoder->finish());
         * device->waitForIdle();
         *
         * auto compact = device->createCompactedAccelerationStructure(blas);  // getCompactedSize() bytes
         * encoder = device->createCommandEncoder();
         * encoder->copyAccelerationStructure(blas, compact);  // a compacting copy
         * device->submit(encoder->finish());
         * // Use `compact` from now on; `blas` can be released.
         * @endcode
         *
         * @return The compacted size in bytes, or 0 when not (yet) known.
         */
        uint64_t getCompactedSize() const;
    };

}
