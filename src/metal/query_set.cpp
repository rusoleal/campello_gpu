#include "Metal.hpp"
#include <campello_gpu/query_set.hpp>

using namespace systems::leal::campello_gpu;

QuerySet::QuerySet(void *pd) : native(pd) {}

QuerySet::~QuerySet() {
    if (native != nullptr) {
        static_cast<MTL::Buffer *>(native)->release();
    }
}
