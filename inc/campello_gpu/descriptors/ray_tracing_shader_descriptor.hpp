#pragma once

#include <memory>
#include <string>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Identifies a single ray tracing shader entry point.
     *
     * Used in `RayTracingPipelineDescriptor` to specify the ray generation shader,
     * each miss shader, and each shader within a hit group.
     */
    struct RayTracingShaderDescriptor {
        /** @brief Shader module containing the entry point. */
        std::shared_ptr<ShaderModule> module;

        /** @brief Name of the shader function within `module`. */
        std::string entryPoint;
    };

}
