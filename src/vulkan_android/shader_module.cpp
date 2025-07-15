#include <android/log.h>
#include <campello_gpu/shader_module.hpp>
#include "shader_module_handle.hpp"

using namespace systems::leal::campello_gpu;

ShaderModule::ShaderModule(void *pd) {
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","ShaderModule::ShaderModule()");
    native = pd;
}

ShaderModule::~ShaderModule() {

    auto handle = (ShaderModuleHandle *)native;

    vkDestroyShaderModule(handle->device, handle->shaderModule, nullptr);

    delete handle;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","ShaderModule::~ShaderModule()");

}
