//
//  main.cpp — campello_gpu ray tracing demo: Cornell box + cube + icosphere.
//
//  A headless (no window) macOS demo that exercises campello_gpu's ray
//  tracing API end to end:
//    - Procedurally builds a 5-wall, 16:9-proportioned Cornell box, plus a
//      cube BLAS (6 geometries, one per face) and an icosphere BLAS.
//    - Creates 7 BLAS total, then a 4x3 jittered grid of 12 foreground
//      instances (6 cubes + 6 spheres, checkerboarded) all reusing just
//      those two BLAS via different TLAS instance transforms — exactly
//      what instancing is for. 17 instances total in one TLAS.
//    - Compiles a ray tracing pipeline from scene.metal (see that file for
//      the shading model: hard shadows + up to 3 mirror bounces for spheres).
//    - Traces the scene into an offscreen storage texture, reads it back to
//      the CPU, and writes a binary PPM image to disk.
//
//  Build & run (see README.md in this directory for the full recipe):
//    xcrun metal    -c scene.metal -o scene.air -std=metal3.0
//    xcrun metallib scene.air -o scene.metallib
//    clang++ -std=c++20 -I <repo>/inc main.cpp -L <build-dir> -lcampello_gpu \
//        -framework Metal -framework Foundation -framework QuartzCore -lobjc \
//        -o raytracing_scene_demo
//    ./raytracing_scene_demo scene.metallib cornell_scene.ppm
//

#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/fence.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/constants/acceleration_structure_build_flag.hpp>
#include <campello_gpu/constants/acceleration_structure_geometry_type.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Small local math helpers — this demo has no dependency on a vector library.
// ---------------------------------------------------------------------------

struct Vec3 { float x, y, z; };

static Vec3 vnormalize(Vec3 v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / len, v.y / len, v.z / len };
}

static Vec3 vmid(Vec3 a, Vec3 b) {
    return vnormalize({ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f });
}

static AccelerationStructureBuildFlag combineBuildFlags(
    AccelerationStructureBuildFlag a, AccelerationStructureBuildFlag b)
{
    return static_cast<AccelerationStructureBuildFlag>(static_cast<int>(a) | static_cast<int>(b));
}

static TextureUsage combineTextureUsage(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<int>(a) | static_cast<int>(b));
}

static void setScaleTranslate(float t[3][4], float scale, float tx, float ty, float tz) {
    t[0][0] = scale; t[0][1] = 0;     t[0][2] = 0;     t[0][3] = tx;
    t[1][0] = 0;     t[1][1] = scale; t[1][2] = 0;     t[1][3] = ty;
    t[2][0] = 0;     t[2][1] = 0;     t[2][2] = scale; t[2][3] = tz;
}

// Fixed-seed xorshift64* PRNG — deterministic so the "random" layout of
// foreground objects is reproducible across runs (repeatable screenshots
// and benchmarks) rather than different every time.
struct SimpleRng {
    uint64_t state;
    explicit SimpleRng(uint64_t seed) : state(seed) {}
    float next01() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        uint64_t r = state * 0x2545F4914F6CDD1DULL;
        return static_cast<float>((r >> 40) & 0xFFFFFF) / static_cast<float>(0x1000000);
    }
    float range(float lo, float hi) { return lo + next01() * (hi - lo); }
};

// ---------------------------------------------------------------------------
// Icosphere generation — a plain triangle soup (no index buffer), so each
// subdivision step just emits 4 new triangles per input triangle. Since
// shading uses the analytic sphere normal (see scene.metal), triangle count
// only affects silhouette smoothness, not shading quality.
// ---------------------------------------------------------------------------

struct Tri { Vec3 a, b, c; };

