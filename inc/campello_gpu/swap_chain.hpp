#pragma once

namespace systems::leal::campello_gpu
{

    class SwapChain
    {
    private:
        friend class Device;
        void *native;

        SwapChain(void *pd);
    public:
        ~SwapChain();
        
    };

}