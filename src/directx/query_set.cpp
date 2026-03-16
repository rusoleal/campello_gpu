#include "common.hpp"
#include <campello_gpu/query_set.hpp>

using namespace systems::leal::campello_gpu;

QuerySet::QuerySet(void* pd) : native(pd) {}

QuerySet::~QuerySet() {
    if (!native) return;
    auto* h = static_cast<QuerySetHandle*>(native);
    if (h->queryHeap)    h->queryHeap->Release();
    if (h->resultBuffer) h->resultBuffer->Release();
    delete h;
}
