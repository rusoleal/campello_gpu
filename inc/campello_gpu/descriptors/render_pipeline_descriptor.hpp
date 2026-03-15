#pragma once

#include <optional>
#include <vector>
#include <campello_gpu/descriptors/depth_stencil_descriptor.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/front_face.hpp>

namespace systems::leal::campello_gpu {

    /**
     * @brief Full configuration for creating a `RenderPipeline`.
     *
     * Describes every fixed-function and programmable stage of the rasterization
     * pipeline. Pass this to `Device::createRenderPipeline()`.
     *
     * @code
     * RenderPipelineDescriptor desc{};
     * desc.vertex.module     = vertShader;
     * desc.vertex.entryPoint = "vertexMain";
     * desc.fragment          = FragmentDescriptor{ fragShader, "fragmentMain", { colorState } };
     * desc.topology          = PrimitiveTopology::triangleList;
     * desc.cullMode          = CullMode::none;
     * desc.frontFace         = FrontFace::ccw;
     * auto pipeline = device->createRenderPipeline(desc);
     * @endcode
     */
    struct RenderPipelineDescriptor
    {
        /**
         * @brief Optional depth/stencil state for the pipeline.
         *
         * Describes depth testing, stencil operations, and polygon bias. Omit
         * (leave as `std::nullopt`) for passes that have no depth attachment.
         */
        std::optional<DepthStencilDescriptor> depthStencil;

        /**
         * @brief Optional fragment stage configuration.
         *
         * Describes the fragment shader entry point and its color outputs.
         * If omitted, the pipeline performs depth-only rasterization — no color
         * attachments are written, but depth testing and stencil ops still apply.
         */
        std::optional<FragmentDescriptor> fragment;

        /**
         * @brief Vertex stage configuration (required).
         *
         * Describes the vertex shader entry point and the buffer layouts that
         * supply per-vertex or per-instance data.
         */
        VertexDescriptor vertex;

        /**
         * @brief Which polygon orientation to cull, if any.
         *
         * Use `CullMode::none` to disable culling, `CullMode::back` to discard
         * back-facing triangles, or `CullMode::front` for shadow-volume techniques.
         */
        CullMode cullMode;

        /**
         * @brief Which winding order defines a front-facing polygon.
         *
         * `FrontFace::ccw` (counter-clockwise) matches the convention used by
         * WebGPU, OpenGL, and most asset exporters. `FrontFace::cw` matches
         * Direct3D conventions.
         */
        FrontFace frontFace;

        /**
         * @brief Primitive assembly topology.
         *
         * Controls how the input vertices are assembled into geometric primitives
         * (points, lines, or triangles).
         */
        PrimitiveTopology topology;
    };

}