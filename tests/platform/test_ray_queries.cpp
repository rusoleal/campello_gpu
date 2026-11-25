// Inline ray queries against acceleration structures, checked hit by hit
// against a brute-force CPU tracer.
//
// The shaders are WGSL using the software ray tracing library
// (`campello_acceleration_structure`, `campello_traceRay`), so these tests
// run on the WebGPU backend; elsewhere they skip.

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/bind_group.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/command_buffer.hpp>
#include <campello_gpu/compute_pass_encoder.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/ray_tracing_pass_encoder.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/descriptors/ray_tracing_pipeline_descriptor.hpp>
#include <campello_gpu/render_pass_encoder.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/texture_view.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/ray_tracing_capabilities.hpp>
#include <campello_gpu/validation_diagnostics.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace systems::leal::campello_gpu;

#if defined(__EMSCRIPTEN__)

namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;

// ---------------------------------------------------------------------------
// GPU side: one invocation per ray writes what campello_traceRay found.
// ---------------------------------------------------------------------------

const char* kTraceWGSL = R"(
@group(0) @binding(0) var scene: campello_acceleration_structure;

struct RayIn { o: vec4f, d: vec4f }
@group(0) @binding(1) var<storage, read> rays: array<RayIn>;

struct Out {
  t: f32, kind: u32, prim: u32, geom: u32,
  inst: u32, instId: u32, b1: f32, b2: f32,
  front: u32, any: u32, pad0: u32, pad1: u32,
}
@group(0) @binding(2) var<storage, read_write> outs: array<Out>;

struct Params { flags: u32, mask: u32, count: u32, pad: u32 }
@group(0) @binding(3) var<uniform> params: Params;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= params.count) { return; }
  var ray: CampelloRay;
  ray.origin = rays[i].o.xyz;
  ray.tMin = rays[i].o.w;
  ray.direction = rays[i].d.xyz;
  ray.tMax = rays[i].d.w;
  let h = campello_traceRay(scene, ray, params.flags, params.mask);
  var o: Out;
  o.t = h.t;
  o.kind = h.kind;
  o.prim = h.primitiveIndex;
  o.geom = h.geometryIndex;
  o.inst = h.instanceIndex;
  o.instId = h.instanceId;
  o.b1 = h.barycentrics.x;
  o.b2 = h.barycentrics.y;
  o.front = select(0u, 1u, h.frontFace);
  o.any = select(0u, 1u, campello_traceRayAny(scene, ray, params.flags, params.mask));
  outs[i] = o;
}
)";

struct GpuHit {
    float t;
    uint32_t kind, prim, geom, inst, instId;
    float b1, b2;
    uint32_t front, any, pad0, pad1;
};
static_assert(sizeof(GpuHit) == 48, "matches struct Out");

struct Ray {
    float o[3];
    float tMin;
    float d[3];
    float tMax;
};

// ---------------------------------------------------------------------------
// CPU side: the same scene, traced by brute force in double precision.
// ---------------------------------------------------------------------------

struct Vec3 { double x, y, z; };
static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct CpuGeometry {
    bool boxes = false;
    std::vector<Vec3> triangles;   // 3 per triangle
    std::vector<Vec3> boxBounds;   // min, max per box
    bool opaque = true;
};

struct CpuInstance {
    std::vector<CpuGeometry>* blas = nullptr;
    double xf[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    uint32_t id = 0;
    uint8_t mask = 0xFF;
};

struct CpuHit {
    bool hit = false;
    bool procedural = false;
    double t = 0;
    uint32_t prim = 0, geom = 0, inst = kNone, instId = 0;
    double b1 = 0, b2 = 0;
    bool front = false;
};

static void invert(const double m[3][4], double out[3][4]) {
    const double a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2];
    const double g = m[2][0], h = m[2][1], i = m[2][2];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double inv = 1.0 / (a * A + b * B + c * C);
    const double r[3][3] = {{A * inv, -(b * i - c * h) * inv, (b * f - c * e) * inv},
                            {B * inv, (a * i - c * g) * inv, -(a * f - c * d) * inv},
                            {C * inv, -(a * h - b * g) * inv, (a * e - b * d) * inv}};
    for (int k = 0; k < 3; ++k) {
        for (int j = 0; j < 3; ++j) out[k][j] = r[k][j];
        out[k][3] = -(r[k][0] * m[0][3] + r[k][1] * m[1][3] + r[k][2] * m[2][3]);
    }
}

static Vec3 apply(const double m[3][4], Vec3 p, double w) {
    return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3] * w,
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3] * w,
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3] * w};
}

static void traceBlas(const std::vector<CpuGeometry>& blas, Vec3 o, Vec3 d, double tMin, CpuHit& best,
                      uint32_t inst, uint32_t instId, uint32_t flags) {
    for (uint32_t g = 0; g < blas.size(); ++g) {
        const CpuGeometry& geo = blas[g];
        if (geo.boxes) {
            for (uint32_t p = 0; p < geo.boxBounds.size() / 2; ++p) {
                const Vec3 mn = geo.boxBounds[2 * p], mx = geo.boxBounds[2 * p + 1];
                double tn = tMin, tf = best.t;
                const double os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
                const double lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
                for (int k = 0; k < 3; ++k) {
                    double t0 = (lo[k] - os[k]) / ds[k], t1 = (hi[k] - os[k]) / ds[k];
                    tn = std::max(tn, std::min(t0, t1));
                    tf = std::min(tf, std::max(t0, t1));
                }
                if (tn <= tf && tn < best.t) {
                    best = {true, true, tn, p, g, inst, instId, 0, 0, true};
                }
            }
            continue;
        }
        for (uint32_t p = 0; p < geo.triangles.size() / 3; ++p) {
            const Vec3 a = geo.triangles[3 * p], b = geo.triangles[3 * p + 1], c = geo.triangles[3 * p + 2];
            const Vec3 e1 = sub(b, a), e2 = sub(c, a);
            const Vec3 pv = cross(d, e2);
            const double det = dot(e1, pv);
            if (std::fabs(det) < 1e-18) continue;
            const Vec3 tv = sub(o, a);
            const double u = dot(tv, pv) / det;
            if (u < 0 || u > 1) continue;
            const Vec3 qv = cross(tv, e1);
            const double v = dot(d, qv) / det;
            if (v < 0 || u + v > 1) continue;
            const double t = dot(e2, qv) / det;
            if (t < tMin || t >= best.t) continue;
            const bool front = dot(cross(e1, e2), d) > 0;
            if (front && (flags & 0x20)) continue;   // cull front facing
            if (!front && (flags & 0x10)) continue;  // cull back facing
            best = {true, false, t, p, g, inst, instId, u, v, front};
        }
    }
}

static CpuHit traceCpu(const std::vector<CpuInstance>* tlas, const std::vector<CpuGeometry>* blas,
                       const Ray& r, uint32_t flags, uint32_t mask) {
    CpuHit best;
    best.t = r.tMax;
    const Vec3 o{r.o[0], r.o[1], r.o[2]}, d{r.d[0], r.d[1], r.d[2]};
    if (!tlas) {
        traceBlas(*blas, o, d, r.tMin, best, kNone, 0, flags);
        return best;
    }
    for (uint32_t i = 0; i < tlas->size(); ++i) {
        const CpuInstance& in = (*tlas)[i];
        if ((in.mask & mask) == 0) continue;
        double inv[3][4];
        invert(in.xf, inv);
        traceBlas(*in.blas, apply(inv, o, 1), apply(inv, d, 0), r.tMin, best, i, in.id, flags);
    }
    return best;
}

// ---------------------------------------------------------------------------
// Scene construction
// ---------------------------------------------------------------------------

/** `count` random triangles of edge ~`size` inside [-1, 1]^3. */
static std::vector<Vec3> randomTriangles(uint32_t count, double size, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> pos(-1.0, 1.0), off(-size, size);
    std::vector<Vec3> tris;
    for (uint32_t i = 0; i < count; ++i) {
        const Vec3 c{pos(rng), pos(rng), pos(rng)};
        for (int k = 0; k < 3; ++k) tris.push_back({c.x + off(rng), c.y + off(rng), c.z + off(rng)});
    }
    return tris;
}

