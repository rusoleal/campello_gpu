#pragma once

#include <campello_gpu/constants/query_set_type.hpp>

namespace systems::leal::campello_gpu {

    struct QuerySetDescriptor {

        uint32_t count;

        QuerySetType type;

    };

}