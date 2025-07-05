#include <campello_gpu/device.hpp>

using namespace systems::leal::campello_gpu;

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage, void *data) {
    auto toReturn = createBuffer(size, usage);
    if (toReturn != nullptr) {
        toReturn->upload(0, size, data);
    }

    return toReturn;
}