/** Vertex buffer (shared vertices are not needed: indices are 0..n-1 or shuffled). */
struct TriangleMesh {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;  // empty = non-indexed
};

static TriangleMesh toMesh(const std::vector<Vec3>& tris, bool indexed) {
    TriangleMesh m;
    if (!indexed) {
        for (const auto& v : tris) {
            m.vertices.push_back(float(v.x)); m.vertices.push_back(float(v.y)); m.vertices.push_back(float(v.z));
        }
        return m;
    }
    // Store vertices in reverse order and index them back, so indexing matters.
    const uint32_t n = uint32_t(tris.size());
    for (uint32_t i = 0; i < n; ++i) {
        const auto& v = tris[n - 1 - i];
        m.vertices.push_back(float(v.x)); m.vertices.push_back(float(v.y)); m.vertices.push_back(float(v.z));
    }
    for (uint32_t i = 0; i < n; ++i) m.indices.push_back(n - 1 - i);
    return m;
}

/** The CPU copy of a triangle geometry as the GPU sees it (float positions). */
static CpuGeometry cpuTriangles(const std::vector<Vec3>& tris) {
    CpuGeometry g;
    for (const auto& v : tris) g.triangles.push_back({double(float(v.x)), double(float(v.y)), double(float(v.z))});
    return g;
}

/** A pinhole camera's rays at `origin`, looking along +z through a w x h grid on z = 0. */
static std::vector<Ray> cameraRays(Vec3 origin, double halfW, double halfH, uint32_t w, uint32_t h,
                                   float tMin = 0.0f, float tMax = 1e30f) {
    std::vector<Ray> rays;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const double px = -halfW + 2 * halfW * (x + 0.5) / w;
            const double py = -halfH + 2 * halfH * (y + 0.5) / h;
            Ray r{};
            r.o[0] = float(origin.x); r.o[1] = float(origin.y); r.o[2] = float(origin.z);
            r.d[0] = float(px - origin.x); r.d[1] = float(py - origin.y); r.d[2] = float(-origin.z);
            r.tMin = tMin;
            r.tMax = tMax;
            rays.push_back(r);
        }
    }
    return rays;
}

// ---------------------------------------------------------------------------
// Running a trace on the GPU
// ---------------------------------------------------------------------------

class RayQueryRig {
public:
    explicit RayQueryRig(std::shared_ptr<Device> device) : device_(std::move(device)) {
        std::vector<EntryObject> entries(4);
        for (uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = ShaderStage::compute;
        }
        entries[0].type = EntryObjectType::accelerationStructure;
        entries[1].type = EntryObjectType::buffer;
        entries[1].data.buffer.type = EntryObjectBufferType::readOnlyStorage;
        entries[2].type = EntryObjectType::buffer;
        entries[2].data.buffer.type = EntryObjectBufferType::storage;
        entries[3].type = EntryObjectType::buffer;
        entries[3].data.buffer.type = EntryObjectBufferType::uniform;
        BindGroupLayoutDescriptor bglDesc{};
        bglDesc.entries = entries;
        bgl_ = device_->createBindGroupLayout(bglDesc);

        PipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayouts = {bgl_};
        ComputePipelineDescriptor cpDesc{};
        cpDesc.compute.module = device_->createShaderModule(kTraceWGSL);
        cpDesc.compute.entryPoint = "main";
        cpDesc.layout = device_->createPipelineLayout(plDesc);
        pipeline_ = device_->createComputePipeline(cpDesc);
    }

    bool ready() const { return bgl_ && pipeline_; }

    std::vector<GpuHit> trace(std::shared_ptr<AccelerationStructure> as, const std::vector<Ray>& rays,
                              uint32_t flags = 0, uint32_t mask = 0xFF) {
        const uint32_t n = uint32_t(rays.size());
        std::vector<GpuHit> hits(n);
        auto rayBuf = device_->createBuffer(n * sizeof(Ray), BufferUsage::storage, const_cast<Ray*>(rays.data()));
        auto outBuf = device_->createBuffer(n * sizeof(GpuHit), BufferUsage::storage);
        uint32_t params[4] = {flags, mask, n, 0};
        auto paramBuf = device_->createBuffer(sizeof(params), BufferUsage::uniform, params);

        BindGroupDescriptor bgDesc{};
        bgDesc.layout = bgl_;
        bgDesc.entries = {{0, as},
                          {1, BufferBinding{rayBuf, 0, n * sizeof(Ray)}},
                          {2, BufferBinding{outBuf, 0, n * sizeof(GpuHit)}},
                          {3, BufferBinding{paramBuf, 0, sizeof(params)}}};
        auto bg = device_->createBindGroup(bgDesc, false);

        auto encoder = device_->createCommandEncoder();
        auto pass = encoder->beginComputePass();
        pass->setPipeline(pipeline_);
        pass->setBindGroup(0, bg, {}, 0, 0);
        pass->dispatchWorkgroups((n + 63) / 64, 1, 1);
        pass->end();
        device_->submit(encoder->finish());
        outBuf->download(0, n * sizeof(GpuHit), hits.data());
        return hits;
    }

private:
    std::shared_ptr<Device> device_;
    std::shared_ptr<BindGroupLayout> bgl_;
    std::shared_ptr<ComputePipeline> pipeline_;
};

/** Builds `as` (BLAS or TLAS) and submits. */
template <typename Desc>
static void build(const std::shared_ptr<Device>& device, const std::shared_ptr<AccelerationStructure>& as,
                  const Desc& desc) {
    auto scratch = device->createBuffer(as->getBuildScratchSize(), BufferUsage::storage);
    auto encoder = device->createCommandEncoder();
    encoder->buildAccelerationStructure(as, desc, scratch);
    device->submit(encoder->finish());
}

/** Compares GPU hits with the CPU tracer; returns the number of mismatching rays. */
static uint32_t compare(const std::vector<GpuHit>& gpu, const std::vector<Ray>& rays,
                        const std::vector<CpuInstance>* tlas, const std::vector<CpuGeometry>* blas,
                        uint32_t flags, uint32_t mask, uint32_t* hitCount = nullptr) {
    uint32_t bad = 0, hits = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const CpuHit ref = traceCpu(tlas, blas, rays[i], flags, mask);
        const GpuHit& g = gpu[i];
        bool ok = (g.kind != 0) == ref.hit && (g.any != 0) == ref.hit;
        if (ok && ref.hit) {
            hits++;
            const bool sameT = std::fabs(g.t - ref.t) <= 1e-4 * std::max(1.0, ref.t);
            // A ray through an edge shared by two triangles may report either.
            const bool edgeTie = std::fabs(g.t - ref.t) <= 1e-5 * std::max(1.0, ref.t) && g.inst == ref.inst;
            const bool samePrim = g.prim == ref.prim && g.geom == ref.geom;
            ok = g.kind == (ref.procedural ? 2u : 1u) && (samePrim || edgeTie) && g.inst == ref.inst &&
                 g.instId == ref.instId && sameT;
            if (ok && samePrim && !ref.procedural) {
                ok = std::fabs(g.b1 - ref.b1) < 1e-3 && std::fabs(g.b2 - ref.b2) < 1e-3 &&
                     (g.front != 0) == ref.front;
            }
        }
        if (!ok) {
            if (bad < 5) {
                printf("  ray %zu: gpu kind %u t %g prim %u geom %u inst %u any %u | cpu hit %d t %g prim %u geom %u inst %u\n",
                       i, g.kind, g.t, g.prim, g.geom, g.inst, g.any, ref.hit, ref.t, ref.prim, ref.geom, ref.inst);
            }
            bad++;
        }
    }
    if (hitCount) *hitCount = hits;
    EXPECT_EQ(validationErrorCount(), 0u) << "WebGPU validation errors";
    return bad;
}

