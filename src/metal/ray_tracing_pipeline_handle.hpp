#pragma once
#include "Metal.hpp"
#include <cstdint>

// Reported as RayTracingCapabilities::maxRecursionDepth on Metal. A Metal "ray
// tracing pipeline" is one compute kernel (the ray generation function, with
// intersection functions linked in), so the backend never enforces
// RayTracingPipelineDescriptor::maxRecursionDepth. 31 matches DXR's
// D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH, the strictest ceiling of
// the hardware backends, so portable code sized against it works everywhere.
constexpr uint32_t kMetalRayTracingMaxRecursionDepth = 31;

// Internal handle stored in RayTracingPipeline::native.
struct MetalRayTracingPipelineData {
    MTL::ComputePipelineState      *pipelineState;
    MTL::IntersectionFunctionTable *intersectionFunctionTable; // nullptr if no custom intersection shaders
    NS::UInteger                    threadExecutionWidth;
    NS::UInteger                    maxTotalThreadsPerThreadgroup;
};
