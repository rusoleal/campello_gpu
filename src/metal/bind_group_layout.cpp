#include <campello_gpu/bind_group_layout.hpp>

using namespace systems::leal::campello_gpu;

// Metal uses implicit binding; this object is a no-op placeholder.
BindGroupLayout::BindGroupLayout(void *pd) : native(pd) {}

BindGroupLayout::~BindGroupLayout() {}
