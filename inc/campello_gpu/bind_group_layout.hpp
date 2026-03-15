#pragma once

namespace systems::leal::campello_gpu {

    class BindGroupLayout {
    private:
        friend class Device;
        void *native;
        BindGroupLayout(void *pd);

    public:
        ~BindGroupLayout();

    };

}