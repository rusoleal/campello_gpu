#pragma once

namespace systems::leal::campello_gpu
{

    class RenderPipeline
    {
    private:
        friend class Device;
        void *native;

        RenderPipeline(void *data);

    public:
        ~RenderPipeline();
    };

}