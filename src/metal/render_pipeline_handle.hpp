#pragma once
#include "Metal.hpp"

// Internal handle stored in RenderPipeline::native.
// Holds the render pipeline state and the optional depth/stencil state
// (both are needed to fully configure a Metal render command encoder).
struct MetalRenderPipelineData {
    MTL::RenderPipelineState *pipelineState;
    MTL::DepthStencilState   *depthStencilState; // nullptr when no depth descriptor was set
    // Rasterizer state. Metal keeps cull mode and winding on the render
    // command encoder rather than in the pipeline state object, so they are
    // remembered here and applied by RenderPassEncoder::setPipeline.
    MTL::CullMode             cullMode = MTL::CullModeNone;
    MTL::Winding              winding  = MTL::WindingCounterClockwise;
    // Metal takes the primitive type per draw call, not from the pipeline:
    // remembered here from RenderPipelineDescriptor::topology.
    MTL::PrimitiveType        primitiveType = MTL::PrimitiveTypeTriangle;
};
