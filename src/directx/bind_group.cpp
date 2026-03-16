#include "common.hpp"
#include <campello_gpu/bind_group.hpp>

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void* pd) : native(pd) {}

BindGroup::~BindGroup() {
    if (!native) return;
    delete static_cast<BindGroupHandle*>(native);
}
