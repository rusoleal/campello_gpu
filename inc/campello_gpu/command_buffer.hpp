#pragma once

namespace systems::leal::campello_gpu
{
    class Device;

    class CommandBuffer
    {
        friend class Device;
        friend class CommandEncoder;
        void *native;
        CommandBuffer(void *pd);
    public:
        ~CommandBuffer();
    };

}