//
//  RaytracingDemo.cpp
//
//  Minimal campello_gpu ray tracing demo for Android (Vulkan KHR backend).
//
//  The demo builds a single-triangle BLAS, wraps it in a TLAS, compiles a
//  ray tracing pipeline from pre-compiled SPIR-V shaders, and dispatches
//  primary rays each frame writing RGBA colours into a storage texture.
//
//  Shaders
//  -------
//  Three SPIR-V modules are needed (compiled offline from GLSL or HLSL):
//
//    raygen.spv   — ray generation shader
//    miss.spv     — miss shader (background colour)
//    closesthit.spv — closest-hit shader (shades by barycentrics)
//
//  Place them in app/src/main/assets/shaders/ and load via AAssetManager
//  (see the loadSpv() helper below).
//
//  For reference, the GLSL source for each shader is shown in comments.
//

#include "RaytracingDemo.h"
#include "AndroidOut.h"

#include <android/asset_manager.h>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// GLSL reference source (compile to SPIR-V with glslangValidator or shaderc)
// ---------------------------------------------------------------------------
//
// --- raygen.glsl ---
// #version 460
// #extension GL_EXT_ray_tracing : require
//
// layout(set=0, binding=0) uniform accelerationStructureEXT tlas;
// layout(set=0, binding=1, rgba32f) uniform image2D outImage;
//
// layout(location=0) rayPayloadEXT vec3 payload;
//
// void main() {
//     ivec2 size = imageSize(outImage);
//     vec2  uv   = (vec2(gl_LaunchIDEXT.xy) + 0.5) / vec2(size) * 2.0 - 1.0;
//     vec3  origin    = vec3(0, 0, -2);
//     vec3  direction = normalize(vec3(uv.x, -uv.y, 1));
//     payload = vec3(0.05, 0.07, 0.15);          // background
//     traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
//                 0, 1, 0, origin, 0.001, direction, 1e9, 0);
//     imageStore(outImage, ivec2(gl_LaunchIDEXT.xy), vec4(payload, 1.0));
// }
//
// --- miss.glsl ---
// #version 460
// #extension GL_EXT_ray_tracing : require
// layout(location=0) rayPayloadInEXT vec3 payload;
// void main() { payload = vec3(0.05, 0.07, 0.15); }
//
// --- closesthit.glsl ---
// #version 460
// #extension GL_EXT_ray_tracing         : require
// #extension GL_EXT_ray_tracing_position_fetch : enable
// layout(location=0) rayPayloadInEXT vec3 payload;
// hitAttributeEXT  vec2 bary;
// void main() {
//     float bz = 1.0 - bary.x - bary.y;
//     payload = vec3(bary.x, bary.y, bz);
// }
// ---------------------------------------------------------------------------

// Loads SPIR-V bytes from the APK assets.
static std::vector<uint8_t> loadSpv(AAssetManager* mgr, const char* path) {
    if (!mgr) return {};
    AAsset* asset = AAssetManager_open(mgr, path, AASSET_MODE_BUFFER);
    if (!asset) return {};
    size_t size = AAsset_getLength(asset);
    std::vector<uint8_t> buf(size);
    AAsset_read(asset, buf.data(), size);
    AAsset_close(asset);
    return buf;
}