std::shared_ptr<Device> softwareRayTracingDevice() {
    auto device = Device::createDefaultDevice(nullptr);
    if (!device || device->getRayTracingCapabilities().tier != RayTracingTier::software) return nullptr;
    // Every check below also requires that nothing raised a WebGPU validation
    // error (builds, copies and traces report them asynchronously, without
    // failing the calls).
    resetValidationErrorCount();
    return device;
}

struct GpuMesh {
    std::shared_ptr<Buffer> vertices, indices;
};

static GpuMesh upload(const std::shared_ptr<Device>& device, const TriangleMesh& m) {
    GpuMesh g;
    g.vertices = device->createBuffer(m.vertices.size() * 4, BufferUsage::accelerationStructureInput,
                                      const_cast<float*>(m.vertices.data()));
    if (!m.indices.empty()) {
        g.indices = device->createBuffer(m.indices.size() * 4, BufferUsage::accelerationStructureInput,
                                         const_cast<uint32_t*>(m.indices.data()));
    }
    return g;
}

static AccelerationStructureGeometryDescriptor triangleGeometry(const GpuMesh& g, const TriangleMesh& m) {
    AccelerationStructureGeometryDescriptor geo{};
    geo.type = AccelerationStructureGeometryType::triangles;
    geo.vertexBuffer = g.vertices;
    geo.vertexStride = 12;
    geo.vertexCount = uint32_t(m.vertices.size() / 3);
    if (g.indices) {
        geo.indexBuffer = g.indices;
        geo.indexFormat = IndexFormat::uint32;
        geo.indexCount = uint32_t(m.indices.size());
    }
    return geo;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(RayQueries, BlasMatchesBruteForce) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    // Two geometries: indexed and non-indexed.
    const auto trisA = randomTriangles(400, 0.25, 1);
    const auto trisB = randomTriangles(150, 0.4, 2);
    const auto meshA = toMesh(trisA, true), meshB = toMesh(trisB, false);
    const auto gA = upload(device, meshA), gB = upload(device, meshB);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(gA, meshA), triangleGeometry(gB, meshB)};
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);

    std::vector<CpuGeometry> cpu = {cpuTriangles(trisA), cpuTriangles(trisB)};
    const auto rays = cameraRays({0.1, -0.05, -4}, 1.2, 1.2, 48, 48);
    uint32_t hits = 0;
    EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF, &hits), 0u);
    EXPECT_GT(hits, rays.size() / 4) << "the scene should cover a good part of the view";

    // Back-face culling and a limited tMax.
    EXPECT_EQ(compare(rig.trace(blas, rays, 0x10), rays, nullptr, &cpu, 0x10, 0xFF), 0u);
    const auto shortRays = cameraRays({0.1, -0.05, -4}, 1.2, 1.2, 32, 32, 0.5f, 0.9f);
    EXPECT_EQ(compare(rig.trace(blas, shortRays), shortRays, nullptr, &cpu, 0, 0xFF), 0u);
}

TEST(RayQueries, TlasMatchesBruteForce) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    const auto tris = randomTriangles(300, 0.3, 3);
    const auto mesh = toMesh(tris, true);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {triangleGeometry(g, mesh)};
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);

    std::vector<CpuGeometry> cpuBlas = {cpuTriangles(tris)};
    std::vector<CpuInstance> cpuTlas(3);
    const double c = std::cos(0.6), s = std::sin(0.6);
    const double xfs[3][3][4] = {
        {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}},
        {{0.5 * c, 0, 0.5 * s, 2.6}, {0, 0.5, 0, 0.4}, {-0.5 * s, 0, 0.5 * c, 1.0}},  // scaled, rotated, moved
        {{1, 0, 0, -2.5}, {0, 1.5, 0, -0.3}, {0, 0, 1, 2.0}},
    };
    TopLevelAccelerationStructureDescriptor tdesc{};
    for (uint32_t i = 0; i < 3; ++i) {
        AccelerationStructureInstance inst{};
        inst.blas = blas;
        inst.instanceId = 100 + i;
        inst.mask = uint8_t(1u << i);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) inst.transform[r][k] = float(xfs[i][r][k]);
        tdesc.instances.push_back(inst);
        cpuTlas[i].blas = &cpuBlas;
        cpuTlas[i].id = 100 + i;
        cpuTlas[i].mask = inst.mask;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) cpuTlas[i].xf[r][k] = double(inst.transform[r][k]);
    }
    auto tlas = device->createTopLevelAccelerationStructure(tdesc);
    ASSERT_NE(tlas, nullptr);

    // BLAS and TLAS built in one command buffer: the TLAS reads the BLAS root box.
    {
        auto scratchB = device->createBuffer(blas->getBuildScratchSize(), BufferUsage::storage);
        auto scratchT = device->createBuffer(tlas->getBuildScratchSize(), BufferUsage::storage);
        auto encoder = device->createCommandEncoder();
        encoder->buildAccelerationStructure(blas, bdesc, scratchB);
        encoder->buildAccelerationStructure(tlas, tdesc, scratchT);
        device->submit(encoder->finish());
    }

    const auto rays = cameraRays({0, 0, -6}, 4.5, 2.0, 64, 32);
    uint32_t hits = 0;
    EXPECT_EQ(compare(rig.trace(tlas, rays), rays, &cpuTlas, nullptr, 0, 0xFF, &hits), 0u);
    EXPECT_GT(hits, rays.size() / 5);
    // Instance masks: only the first and third instance.
    EXPECT_EQ(compare(rig.trace(tlas, rays, 0, 0x5), rays, &cpuTlas, nullptr, 0, 0x5), 0u);
}

TEST(RayQueries, LargeBlasMatchesBruteForce) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    // Many sort tiles (1024 keys each) and a deep tree.
    const auto tris = randomTriangles(20000, 0.05, 4);
    const auto mesh = toMesh(tris, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh)};
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);

    std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
    const auto rays = cameraRays({0, 0, -4}, 1.1, 1.1, 24, 24);
    EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u);
}

TEST(RayQueries, PreferFastTraceMatchesBruteForce) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    // Treelet restructuring rewrites the tree; the hits must not change, and a
    // refit of the restructured tree must follow moved vertices.
    auto tris = randomTriangles(5000, 0.08, 31);
    auto mesh = toMesh(tris, true);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh)};
    desc.buildFlags = static_cast<AccelerationStructureBuildFlag>(
        static_cast<int>(AccelerationStructureBuildFlag::preferFastTrace) |
        static_cast<int>(AccelerationStructureBuildFlag::allowUpdate));
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);
    std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
    const auto rays = cameraRays({0.05, 0.02, -4}, 1.1, 1.1, 32, 32);
    EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u) << "built";

    for (auto& v : tris) { v.y += 0.2 * std::sin(v.x * 4); }
    mesh = toMesh(tris, true);
    g.vertices->upload(0, mesh.vertices.size() * 4, mesh.vertices.data());
    {
        auto scratch = device->createBuffer(blas->getUpdateScratchSize(), BufferUsage::storage);
        auto encoder = device->createCommandEncoder();
        encoder->updateAccelerationStructure(blas, blas, scratch);
        device->submit(encoder->finish());
    }
    cpu = {cpuTriangles(tris)};
    EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u) << "refit";

    // A TLAS of many instances, restructured too.
    TopLevelAccelerationStructureDescriptor tdesc{};
    std::vector<CpuInstance> cpuTlas;
    std::mt19937 rng(32);
    std::uniform_real_distribution<double> pos(-3.0, 3.0);
    for (uint32_t i = 0; i < 40; ++i) {
        AccelerationStructureInstance inst{};
        inst.blas = blas;
        inst.instanceId = i;
        const double s = 0.3 + 0.01 * i;
        const double tx = pos(rng), ty = pos(rng) * 0.6, tz = std::fabs(pos(rng));
        const double m[3][4] = {{s, 0, 0, tx}, {0, s, 0, ty}, {0, 0, s, tz}};
        CpuInstance ci;
        ci.blas = &cpu;
        ci.id = i;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) { inst.transform[r][k] = float(m[r][k]); ci.xf[r][k] = double(inst.transform[r][k]); }
        tdesc.instances.push_back(inst);
        cpuTlas.push_back(ci);
    }
    tdesc.buildFlags = AccelerationStructureBuildFlag::preferFastTrace;
    auto tlas = device->createTopLevelAccelerationStructure(tdesc);
    ASSERT_NE(tlas, nullptr);
    build(device, tlas, tdesc);
    const auto trays = cameraRays({0, 0, -6}, 4.0, 2.5, 48, 32);
    EXPECT_EQ(compare(rig.trace(tlas, trays), trays, &cpuTlas, nullptr, 0, 0xFF), 0u) << "tlas";
}

