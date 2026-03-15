#pragma once

#include <memory>
#include <string>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Configuration for the compute shader stage of a `ComputePipeline`.
     *
     * Identifies the shader module and the entry point function name for a
     * compute dispatch. Used as the `compute` field of `ComputePipelineDescriptor`.
     */
    struct ComputeDescriptor {
        /**
         * @brief Shader module containing the compute entry point.
         */
        std::shared_ptr<ShaderModule> module;

        /**
         * @brief Name of the compute function within `module`.
         */
        std::string entryPoint;
    };

}