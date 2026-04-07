#pragma once
#include "Metal.hpp"

// Internal handle stored in RayTracingPipeline::native.
struct MetalRayTracingPipelineData {
    MTL::ComputePipelineState      *pipelineState;
    MTL::IntersectionFunctionTable *intersectionFunctionTable; // nullptr if no custom intersection shaders
    NS::UInteger                    threadExecutionWidth;
    NS::UInteger                    maxTotalThreadsPerThreadgroup;
};
