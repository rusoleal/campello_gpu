//
//  scene.metal
//
//  Ray tracing shader for the campello_gpu Cornell-box demo (see main.cpp).
//  A single compute kernel does everything: camera ray generation, BVH
//  traversal via metal::raytracing's `intersector`, per-object shading, a
//  hard shadow ray toward one point light, and up to 3 mirror-reflection
//  bounces off spheres (a sphere reflecting another reflective sphere keeps
//  chaining until the bounce budget runs out or a non-reflective surface is
//  hit). This matches how campello_gpu's Metal backend implements
//  RayTracingPassEncoder: as a compute pass, not a hardware recursive
//  pipeline, so "recursion" here is just a bounded loop inside the kernel.
//
//  The scene has 5 wall instances (IDs 0..4) plus a 4x3 checkerboarded grid
//  of 12 foreground instances (IDs 5..16, cubes and spheres alternating)
//  all reusing just two BLAS (one cube, one icosphere) via per-instance
//  transforms. Wall geometry/normals are hardcoded per instance ID; cube
//  face normals are hardcoded per geometry_id; sphere normals are computed
//  analytically from the per-instance transform (see shadeHit below) so
//  no per-object data needs to be duplicated between this file and
//  main.cpp beyond the grid layout constants.
//
//  Requires Metal 3 (macOS 13+) for metal::raytracing support.
//

#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace metal::raytracing;

// ---------------------------------------------------------------------------
// Scene constants. Instance IDs and the foreground grid layout MUST match
// the values used to build the TLAS in main.cpp.
// ---------------------------------------------------------------------------

constant uint kInstanceFloor   = 0;
constant uint kInstanceCeiling = 1;
constant uint kInstanceBack    = 2;
constant uint kInstanceLeft    = 3;
constant uint kInstanceRight   = 4;

// Foreground objects start at instance ID 5 and fill a gridCols x gridRows
// grid in row-major order, checkerboarded cube/sphere by (row+col)%2 —
// must match the grid main.cpp builds.
constant uint kFgStart  = 5;
constant uint kGridCols = 4;

// Cube face normals, indexed by geometry_id within the cube BLAS (6
// per-face geometries). Cube instances apply no rotation, so object-space
// face normals equal world-space face normals.
constant float3 kCubeFaceNormals[6] = {
    float3( 1.0,  0.0,  0.0),
    float3(-1.0,  0.0,  0.0),
    float3( 0.0,  1.0,  0.0),
    float3( 0.0, -1.0,  0.0),
    float3( 0.0,  0.0,  1.0),
    float3( 0.0,  0.0, -1.0),
};

constant float3 kWallColorWhite = float3(0.75, 0.75, 0.72);
constant float3 kWallColorRed   = float3(0.75, 0.15, 0.12);
constant float3 kWallColorGreen = float3(0.15, 0.65, 0.20);
constant float  kReflectivity = 0.35; // applied uniformly to every surface — walls, cubes, and spheres alike

constant float3 kLightPos       = float3(0.0, 0.85, -0.1);
constant float3 kLightColor     = float3(1.0, 0.96, 0.88);
constant float  kLightIntensity = 1.6;
constant float  kAmbient        = 0.20;

// ---------------------------------------------------------------------------
// Shading helpers
// ---------------------------------------------------------------------------

struct HitInfo {
    float3 position;
    float3 normal;
    float3 albedo;
    float  reflectivity;
};

static float3 backgroundColor(float3 dir) {
    float t = 0.5 * (dir.y + 1.0);
    return mix(float3(0.02, 0.02, 0.05), float3(0.05, 0.08, 0.18), t);
}

