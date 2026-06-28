#pragma once

#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    struct RenderPipelineHandle
    {
        VkDevice         device;
        VkPipeline       pipeline;
        VkPipelineLayout pipelineLayout;
        bool             ownsPipelineLayout = false; ///< True if the layout was internally created (must destroy it).
    };
}
