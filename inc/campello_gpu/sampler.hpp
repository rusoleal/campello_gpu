#pragma once

namespace systems::leal::campello_gpu
{

    class Sampler
    {
    private:
        friend class Device;
        void *native;

        Sampler(void *pd);

    public:
        ~Sampler();
    };

}