// Deterministic hash -> HSV rainbow color, so each foreground instance gets
// a distinct-looking color purely from its instance_id — no per-instance
// color buffer needs to be bound.
static float3 hashColor(uint id) {
    float hue = fract(sin(float(id) * 12.9898) * 43758.5453);
    float3 p = abs(fract(hue + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    float3 rgb = clamp(p - 1.0, 0.0, 1.0);
    return mix(float3(1.0), rgb, 0.85); // slightly desaturate off pure hue
}

static HitInfo shadeHit(float3 hitPosition,
                        intersection_result<triangle_data, instancing, world_space_data> result)
{
    HitInfo info;
    info.position     = hitPosition;
    info.reflectivity = kReflectivity;

    uint instId = result.instance_id;
    uint geoId  = result.geometry_id;

    if (instId == kInstanceFloor) {
        info.normal = float3(0, 1, 0);
        info.albedo = kWallColorWhite;
    } else if (instId == kInstanceCeiling) {
        info.normal = float3(0, -1, 0);
        info.albedo = kWallColorWhite;
    } else if (instId == kInstanceBack) {
        info.normal = float3(0, 0, -1);
        info.albedo = kWallColorWhite;
    } else if (instId == kInstanceLeft) {
        info.normal = float3(1, 0, 0);
        info.albedo = kWallColorRed;
    } else if (instId == kInstanceRight) {
        info.normal = float3(-1, 0, 0);
        info.albedo = kWallColorGreen;
    } else {
        uint index   = instId - kFgStart;
        uint row     = index / kGridCols;
        uint col     = index % kGridCols;
        bool isCube  = ((row + col) % 2) == 0;
        info.albedo  = hashColor(instId);

        if (isCube) {
            info.normal = kCubeFaceNormals[min(geoId, 5u)];
        } else {
            // Sphere — analytic normal computed from the per-instance
            // transform rather than a hardcoded center, since many
            // differently-placed spheres all share the same BLAS. The
            // instance applies only uniform scale + translation (no
            // rotation), so the local-space normal direction survives
            // unchanged into world space.
            float3 worldCenter = result.object_to_world_transform * float4(0.0, 0.0, 0.0, 1.0);
            info.normal         = normalize(hitPosition - worldCenter);
        }
    }
    return info;
}

// ---------------------------------------------------------------------------
// Ray generation kernel.
//   buffer(0)  — TLAS (instance_acceleration_structure)
//   texture(0) — output image, storage-write access
// ---------------------------------------------------------------------------

[[kernel]]
void rayGenMain(
    instance_acceleration_structure accelStructure [[buffer(0)]],
    texture2d<float, access::write>  outTexture     [[texture(0)]],
    uint2 gid [[thread_position_in_grid]])
{
    const uint2 size = uint2(outTexture.get_width(), outTexture.get_height());
    if (gid.x >= size.x || gid.y >= size.y) return;

    float2 uv = (float2(gid) + 0.5) / float2(size) * 2.0 - 1.0;
    uv.x *= float(size.x) / float(size.y);

    // Chosen so the box (half-height 1, half-width 16/9, camera at z=-3.4,
    // front opening at z=-1 i.e. distance 2.4) exactly fills the frame at
    // its nearest edge: fovScale = boxHalfHeight / distanceToFrontOpening.
    // Keep in sync with `bx` and the camera Z in main.cpp.
    const float fovScale = 1.0 / 2.4;

    ray primaryRay;
    primaryRay.origin       = float3(0.0, 0.0, -3.4);
    primaryRay.direction    = normalize(float3(uv.x * fovScale, -uv.y * fovScale, 1.0));
    primaryRay.min_distance = 0.001;
    primaryRay.max_distance = 1000.0;

    // world_space_data tag requested so intersection_result exposes
    // object_to_world_transform (used for per-instance sphere normals).
    intersector<triangle_data, instancing, world_space_data> isect;
    isect.assume_geometry_type(geometry_type::triangle);

    intersector<triangle_data, instancing> shadowIsect;
    shadowIsect.assume_geometry_type(geometry_type::triangle);
    shadowIsect.accept_any_intersection(true);

    float3 accumColor = float3(0.0);
    float3 throughput = float3(1.0);
    ray currentRay = primaryRay;

    // Bounded to 4 iterations: the primary hit, plus up to 3 chained
    // mirror-reflection bounces. Every surface (walls, cubes, spheres) is
    // reflective now, so a path only stops early if it flies out through
    // the box's open front — otherwise it always uses the full bounce
    // budget. This is the whole "recursion" story on this compute-based
    // backend — no hardware call-stack involved, just a fixed-size loop.
    const int maxBounces = 3;
    for (int bounce = 0; bounce <= maxBounces; bounce++) {
        auto result = isect.intersect(currentRay, accelStructure, 0xFF);

        if (result.type != intersection_type::triangle) {
            accumColor += throughput * backgroundColor(currentRay.direction);
            break;
        }

        float3 hitPosition = currentRay.origin + currentRay.direction * result.distance;
        HitInfo hitInfo = shadeHit(hitPosition, result);

        float3 toLight   = kLightPos - hitInfo.position;
        float  lightDist = length(toLight);
        float3 lightDir  = toLight / lightDist;

        ray shadowRay;
        shadowRay.origin       = hitInfo.position + hitInfo.normal * 0.003;
        shadowRay.direction    = lightDir;
        shadowRay.min_distance = 0.001;
        shadowRay.max_distance = lightDist - 0.01;

        auto shadowResult = shadowIsect.intersect(shadowRay, accelStructure, 0xFF);
        float shadowFactor = (shadowResult.type == intersection_type::triangle) ? 0.0 : 1.0;

        float ndotl   = max(dot(hitInfo.normal, lightDir), 0.0);
        float atten   = kLightIntensity / (1.0 + 0.15 * lightDist * lightDist);
        float3 direct = hitInfo.albedo * (kAmbient + ndotl * atten * shadowFactor) * kLightColor;

        accumColor += throughput * direct * (1.0 - hitInfo.reflectivity);

        if (hitInfo.reflectivity > 0.0 && bounce < maxBounces) {
            throughput *= hitInfo.reflectivity;
            currentRay.origin       = hitInfo.position + hitInfo.normal * 0.003;
            currentRay.direction    = reflect(currentRay.direction, hitInfo.normal);
            currentRay.min_distance = 0.001;
            currentRay.max_distance = 1000.0;
            continue;
        }
        break;
    }

    // Simple Reinhard tonemap + gamma correction so bright surfaces (walls lit
    // directly under the point light) don't just clip to flat white.
    float3 mapped = accumColor / (1.0 + accumColor);
    mapped = pow(mapped, float3(1.0 / 2.2));
    outTexture.write(float4(mapped, 1.0), gid);
}