bool RaytracingDemo::setup(std::shared_ptr<Device> device) {
    _ready  = false;
    _device = device;

    // -----------------------------------------------------------------------
    // 1.  Feature check.
    // -----------------------------------------------------------------------
    if (!_device) { aout << "RaytracingDemo: null device\n"; return false; }
    auto features = _device->getFeatures();
    if (!features.count(Feature::raytracing)) {
        aout << "RaytracingDemo: Feature::raytracing not available\n";
        return false;
    }
    aout << "RaytracingDemo: raytracing supported on " << _device->getName() << "\n";

    // -----------------------------------------------------------------------
    // 2.  Triangle vertex buffer.
    // -----------------------------------------------------------------------
    float vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f,
    };
    auto vertexBuffer = _device->createBuffer(sizeof(vertices),
                            BufferUsage::accelerationStructureInput, vertices);
    if (!vertexBuffer) { aout << "RaytracingDemo: vertex buffer failed\n"; return false; }

    // -----------------------------------------------------------------------
    // 3.  BLAS.
    // -----------------------------------------------------------------------
    AccelerationStructureGeometryDescriptor geoDesc{};
    geoDesc.type         = AccelerationStructureGeometryType::triangles;
    geoDesc.opaque       = true;
    geoDesc.vertexBuffer = vertexBuffer;
    geoDesc.vertexOffset = 0;
    geoDesc.vertexStride = sizeof(float) * 3;
    geoDesc.vertexCount  = 3;
    geoDesc.componentType = ComponentType::ctFloat;

    BottomLevelAccelerationStructureDescriptor blasDesc{};
    blasDesc.geometries = { geoDesc };
    blasDesc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;

    auto blas = _device->createBottomLevelAccelerationStructure(blasDesc);
    if (!blas) { aout << "RaytracingDemo: createBLAS failed\n"; return false; }

    {
        auto scratch  = _device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);
        auto encoder  = _device->createCommandEncoder();
        encoder->buildAccelerationStructure(blas, blasDesc, scratch);
        auto cmdBuf = encoder->finish();
        _device->submit(cmdBuf);
    }

    // -----------------------------------------------------------------------
    // 4.  TLAS.
    // -----------------------------------------------------------------------
    AccelerationStructureInstance instance{};
    instance.blas           = blas;
    instance.instanceId     = 0;
    instance.mask           = 0xFF;
    instance.hitGroupOffset = 0;
    instance.opaque         = true;

    TopLevelAccelerationStructureDescriptor tlasDesc{};
    tlasDesc.instances  = { instance };
    tlasDesc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;

    _tlas = _device->createTopLevelAccelerationStructure(tlasDesc);
    if (!_tlas) { aout << "RaytracingDemo: createTLAS failed\n"; return false; }

    {
        auto scratch  = _device->createBuffer(_tlas->getBuildScratchSize(), BufferUsage::storage);
        auto encoder  = _device->createCommandEncoder();
        encoder->buildAccelerationStructure(_tlas, tlasDesc, scratch);
        auto cmdBuf = encoder->finish();
        _device->submit(cmdBuf);
    }

    // -----------------------------------------------------------------------
    // 5.  Bind group layout.
    //     Slot 0: TLAS  (rayGeneration)
    //     Slot 1: storage image / texture (rayGeneration)
    // -----------------------------------------------------------------------
    BindGroupLayoutDescriptor bglDesc{};
    {
        BindGroupLayoutEntry asEntry{};
        asEntry.binding    = 0;
        asEntry.type       = EntryObjectType::accelerationStructure;
        asEntry.visibility = ShaderStage::rayGeneration;
        bglDesc.entries.push_back(asEntry);

        BindGroupLayoutEntry imgEntry{};
        imgEntry.binding    = 1;
        imgEntry.type       = EntryObjectType::texture;
        imgEntry.visibility = ShaderStage::rayGeneration;
        bglDesc.entries.push_back(imgEntry);
    }
    _bindGroupLayout = _device->createBindGroupLayout(bglDesc);

    PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayouts = { _bindGroupLayout };
    _pipelineLayout = _device->createPipelineLayout(plDesc);

    // -----------------------------------------------------------------------
    // 6.  Shader modules.
    //     SPIR-V files must be present in assets/shaders/.
    //     Pass an AAssetManager* here; for the demo we leave mgr = nullptr
    //     and return false if shaders can't be loaded.
    // -----------------------------------------------------------------------
    AAssetManager* mgr = nullptr; // set from android_app* in production code

    auto rgenSpv  = loadSpv(mgr, "shaders/raygen.spv");
    auto missSpv  = loadSpv(mgr, "shaders/miss.spv");
    auto chitSpv  = loadSpv(mgr, "shaders/closesthit.spv");

    if (rgenSpv.empty() || missSpv.empty() || chitSpv.empty()) {
        aout << "RaytracingDemo: SPIR-V shaders not found in assets/shaders/\n"
             << "  Compile the GLSL reference source at the top of RaytracingDemo.cpp\n"
             << "  with glslangValidator --target-env vulkan1.2 and place the .spv\n"
             << "  files in app/src/main/assets/shaders/.\n";
        return false;
    }

    auto rgenModule = _device->createShaderModule(rgenSpv.data(), rgenSpv.size());
    auto missModule = _device->createShaderModule(missSpv.data(), missSpv.size());
    auto chitModule = _device->createShaderModule(chitSpv.data(), chitSpv.size());
    if (!rgenModule || !missModule || !chitModule) {
        aout << "RaytracingDemo: createShaderModule failed\n";
        return false;
    }

    // -----------------------------------------------------------------------
    // 7.  Ray tracing pipeline.
    // -----------------------------------------------------------------------
    RayTracingHitGroupDescriptor hitGroup{};
    hitGroup.closestHit = RayTracingShaderDescriptor{ chitModule, "main" };

    RayTracingPipelineDescriptor rtDesc{};
    rtDesc.rayGeneration   = { rgenModule, "main" };
    rtDesc.missShaders     = { { missModule, "main" } };
    rtDesc.hitGroups       = { hitGroup };
    rtDesc.layout          = _pipelineLayout;
    rtDesc.maxRecursionDepth = 1;

    _pipeline = _device->createRayTracingPipeline(rtDesc);
    if (!_pipeline) { aout << "RaytracingDemo: createRayTracingPipeline failed\n"; return false; }

    _ready = true;
    aout << "RaytracingDemo: setup complete\n";
    return true;
}

void RaytracingDemo::render(std::shared_ptr<CommandEncoder> encoder,
                             std::shared_ptr<Texture> outputTexture)
{
    if (!_ready || !encoder || !outputTexture) return;

    // Bind group: TLAS at 0, output texture at 1.
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = _bindGroupLayout;
    {
        BindGroupDescriptor::Entry asEntry{};
        asEntry.binding  = 0;
        asEntry.resource = _tlas;
        bgDesc.entries.push_back(asEntry);

        BindGroupDescriptor::Entry texEntry{};
        texEntry.binding  = 1;
        texEntry.resource = outputTexture;
        bgDesc.entries.push_back(texEntry);
    }
    auto bindGroup = _device->createBindGroup(bgDesc);

    auto rtPass = encoder->beginRayTracingPass();
    if (!rtPass) return;

    rtPass->setPipeline(_pipeline);
    if (bindGroup)
        rtPass->setBindGroup(0, bindGroup, {}, 0, 0);
    rtPass->traceRays(outputTexture->getWidth(),
                      outputTexture->getHeight(),
                      1);
    rtPass->end();
}
