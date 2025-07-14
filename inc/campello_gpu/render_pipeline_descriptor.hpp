#pragma once

#include <optional>
#include <vector>
#include <campello_gpu/depth_stencil_descriptor.hpp>
#include <campello_gpu/fragment_descriptor.hpp>

namespace systems::leal::campello_gpu {

    class RenderPipelineDescriptor
    {
        /**
         * An object (see depthStencil object structure) describing depth-stencil properties
         * including testing, operations, and bias.
         */
        std::optional<DepthStencilDescriptor> depthStencil;

        /**
         * An object (see fragment object structure) describing the fragment shader entry point
         * of the pipeline and its output colors. If no fragment shader entry point is defined,
         * the pipeline will not produce any color attachment outputs, but it still performs
         * rasterization and produces depth values based on the vertex position output.
         * Depth testing and stencil operations can still be used.
         */
        std::optional<FragmentDescriptor> fragment;

    };

}