static std::vector<Tri> buildIcosphere(int subdivisions) {
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    Vec3 v[12] = {
        vnormalize({-1,  t, 0}), vnormalize({ 1,  t, 0}), vnormalize({-1, -t, 0}), vnormalize({ 1, -t, 0}),
        vnormalize({ 0, -1,  t}), vnormalize({ 0,  1,  t}), vnormalize({ 0, -1, -t}), vnormalize({ 0,  1, -t}),
        vnormalize({ t,  0, -1}), vnormalize({ t,  0,  1}), vnormalize({-t,  0, -1}), vnormalize({-t,  0,  1}),
    };
    int faces[20][3] = {
        {0,11,5}, {0,5,1}, {0,1,7}, {0,7,10}, {0,10,11},
        {1,5,9},  {5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},  {3,4,2}, {3,2,6}, {3,6,8}, {3,8,9},
        {4,9,5},  {2,4,11},{6,2,10},{8,6,7}, {9,8,1},
    };

    std::vector<Tri> tris;
    tris.reserve(20);
    for (auto& f : faces) tris.push_back({ v[f[0]], v[f[1]], v[f[2]] });

    for (int s = 0; s < subdivisions; s++) {
        std::vector<Tri> next;
        next.reserve(tris.size() * 4);
        for (auto& tri : tris) {
            Vec3 ab = vmid(tri.a, tri.b);
            Vec3 bc = vmid(tri.b, tri.c);
            Vec3 ca = vmid(tri.c, tri.a);
            next.push_back({ tri.a, ab, ca });
            next.push_back({ tri.b, bc, ab });
            next.push_back({ tri.c, ca, bc });
            next.push_back({ ab, bc, ca });
        }
        tris = std::move(next);
    }
    return tris;
}

static std::vector<float> flattenTriangles(const std::vector<Tri>& tris) {
    std::vector<float> out;
    out.reserve(tris.size() * 9);
    for (auto& t : tris) {
        out.insert(out.end(), { t.a.x, t.a.y, t.a.z });
        out.insert(out.end(), { t.b.x, t.b.y, t.b.z });
        out.insert(out.end(), { t.c.x, t.c.y, t.c.z });
    }
    return out;
}

// A single quad as two non-indexed triangles (a,b,c) + (a,c,d).
static std::vector<float> quadVertices(Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    return {
        a.x, a.y, a.z,  b.x, b.y, b.z,  c.x, c.y, c.z,
        a.x, a.y, a.z,  c.x, c.y, c.z,  d.x, d.y, d.z,
    };
}

// ---------------------------------------------------------------------------
// BLAS bookkeeping — the descriptor used at build time must match the one
// used at creation time, so each entry keeps its own copy.
// ---------------------------------------------------------------------------

struct BlasEntry {
    std::shared_ptr<AccelerationStructure> as;
    BottomLevelAccelerationStructureDescriptor desc;
};

static AccelerationStructureGeometryDescriptor makeSoupGeometry(
    std::shared_ptr<Buffer> vbuf, uint32_t vertexCount)
{
    AccelerationStructureGeometryDescriptor g{};
    g.type          = AccelerationStructureGeometryType::triangles;
    g.opaque        = true;
    g.vertexBuffer  = vbuf;
    g.vertexOffset  = 0;
    g.vertexStride  = sizeof(float) * 3;
    g.vertexCount   = vertexCount;
    g.componentType = ComponentType::ctFloat;
    return g;
}

static BlasEntry makeBlasEntry(
    const std::shared_ptr<Device>& device,
    std::vector<AccelerationStructureGeometryDescriptor> geometries,
    AccelerationStructureBuildFlag flags)
{
    BlasEntry entry;
    entry.desc.geometries = std::move(geometries);
    entry.desc.buildFlags = flags;
    entry.as = device->createBottomLevelAccelerationStructure(entry.desc);
    return entry;
}