TEST(RayQueries, CompactedStructuresTraceTheSame) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    // Triangles and boxes in one BLAS, compacted (quantized nodes).
    const auto tris = randomTriangles(3000, 0.1, 41);
    const auto mesh = toMesh(tris, true);
    const auto g = upload(device, mesh);
    const std::vector<float> boxes = {-1.2f, -1.2f, 1.0f, -0.9f, -0.8f, 1.4f, 0.8f, 0.7f, 1.1f, 1.1f, 1.2f, 1.5f};
    auto bb = device->createBuffer(boxes.size() * 4, BufferUsage::accelerationStructureInput,
                                   const_cast<float*>(boxes.data()));
    AccelerationStructureGeometryDescriptor boxGeo{};
    boxGeo.type = AccelerationStructureGeometryType::boundingBoxes;
    boxGeo.aabbBuffer = bb;
    boxGeo.aabbStride = 24;
    boxGeo.aabbCount = 2;
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh), boxGeo};
    desc.buildFlags = AccelerationStructureBuildFlag::allowCompaction;
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);

    // The same structure built updatable compacts to its full size: the ratio
    // is what quantization saves.
    auto updatableDesc = desc;
    updatableDesc.buildFlags = static_cast<AccelerationStructureBuildFlag>(
        static_cast<int>(AccelerationStructureBuildFlag::allowCompaction) |
        static_cast<int>(AccelerationStructureBuildFlag::allowUpdate));
    auto updatable = device->createBottomLevelAccelerationStructure(updatableDesc);
    build(device, updatable, updatableDesc);
    const uint64_t compactSize = blas->getCompactedSize(), fullSize = updatable->getCompactedSize();
    ASSERT_GT(compactSize, 0u);
    printf("  compacted %llu bytes, full %llu bytes (%.0f%%)\n", (unsigned long long)compactSize,
           (unsigned long long)fullSize, 100.0 * compactSize / fullSize);
    EXPECT_LT(compactSize, fullSize * 85 / 100);

    auto compact = device->createCompactedAccelerationStructure(blas);
    ASSERT_NE(compact, nullptr);
    {
        auto encoder = device->createCommandEncoder();
        encoder->copyAccelerationStructure(blas, compact);
        device->submit(encoder->finish());
    }
    blas.reset();  // the compacted copy stands alone

    CpuGeometry cpuBoxes;
    cpuBoxes.boxes = true;
    for (size_t i = 0; i < boxes.size(); i += 3) cpuBoxes.boxBounds.push_back({boxes[i], boxes[i + 1], boxes[i + 2]});
    std::vector<CpuGeometry> cpu = {cpuTriangles(tris), cpuBoxes};
    const auto rays = cameraRays({0.03, -0.02, -4}, 1.3, 1.3, 40, 40);
    EXPECT_EQ(compare(rig.trace(compact, rays), rays, nullptr, &cpu, 0, 0xFF), 0u) << "compacted BLAS";

    // A TLAS over the compacted BLAS, itself compacted.
    std::vector<CpuInstance> cpuTlas;
    TopLevelAccelerationStructureDescriptor tdesc{};
    for (uint32_t i = 0; i < 12; ++i) {
        AccelerationStructureInstance inst{};
        inst.blas = compact;
        inst.instanceId = i;
        inst.transform[0][0] = inst.transform[1][1] = inst.transform[2][2] = 0.5f;
        inst.transform[0][3] = -3.0f + 0.55f * float(i);
        inst.transform[1][3] = (i % 2) ? 0.6f : -0.6f;
        inst.transform[2][3] = 0.2f * float(i % 3);
        CpuInstance ci;
        ci.blas = &cpu;
        ci.id = i;
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 4; ++k) ci.xf[r][k] = inst.transform[r][k];
        tdesc.instances.push_back(inst);
        cpuTlas.push_back(ci);
    }
    tdesc.buildFlags = AccelerationStructureBuildFlag::allowCompaction;
    auto tlas = device->createTopLevelAccelerationStructure(tdesc);
    ASSERT_NE(tlas, nullptr);
    build(device, tlas, tdesc);
    auto compactTlas = device->createCompactedAccelerationStructure(tlas);
    ASSERT_NE(compactTlas, nullptr);
    {
        auto encoder = device->createCommandEncoder();
        encoder->copyAccelerationStructure(tlas, compactTlas);
        device->submit(encoder->finish());
    }
    const auto trays = cameraRays({0, 0, -6}, 3.5, 1.5, 56, 24);
    EXPECT_EQ(compare(rig.trace(compactTlas, trays), trays, &cpuTlas, nullptr, 0, 0xFF), 0u) << "compacted TLAS";
}

TEST(RayQueries, SingleTriangleUint16AndBoxes) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    // A one-primitive BLAS (the one-node tree).
    {
        const std::vector<Vec3> tri = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0.5}};
        const auto mesh = toMesh(tri, false);
        const auto g = upload(device, mesh);
        BottomLevelAccelerationStructureDescriptor desc{};
        desc.geometries = {triangleGeometry(g, mesh)};
        auto blas = device->createBottomLevelAccelerationStructure(desc);
        ASSERT_NE(blas, nullptr);
        build(device, blas, desc);
        std::vector<CpuGeometry> cpu = {cpuTriangles(tri)};
        const auto rays = cameraRays({0, 0, -3}, 1.5, 1.5, 16, 16);
        EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u);
    }
    // 16-bit indices (an odd count of indices per word boundary).
    {
        const std::vector<float> verts = {-1, -1, 0, 1, -1, 0.2f, 1, 1, 0.4f, -1, 1, 0.1f, 0, 0, -0.5f};
        const std::vector<uint16_t> idx = {0, 1, 4, 1, 2, 4, 2, 3, 4};
        auto vb = device->createBuffer(verts.size() * 4, BufferUsage::accelerationStructureInput,
                                       const_cast<float*>(verts.data()));
        auto ib = device->createBuffer(idx.size() * 2, BufferUsage::accelerationStructureInput,
                                       const_cast<uint16_t*>(idx.data()));
        AccelerationStructureGeometryDescriptor geo{};
        geo.vertexBuffer = vb;
        geo.vertexStride = 12;
        geo.vertexCount = 5;
        geo.indexBuffer = ib;
        geo.indexFormat = IndexFormat::uint16;
        geo.indexCount = uint32_t(idx.size());
        BottomLevelAccelerationStructureDescriptor desc{};
        desc.geometries = {geo};
        auto blas = device->createBottomLevelAccelerationStructure(desc);
        ASSERT_NE(blas, nullptr);
        build(device, blas, desc);
        std::vector<Vec3> tris;
        for (uint16_t i : idx) tris.push_back({verts[3 * i], verts[3 * i + 1], verts[3 * i + 2]});
        std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
        const auto rays = cameraRays({0, 0, -3}, 1.5, 1.5, 16, 16);
        EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u);
    }
    // Procedural boxes, hit at their entry distance.
    {
        const std::vector<float> boxes = {-1, -1, 0, -0.2f, -0.2f, 0.5f, 0.1f, 0.1f, 1, 0.9f, 0.8f, 1.2f,
                                          -0.5f, 0.33f, -0.4f, 0.0f, 0.9f, 0.2f};
        auto bb = device->createBuffer(boxes.size() * 4, BufferUsage::accelerationStructureInput,
                                       const_cast<float*>(boxes.data()));
        AccelerationStructureGeometryDescriptor geo{};
        geo.type = AccelerationStructureGeometryType::boundingBoxes;
        geo.aabbBuffer = bb;
        geo.aabbStride = 24;
        geo.aabbCount = 3;
        BottomLevelAccelerationStructureDescriptor desc{};
        desc.geometries = {geo};
        auto blas = device->createBottomLevelAccelerationStructure(desc);
        ASSERT_NE(blas, nullptr);
        build(device, blas, desc);
        CpuGeometry cg;
        cg.boxes = true;
        for (size_t i = 0; i < boxes.size(); i += 3) cg.boxBounds.push_back({boxes[i], boxes[i + 1], boxes[i + 2]});
        std::vector<CpuGeometry> cpu = {cg};
        const auto rays = cameraRays({0, 0, -3}, 1.5, 1.5, 16, 16);
        EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u);
    }
}

