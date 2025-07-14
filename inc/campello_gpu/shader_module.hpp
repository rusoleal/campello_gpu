#pragma once

namespace systems::leal::campello_gpu {

    class ShaderModule {
    private:
        friend class Device;
        void *native;
        ShaderModule(void *pd);
    public:
        ~ShaderModule();
    };

}