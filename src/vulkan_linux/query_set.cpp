#include <campello_gpu/query_set.hpp>
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

QuerySet::QuerySet(void *pd) {
    native = pd;
    
}

QuerySet::~QuerySet() {

    auto handle = (QuerySetHandle *)native;

    vkDestroyQueryPool(handle->device, handle->queryPool, nullptr);

    delete handle;
    
}
