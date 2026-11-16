#pragma once

#include <memory>
#include <vulkan/vulkan.h>

namespace systems::leal::campello_gpu
{
    class PipelineLayout;

    struct RenderPipelineHandle
    {
        VkDevice         device;
        VkPipeline       pipeline;
        VkPipelineLayout pipelineLayout;
        bool             ownsPipelineLayout = false; ///< True if the layout was internally created (must destroy it).
        /// The caller's PipelineLayout, kept alive as long as the pipeline: the
        /// pipeline records the raw VkPipelineLayout above, and binding descriptor
        /// sets against a destroyed layout is undefined (callers commonly drop
        /// the layout right after creating the pipeline, as on Metal).
        std::shared_ptr<PipelineLayout> layoutRef;
    };
}
