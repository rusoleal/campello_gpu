#pragma once

#include <campello_gpu/descriptors/compute_descriptor.hpp>
#include <campello_gpu/pipeline_layout.hpp>

namespace systems::leal::campello_gpu {

    struct ComputePipelineDescriptor
    {
        /**
         * An object describing the compute shader entry point of the pipeline.
         */
        ComputeDescriptor compute;

        /**
         * Defines the layout (structure, purpose, and type) of all the GPU resources (buffers, textures, etc.)
         * used during the execution of the pipeline.
         */
        std::shared_ptr<PipelineLayout> layout;
    };

}