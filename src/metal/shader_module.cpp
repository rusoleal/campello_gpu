#include "shader_module_handle.hpp"
#include <campello_gpu/shader_module.hpp>

using namespace systems::leal::campello_gpu;

ShaderModule::ShaderModule(void *pd) : native(pd) {}

ShaderModule::~ShaderModule() {
    if (native != nullptr) {
        delete static_cast<MetalShaderModuleData *>(native);
    }
}
