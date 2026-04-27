#include <campello_gpu/query_set.hpp>
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

QuerySet::QuerySet(void* pd) {
    native = pd;
}

QuerySet::~QuerySet() {
    auto* handle = static_cast<QuerySetHandle*>(native);
    if (handle->deviceData) {
        handle->deviceData->querySetCount--;
    }
    if (handle->querySet) {
        wgpuQuerySetRelease(handle->querySet);
    }
    delete handle;
}
