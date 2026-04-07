#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Bitmask hints that control how an acceleration structure is built.
     *
     * Combine flags with bitwise OR in `BottomLevelAccelerationStructureDescriptor`
     * or `TopLevelAccelerationStructureDescriptor` to tune the AS for the intended
     * workload.
     *
     * @code
     * desc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace
     *                 | AccelerationStructureBuildFlag::allowCompaction;
     * @endcode
     */
    enum class AccelerationStructureBuildFlag {
        none             = 0x00, ///< No hints; implementation picks defaults.
        preferFastTrace  = 0x01, ///< Optimise for ray traversal speed (larger build cost).
        preferFastBuild  = 0x02, ///< Optimise for build speed (may reduce trace performance).
        allowUpdate      = 0x04, ///< Allow incremental updates via `updateAccelerationStructure`.
        allowCompaction  = 0x08  ///< Allow post-build compaction to reduce GPU memory usage.
    };

}
