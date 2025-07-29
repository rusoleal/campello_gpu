#pragma once

#include <memory>
#include <string>
#include <campello_gpu/shader_module.hpp>

namespace systems::leal::campello_gpu {

    struct ComputeDescriptor {

        /**
         * A ShaderModule object containing the shader code that this programmable stage will execute.
         */
        std::shared_ptr<ShaderModule> module;

        /**
         * The name of the function in the module that this stage will use to perform its work.
         */
        std::string entryPoint;
    
    };

}