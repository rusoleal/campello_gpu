#pragma once

#include <optional>
#include <vector>
#include <campello_gpu/depth_stencil_descriptor.hpp>
#include <campello_gpu/fragment_descriptor.hpp>
#include <campello_gpu/vertex_descriptor.hpp>
#include <campello_gpu/primitive_topology.hpp>
#include <campello_gpu/cull_mode.hpp>
#include <campello_gpu/front_face.hpp>

namespace systems::leal::campello_gpu {

    struct RenderPipelineDescriptor
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

        /**
         * An object (see vertex object structure) describing the vertex shader entry point of the
         * pipeline and its input buffer layouts.
         */
        VertexDescriptor vertex;

        /**
         * An enumerated value that defines which polygon orientation will be culled, if any.
         */
        CullMode cullMode;

        /**
         * An enumerated value that defines which polygons are considered front-facing.
         */
        FrontFace frontFace;

        /**
         * An enumerated value that defines the type of primitive to be constructed from the
         * specified vertex inputs.
         */
        PrimitiveTopology topology;

        RenderPipelineDescriptor();
    };

}