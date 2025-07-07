#pragma once

#include <vulkan/vulkan.h>
#include <campello_gpu/swap_chain.hpp>

namespace systems::leal::campello_gpu {

    struct SwapChainHandle {
        VkDevice device;
        VkSwapchainKHR swapChain;
    };    

}