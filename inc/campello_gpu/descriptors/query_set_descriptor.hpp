#pragma once

#include <campello_gpu/constants/query_set_type.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Configuration for creating a `QuerySet`.
     *
     * Specifies the type of measurement and the number of slots in the set.
     *
     * @code
     * QuerySetDescriptor desc{};
     * desc.type  = QuerySetType::occlusion;
     * desc.count = 4;
     * auto qs = device->createQuerySet(desc);
     * @endcode
     */
    struct QuerySetDescriptor {
        /** @brief Number of query slots to allocate (must be > 0). */
        uint32_t count;

        /** @brief Measurement type: `occlusion` or `timestamp`. */
        QuerySetType type;
    };

}
