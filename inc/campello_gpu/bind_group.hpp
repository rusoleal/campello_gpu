#pragma once

namespace systems::leal::campello_gpu {

    class BindGroup {
    private:
        friend class Device;
        friend class ComputePassEncoder;
        void *native;
        BindGroup(void *pd);
    public:
       ~BindGroup();
    };

}