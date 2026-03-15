#pragma once

namespace systems::leal::campello_gpu {

    /**
     * @brief Configuration for creating a `PipelineLayout`.
     *
     * A `PipelineLayout` declares the bind group layouts used by a pipeline.
     * Currently a placeholder; bind group layout list will be added in a future
     * release.
     *
     * @code
     * PipelineLayoutDescriptor desc{};
     * auto layout = device->createPipelineLayout(desc);
     * @endcode
     */
    struct PipelineLayoutDescriptor {
    };

}