TEST(RayQueries, RefitAndCopyFollowTheGeometry) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    RayQueryRig rig(device);
    ASSERT_TRUE(rig.ready());

    auto tris = randomTriangles(500, 0.2, 5);
    auto mesh = toMesh(tris, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh)};
    desc.buildFlags = AccelerationStructureBuildFlag::allowUpdate;
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);

    // Move every vertex (same topology), upload, refit.
    for (auto& v : tris) { v.x = v.x * 0.8 + 0.1 * std::sin(v.y * 3); v.z += 0.3 * v.x; }
    mesh = toMesh(tris, false);
    g.vertices->upload(0, mesh.vertices.size() * 4, mesh.vertices.data());
    {
        auto scratch = device->createBuffer(blas->getUpdateScratchSize(), BufferUsage::storage);
        auto encoder = device->createCommandEncoder();
        encoder->updateAccelerationStructure(blas, blas, scratch);
        device->submit(encoder->finish());
    }
    std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
    const auto rays = cameraRays({0, 0, -4}, 1.2, 1.2, 32, 32);
    EXPECT_EQ(compare(rig.trace(blas, rays), rays, nullptr, &cpu, 0, 0xFF), 0u) << "after refit";

    auto copy = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(copy, nullptr);
    {
        auto encoder = device->createCommandEncoder();
        encoder->copyAccelerationStructure(blas, copy);
        device->submit(encoder->finish());
    }
    EXPECT_EQ(compare(rig.trace(copy, rays), rays, nullptr, &cpu, 0, 0xFF), 0u) << "copy";
}

TEST(RayQueries, FragmentShaderTraces) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    const auto tris = randomTriangles(60, 0.35, 6);
    const auto mesh = toMesh(tris, true);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh)};
    auto blas = device->createBottomLevelAccelerationStructure(desc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, desc);

    // Each pixel traces the ray cameraRays() makes for it and writes
    // (primitive + 1, kind) into red and green.
    const uint32_t w = 32, h = 32;
    const char* wgsl = R"(
@group(0) @binding(0) var scene: campello_acceleration_structure;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  var p = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
  let px = -1.5 + 3.0 * pos.x / 32.0;
  let py = -1.5 + 3.0 * pos.y / 32.0;
  var ray: CampelloRay;
  ray.origin = vec3f(0.0, 0.0, -3.0);
  ray.direction = vec3f(px, py, 3.0);
  ray.tMin = 0.0;
  ray.tMax = 1e30;
  let hit = campello_traceRay(scene, ray, CAMPELLO_RAY_FLAG_NONE, 0xFFu);
  if (hit.kind == CAMPELLO_HIT_NONE) { return vec4f(0.0, 0.0, 0.0, 1.0); }
  return vec4f(f32(hit.primitiveIndex + 1u) / 255.0, f32(hit.kind) / 255.0, 0.0, 1.0);
}
)";
    EntryObject entry{};
    entry.binding = 0;
    entry.visibility = ShaderStage::fragment;
    entry.type = EntryObjectType::accelerationStructure;
    BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entries = {entry};
    auto bgl = device->createBindGroupLayout(bglDesc);
    PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayouts = {bgl};
    auto module = device->createShaderModule(wgsl);
    RenderPipelineDescriptor rpDesc{};
    rpDesc.layout = device->createPipelineLayout(plDesc);
    rpDesc.vertex.module = module;
    rpDesc.vertex.entryPoint = "vs";
    rpDesc.fragment = FragmentDescriptor{};
    rpDesc.fragment->module = module;
    rpDesc.fragment->entryPoint = "fs";
    rpDesc.fragment->targets = {ColorState{PixelFormat::rgba8unorm, ColorWrite::all, std::nullopt}};
    rpDesc.topology = PrimitiveTopology::triangleList;
    rpDesc.cullMode = CullMode::none;
    rpDesc.frontFace = FrontFace::ccw;
    auto pipeline = device->createRenderPipeline(rpDesc);
    ASSERT_NE(pipeline, nullptr);

    BindGroupDescriptor bgDesc{};
    bgDesc.layout = bgl;
    bgDesc.entries = {{0, blas}};
    auto bg = device->createBindGroup(bgDesc, false);

    auto target = device->createTexture(TextureType::tt2d, PixelFormat::rgba8unorm, w, h, 1, 1, 1,
                                        TextureUsage::renderTarget);
    ColorAttachment ca{};
    ca.view = target->createView(PixelFormat::rgba8unorm);
    ca.loadOp = LoadOp::clear;
    ca.storeOp = StoreOp::store;
    BeginRenderPassDescriptor rp{};
    rp.colorAttachments = {ca};
    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginRenderPass(rp);
    pass->setPipeline(pipeline);
    pass->setBindGroup(0, bg, {}, 0, 0);
    pass->draw(3, 1, 0, 0);
    pass->end();
    device->submit(encoder->finish());

    std::vector<uint8_t> px(w * h * 4);
    ASSERT_TRUE(target->download(0, 0, px.data(), px.size()));
    std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
    const auto rays = cameraRays({0, 0, -3}, 1.5, 1.5, w, h);
    uint32_t bad = 0, hits = 0;
    for (uint32_t i = 0; i < w * h; ++i) {
        const CpuHit ref = traceCpu(nullptr, &cpu, rays[i], 0, 0xFF);
        const uint32_t prim = px[i * 4], kind = px[i * 4 + 1];
        hits += ref.hit;
        if (ref.hit ? (kind != 1 || prim != ref.prim + 1) : kind != 0) bad++;
    }
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(hits, 0u);
}

TEST(RayQueries, TlasMustShareItsBlasHeapPage) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    // One-triangle BLASes take one 256-byte record each: a 1 KiB page holds four.
    device->setAccelerationStructureHeapHint(1024);
    const std::vector<Vec3> tri = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
    const auto mesh = toMesh(tri, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor desc{};
    desc.geometries = {triangleGeometry(g, mesh)};
    std::vector<std::shared_ptr<AccelerationStructure>> blases;
    for (int i = 0; i < 5; ++i) {
        blases.push_back(device->createBottomLevelAccelerationStructure(desc));
        ASSERT_NE(blases.back(), nullptr);
    }
    auto tlasOf = [&](std::initializer_list<int> which) {
        TopLevelAccelerationStructureDescriptor t{};
        for (int i : which) {
            AccelerationStructureInstance inst{};
            inst.blas = blases[i];
            t.instances.push_back(inst);
        }
        return device->createTopLevelAccelerationStructure(t);
    };
    EXPECT_EQ(tlasOf({0, 4}), nullptr) << "BLASes 0 and 4 are in different pages";
    EXPECT_NE(tlasOf({4}), nullptr) << "the second page has room for the TLAS";
    EXPECT_EQ(tlasOf({0, 1}), nullptr) << "the first page is full";
    device->setAccelerationStructureHeapHint(0);
}


// ---------------------------------------------------------------------------
// Ray tracing pipelines (emulated: stages stitched into one compute shader)
// ---------------------------------------------------------------------------

