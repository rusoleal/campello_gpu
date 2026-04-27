#pragma once

#include <memory>
#include <vector>

namespace systems::leal::campello_gpu {

    class BindGroupLayout;

    /**
     * @brief Configuration for creating a `PipelineLayout`.
     *
     * A `PipelineLayout` declares the bind group layouts used by a pipeline.
     *
     * @code
     * PipelineLayoutDescriptor desc{};
     * desc.bindGroupLayouts = { layout0, layout1 };
     * auto layout = device->createPipelineLayout(desc);
     * @endcode
     */
    struct PipelineLayoutDescriptor {
        std::vector<std::shared_ptr<BindGroupLayout>> bindGroupLayouts;
    };

}
