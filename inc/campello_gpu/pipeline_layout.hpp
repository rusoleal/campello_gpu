#pragma once

namespace systems::leal::campello_gpu {

    class PipelineLayout {
        friend class Device;
    private:
        void *native;
        PipelineLayout(void *pd);

    public:
        ~PipelineLayout();

    };

}