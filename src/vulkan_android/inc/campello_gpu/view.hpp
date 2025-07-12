#pragma once

#include <memory>
#include <campello_gpu/device.hpp>

namespace systems::leal::campello_gpu {

    class View {
    private:
        void *native;
        
        View(void *pd);
    public:
        static std::shared_ptr<View> createView(struct ANativeWindow *window, std::shared_ptr<Device> device);
    };
}