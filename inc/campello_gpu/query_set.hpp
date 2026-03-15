#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief A set of GPU query slots for occlusion or timestamp queries.
     *
     * A `QuerySet` is a fixed-size array of query slots. Each slot records a GPU
     * measurement:
     * - **Occlusion** — counts how many samples passed the depth/stencil test
     *   between a `beginOcclusionQuery` / `endOcclusionQuery` pair.
     * - **Timestamp** — records the GPU clock at a specific point in the command
     *   stream via `CommandEncoder::writeTimestamp()`.
     *
     * Results are copied to a `Buffer` via `CommandEncoder::resolveQuerySet()` and
     * then read back by the CPU.
     *
     * Query sets are created by `Device::createQuerySet()`.
     *
     * @code
     * QuerySetDescriptor desc{};
     * desc.type  = QuerySetType::occlusion;
     * desc.count = 8;
     * auto querySet = device->createQuerySet(desc);
     * @endcode
     */
    class QuerySet {
    private:
        friend class Device;
        friend class CommandEncoder;
        void *native;

        QuerySet(void *pd);
    public:
        ~QuerySet();
    };

}
