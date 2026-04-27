#include <campello_gpu/shader_module.hpp>
#include "shader_module_handle.hpp"

using namespace systems::leal::campello_gpu;

ShaderModule::ShaderModule(void* pd) {
    native = pd;
}

ShaderModule::~ShaderModule() {
    auto* handle = static_cast<ShaderModuleHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->shaderModuleCount--;
        handle->deviceData->shaderModuleBytes.fetch_sub(handle->size);
    }
    if (handle->shaderModule) {
        wgpuShaderModuleRelease(handle->shaderModule);
    }
    delete handle;
}
