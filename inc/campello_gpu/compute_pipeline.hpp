#pragma once

namespace systems::leal::campello_gpu
{

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
    };

}
