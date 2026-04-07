#pragma once

#include <memory>
#include <vector>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>

namespace systems::leal::campello_gpu {

    class CommandEncoder;

    /**
     * @brief Records ray tracing dispatch commands inside a ray tracing pass.
     *
     * A `RayTracingPassEncoder` is obtained from `CommandEncoder::beginRayTracingPass()`.
     * Use it to bind a ray tracing pipeline, set bind groups, and dispatch rays. Call
     * `end()` when all ray tracing work for the pass has been recorded.
     *
     * **Usage pattern:**
     * @code
     * auto rte = encoder->beginRayTracingPass();
     * rte->setPipeline(rtPipeline);
     * rte->setBindGroup(0, bindGroup, {});
     * rte->traceRays(outputWidth, outputHeight);
     * rte->end();
     * @endcode
     *
     * On Metal the pass is implemented as a compute pass with an intersection function
     * table; on Vulkan it maps to `vkCmdTraceRaysKHR`; on DirectX 12 to `DispatchRays`.
     */
    class RayTracingPassEncoder {
        friend class CommandEncoder;
        void *native;

        RayTracingPassEncoder(void *pd);

    public:
        ~RayTracingPassEncoder();

        /**
         * @brief Sets the ray tracing pipeline for subsequent trace calls.
         *
         * Must be called before any `traceRays()` invocation.
         *
         * @param pipeline The compiled ray tracing pipeline to use.
         */
        void setPipeline(std::shared_ptr<RayTracingPipeline> pipeline);

        /**
         * @brief Binds a `BindGroup` to the given index for subsequent trace calls.
         *
         * @param index                  Binding group index in the pipeline layout.
         * @param bindGroup              The resource group to bind.
         * @param dynamicOffsets         Dynamic offsets for dynamic-offset bindings.
         * @param dynamicOffsetsStart    First element in `dynamicOffsets` to use.
         * @param dynamicOffsetsLength   Number of dynamic offsets to apply.
         */
        void setBindGroup(uint32_t index, std::shared_ptr<BindGroup> bindGroup,
                          const std::vector<uint32_t> &dynamicOffsets = {},
                          uint64_t dynamicOffsetsStart = 0,
                          uint64_t dynamicOffsetsLength = 0);

        /**
         * @brief Dispatches a grid of rays.
         *
         * Launches `width × height × depth` rays. The ray generation shader is invoked
         * once per ray; the launch dimensions are available inside the shader as the
         * dispatch grid size.
         *
         * @param width  Number of rays along the X axis (typically the render target width).
         * @param height Number of rays along the Y axis (default: 1).
         * @param depth  Number of rays along the Z axis (default: 1).
         */
        void traceRays(uint32_t width, uint32_t height = 1, uint32_t depth = 1);

        /**
         * @brief Ends the ray tracing pass.
         *
         * Must be called exactly once after all trace commands. After this call the
         * encoder must not be used again.
         */
        void end();
    };

}
