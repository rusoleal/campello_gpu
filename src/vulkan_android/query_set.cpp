#include <android/log.h>
#include <campello_gpu/query_set.hpp>
#include "query_set_handle.hpp"

using namespace systems::leal::campello_gpu;

QuerySet::QuerySet(void *pd) {
    native = pd;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","QuerySet::QuerySet()");
}

QuerySet::~QuerySet() {

    auto handle = (QuerySetHandle *)native;

    vkDestroyQueryPool(handle->device, handle->queryPool, nullptr);

    delete handle;
    __android_log_print(ANDROID_LOG_DEBUG,"campello_gpu","QuerySet::~QuerySet()");
}
