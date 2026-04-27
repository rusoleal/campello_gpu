#include <campello_gpu/bind_group_layout.hpp>
#include "bind_group_layout_handle.hpp"

using namespace systems::leal::campello_gpu;

BindGroupLayout::BindGroupLayout(void* pd) {
    native = pd;
}

BindGroupLayout::~BindGroupLayout() {
    auto* handle = static_cast<BindGroupLayoutHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->bindGroupLayoutCount--;
    }
    if (handle->layout) {
        wgpuBindGroupLayoutRelease(handle->layout);
    }
    delete handle;
}
