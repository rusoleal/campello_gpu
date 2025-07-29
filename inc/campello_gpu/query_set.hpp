#pragma once

namespace systems::leal::campello_gpu {

    class QuerySet {
    private:
        friend class Device;
        void *native;

        QuerySet(void *pd);
    public:
        ~QuerySet();
        
    };

}