namespace {

struct PipelineOut {
    uint32_t value, prim;
    float t;
    uint32_t pad;
};

/** Runs `pipeline` over a w x h launch with `as` at binding 0 and the output at binding 1. */
static std::vector<PipelineOut> runPipeline(const std::shared_ptr<Device>& device,
                                            const std::shared_ptr<RayTracingPipeline>& pipeline,
                                            const std::shared_ptr<BindGroupLayout>& bgl,
                                            const std::shared_ptr<AccelerationStructure>& as, uint32_t w, uint32_t h) {
    std::vector<PipelineOut> out(size_t(w) * h, PipelineOut{0xDEADu, 0, 0, 0});
    auto outBuf = device->createBuffer(out.size() * sizeof(PipelineOut), BufferUsage::storage, out.data());
    BindGroupDescriptor bgDesc{};
    bgDesc.layout = bgl;
    bgDesc.entries = {{0, as}, {1, BufferBinding{outBuf, 0, out.size() * sizeof(PipelineOut)}}};
    auto bg = device->createBindGroup(bgDesc, false);
    auto encoder = device->createCommandEncoder();
    auto pass = encoder->beginRayTracingPass();
    pass->setPipeline(pipeline);
    pass->setBindGroup(0, bg, {}, 0, 0);
    pass->traceRays(w, h, 1);
    pass->end();
    device->submit(encoder->finish());
    outBuf->download(0, out.size() * sizeof(PipelineOut), out.data());
    EXPECT_EQ(validationErrorCount(), 0u) << "WebGPU validation errors";
    return out;
}

static std::shared_ptr<BindGroupLayout> pipelineBindGroupLayout(const std::shared_ptr<Device>& device) {
    std::vector<EntryObject> entries(2);
    for (uint32_t i = 0; i < 2; ++i) {
        entries[i].binding = i;
        entries[i].visibility = ShaderStage::compute;
    }
    entries[0].type = EntryObjectType::accelerationStructure;
    entries[1].type = EntryObjectType::buffer;
    entries[1].data.buffer.type = EntryObjectBufferType::storage;
    BindGroupLayoutDescriptor desc{};
    desc.entries = entries;
    return device->createBindGroupLayout(desc);
}

static std::shared_ptr<PipelineLayout> layoutOf(const std::shared_ptr<Device>& device,
                                                const std::shared_ptr<BindGroupLayout>& bgl) {
    PipelineLayoutDescriptor desc{};
    desc.bindGroupLayouts = {bgl};
    return device->createPipelineLayout(desc);
}

// Shared by the pipeline modules below: bindings, payload, a camera ray per
// launch index (the rays cameraRays() makes) and the output write.
const char* kPipelineCommon = R"(
@group(0) @binding(0) var scene: campello_acceleration_structure;
struct Out { value: u32, prim: u32, t: f32, pad: u32 }
@group(0) @binding(1) var<storage, read_write> outs: array<Out>;
struct Payload { value: u32, prim: u32, t: f32 }
alias CampelloPayload = Payload;

fn cameraRay(origin: vec3f, halfW: f32, halfH: f32) -> CampelloRay {
  let id = campello_launchId();
  let size = campello_launchSize();
  let px = -halfW + 2.0 * halfW * (f32(id.x) + 0.5) / f32(size.x);
  let py = -halfH + 2.0 * halfH * (f32(id.y) + 0.5) / f32(size.y);
  var ray: CampelloRay;
  ray.origin = origin;
  ray.direction = vec3f(px - origin.x, py - origin.y, -origin.z);
  ray.tMin = 0.0;
  ray.tMax = 1e30;
  return ray;
}

fn store(p: Payload) {
  let id = campello_launchId();
  outs[id.y * campello_launchSize().x + id.x] = Out(p.value, p.prim, p.t, 0u);
}

fn record(p: ptr<function, CampelloPayload>, value: u32, hit: CampelloHit) {
  (*p).value = value;
  (*p).prim = hit.primitiveIndex;
  (*p).t = hit.t;
}
)";

} // namespace

TEST(RayTracingPipelines, HitGroupsAndMissShadersAreDispatched) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    ASSERT_TRUE(device->getRayTracingCapabilities().pipelines);

    // A BLAS of two geometries, instanced twice with hit group offsets 0 and 2:
    // hit group = hitGroupOffset + 1 * geometryIndex + 0.
    const auto trisA = randomTriangles(120, 0.3, 11), trisB = randomTriangles(80, 0.3, 12);
    const auto meshA = toMesh(trisA, true), meshB = toMesh(trisB, false);
    const auto gA = upload(device, meshA), gB = upload(device, meshB);
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {triangleGeometry(gA, meshA), triangleGeometry(gB, meshB)};
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, bdesc);

    std::vector<CpuGeometry> cpuBlas = {cpuTriangles(trisA), cpuTriangles(trisB)};
    std::vector<CpuInstance> cpuTlas(2);
    TopLevelAccelerationStructureDescriptor tdesc{};
    const uint32_t offsets[2] = {0, 2};
    for (uint32_t i = 0; i < 2; ++i) {
        AccelerationStructureInstance inst{};
        inst.blas = blas;
        inst.hitGroupOffset = offsets[i];
        inst.transform[0][3] = i == 0 ? -1.6f : 1.6f;
        tdesc.instances.push_back(inst);
        cpuTlas[i].blas = &cpuBlas;
        cpuTlas[i].xf[0][3] = inst.transform[0][3];
    }
    auto tlas = device->createTopLevelAccelerationStructure(tdesc);
    ASSERT_NE(tlas, nullptr);
    build(device, tlas, tdesc);

    const std::string wgsl = std::string(kPipelineCommon) + R"(
fn rayGen() {
  var p: Payload;
  p.value = 0xFFFFFFFFu;
  campello_traceRayPipeline(scene, cameraRay(vec3f(0.0, 0.0, -6.0), 4.5, 2.0), CAMPELLO_RAY_FLAG_NONE, 0xFFu,
                            0u, 1u, 1u, &p);
  store(p);
}
fn miss0(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = 100u; }
fn miss1(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = 200u; }
fn hit0(p: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) { record(p, 1000u, hit); }
fn hit1(p: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) { record(p, 1001u, hit); }
fn hit2(p: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) { record(p, 1002u, hit); }
fn hit3(p: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) { record(p, 1003u, hit); }
)";
    auto module = device->createShaderModule(wgsl.c_str());
    ASSERT_NE(module, nullptr);
    auto bgl = pipelineBindGroupLayout(device);
    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = {module, "rayGen"};
    desc.missShaders = {{module, "miss0"}, {module, "miss1"}};
    for (const char* name : {"hit0", "hit1", "hit2", "hit3"}) {
        RayTracingHitGroupDescriptor hg{};
        hg.closestHit = RayTracingShaderDescriptor{module, name};
        desc.hitGroups.push_back(hg);
    }
    desc.layout = layoutOf(device, bgl);
    auto pipeline = device->createRayTracingPipeline(desc);
    ASSERT_NE(pipeline, nullptr);

    // A launch that isn't a multiple of the 8x8 workgroup.
    const uint32_t w = 61, h = 27;
    const auto out = runPipeline(device, pipeline, bgl, tlas, w, h);
    const auto rays = cameraRays({0, 0, -6}, 4.5, 2.0, w, h);
    uint32_t bad = 0, hits = 0, misses = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const CpuHit ref = traceCpu(&cpuTlas, nullptr, rays[i], 0, 0xFF);
        if (!ref.hit) {
            misses++;
            bad += out[i].value != 200u;  // miss index 1
            continue;
        }
        hits++;
        const uint32_t expected = 1000u + offsets[ref.inst] + ref.geom;
        const bool ok = out[i].value == expected && out[i].prim == ref.prim &&
                        std::fabs(out[i].t - ref.t) <= 1e-4 * std::max(1.0, ref.t);
        if (!ok && bad < 5) {
            printf("  ray %zu: value %u prim %u t %g | expected %u prim %u t %g\n", i, out[i].value, out[i].prim,
                   out[i].t, expected, ref.prim, ref.t);
        }
        bad += !ok;
    }
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(hits, 0u);
    EXPECT_GT(misses, 0u);
}

