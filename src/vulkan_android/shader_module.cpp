#include <campello_gpu/shader_module.hpp>
#include "shader_module_handle.hpp"

using namespace systems::leal::campello_gpu;

ShaderModule::ShaderModule(void *pd) {
    native = pd;
}

ShaderModule::~ShaderModule() {

    auto handle = (ShaderModuleHandle *)native;

    vkDestroyShaderModule(handle->device, handle->shaderModule, nullptr);

}