int main(int argc, char** argv) {
    const std::string metallibPath    = argc > 1 ? argv[1] : "scene.metallib";
    const std::string outputPath      = argc > 2 ? argv[2] : "cornell_scene.ppm";
    const int         benchIterations = argc > 3 ? std::atoi(argv[3]) : 200;
    const uint32_t width  = argc > 4 ? static_cast<uint32_t>(std::atoi(argv[4])) : 1920;
    const uint32_t height = argc > 5 ? static_cast<uint32_t>(std::atoi(argv[5])) : 1080;

    // -----------------------------------------------------------------------
    // Device + feature check.
    // -----------------------------------------------------------------------
    auto device = Device::createDefaultDevice(nullptr);
    if (!device) { std::cerr << "Failed to create campello_gpu device\n"; return 1; }

    auto features = device->getFeatures();
    if (!features.count(Feature::raytracing)) {
        std::cerr << "Feature::raytracing not available on " << device->getName() << "\n";
        return 1;
    }
    std::cout << "Raytracing supported on " << device->getName() << "\n";

    const auto fastTrace = AccelerationStructureBuildFlag::preferFastTrace;

    // -----------------------------------------------------------------------
    // Cornell box shell — 5 walls, each its own BLAS + TLAS instance.
    // Box footprint is 16:9 (width:height) to match the default 1920x1080
    // output: x in [-bx,bx], y in [-1,1], z in [-1,1]; the z=-1 face (facing
    // the camera) is open. bx and the camera fovScale in scene.metal are
    // tuned together so the box fills the frame at the front opening — see
    // the comment above fovScale there. Normals point inward; hardcoded
    // again in scene.metal per instance ID.
    // -----------------------------------------------------------------------
    const float bx = 16.0f / 9.0f; // box half-width; half-height stays 1

    auto floorVerts   = quadVertices({-bx,-1,-1}, { bx,-1,-1}, { bx,-1, 1}, {-bx,-1, 1});
    auto ceilVerts    = quadVertices({-bx, 1, 1}, { bx, 1, 1}, { bx, 1,-1}, {-bx, 1,-1});
    auto backVerts    = quadVertices({-bx,-1, 1}, { bx,-1, 1}, { bx, 1, 1}, {-bx, 1, 1});
    auto leftVerts    = quadVertices({-bx,-1, 1}, {-bx,-1,-1}, {-bx, 1,-1}, {-bx, 1, 1}); // red
    auto rightVerts   = quadVertices({ bx,-1,-1}, { bx,-1, 1}, { bx, 1, 1}, { bx, 1,-1}); // green

    auto floorBuf = device->createBuffer(floorVerts.size() * sizeof(float), BufferUsage::accelerationStructureInput, floorVerts.data());
    auto ceilBuf  = device->createBuffer(ceilVerts.size()  * sizeof(float), BufferUsage::accelerationStructureInput, ceilVerts.data());
    auto backBuf  = device->createBuffer(backVerts.size()  * sizeof(float), BufferUsage::accelerationStructureInput, backVerts.data());
    auto leftBuf  = device->createBuffer(leftVerts.size()  * sizeof(float), BufferUsage::accelerationStructureInput, leftVerts.data());
    auto rightBuf = device->createBuffer(rightVerts.size() * sizeof(float), BufferUsage::accelerationStructureInput, rightVerts.data());

    std::vector<BlasEntry> blasEntries; // index order == instance ID order
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(floorBuf, 6) }, fastTrace)); // 0
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(ceilBuf,  6) }, fastTrace)); // 1
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(backBuf,  6) }, fastTrace)); // 2
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(leftBuf,  6) }, fastTrace)); // 3
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(rightBuf, 6) }, fastTrace)); // 4

    // -----------------------------------------------------------------------
    // Cube — one BLAS, 6 geometries (one per face) so geometry_id selects the
    // correct face normal in the shader. Unit cube in object space
    // ([-0.5,0.5]^3); placed via the TLAS instance transform below.
    // -----------------------------------------------------------------------
    Vec3 c000{-0.5f,-0.5f,-0.5f}, c001{-0.5f,-0.5f, 0.5f}, c010{-0.5f, 0.5f,-0.5f}, c011{-0.5f, 0.5f, 0.5f};
    Vec3 c100{ 0.5f,-0.5f,-0.5f}, c101{ 0.5f,-0.5f, 0.5f}, c110{ 0.5f, 0.5f,-0.5f}, c111{ 0.5f, 0.5f, 0.5f};

    std::vector<std::vector<float>> cubeFaceVerts = {
        quadVertices(c100, c101, c111, c110), // +X
        quadVertices(c001, c000, c010, c011), // -X
        quadVertices(c010, c110, c111, c011), // +Y
        quadVertices(c001, c101, c100, c000), // -Y
        quadVertices(c101, c001, c011, c111), // +Z
        quadVertices(c000, c100, c110, c010), // -Z
    };
    std::vector<std::shared_ptr<Buffer>> cubeFaceBufs;
    std::vector<AccelerationStructureGeometryDescriptor> cubeGeos;
    for (auto& fv : cubeFaceVerts) {
        auto buf = device->createBuffer(fv.size() * sizeof(float), BufferUsage::accelerationStructureInput, fv.data());
        cubeFaceBufs.push_back(buf);
        cubeGeos.push_back(makeSoupGeometry(buf, 6));
    }
    blasEntries.push_back(makeBlasEntry(device, cubeGeos, fastTrace)); // 5

    // -----------------------------------------------------------------------
    // Icosphere — unit sphere (radius 1) in object space, placed via the
    // TLAS instance transform. Subdivision level 3 -> 1280 triangles.
    // -----------------------------------------------------------------------
    auto icoTris  = buildIcosphere(3);
    auto icoVerts = flattenTriangles(icoTris);
    auto icoBuf   = device->createBuffer(icoVerts.size() * sizeof(float), BufferUsage::accelerationStructureInput, icoVerts.data());
    blasEntries.push_back(makeBlasEntry(device, { makeSoupGeometry(icoBuf, static_cast<uint32_t>(icoTris.size() * 3)) }, fastTrace)); // 6

    for (auto& e : blasEntries) {
        if (!e.as) { std::cerr << "Failed to create a BLAS\n"; return 1; }
    }

    // -----------------------------------------------------------------------
    // Build every BLAS in one command buffer, then wait for the GPU before
    // building the TLAS (its instances must reference fully-built BLAS).
    // -----------------------------------------------------------------------
    {
        auto encoder = device->createCommandEncoder();
        for (auto& e : blasEntries) {
            auto scratch = device->createBuffer(e.as->getBuildScratchSize(), BufferUsage::storage);
            encoder->buildAccelerationStructure(e.as, e.desc, scratch);
        }
        auto cmdBuf = encoder->finish();
        auto fence  = device->createFence();
        device->submit(cmdBuf, fence);
        if (!fence->wait()) { std::cerr << "Timed out waiting for BLAS builds\n"; return 1; }
    }

    // -----------------------------------------------------------------------
    // TLAS instances. Wall instance IDs 0..4 must match scene.metal's
    // kInstanceFloor..kInstanceRight. Foreground objects start at instance
    // ID kFgStart=5 and fill a gridCols x gridRows grid in row-major order
    // (index = id - 5, row = index/gridCols, col = index%gridCols),
    // checkerboarded cube/sphere by (row+col)%2 — scene.metal recomputes
    // the same row/col/parity from instance_id alone to pick the shading
    // category, so gridCols and the parity formula must stay in sync
    // between the two files. Both categories reuse the SAME cube/icosphere
    // BLAS via per-instance transforms; per-object color is derived in the
    // shader from instance_id, not stored here.
    // -----------------------------------------------------------------------
    std::vector<AccelerationStructureInstance> instances;
    instances.reserve(5 + 12);
    for (uint32_t i = 0; i < 5; i++) {
        AccelerationStructureInstance inst{};
        inst.blas       = blasEntries[i].as;
        inst.instanceId = i;
        inst.mask       = 0xFF;
        inst.opaque     = true;
        // transform defaults to identity — wall vertices are already in world space.
        instances.push_back(inst);
    }

    const int gridCols = 4, gridRows = 3; // 12 cells total
    const float marginX = 0.35f, marginZ = 0.30f;
    const float cellW = (2.0f * bx - 2.0f * marginX) / gridCols;
    const float cellD = (2.0f * 1.0f - 2.0f * marginZ) / gridRows;
    const float startX = -bx + marginX + cellW * 0.5f;
    const float startZ = -1.0f + marginZ + cellD * 0.5f;

    SimpleRng rng(12345); // fixed seed -> reproducible layout
    uint32_t nextInstanceId = 5;
    for (int r = 0; r < gridRows; r++) {
        for (int c = 0; c < gridCols; c++) {
            bool isCube = ((r + c) % 2) == 0; // checkerboard -> 6 cubes + 6 spheres
            float cx = startX + c * cellW + rng.range(-0.08f, 0.08f);
            float cz = startZ + r * cellD + rng.range(-0.08f, 0.08f);

            AccelerationStructureInstance inst{};
            inst.instanceId = nextInstanceId++;
            inst.mask       = 0xFF;
            inst.opaque     = true;

            if (isCube) {
                float scale = rng.range(0.24f, 0.40f); // local cube spans [-0.5,0.5]
                inst.blas = blasEntries[5].as;
                setScaleTranslate(inst.transform, scale, cx, -1.0f + scale * 0.5f, cz);
            } else {
                float scale = rng.range(0.14f, 0.22f); // local sphere radius 1 -> world radius = scale
                inst.blas = blasEntries[6].as;
                setScaleTranslate(inst.transform, scale, cx, -1.0f + scale, cz);
            }
            instances.push_back(inst);
        }
    }

    TopLevelAccelerationStructureDescriptor tlasDesc{};
    tlasDesc.instances  = instances;
    tlasDesc.buildFlags = fastTrace;

    auto tlas = device->createTopLevelAccelerationStructure(tlasDesc);
    if (!tlas) { std::cerr << "Failed to create TLAS\n"; return 1; }

    {
        auto scratch = device->createBuffer(tlas->getBuildScratchSize(), BufferUsage::storage);
        auto encoder = device->createCommandEncoder();
        encoder->buildAccelerationStructure(tlas, tlasDesc, scratch);
        auto cmdBuf = encoder->finish();
        auto fence  = device->createFence();
        device->submit(cmdBuf, fence);
        if (!fence->wait()) { std::cerr << "Timed out waiting for TLAS build\n"; return 1; }
    }

    std::cout << "Built " << blasEntries.size() << " BLAS + 1 TLAS ("
              << instances.size() << " instances)\n";

    // -----------------------------------------------------------------------
    // Bind group layout: slot 0 = TLAS, slot 1 = output storage texture.
    // -----------------------------------------------------------------------
    BindGroupLayoutDescriptor bglDesc{};
    {
        EntryObject asEntry{};
        asEntry.binding    = 0;
        asEntry.type       = EntryObjectType::accelerationStructure;
        asEntry.visibility = ShaderStage::rayGeneration;
        bglDesc.entries.push_back(asEntry);

        // NOTE: Metal keeps separate argument-index spaces per resource type
        // (buffers, textures, samplers each have their own 0..N table), so
        // this binding is the *texture* index — it must match `[[texture(0)]]`
        // in scene.metal, independently of the AS using buffer index 0 above.
        EntryObject texEntry{};
        texEntry.binding    = 0;
        texEntry.type       = EntryObjectType::texture;
        texEntry.visibility = ShaderStage::rayGeneration;
        texEntry.data.texture = { false, EntryObjectTextureType::ttFloat, TextureType::tt2d };
        bglDesc.entries.push_back(texEntry);
    }
    auto bindGroupLayout = device->createBindGroupLayout(bglDesc);

    PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayouts = { bindGroupLayout };
    auto pipelineLayout = device->createPipelineLayout(plDesc);

    // -----------------------------------------------------------------------
    // Shader module + ray tracing pipeline.
    // -----------------------------------------------------------------------
    std::ifstream libFile(metallibPath, std::ios::binary | std::ios::ate);
    if (!libFile) { std::cerr << "Could not open " << metallibPath << "\n"; return 1; }
    auto libSize = static_cast<size_t>(libFile.tellg());
    libFile.seekg(0);
    std::vector<uint8_t> libBytes(libSize);
    libFile.read(reinterpret_cast<char*>(libBytes.data()), libSize);

    auto shaderModule = device->createShaderModule(libBytes.data(), libBytes.size());
    if (!shaderModule) { std::cerr << "createShaderModule failed\n"; return 1; }

    RayTracingPipelineDescriptor rtDesc{};
    rtDesc.rayGeneration     = { shaderModule, "rayGenMain" };
    rtDesc.layout            = pipelineLayout;
    rtDesc.maxRecursionDepth = 4; // primary + up to 3 mirror bounces (see scene.metal)

    auto pipeline = device->createRayTracingPipeline(rtDesc);
    if (!pipeline) { std::cerr << "createRayTracingPipeline failed\n"; return 1; }

    // -----------------------------------------------------------------------
    // Output texture + readback buffer.
    // -----------------------------------------------------------------------
    auto outputTexture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        width, height, 1, 1, 1,
        combineTextureUsage(TextureUsage::storageBinding, TextureUsage::copySrc));
    if (!outputTexture) { std::cerr << "createTexture failed\n"; return 1; }

    BindGroupDescriptor bgDesc{};
    bgDesc.layout  = bindGroupLayout;
    bgDesc.entries = {
        { 0, tlas },           // buffer index 0 in the shader
        { 0, outputTexture },  // texture index 0 in the shader (separate space)
    };
    auto bindGroup = device->createBindGroup(bgDesc);
    if (!bindGroup) { std::cerr << "createBindGroup failed\n"; return 1; }

    const uint64_t bytesPerRow  = static_cast<uint64_t>(width) * 4;
    const uint64_t imageBytes   = bytesPerRow * height;
    auto readbackBuffer = device->createBuffer(imageBytes, BufferUsage::copyDst);
    if (!readbackBuffer) { std::cerr << "createBuffer (readback) failed\n"; return 1; }

    // -----------------------------------------------------------------------
    // Trace rays, then copy the result into the readback buffer.
    // -----------------------------------------------------------------------
    {
        auto encoder = device->createCommandEncoder();
        auto rtPass  = encoder->beginRayTracingPass();
        if (!rtPass) { std::cerr << "beginRayTracingPass failed\n"; return 1; }

        rtPass->setPipeline(pipeline);
        rtPass->setBindGroup(0, bindGroup);
        rtPass->traceRays(width, height, 1);
        rtPass->end();

        encoder->copyTextureToBuffer(outputTexture, 0, 0, readbackBuffer, 0, bytesPerRow);

        auto cmdBuf = encoder->finish();
        auto fence  = device->createFence();
        device->submit(cmdBuf, fence);
        if (!fence->wait()) { std::cerr << "Timed out waiting for the ray tracing pass\n"; return 1; }
        if (fence->didFail()) {
            std::cerr << "Ray tracing pass failed: " << fence->failureReason() << "\n"; return 1;
        }
    }

    std::cout << "Traced " << width << "x" << height << " pixels\n";

    // -----------------------------------------------------------------------
    // Download pixels and write a binary PPM (P6) — no external dependency.
    // -----------------------------------------------------------------------
    std::vector<uint8_t> pixels(imageBytes);
    if (!readbackBuffer->download(0, imageBytes, pixels.data())) {
        std::cerr << "Buffer::download failed\n"; return 1;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out) { std::cerr << "Could not open " << outputPath << " for writing\n"; return 1; }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t* row = pixels.data() + static_cast<size_t>(y) * bytesPerRow;
        for (uint32_t x = 0; x < width; x++) {
            out.write(reinterpret_cast<const char*>(row + x * 4), 3); // drop alpha
        }
    }

    std::cout << "Wrote " << outputPath << "\n";

    // -----------------------------------------------------------------------
    // Performance benchmark: repeat the ray tracing dispatch alone (no
    // texture readback) many times and report timing statistics. This
    // excludes one-time setup cost (BLAS/TLAS build, pipeline compilation,
    // texture/buffer allocation) — only the steady-state per-frame cost of
    // record + submit + GPU execution + CPU wait.
    // -----------------------------------------------------------------------
    if (benchIterations > 0) {
        const int warmupIterations = 5;

        auto runOnce = [&]() {
            auto encoder = device->createCommandEncoder();
            auto rtPass  = encoder->beginRayTracingPass();
            rtPass->setPipeline(pipeline);
            rtPass->setBindGroup(0, bindGroup);
            rtPass->traceRays(width, height, 1);
            rtPass->end();
            auto cmdBuf = encoder->finish();
            auto fence  = device->createFence();
            device->submit(cmdBuf, fence);
            fence->wait();
            return fence;
        };

        for (int i = 0; i < warmupIterations; i++) runOnce();

        std::vector<double> samplesMs;
        samplesMs.reserve(benchIterations);
        for (int i = 0; i < benchIterations; i++) {
            auto t0    = std::chrono::high_resolution_clock::now();
            auto fence = runOnce();
            auto t1    = std::chrono::high_resolution_clock::now();
            if (fence->didFail()) {
                std::cerr << "Benchmark iteration " << i << " failed: " << fence->failureReason() << "\n";
                return 1;
            }
            samplesMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }

        std::sort(samplesMs.begin(), samplesMs.end());
        double sum = 0.0;
        for (double v : samplesMs) sum += v;
        double mean = sum / samplesMs.size();
        double variance = 0.0;
        for (double v : samplesMs) variance += (v - mean) * (v - mean);
        variance /= samplesMs.size();
        double stddev  = std::sqrt(variance);
        double median  = samplesMs[samplesMs.size() / 2];
        double p95     = samplesMs[static_cast<size_t>(samplesMs.size() * 0.95)];
        double minMs   = samplesMs.front();
        double maxMs   = samplesMs.back();
        double mraysPerSec = (static_cast<double>(width) * height) / (mean / 1000.0) / 1e6;

        std::cout << "\n--- Ray tracing performance (" << benchIterations
                  << " iterations, " << warmupIterations << " warmup, discarded) ---\n";
        std::cout << "Resolution:  " << width << "x" << height
                  << " (" << (width * height) << " rays/frame)\n";
        std::cout << "Min:         " << minMs   << " ms\n";
        std::cout << "Mean:        " << mean    << " ms\n";
        std::cout << "Median:      " << median  << " ms\n";
        std::cout << "P95:         " << p95     << " ms\n";
        std::cout << "Max:         " << maxMs   << " ms\n";
        std::cout << "Stddev:      " << stddev  << " ms\n";
        std::cout << "Throughput:  " << (1000.0 / mean) << " fps, "
                  << mraysPerSec << " Mrays/s\n";
    }

    return 0;
}