TEST(RayTracingPipelines, AnyHitCanIgnoreCandidates) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    const auto tris = randomTriangles(300, 0.3, 13);
    const auto mesh = toMesh(tris, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {triangleGeometry(g, mesh)};
    bdesc.geometries[0].opaque = false;  // any-hit runs only for non-opaque geometry
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, bdesc);

    const std::string wgsl = std::string(kPipelineCommon) + R"(
fn rayGen() {
  var p: Payload;
  p.value = 0xFFFFFFFFu;
  campello_traceRayPipeline(scene, cameraRay(vec3f(0.0, 0.0, -4.0), 1.2, 1.2), CAMPELLO_RAY_FLAG_NONE, 0xFFu,
                            0u, 0u, 0u, &p);
  store(p);
}
fn miss(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = 100u; }
fn hit(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) { record(p, 1000u, h); }
// Odd primitives only, as if the even ones were alpha-tested away.
fn anyHit(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) -> u32 {
  if ((h.primitiveIndex & 1u) == 0u) { return CAMPELLO_IGNORE; }
  return CAMPELLO_ACCEPT;
}
)";
    auto module = device->createShaderModule(wgsl.c_str());
    auto bgl = pipelineBindGroupLayout(device);
    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = {module, "rayGen"};
    desc.missShaders = {{module, "miss"}};
    RayTracingHitGroupDescriptor hg{};
    hg.closestHit = RayTracingShaderDescriptor{module, "hit"};
    hg.anyHit = RayTracingShaderDescriptor{module, "anyHit"};
    desc.hitGroups = {hg};
    desc.layout = layoutOf(device, bgl);
    auto pipeline = device->createRayTracingPipeline(desc);
    ASSERT_NE(pipeline, nullptr);

    const uint32_t w = 32, h = 32;
    const auto out = runPipeline(device, pipeline, bgl, blas, w, h);
    // The CPU scene without the even triangles (degenerate, so never hit).
    CpuGeometry odd = cpuTriangles(tris);
    for (size_t p = 0; p < odd.triangles.size() / 3; p += 2) {
        odd.triangles[3 * p + 1] = odd.triangles[3 * p + 2] = odd.triangles[3 * p];
    }
    std::vector<CpuGeometry> cpu = {odd};
    const auto rays = cameraRays({0, 0, -4}, 1.2, 1.2, w, h);
    uint32_t bad = 0, hits = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const CpuHit ref = traceCpu(nullptr, &cpu, rays[i], 0, 0xFF);
        hits += ref.hit;
        bad += ref.hit ? (out[i].value != 1000u || out[i].prim != ref.prim) : out[i].value != 100u;
    }
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(hits, 0u);
}

TEST(RayTracingPipelines, IntersectionFunctionsTraceSpheres) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    const std::vector<float> boxes = {-1.0f, -1.0f, -0.5f, 0.0f, 0.0f, 0.5f,   0.1f, -0.6f, 0.0f, 0.9f, 0.2f, 0.8f,
                                      -0.7f, 0.2f, -0.2f, 0.1f, 1.0f, 0.6f};
    auto bb = device->createBuffer(boxes.size() * 4, BufferUsage::accelerationStructureInput,
                                   const_cast<float*>(boxes.data()));
    AccelerationStructureGeometryDescriptor geo{};
    geo.type = AccelerationStructureGeometryType::boundingBoxes;
    geo.aabbBuffer = bb;
    geo.aabbStride = 24;
    geo.aabbCount = 3;
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {geo};
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, bdesc);

    const std::string wgsl = std::string(kPipelineCommon) + R"(
fn rayGen() {
  var p: Payload;
  p.value = 0xFFFFFFFFu;
  campello_traceRayPipeline(scene, cameraRay(vec3f(0.0, 0.0, -3.0), 1.5, 1.5), CAMPELLO_RAY_FLAG_NONE, 0xFFu,
                            0u, 0u, 0u, &p);
  store(p);
}
fn miss(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = 100u; }
fn hit(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) { record(p, 1000u + h.primitiveIndex, h); }
// The sphere inscribed in the box.
fn sphere(ray: CampelloRay, prim: CampelloProcedural) -> CampelloIntersection {
  let e = prim.boxMax - prim.boxMin;
  let c = 0.5 * (prim.boxMin + prim.boxMax);
  let r = 0.5 * min(min(e.x, e.y), e.z);
  let oc = ray.origin - c;
  let a = dot(ray.direction, ray.direction);
  let b = dot(oc, ray.direction);
  let disc = b * b - a * (dot(oc, oc) - r * r);
  var res: CampelloIntersection;
  if (disc < 0.0) { return res; }
  res.t = (-b - sqrt(disc)) / a;
  res.hit = res.t >= ray.tMin && res.t <= ray.tMax;
  res.attributes = vec4f(normalize(oc + res.t * ray.direction), 0.0);
  return res;
}
)";
    auto module = device->createShaderModule(wgsl.c_str());
    auto bgl = pipelineBindGroupLayout(device);
    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = {module, "rayGen"};
    desc.missShaders = {{module, "miss"}};
    RayTracingHitGroupDescriptor hg{};
    hg.closestHit = RayTracingShaderDescriptor{module, "hit"};
    hg.intersection = RayTracingShaderDescriptor{module, "sphere"};
    desc.hitGroups = {hg};
    desc.layout = layoutOf(device, bgl);
    auto pipeline = device->createRayTracingPipeline(desc);
    ASSERT_NE(pipeline, nullptr);

    const uint32_t w = 32, h = 32;
    const auto out = runPipeline(device, pipeline, bgl, blas, w, h);
    const auto rays = cameraRays({0, 0, -3}, 1.5, 1.5, w, h);
    uint32_t bad = 0, hits = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const Vec3 o{rays[i].o[0], rays[i].o[1], rays[i].o[2]}, d{rays[i].d[0], rays[i].d[1], rays[i].d[2]};
        double bestT = 1e30;
        int best = -1;
        for (int b = 0; b < 3; ++b) {
            const float* bx = &boxes[b * 6];
            const Vec3 c{0.5 * (bx[0] + bx[3]), 0.5 * (bx[1] + bx[4]), 0.5 * (bx[2] + bx[5])};
            const double r = 0.5 * std::min({bx[3] - bx[0], bx[4] - bx[1], bx[5] - bx[2]});
            const Vec3 oc = sub(o, c);
            const double a = dot(d, d), hb = dot(oc, d), disc = hb * hb - a * (dot(oc, oc) - r * r);
            if (disc < 0) continue;
            const double t = (-hb - std::sqrt(disc)) / a;
            if (t >= 0 && t < bestT) { bestT = t; best = b; }
        }
        hits += best >= 0;
        const bool ok = best < 0 ? out[i].value == 100u
                                 : out[i].value == 1000u + uint32_t(best) && std::fabs(out[i].t - bestT) < 1e-3;
        bad += !ok;
    }
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(hits, 0u);
}

TEST(RayTracingPipelines, RecursionLimitsAreEnforced) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";
    EXPECT_EQ(device->getRayTracingCapabilities().maxRecursionDepth, 4u);

    const std::string wgsl = std::string(kPipelineCommon) + R"(
