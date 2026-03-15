#include <campello_gpu/bind_group.hpp>

using namespace systems::leal::campello_gpu;

// Metal binds resources directly on the encoder; this object is a no-op placeholder.
BindGroup::BindGroup(void *pd) : native(pd) {}

BindGroup::~BindGroup() {}
