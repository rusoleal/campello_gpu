#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/bind_group.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Records compute dispatch commands inside a compute pass.
     *
     * A `ComputePassEncoder` is obtained from `CommandEncoder::beginComputePass()`.
     * Use it to bind a compute pipeline and dispatch thread groups. Call `end()` when
     * all compute work for the pass has been recorded.
     *
     * **Usage pattern:**
     * @code
     * auto cpe = encoder->beginComputePass();
     * cpe->setPipeline(computePipeline);
     * cpe->dispatchWorkgroups(groupsX, groupsY, groupsZ);
     * cpe->end();
     * @endcode
     */
    class ComputePassEncoder {
    private:
        friend class CommandEncoder;
        void *native;
        ComputePassEncoder(void *pd);
    public:
       ~ComputePassEncoder();

        /**
         * @brief Dispatches a grid of thread groups.
         *
         * Launches `workgroupCountX × workgroupCountY × workgroupCountZ` thread groups,
         * each of a size determined by the bound compute pipeline.
         *
         * @param workgroupCountX Number of thread groups along the X axis.
         * @param workgroupCountY Number of thread groups along the Y axis (default: 1).
         * @param workgroupCountZ Number of thread groups along the Z axis (default: 1).
         */
        void dispatchWorkgroups(uint64_t workgroupCountX,
                                uint64_t workgroupCountY = 1,
                                uint64_t workgroupCountZ = 1);

        /**
         * @brief Dispatches thread groups with counts sourced from a buffer.
         *
         * The GPU reads the X/Y/Z workgroup counts from `indirectBuffer` at
         * `indirectOffset` at execution time, enabling GPU-driven dispatch.
         *
         * @param indirectBuffer Buffer containing the dispatch arguments.
         * @param indirectOffset Byte offset within the buffer to the argument struct.
         */
        void dispatchWorkgroupsIndirect(std::shared_ptr<Buffer> indirectBuffer,
                                        uint64_t indirectOffset);

        /**
         * @brief Ends the compute pass.
         *
         * Must be called exactly once after all dispatch commands. After this call
         * the encoder must not be used again.
         */
        void end();

        /**
         * @brief Binds a `BindGroup` to the given index for subsequent dispatches.
         *
         * @param index                  Binding group index in the pipeline layout.
         * @param bindGroup              The resource group to bind.
         * @param dynamicOffsets         Dynamic offsets for dynamic-offset bindings.
         * @param dynamicOffsetsStart    First element in `dynamicOffsets` to use.
         * @param dynamicOffsetsLength   Number of dynamic offsets to apply.
         */
        void setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                          const std::vector<uint32_t> &dynamicOffsets,
                          uint64_t dynamicOffsetsStart,
                          uint64_t dynamicOffsetsLength);

        /**
         * @brief Sets the compute pipeline for subsequent dispatches.
         *
         * Must be called before any dispatch. The pipeline encodes the compute shader
         * entry point and thread execution configuration.
         *
         * @param pipeline The compiled compute pipeline to use.
         */
        void setPipeline(std::shared_ptr<ComputePipeline> pipeline);
    };

}
