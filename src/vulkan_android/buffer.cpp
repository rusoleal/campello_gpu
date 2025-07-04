#include <campello_gpu/buffer.hpp>

using namespace systems::leal::campello_gpu;

Buffer::Buffer(void *pd) {
    this->native = pd;
}

Buffer::~Buffer() {

}

uint64_t Buffer::getLength() {
    return 0;
}
