#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief Memory storage location for a GPU resource.
     *
     * Controls where the backing memory for a buffer or texture resides, which
     * affects CPU/GPU access patterns and performance characteristics.
     */
    enum class StorageMode
    {
        /**
         * @brief Memory is accessible by both the CPU and GPU (shared/managed memory).
         *
         * Suitable for upload buffers and resources that the CPU writes and the GPU reads.
         * Corresponds to Metal's `MTLStorageModeShared` and Vulkan's host-visible memory.
         */
        hostVisible,

        /**
         * @brief Memory resides exclusively on the GPU (device-local memory).
         *
         * The CPU cannot directly access this memory. Optimal for resources that are
         * written once (via a copy/upload) and read many times by the GPU.
         * Corresponds to Metal's `MTLStorageModePrivate` and Vulkan's device-local memory.
         */
        devicePrivate,

        /**
         * @brief Lazily-allocated GPU memory used only within a single render pass.
         *
         * The actual memory may never be allocated if the resource is never accessed
         * outside of tile/on-chip storage. Ideal for MSAA resolve targets and transient
         * depth buffers on tile-based GPUs (Apple Silicon, mobile).
         * Corresponds to Metal's `MTLStorageModeMemoryless` and Vulkan's lazily-allocated memory.
         */
        deviceTransient
    };

}
