#include "common.hpp"
#include <campello_gpu/bind_group_layout.hpp>

using namespace systems::leal::campello_gpu;

BindGroupLayout::BindGroupLayout(void* pd) : native(pd) {}

BindGroupLayout::~BindGroupLayout() {
    if (!native) return;
    delete static_cast<BindGroupLayoutHandle*>(native);
}
