#include "Metal.hpp"
#include <campello_gpu/shader_module.hpp>

using namespace systems::leal::campello_gpu;

ShaderModule::ShaderModule(void *pd) : native(pd) {}

ShaderModule::~ShaderModule() {
    if (native != nullptr) {
        static_cast<MTL::Library *>(native)->release();
    }
}
