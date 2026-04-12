#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include "bind_group_data.hpp"

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void *pd) : native(pd) {}

BindGroup::~BindGroup() {
    if (native != nullptr) {
        delete static_cast<MetalBindGroupData *>(native);
    }
}
