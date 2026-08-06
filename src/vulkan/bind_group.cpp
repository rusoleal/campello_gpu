#include <campello_gpu/bind_group.hpp>
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void *pd) {
    this->native = pd;
}

BindGroup::~BindGroup() {
    auto data = (BindGroupHandle *)native;
    if (data->ownerDevice != VK_NULL_HANDLE && data->ownerPool != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(data->ownerDevice, data->ownerPool,
                             1, &data->descriptorSet);
    }
    delete data;
}
