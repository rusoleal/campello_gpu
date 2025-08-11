#pragma once

namespace systems::leal::campello_gpu
{
    class Device;

    class TextureView
    {
    private:
        friend class Device;
        void *native;

        TextureView(void *pd);

    public:
        ~TextureView();

    };
}
