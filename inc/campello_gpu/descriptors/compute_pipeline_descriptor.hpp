#pragma once

#include <campello_gpu/descriptors/compute_descriptor.hpp>
#include <campello_gpu/pipeline_layout.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Full configuration for creating a `ComputePipeline`.
     *
     * Specifies the compute shader entry point and the resource layout that
     * the pipeline uses. Pass to `Device::createComputePipeline()`.
     *
     * @code
     * ComputePipelineDescriptor desc{};
     * desc.compute.module     = computeShader;
     * desc.compute.entryPoint = "computeMain";
     * desc.layout             = pipelineLayout;
     * auto pipeline = device->createComputePipeline(desc);
     * @endcode
     */
    struct ComputePipelineDescriptor
    {
        /**
         * @brief Compute shader stage configuration.
         *
         * Specifies the shader module and the name of the compute entry point
         * function within that module.
         */
        ComputeDescriptor compute;

        /**
         * @brief Pipeline layout describing the bind group structure.
         *
         * Defines the type and arrangement of all GPU resources (buffers, textures,
         * samplers) visible to the compute shader during execution.
         */
        std::shared_ptr<PipelineLayout> layout;
    };

}