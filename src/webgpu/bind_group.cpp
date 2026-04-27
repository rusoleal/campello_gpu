#include <campello_gpu/bind_group.hpp>
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void* pd) {
    native = pd;
}

BindGroup::~BindGroup() {
    auto* handle = static_cast<BindGroupHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->bindGroupCount--;
    }
    if (handle->bindGroup) {
        wgpuBindGroupRelease(handle->bindGroup);
    }
    delete handle;
}