fn rayGen() {
  var p: Payload;
  campello_traceRayPipeline(scene, cameraRay(vec3f(0.0, 0.0, -3.0), 1.0, 1.0), 0u, 0xFFu, 0u, 0u, 0u, &p);
  store(p);
}
fn miss(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = 100u; }
fn bounce(p: ptr<function, CampelloPayload>, ray: CampelloRay) {
  campello_traceRayPipeline(scene, ray, 0u, 0xFFu, 0u, 0u, 0u, p);
}
fn reflect(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) { bounce(p, ray); }
fn tracingAnyHit(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) -> u32 {
  bounce(p, ray);
  return CAMPELLO_ACCEPT;
}
)";
    auto module = device->createShaderModule(wgsl.c_str());
    auto bgl = pipelineBindGroupLayout(device);
    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = {module, "rayGen"};
    desc.missShaders = {{module, "miss"}};
    desc.layout = layoutOf(device, bgl);
    RayTracingHitGroupDescriptor recursive{};
    recursive.closestHit = RayTracingShaderDescriptor{module, "reflect"};  // traces through a helper
    desc.hitGroups = {recursive};

    desc.maxRecursionDepth = 1;
    EXPECT_EQ(device->createRayTracingPipeline(desc), nullptr) << "a tracing closest-hit needs depth > 1";
    desc.maxRecursionDepth = 4;
    EXPECT_NE(device->createRayTracingPipeline(desc), nullptr);
    desc.maxRecursionDepth = 5;
    EXPECT_EQ(device->createRayTracingPipeline(desc), nullptr) << "beyond the device's maxRecursionDepth";

    desc.maxRecursionDepth = 2;
    RayTracingHitGroupDescriptor badAnyHit{};
    badAnyHit.anyHit = RayTracingShaderDescriptor{module, "tracingAnyHit"};
    desc.hitGroups = {badAnyHit};
    EXPECT_EQ(device->createRayTracingPipeline(desc), nullptr) << "any-hit may not trace";
}

TEST(RayTracingPipelines, RecursionUnrollsToTheRequestedDepth) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    // Six parallel quads at z = 0..5: a ray along +z inside them passes all six.
    std::vector<Vec3> tris;
    for (int z = 0; z < 6; ++z) {
        const Vec3 a{-1, -1, double(z)}, b{1, -1, double(z)}, c{1, 1, double(z)}, d{-1, 1, double(z)};
        tris.insert(tris.end(), {a, b, c, a, c, d});
    }
    const auto mesh = toMesh(tris, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {triangleGeometry(g, mesh)};
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, bdesc);

    // Each closest-hit counts one surface and continues the ray through it, by
    // way of a helper that ray generation calls too.
    const std::string wgsl = std::string(kPipelineCommon) + R"(
fn continueRay(p: ptr<function, CampelloPayload>, origin: vec3f, dir: vec3f) {
  var ray: CampelloRay;
  ray.origin = origin;
  ray.direction = dir;
  ray.tMin = 1e-3;
  ray.tMax = 1e30;
  campello_traceRayPipeline(scene, ray, CAMPELLO_RAY_FLAG_NONE, 0xFFu, 0u, 0u, 0u, p);
}
fn rayGen() {
  let id = campello_launchId();
  let size = campello_launchSize();
  let xy = -1.5 + 3.0 * (vec2f(id.xy) + 0.5) / vec2f(size.xy);
  var p: Payload;
  p.value = 0u;
  continueRay(&p, vec3f(xy, -3.0), vec3f(0.0, 0.0, 1.0));
  store(p);
}
fn miss(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value += 1000u; }
fn passThrough(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) {
  (*p).value += 1u;
  continueRay(p, ray.origin + h.t * ray.direction, ray.direction);
}
)";
    auto module = device->createShaderModule(wgsl.c_str());
    auto bgl = pipelineBindGroupLayout(device);
    const uint32_t w = 16, h = 16;
    for (uint32_t depth : {2u, 3u, 4u}) {
        RayTracingPipelineDescriptor desc{};
        desc.rayGeneration = {module, "rayGen"};
        desc.missShaders = {{module, "miss"}};
        RayTracingHitGroupDescriptor hg{};
        hg.closestHit = RayTracingShaderDescriptor{module, "passThrough"};
        desc.hitGroups = {hg};
        desc.layout = layoutOf(device, bgl);
        desc.maxRecursionDepth = depth;
        auto pipeline = device->createRayTracingPipeline(desc);
        ASSERT_NE(pipeline, nullptr) << "depth " << depth;
        const auto out = runPipeline(device, pipeline, bgl, blas, w, h);
        uint32_t bad = 0;
        for (uint32_t y = 0; y < h; ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                const double px = -1.5 + 3.0 * (x + 0.5) / w, py = -1.5 + 3.0 * (y + 0.5) / h;
                const bool inside = std::fabs(px) < 1 && std::fabs(py) < 1;
                // Inside: `depth` closest-hits, then the trace past the limit
                // returns without effect. Outside: a miss.
                const uint32_t expected = inside ? depth : 1000u;
                if (out[y * w + x].value != expected) {
                    if (bad < 3) printf("  depth %u (%u,%u): %u, expected %u\n", depth, x, y, out[y * w + x].value, expected);
                    bad++;
                }
            }
        }
        EXPECT_EQ(bad, 0u) << "depth " << depth;
    }
}

TEST(RayTracingPipelines, StagesFromSeveralModules) {
    auto device = softwareRayTracingDevice();
    if (!device) GTEST_SKIP() << "No software ray tracing device";

    const auto tris = randomTriangles(100, 0.3, 21);
    const auto mesh = toMesh(tris, false);
    const auto g = upload(device, mesh);
    BottomLevelAccelerationStructureDescriptor bdesc{};
    bdesc.geometries = {triangleGeometry(g, mesh)};
    auto blas = device->createBottomLevelAccelerationStructure(bdesc);
    ASSERT_NE(blas, nullptr);
    build(device, blas, bdesc);

    // Both modules share the bindings and payload (identical declarations),
    // and each has its own, different `tag()`.
    const std::string rayGenModule = std::string(kPipelineCommon) + R"(
fn tag() -> u32 { return 100u; }
fn rayGen() {
  var p: Payload;
  campello_traceRayPipeline(scene, cameraRay(vec3f(0.0, 0.0, -4.0), 1.2, 1.2), 0u, 0xFFu, 0u, 0u, 0u, &p);
  store(p);
}
fn miss(p: ptr<function, CampelloPayload>, ray: CampelloRay) { (*p).value = tag(); }
)";
    const std::string hitModule = std::string(kPipelineCommon) + R"(
fn tag() -> u32 { return 7000u; }
// A comment mentioning tag() and a member named like it: struct S { tag: u32 }
struct Tagged { tag: u32 }
fn hit(p: ptr<function, CampelloPayload>, ray: CampelloRay, h: CampelloHit) {
  var t: Tagged;
  t.tag = tag() + h.primitiveIndex;
  record(p, t.tag, h);
}
)";
    auto modA = device->createShaderModule(rayGenModule.c_str());
    auto modB = device->createShaderModule(hitModule.c_str());
    auto bgl = pipelineBindGroupLayout(device);
    RayTracingPipelineDescriptor desc{};
    desc.rayGeneration = {modA, "rayGen"};
    desc.missShaders = {{modA, "miss"}};
    RayTracingHitGroupDescriptor hg{};
    hg.closestHit = RayTracingShaderDescriptor{modB, "hit"};
    desc.hitGroups = {hg};
    desc.layout = layoutOf(device, bgl);
    auto pipeline = device->createRayTracingPipeline(desc);
    ASSERT_NE(pipeline, nullptr);

    const uint32_t w = 24, h = 24;
    const auto out = runPipeline(device, pipeline, bgl, blas, w, h);
    std::vector<CpuGeometry> cpu = {cpuTriangles(tris)};
    const auto rays = cameraRays({0, 0, -4}, 1.2, 1.2, w, h);
    uint32_t bad = 0, hits = 0;
    for (size_t i = 0; i < rays.size(); ++i) {
        const CpuHit ref = traceCpu(nullptr, &cpu, rays[i], 0, 0xFF);
        hits += ref.hit;
        bad += ref.hit ? out[i].value != 7000u + ref.prim : out[i].value != 100u;
    }
    EXPECT_EQ(bad, 0u);
    EXPECT_GT(hits, 0u);
}

#else

TEST(RayQueries, WebGPUOnly) {
    GTEST_SKIP() << "Inline ray query tests use WGSL and run on the WebGPU backend";
}

#endif
