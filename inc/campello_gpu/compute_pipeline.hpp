#pragma once

namespace systems::leal::campello_gpu
{

    class ComputePipeline
    {
    private:
        friend class Device;
        void *native;

        ComputePipeline(void *data);

    public:
        ~ComputePipeline();
    };

}