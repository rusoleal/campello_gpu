#pragma once

namespace systems::leal::campello_gpu {

    class BindGroupLayout {
    private:
        void *native;
        BindGroupLayout(void *pd);

    public:
        ~BindGroupLayout();

    };

}