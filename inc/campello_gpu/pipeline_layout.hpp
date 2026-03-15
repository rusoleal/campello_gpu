#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Describes the full set of resources accessible to a pipeline.
     *
     * A `PipelineLayout` specifies the complete binding layout for all shader stages
     * in a pipeline — which `BindGroupLayout` objects are used and in which order.
     * Pipelines that share a compatible layout can share descriptor sets/bind groups
     * between draw/dispatch calls without rebinding.
     *
     * On Metal, pipeline layouts are implicit and this type is a no-op placeholder.
     * On Vulkan, this maps to `VkPipelineLayout`.
     *
     * Pipeline layouts are created by `Device::createPipelineLayout()`.
     */
    class PipelineLayout {
        friend class Device;
    private:
        void *native;
        PipelineLayout(void *pd);

    public:
        ~PipelineLayout();
    };

}
