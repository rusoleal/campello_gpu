#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu
{

    /**
     * @brief Size of a single compute threadgroup for a compiled pipeline.
     *
     * On most backends this reflects the shader's declared local workgroup size.
     * On Metal it reflects the thread execution width the backend will use when
     * dispatching, because the Metal encoder currently ignores the shader-declared
     * threadgroup size.
     */
    struct WorkgroupSize
    {
        uint32_t x = 1;
        uint32_t y = 1;
        uint32_t z = 1;
    };

    /**
     * @brief A compiled, immutable compute pipeline state object.
     *
     * A `ComputePipeline` bundles a single compute shader entry point and its pipeline
     * layout into a pre-compiled GPU object. It is bound during a compute pass with
     * `ComputePassEncoder::setPipeline()` before dispatching work groups.
     *
     * Compute pipelines are created by `Device::createComputePipeline()`.
     *
     * @code
     * ComputePipelineDescriptor desc;
     * desc.compute.module     = shaderModule;
     * desc.compute.entryPoint = "computeMain";
     * auto pipeline = device->createComputePipeline(desc);
     * @endcode
     */
    class ComputePipeline
    {
    private:
        friend class Device;
        friend class ComputePassEncoder;
        void *native;

        ComputePipeline(void *data);

    public:
        ~ComputePipeline();

        /**
         * @brief Returns the workgroup size this pipeline will use at dispatch time.
         */
        WorkgroupSize getWorkgroupSize() const;
    };

}
