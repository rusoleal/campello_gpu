#pragma once
#include "Metal.hpp"

// Internal handle stored in RenderPipeline::native.
// Holds the render pipeline state and the optional depth/stencil state
// (both are needed to fully configure a Metal render command encoder).
struct MetalRenderPipelineData {
    MTL::RenderPipelineState *pipelineState;
    MTL::DepthStencilState   *depthStencilState; // nullptr when no depth descriptor was set
};
