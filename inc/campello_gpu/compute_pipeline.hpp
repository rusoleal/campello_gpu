#pragma once

namespace systems::leal::campello_gpu
{

    class ComputePipeline
    {
    private:
        friend class Device;
        friend class ComputePassEncoder;
        void *native;

        ComputePipeline(void *data);

    public:
        ~ComputePipeline();
    };

}