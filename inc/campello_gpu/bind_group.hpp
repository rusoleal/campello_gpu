#pragma once

namespace systems::leal::campello_gpu {

    class BindGroup {
    private:
        void *native;
        BindGroup(void *pd);
    public:
       ~BindGroup();
    };

}