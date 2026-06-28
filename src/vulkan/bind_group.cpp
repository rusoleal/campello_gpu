#include <campello_gpu/bind_group.hpp>
#include "bind_group_handle.hpp"

using namespace systems::leal::campello_gpu;

BindGroup::BindGroup(void *pd) {
    this->native = pd;
}

BindGroup::~BindGroup() {
    // Descriptor sets are owned by the pool; freeing the pool frees them all.
    auto data = (BindGroupHandle *)native;
    delete data;
}
