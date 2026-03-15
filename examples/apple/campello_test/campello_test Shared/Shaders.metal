//
//  Shaders.metal
//  campello_test Shared
//
//  Triangle demo using campello_gpu.
//  Vertex positions and colours are hard-coded so no vertex buffer is required.
//

#include <metal_stdlib>
using namespace metal;

constant float2 kPositions[3] = {
    float2( 0.0,  0.6),
    float2(-0.6, -0.6),
    float2( 0.6, -0.6)
};

constant float3 kColors[3] = {
    float3(1.0, 0.2, 0.2),   // red   (top)
    float3(0.2, 1.0, 0.2),   // green (bottom-left)
    float3(0.2, 0.4, 1.0)    // blue  (bottom-right)
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vertexMain(uint vid [[vertex_id]]) {
    VertexOut out;
    out.position = float4(kPositions[vid], 0.0, 1.0);
    out.color    = kColors[vid];
    return out;
}

fragment float4 fragmentMain(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
