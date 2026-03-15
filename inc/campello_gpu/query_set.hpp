#pragma once

namespace systems::leal::campello_gpu {

    class QuerySet {
    private:
        friend class Device;
        friend class CommandEncoder;
        void *native;

        QuerySet(void *pd);
    public:
        ~QuerySet();
        
    };

}