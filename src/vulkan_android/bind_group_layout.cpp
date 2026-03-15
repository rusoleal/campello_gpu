#include <android/log.h>
#include <campello_gpu/bind_group_layout.hpp>
#include "bind_group_layout_handle.hpp"

using namespace systems::leal::campello_gpu;

BindGroupLayout::BindGroupLayout(void *pd) {
    this->native = pd;
}

BindGroupLayout::~BindGroupLayout() {
    auto data = (BindGroupLayoutHandle *)native;
    if (data->layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(data->device, data->layout, nullptr);
    }
    delete data;
}
