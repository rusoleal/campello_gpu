#include <campello_gpu/bind_group_layout.hpp>
#include "bind_group_layout_data.hpp"

using namespace systems::leal::campello_gpu;

BindGroupLayout::BindGroupLayout(void *pd) : native(pd) {}

BindGroupLayout::~BindGroupLayout() {
    if (native != nullptr) {
        delete static_cast<MetalBindGroupLayoutData *>(native);
    }
}
