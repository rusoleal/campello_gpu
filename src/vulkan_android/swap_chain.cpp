#include <campello_gpu/swap_chain.hpp>
#include "swap_chain_handle.hpp"
using namespace systems::leal::campello_gpu;

SwapChain::SwapChain(void *pd) {
    this->native = pd;
}

SwapChain::~SwapChain() {
    auto handle = (SwapChainHandle *)this->native
    vkDestroySwapchainKHR(handle->device, handle->swapChain);
}
