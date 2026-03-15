#pragma once

#include <stdint.h>

namespace systems::leal::campello_gpu
{
    class Device;

    class Buffer
    {
        friend class Device;
        friend class CommandEncoder;
        friend class RenderPassEncoder;
        friend class ComputePassEncoder;
        void *native;

        Buffer(void *pd);

    public:
        uint64_t getLength();
        bool upload(uint64_t offset, uint64_t length, void *data);

        ~Buffer();
    };

}