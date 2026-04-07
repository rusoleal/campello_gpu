//
//  RaytracingShaders.metal
//  campello_test Shared
//
//  Minimal ray tracing shaders used by the RaytracingDemo.
//  Requires Metal 3 (macOS 13 / iOS 16) for metal::raytracing support.
//

#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

// ---------------------------------------------------------------------------
// Output texture is bound at texture slot 0 from campello_gpu's setBindGroup.
// Acceleration structure is bound at buffer slot 0.
// ---------------------------------------------------------------------------

[[kernel]]
void rayGenMain(
    primitive_acceleration_structure accelStructure [[buffer(0)]],
    texture2d<float, access::write>  outTexture     [[texture(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 size = uint2(outTexture.get_width(), outTexture.get_height());
    if (gid.x >= size.x || gid.y >= size.y) return;

    // Map pixel coordinates to normalised device coordinates ([-1, 1] x [-1, 1]).
    float2 uv = (float2(gid) + 0.5f) / float2(size) * 2.0f - 1.0f;

    // Simple pin-hole camera pointing down +Z.
    ray r;
    r.origin       = float3(0.0f, 0.0f, -2.0f);
    r.direction    = normalize(float3(uv.x, -uv.y, 1.0f));
    r.min_distance = 0.001f;
    r.max_distance = FLT_MAX;

    intersector<triangle_data, instancing> isect;
    isect.assume_geometry_type(geometry_type::triangle);
    isect.force_opacity(opacity_type::opaque);

    auto result = isect.intersect(r, accelStructure, 0xFF);

    float4 color;
    if (result.type == intersection_type::triangle) {
        // Shade using barycentric coordinates for a simple rainbow triangle.
        float2 bary  = result.triangle_barycentric_coord;
        float  baryZ = 1.0f - bary.x - bary.y;
        color = float4(bary.x, bary.y, baryZ, 1.0f);
    } else {
        // Background: sky gradient.
        color = float4(0.05f, 0.07f + uv.y * 0.05f, 0.15f, 1.0f);
    }

    outTexture.write(color, gid);
}
