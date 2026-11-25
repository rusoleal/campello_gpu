// WGSL side of software ray tracing: the traversal library, the rewriting that
// Device::createShaderModule(const char*) applies to shaders using inline ray
// queries, and the stitching that Device::createRayTracingPipeline() applies
// to ray tracing pipeline modules.
//
// Inline ray queries (any compute or fragment shader):
//
//   @group(0) @binding(0) var scene: campello_acceleration_structure;
//   ...
//   let hit = campello_traceRay(scene, ray, CAMPELLO_RAY_FLAG_NONE, 0xFFu);
//   let occluded = campello_traceRayAny(scene, ray, CAMPELLO_RAY_FLAG_NONE, 0xFFu);
//
// Each acceleration-structure declaration becomes a read-only storage buffer
// over the acceleration-structure heap page; the group's roots uniform at
// binding kAsRootsBinding (indexed by binding number) holds the word offset of
// the bound TLAS or BLAS record. Calls are rewritten to functions specialised
// for that variable (WGSL can't pass storage pointers to functions), and the
// library is appended, which keeps the user's line numbers and any leading
// `enable` directives in place.
//
// Ray tracing pipelines (one module holding every stage, as plain functions):
//
//   alias CampelloPayload = MyPayload;
//   fn rayGen() { ...; campello_traceRayPipeline(scene, ray, flags, mask,
//                                                 sbtOffset, sbtStride, missIndex, &payload); }
//   fn miss(payload: ptr<function, CampelloPayload>, ray: CampelloRay) { ... }
//   fn closestHit(payload: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) { ... }
//   fn anyHit(payload: ptr<function, CampelloPayload>, ray: CampelloRay, hit: CampelloHit) -> u32 { ... }
//   fn intersect(ray: CampelloRay, prim: CampelloProcedural) -> CampelloIntersection { ... }
//
// buildRayTracingPipelineWGSL() appends a traversal that calls the stages
// through generated `switch` dispatchers (WGSL has no function pointers) and
// a compute entry point, campello_rt_main, that runs the ray generation
// function once per launch index.

#include "rt_internal.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <set>

namespace systems::leal::campello_gpu {

static const char kPrelude[] = R"WGSL(
// ---- campello_gpu software ray tracing ----

struct CampelloRay {
  origin: vec3f,
  tMin: f32,
  direction: vec3f,
  tMax: f32,
}

struct CampelloHit {
  kind: u32,              // CAMPELLO_HIT_NONE | _TRIANGLE | _PROCEDURAL
  t: f32,
  barycentrics: vec2f,    // weights of the second and third vertex
  frontFace: bool,
  instanceIndex: u32,
  instanceId: u32,
  hitGroupOffset: u32,
  hitGroupIndex: u32,     // hitGroupOffset + sbtStride * geometryIndex + sbtOffset
  geometryIndex: u32,
  primitiveIndex: u32,
  attributes: vec4f,      // procedural: from the intersection function; triangles: (b1, b2, 0, 0)
  objectToWorld: mat4x3f,
  worldToObject: mat4x3f,
}

// What an intersection function sees: one procedural box, in object space.
struct CampelloProcedural {
  boxMin: vec3f,
  primitiveIndex: u32,
  boxMax: vec3f,
  geometryIndex: u32,
  instanceIndex: u32,
  instanceId: u32,
}

// What an intersection function returns.
struct CampelloIntersection {
  hit: bool,
  t: f32,
  attributes: vec4f,
}

const CAMPELLO_HIT_NONE: u32 = 0u;
const CAMPELLO_HIT_TRIANGLE: u32 = 1u;
const CAMPELLO_HIT_PROCEDURAL: u32 = 2u;

// Any-hit results.
const CAMPELLO_ACCEPT: u32 = 0u;
const CAMPELLO_IGNORE: u32 = 1u;
const CAMPELLO_ACCEPT_AND_END: u32 = 2u;

const CAMPELLO_RAY_FLAG_NONE: u32 = 0x0u;
const CAMPELLO_RAY_FLAG_OPAQUE: u32 = 0x1u;
const CAMPELLO_RAY_FLAG_NO_OPAQUE: u32 = 0x2u;
const CAMPELLO_RAY_FLAG_TERMINATE_ON_FIRST_HIT: u32 = 0x4u;
const CAMPELLO_RAY_FLAG_SKIP_CLOSEST_HIT: u32 = 0x8u;
const CAMPELLO_RAY_FLAG_CULL_BACK_FACING_TRIANGLES: u32 = 0x10u;
const CAMPELLO_RAY_FLAG_CULL_FRONT_FACING_TRIANGLES: u32 = 0x20u;
const CAMPELLO_RAY_FLAG_CULL_OPAQUE: u32 = 0x40u;
const CAMPELLO_RAY_FLAG_CULL_NO_OPAQUE: u32 = 0x80u;
const CAMPELLO_RAY_FLAG_SKIP_TRIANGLES: u32 = 0x100u;
const CAMPELLO_RAY_FLAG_SKIP_AABBS: u32 = 0x200u;

const CAMPELLO_INTERNAL_LEAF: u32 = 0x80000000u;
const CAMPELLO_INTERNAL_INVALID: u32 = 0xFFFFFFFFu;
const CAMPELLO_INTERNAL_MARK: u32 = 0xFFFFFFFEu;
const CAMPELLO_INTERNAL_INF: f32 = 3.0e38;

// Entry distance of the ray into a box, or CAMPELLO_INTERNAL_INF on a miss.
fn campello_internal_slab(mn: vec3f, mx: vec3f, o: vec3f, invD: vec3f, tMin: f32, tMax: f32) -> f32 {
  let t0 = (mn - o) * invD;
  let t1 = (mx - o) * invD;
  let lo = min(t0, t1);
  let hi = max(t0, t1);
  let tn = max(max(lo.x, lo.y), max(lo.z, tMin));
  let tf = min(min(hi.x, hi.y), min(hi.z, tMax));
  if (tn <= tf) { return tn; }
  return CAMPELLO_INTERNAL_INF;
}

fn campello_internal_safeInverse(d: vec3f) -> vec3f {
  let tiny = vec3f(1e-20);
  let s = select(vec3f(1.0), vec3f(-1.0), d < vec3f(0.0));
  return 1.0 / select(d, s * tiny, abs(d) < tiny);
}

// Watertight ray/triangle test (Woop, Benthin, Wald 2013). Returns
// (t, b1, b2, 1) on a hit, w = 0 on a miss.
fn campello_internal_triangle(o: vec3f, d: vec3f, a: vec3f, b: vec3f, c: vec3f) -> vec4f {
  let ad = abs(d);
  var kz = 2u;
  if (ad.x > ad.y && ad.x > ad.z) { kz = 0u; } else if (ad.y > ad.z) { kz = 1u; }
  var kx = (kz + 1u) % 3u;
  var ky = (kx + 1u) % 3u;
  if (d[kz] < 0.0) { let tmp = kx; kx = ky; ky = tmp; }
  let sx = d[kx] / d[kz];
  let sy = d[ky] / d[kz];
  let sz = 1.0 / d[kz];
  let A = a - o;
  let B = b - o;
  let C = c - o;
  let ax = A[kx] - sx * A[kz];
  let ay = A[ky] - sy * A[kz];
  let bx = B[kx] - sx * B[kz];
  let by = B[ky] - sy * B[kz];
  let cx = C[kx] - sx * C[kz];
  let cy = C[ky] - sy * C[kz];
  let u = cx * by - cy * bx;
  let v = ax * cy - ay * cx;
  let w = bx * ay - by * ax;
  // The paper recomputes edge functions that are exactly 0 in double
  // precision; WGSL has no f64, so a ray through a shared edge could see both
  // neighbours' values rounded the wrong way and fall through the crack.
  // A tiny relative tolerance closes it: such a ray may hit both triangles,
  // and the nearer one wins as usual.
  let eps = 1e-6 * (abs(u) + abs(v) + abs(w));
  if ((u < -eps || v < -eps || w < -eps) && (u > eps || v > eps || w > eps)) { return vec4f(0.0); }
  let det = u + v + w;
  if (det == 0.0) { return vec4f(0.0); }
  let t = (u * sz * A[kz] + v * sz * B[kz] + w * sz * C[kz]) / det;
  return vec4f(t, v / det, w / det, 1.0);
}

fn campello_internal_rows(r0: vec4u, r1: vec4u, r2: vec4u) -> mat4x3f {
  let a = bitcast<vec4f>(r0);
  let b = bitcast<vec4f>(r1);
  let c = bitcast<vec4f>(r2);
  return mat4x3f(vec3f(a.x, b.x, c.x), vec3f(a.y, b.y, c.y), vec3f(a.z, b.z, c.z), vec3f(a.w, b.w, c.w));
}

// The four bytes of a word, low first (unpack4xU8 is a language extension not every browser has).
fn campello_internal_bytes(w: u32) -> vec4u {
  return (vec4u(w) >> vec4u(0u, 8u, 16u, 24u)) & vec4u(255u);
}

fn campello_internal_identity() -> mat4x3f {
  return mat4x3f(vec3f(1.0, 0.0, 0.0), vec3f(0.0, 1.0, 0.0), vec3f(0.0, 0.0, 1.0), vec3f(0.0));
}
)WGSL";

// The traversal, instantiated per acceleration-structure variable (CAMPELLO_AS,
// whose record word offset is CAMPELLO_ROOT) and per flavour: inline queries,
// or a pipeline trace that calls any-hit and intersection functions.
static const char kTraversal[] = R"WGSL(
fn CAMPELLO_FN(rayIn: CampelloRay, flags: u32, mask: u32CAMPELLO_PARAMS) -> CampelloHit {
  var hit: CampelloHit;
  hit.kind = CAMPELLO_HIT_NONE;
  hit.t = rayIn.tMax;
  hit.instanceIndex = CAMPELLO_INTERNAL_INVALID;
  hit.objectToWorld = campello_internal_identity();
  hit.worldToObject = campello_internal_identity();

  let root = (CAMPELLO_ROOT) >> 2u;
  let h0 = CAMPELLO_AS[root];
  let h1 = CAMPELLO_AS[root + 1u];
  if (h0.w == 0u) { return hit; }
  let topIsTlas = h0.x == 2u;
  // Header version 2: a compacted record with quantized nodes (rt_internal.hpp).
  let topQuantized = h0.y == 2u;
  var quantized = topQuantized;

  var nodeBase = root + (h1.x >> 2u);
  var primBase = root + (h1.y >> 2u);
  let tlasNodeBase = nodeBase;
  let tlasPrimBase = primBase;
  var inBlas = !topIsTlas;
  var instance = CAMPELLO_INTERNAL_INVALID;
  var instanceOpaque = false;
  var instanceId = 0u;
  var hitGroupOffset = 0u;

  var o = rayIn.origin;
  var d = rayIn.direction;
  var invD = campello_internal_safeInverse(d);

  var stack: array<u32, 64>;
  var sp = 1u;
  stack[0] = 0u;
  var done = false;

  while (sp > 0u && !done) {
    sp -= 1u;
    let item = stack[sp];

    if (item == CAMPELLO_INTERNAL_MARK) {
      // Leaving a BLAS: back to the world-space ray and the TLAS.
      o = rayIn.origin;
      d = rayIn.direction;
      invD = campello_internal_safeInverse(d);
      nodeBase = tlasNodeBase;
      primBase = tlasPrimBase;
      inBlas = false;
      quantized = topQuantized;
      continue;
    }

    if ((item & CAMPELLO_INTERNAL_LEAF) == 0u) {
      var lc: u32;
      var rc: u32;
      var lmn: vec3f;
      var lmx: vec3f;
      var rmn: vec3f;
      var rmx: vec3f;
      if (quantized) {
        let nb = nodeBase + item * 3u;
        let a = CAMPELLO_AS[nb];
        let b = CAMPELLO_AS[nb + 1u];
        let c = CAMPELLO_AS[nb + 2u];
        let origin = bitcast<vec3f>(a.xyz);
        let scale = exp2(vec3f(vec3i(vec3u(a.w, a.w >> 8u, a.w >> 16u) & vec3u(255u)) - vec3i(128)));
        let q0 = campello_internal_bytes(b.z);
        let q1 = campello_internal_bytes(b.w);
        let q2 = campello_internal_bytes(c.x);
        lc = b.x;
        rc = b.y;
        lmn = origin + vec3f(vec3u(q0.x, q0.y, q0.z)) * scale;
        lmx = origin + vec3f(vec3u(q0.w, q1.x, q1.y)) * scale;
        rmn = origin + vec3f(vec3u(q1.z, q1.w, q2.x)) * scale;
        rmx = origin + vec3f(vec3u(q2.y, q2.z, q2.w)) * scale;
      } else {
        let nb = nodeBase + item * 4u;
        let n0 = CAMPELLO_AS[nb];
        let n1 = CAMPELLO_AS[nb + 1u];
        let n2 = CAMPELLO_AS[nb + 2u];
        let n3 = CAMPELLO_AS[nb + 3u];
        lc = n0.w;
        rc = n1.w;
        lmn = bitcast<vec3f>(n0.xyz);
        lmx = bitcast<vec3f>(n1.xyz);
        rmn = bitcast<vec3f>(n2.xyz);
        rmx = bitcast<vec3f>(n3.xyz);
      }
      var tl = CAMPELLO_INTERNAL_INF;
      var tr = CAMPELLO_INTERNAL_INF;
      if (lc != CAMPELLO_INTERNAL_INVALID) {
        tl = campello_internal_slab(lmn, lmx, o, invD, rayIn.tMin, hit.t);
      }
      if (rc != CAMPELLO_INTERNAL_INVALID) {
        tr = campello_internal_slab(rmn, rmx, o, invD, rayIn.tMin, hit.t);
      }
      let hitL = tl < CAMPELLO_INTERNAL_INF;
      let hitR = tr < CAMPELLO_INTERNAL_INF;
      if (hitL && hitR) {
        // Far child first, so the near one is popped next.
        if (tl <= tr) {
          stack[sp] = rc; stack[sp + 1u] = lc;
        } else {
          stack[sp] = lc; stack[sp + 1u] = rc;
        }
        sp = min(sp + 2u, 63u);
      } else if (hitL) {
        stack[sp] = lc; sp = min(sp + 1u, 63u);
      } else if (hitR) {
        stack[sp] = rc; sp = min(sp + 1u, 63u);
      }
      continue;
    }

    let leaf = item & ~CAMPELLO_INTERNAL_LEAF;

    if (!inBlas) {
      // TLAS leaf: enter the instance's BLAS with the ray in object space.
      let ib = primBase + leaf * 8u;
      let i6 = CAMPELLO_AS[ib + 6u];
      if (((i6.x >> 24u) & mask) == 0u || i6.z == CAMPELLO_INTERNAL_INVALID) { continue; }
      let blas = i6.z >> 2u;
      let bh0 = CAMPELLO_AS[blas];
      let bh1 = CAMPELLO_AS[blas + 1u];
      if (bh0.w == 0u) { continue; }
      let w2o = campello_internal_rows(CAMPELLO_AS[ib + 3u], CAMPELLO_AS[ib + 4u], CAMPELLO_AS[ib + 5u]);
      o = w2o * vec4f(rayIn.origin, 1.0);
      d = w2o * vec4f(rayIn.direction, 0.0);
      invD = campello_internal_safeInverse(d);
      nodeBase = blas + (bh1.x >> 2u);
      primBase = blas + (bh1.y >> 2u);
      inBlas = true;
      quantized = bh0.y == 2u;
      instance = leaf;
      instanceOpaque = ((i6.y >> 24u) & 1u) != 0u;
      instanceId = i6.x & 0xFFFFFFu;
      hitGroupOffset = i6.y & 0xFFFFFFu;
      stack[sp] = CAMPELLO_INTERNAL_MARK;
      stack[sp + 1u] = 0u;
      sp = min(sp + 2u, 63u);
      continue;
    }

    // BLAS leaf: a triangle or a procedural box.
    let pb = primBase + leaf * 3u;
    let p0 = CAMPELLO_AS[pb];
    let p1 = CAMPELLO_AS[pb + 1u];
    let p2 = CAMPELLO_AS[pb + 2u];
    let pflags = p2.w;
    var opaque = (pflags & 1u) != 0u || instanceOpaque;
    if ((flags & CAMPELLO_RAY_FLAG_OPAQUE) != 0u) { opaque = true; }
    if ((flags & CAMPELLO_RAY_FLAG_NO_OPAQUE) != 0u) { opaque = false; }
    if (opaque && (flags & CAMPELLO_RAY_FLAG_CULL_OPAQUE) != 0u) { continue; }
    if (!opaque && (flags & CAMPELLO_RAY_FLAG_CULL_NO_OPAQUE) != 0u) { continue; }

    var cand = hit;
    cand.primitiveIndex = p0.w;
    cand.geometryIndex = p1.w;
    cand.instanceIndex = instance;
    cand.instanceId = instanceId;
    cand.hitGroupOffset = hitGroupOffset;
    cand.hitGroupIndex = hitGroupOffset + CAMPELLO_SBT_STRIDE * p1.w + CAMPELLO_SBT_OFFSET;
    cand.attributes = vec4f(0.0);
    if ((pflags & 2u) != 0u) {
      if ((flags & CAMPELLO_RAY_FLAG_SKIP_AABBS) != 0u) { continue; }
CAMPELLO_PROCEDURAL
      cand.kind = CAMPELLO_HIT_PROCEDURAL;
      cand.barycentrics = vec2f(0.0);
      cand.frontFace = true;
    } else {
      if ((flags & CAMPELLO_RAY_FLAG_SKIP_TRIANGLES) != 0u) { continue; }
      let a = bitcast<vec3f>(p0.xyz);
      let b = bitcast<vec3f>(p1.xyz);
      let c = bitcast<vec3f>(p2.xyz);
      let r = campello_internal_triangle(o, d, a, b, c);
      if (r.w == 0.0 || r.x < rayIn.tMin || r.x >= hit.t) { continue; }
      // Front facing = clockwise as seen from the ray origin (DXR / Metal default).
      let front = dot(cross(b - a, c - a), d) > 0.0;
      if (front && (flags & CAMPELLO_RAY_FLAG_CULL_FRONT_FACING_TRIANGLES) != 0u) { continue; }
      if (!front && (flags & CAMPELLO_RAY_FLAG_CULL_BACK_FACING_TRIANGLES) != 0u) { continue; }
      cand.kind = CAMPELLO_HIT_TRIANGLE;
      cand.t = r.x;
      cand.barycentrics = r.yz;
      cand.attributes = vec4f(r.yz, 0.0, 0.0);
      cand.frontFace = front;
    }
    if (cand.t >= hit.t) { continue; }
CAMPELLO_ANYHIT
    hit = cand;
    if ((flags & CAMPELLO_RAY_FLAG_TERMINATE_ON_FIRST_HIT) != 0u) { done = true; }
  }

  if (hit.kind != CAMPELLO_HIT_NONE && hit.instanceIndex != CAMPELLO_INTERNAL_INVALID) {
    let ib = tlasPrimBase + hit.instanceIndex * 8u;
    hit.objectToWorld = campello_internal_rows(CAMPELLO_AS[ib], CAMPELLO_AS[ib + 1u], CAMPELLO_AS[ib + 2u]);
    hit.worldToObject = campello_internal_rows(CAMPELLO_AS[ib + 3u], CAMPELLO_AS[ib + 4u], CAMPELLO_AS[ib + 5u]);
  }
  return hit;
}
)WGSL";

// Inline queries: procedural boxes are hit at their entry distance, and
// there is no any-hit stage.
static const char kInlineProcedural[] = R"WGSL(
      cand.t = campello_internal_slab(bitcast<vec3f>(p0.xyz), bitcast<vec3f>(p1.xyz), o, invD, rayIn.tMin, hit.t);)WGSL";

static const char kInlineApi[] = R"WGSL(
fn campello_traceRay_CAMPELLO_AS(ray: CampelloRay, flags: u32, mask: u32) -> CampelloHit {
  return campello_internal_trace_CAMPELLO_AS(ray, flags, mask);
}

fn campello_traceRayAny_CAMPELLO_AS(ray: CampelloRay, flags: u32, mask: u32) -> bool {
  let h = campello_internal_trace_CAMPELLO_AS(ray, flags | CAMPELLO_RAY_FLAG_TERMINATE_ON_FIRST_HIT, mask);
  return h.kind != CAMPELLO_HIT_NONE;
}
)WGSL";

// Pipelines: the intersection and any-hit stages of the candidate's hit group.
static const char kPipelineProcedural[] = R"WGSL(
      var prim: CampelloProcedural;
      prim.boxMin = bitcast<vec3f>(p0.xyz);
      prim.boxMax = bitcast<vec3f>(p1.xyz);
      prim.primitiveIndex = p0.w;
      prim.geometryIndex = p1.w;
      prim.instanceIndex = instance;
      prim.instanceId = instanceId;
      var objectRay: CampelloRay;
      objectRay.origin = o;
      objectRay.direction = d;
      objectRay.tMin = rayIn.tMin;
      objectRay.tMax = hit.t;
      let isect = campello_internal_intersection(cand.hitGroupIndex, objectRay, prim);
      if (!isect.hit || isect.t < rayIn.tMin || isect.t >= hit.t) { continue; }
      cand.t = isect.t;
      cand.attributes = isect.attributes;)WGSL";

static const char kPipelineAnyHit[] = R"WGSL(
    if (CAMPELLO_INTERNAL_HAS_ANY_HIT && !opaque) {
      if (cand.instanceIndex != CAMPELLO_INTERNAL_INVALID) {
        let ib = tlasPrimBase + cand.instanceIndex * 8u;
        cand.objectToWorld = campello_internal_rows(CAMPELLO_AS[ib], CAMPELLO_AS[ib + 1u], CAMPELLO_AS[ib + 2u]);
        cand.worldToObject = campello_internal_rows(CAMPELLO_AS[ib + 3u], CAMPELLO_AS[ib + 4u], CAMPELLO_AS[ib + 5u]);
      }
      let verdict = campello_internal_anyHit(cand.hitGroupIndex, payload, rayIn, cand);
      if (verdict == CAMPELLO_IGNORE) { continue; }
      if (verdict == CAMPELLO_ACCEPT_AND_END) { done = true; }
    })WGSL";

// One per recursion level L (0 = traced from ray generation): the closest-hit
// and miss functions it calls are level L's (see buildRayTracingPipelineWGSL()).
static const char kPipelineApi[] = R"WGSL(
fn campello_traceRayPipeline_CAMPELLO_AS_LCAMPELLO_LEVEL(ray: CampelloRay, flags: u32, mask: u32, sbtOffset: u32,
                                         sbtStride: u32, missIndex: u32, payload: ptr<function, CampelloPayload>) {
  let hit = campello_internal_traceP_CAMPELLO_AS(ray, flags, mask, sbtOffset, sbtStride, payload);
  if (hit.kind == CAMPELLO_HIT_NONE) {
    campello_internal_miss_LCAMPELLO_LEVEL(missIndex, payload, ray);
    return;
  }
  if ((flags & CAMPELLO_RAY_FLAG_SKIP_CLOSEST_HIT) == 0u) {
    campello_internal_closestHit_LCAMPELLO_LEVEL(hit.hitGroupIndex, payload, ray, hit);
  }
}
)WGSL";

// Past maxRecursionDepth a trace returns at once, leaving the payload as it is.
static const char kPipelineStub[] = R"WGSL(
fn campello_traceRayPipeline_CAMPELLO_AS_LCAMPELLO_LEVEL(ray: CampelloRay, flags: u32, mask: u32, sbtOffset: u32,
                                         sbtStride: u32, missIndex: u32, payload: ptr<function, CampelloPayload>) {
}
)WGSL";

static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
}

namespace {

struct AsVar {
    uint32_t group, binding;
    std::string name;
};

/**
 * Rewrites the acceleration-structure declarations and the trace calls of
 * `source`; fills `vars`. Returns `source` unchanged when it declares none.
 */
std::string rewriteDeclarations(const std::string& source, std::vector<AsVar>& vars) {
    if (source.find("campello_acceleration_structure") == std::string::npos) return source;
    static const std::regex decl(
        R"(@group\s*\(\s*(\d+)\s*\)\s*@binding\s*\(\s*(\d+)\s*\)\s*var\s+(\w+)\s*:\s*campello_acceleration_structure\s*;)");
    std::string out;
    size_t last = 0;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), decl); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        AsVar v{static_cast<uint32_t>(std::stoul(m[1])), static_cast<uint32_t>(std::stoul(m[2])), m[3]};
        vars.push_back(v);
        out.append(source, last, m.position() - last);
        out += "@group(" + std::to_string(v.group) + ") @binding(" + std::to_string(v.binding) +
               ") var<storage, read> " + v.name + ": array<vec4u>;";
        last = m.position() + m.length();
    }
    out.append(source, last, std::string::npos);
    for (const auto& v : vars) {
        // campello_traceRay[Any|Pipeline](name, ...) -> specialised functions.
        const std::regex call(R"(\bcampello_traceRay(Any|Pipeline)?\s*\(\s*)" + v.name + R"(\s*,\s*)");
        out = std::regex_replace(out, call, "campello_traceRay$1_" + v.name + "(");
    }
    return out;
}

std::string rootsDeclarations(const std::vector<AsVar>& vars) {
    std::map<uint32_t, uint32_t> maxBinding;
    for (const auto& v : vars) maxBinding[v.group] = std::max(maxBinding[v.group], v.binding);
    std::string out;
    for (const auto& [group, maxB] : maxBinding) {
        out += "@group(" + std::to_string(group) + ") @binding(" + std::to_string(kAsRootsBinding) +
               ") var<uniform> campello_internal_roots_" + std::to_string(group) + ": array<vec4u, " +
               std::to_string(maxB + 1) + ">;\n";
    }
    return out;
}

/** The traversal of `v` in one flavour. */
std::string traversal(const AsVar& v, bool pipeline) {
    std::string fn = kTraversal;
    if (pipeline) {
        replaceAll(fn, "CAMPELLO_FN", "campello_internal_traceP_CAMPELLO_AS");
        replaceAll(fn, "CAMPELLO_PARAMS", ", sbtOffset: u32, sbtStride: u32, payload: ptr<function, CampelloPayload>");
        replaceAll(fn, "CAMPELLO_SBT_STRIDE", "sbtStride");
        replaceAll(fn, "CAMPELLO_SBT_OFFSET", "sbtOffset");
        replaceAll(fn, "CAMPELLO_PROCEDURAL", kPipelineProcedural);
        replaceAll(fn, "CAMPELLO_ANYHIT", kPipelineAnyHit);
    } else {
        replaceAll(fn, "CAMPELLO_FN", "campello_internal_trace_CAMPELLO_AS");
        replaceAll(fn, "CAMPELLO_PARAMS", "");
        replaceAll(fn, "CAMPELLO_SBT_STRIDE", "0u");
        replaceAll(fn, "CAMPELLO_SBT_OFFSET", "0u");
        replaceAll(fn, "CAMPELLO_PROCEDURAL", kInlineProcedural);
        replaceAll(fn, "CAMPELLO_ANYHIT", "");
        fn += kInlineApi;
    }
    replaceAll(fn, "CAMPELLO_ROOT",
               "campello_internal_roots_" + std::to_string(v.group) + "[" + std::to_string(v.binding) + "].x");
    replaceAll(fn, "CAMPELLO_AS", v.name);
    return fn;
}

// ---- A function-level view of WGSL, enough to find stages and their calls ----

/** `source` with comments blanked out (same length, so offsets still match). */
std::string withoutComments(const std::string& s) {
    std::string out = s;
    size_t i = 0;
    while (i < out.size()) {
        if (out.compare(i, 2, "//") == 0) {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out.compare(i, 2, "/*") == 0) {
            int depth = 0;
            do {
                if (out.compare(i, 2, "/*") == 0) { depth++; out[i] = out[i + 1] = ' '; i += 2; }
                else if (out.compare(i, 2, "*/") == 0) { depth--; out[i] = out[i + 1] = ' '; i += 2; }
                else { if (out[i] != '\n') out[i] = ' '; i++; }
            } while (depth > 0 && i < out.size());
        } else {
            i++;
        }
    }
    return out;
}

struct WgslFunction {
    size_t begin = 0, end = 0;      // [begin, end) of `fn ... { ... }` in the clean text
    std::set<std::string> calls;    // identifiers called in its body
};

/** The functions of comment-free WGSL, by name. */
std::map<std::string, WgslFunction> functions(const std::string& clean) {
    std::map<std::string, WgslFunction> fns;
    static const std::regex fnDecl(R"(\bfn\s+([A-Za-z_]\w*)\s*\()");
    static const std::regex callRe(R"(\b([A-Za-z_]\w*)\s*\()");
    for (auto it = std::sregex_iterator(clean.begin(), clean.end(), fnDecl); it != std::sregex_iterator(); ++it) {
        const std::string name = (*it)[1];
        size_t open = clean.find('{', it->position() + it->length());
        if (open == std::string::npos) continue;
        int depth = 0;
        size_t close = open;
        for (; close < clean.size(); ++close) {
            if (clean[close] == '{') depth++;
            else if (clean[close] == '}' && --depth == 0) break;
        }
        WgslFunction f;
        f.begin = it->position();
        f.end = std::min(close + 1, clean.size());
        const std::string body = clean.substr(open, close - open);
        for (auto c = std::sregex_iterator(body.begin(), body.end(), callRe); c != std::sregex_iterator(); ++c) {
            f.calls.insert((*c)[1]);
        }
        fns[name] = std::move(f);
    }
    return fns;
}

bool isTraceCall(const std::string& name) { return name.rfind("campello_traceRayPipeline", 0) == 0; }

/** The functions that reach a pipeline trace call (directly or through calls). */
std::set<std::string> tracingFunctions(const std::map<std::string, WgslFunction>& fns) {
    std::set<std::string> tracing;
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto& [name, f] : fns) {
            if (tracing.count(name)) continue;
            for (const auto& c : f.calls) {
                if (isTraceCall(c) || tracing.count(c)) {
                    tracing.insert(name);
                    changed = true;
                    break;
                }
            }
        }
    }
    return tracing;
}

/**
 * Renames identifiers, except struct members: member accesses (after a '.')
 * and member declarations (`name:` inside a struct body) have their own
 * namespace.
 */
std::string renameIdentifiers(const std::string& text, const std::map<std::string, std::string>& renames) {
    std::string out;
    out.reserve(text.size());
    bool structPending = false, inStruct = false;
    size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) j++;
            const std::string id = text.substr(i, j - i);
            if (id == "struct") structPending = true;
            size_t k = out.size();
            while (k > 0 && std::isspace(static_cast<unsigned char>(out[k - 1]))) k--;
            bool member = k > 0 && out[k - 1] == '.';
            if (inStruct) {
                size_t n = j;
                while (n < text.size() && std::isspace(static_cast<unsigned char>(text[n]))) n++;
                member |= n < text.size() && text[n] == ':';
            }
            auto it = renames.find(id);
            out += (it != renames.end() && !member) ? it->second : id;
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            // Numbers (0x1Fu, 1e-3f): copy whole so their letters aren't taken as identifiers.
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '.' ||
                                       ((text[j] == '-' || text[j] == '+') && (text[j - 1] == 'e' || text[j - 1] == 'E'))))
                j++;
            out.append(text, i, j - i);
            i = j;
        } else {
            if (c == '{' && structPending) { inStruct = true; structPending = false; }
            else if (c == '}' && inStruct) inStruct = false;
            out += c;
            i++;
        }
    }
    return out;
}

// ---- Merging several modules' declarations ----

struct WgslItem {
    std::string name;   // empty for directives and const_assert
    bool directive = false;
    std::string text;
};

std::vector<WgslItem> topLevelItems(const std::string& clean) {
    std::vector<WgslItem> items;
    static const std::regex named(
        R"(^\s*(?:@\w+\s*(?:\([^)]*\))?\s*)*(fn|struct|var(?:\s*<[^>]*>)?|const|override|alias)\s+([A-Za-z_]\w*))");
    static const std::regex directive(R"(^\s*(enable|requires|diagnostic)\b)");
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < clean.size(); ++i) {
        const char c = clean[i];
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') depth--;
        const bool closesBlock = c == '}' && depth == 0;
        if ((c == ';' && depth == 0) || closesBlock) {
            std::string text = clean.substr(start, i + 1 - start);
            start = i + 1;
            std::smatch m;
            WgslItem item;
            if (std::regex_search(text, m, named)) {
                // A block item (fn, struct) ends at its brace; others at ';'.
                const bool block = m[1] == "fn" || m[1] == "struct";
                if (closesBlock != block) {
                    start -= text.size();  // not this item's end: keep scanning
                    continue;
                }
                item.name = m[2];
            } else if (std::regex_search(text, directive)) {
                item.directive = true;
            }
            if (text.find_first_not_of(" \t\r\n;") == std::string::npos) continue;
            item.text = text;
            items.push_back(std::move(item));
        }
    }
    return items;
}

std::string normalized(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) { space = true; continue; }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

} // namespace

std::string mergeRayTracingModules(const std::vector<std::string>& sources,
                                   std::vector<std::map<std::string, std::string>>& renames) {
    renames.assign(sources.size(), {});
    if (sources.size() == 1) return sources[0];
    std::map<std::string, std::string> declared;  // name -> normalized text
    std::set<std::string> directives;
    std::string directivesText, body;
    for (size_t k = 0; k < sources.size(); ++k) {
        const auto items = topLevelItems(withoutComments(sources[k]));
        std::vector<const WgslItem*> keep;
        for (const auto& item : items) {
            if (item.directive) {
                if (directives.insert(normalized(item.text)).second) directivesText += item.text + "\n";
                continue;
            }
            if (item.name.empty()) { keep.push_back(&item); continue; }
            auto it = declared.find(item.name);
            if (it == declared.end()) {
                declared[item.name] = normalized(item.text);
                keep.push_back(&item);
            } else if (it->second != normalized(item.text)) {
                // Same name, different declaration: this module's gets its own name.
                const std::string fresh = item.name + "__campello_m" + std::to_string(k);
                renames[k][item.name] = fresh;
                declared[fresh] = normalized(item.text);
                keep.push_back(&item);
            }  // identical to an earlier module's declaration: shared
        }
        for (const WgslItem* item : keep) body += renameIdentifiers(item->text, renames[k]) + "\n";
    }
    return directivesText + body;
}

bool buildRayTracingPipelineWGSL(const std::string& source, const RayTracingStages& stages, uint32_t launchGroup,
                                 uint32_t maxRecursionDepth, std::string& out, std::string& error) {
    const uint32_t depth = std::max(1u, maxRecursionDepth);
    std::vector<AsVar> vars;
    const std::string rewritten = rewriteDeclarations(source, vars);
    const std::string clean = withoutComments(rewritten);
    const auto fns = functions(clean);
    const auto tracing = tracingFunctions(fns);

    auto requireFn = [&](const std::string& name, const char* what) {
        if (fns.count(name)) return true;
        error = std::string(what) + " function '" + name + "' not found in the module";
        return false;
    };
    if (!requireFn(stages.rayGeneration, "ray generation")) return false;
    if (clean.find("CampelloPayload") == std::string::npos) {
        error = "the module must declare its payload type: alias CampelloPayload = <type>;";
        return false;
    }
    // Recursion is unrolled by cloning the stages that trace (below). Any-hit
    // and intersection functions may not trace, as on DXR; closest-hit and miss
    // functions may, when the pipeline allows a depth above 1.
    auto checkStage = [&](const std::string& name, const char* what, bool mayTrace) {
        if (!requireFn(name, what)) return false;
        if (!tracing.count(name)) return true;
        if (!mayTrace) {
            error = std::string(what) + " function '" + name + "' traces rays, which only ray generation, "
                    "closest-hit and miss functions may do";
            return false;
        }
        if (depth == 1) {
            error = std::string(what) + " function '" + name + "' traces rays, but the pipeline's "
                    "maxRecursionDepth is 1 (trace from ray generation only)";
            return false;
        }
        return true;
    };
    std::set<std::string> recursive;  // closest-hit / miss functions that trace
    for (const auto& m : stages.miss) {
        if (!checkStage(m, "miss", true)) return false;
        if (tracing.count(m)) recursive.insert(m);
    }
    bool hasAnyHit = false;
    for (const auto& g : stages.hitGroups) {
        if (!g.closestHit.empty()) {
            if (!checkStage(g.closestHit, "closest-hit", true)) return false;
            if (tracing.count(g.closestHit)) recursive.insert(g.closestHit);
        }
        if (!g.anyHit.empty() && !checkStage(g.anyHit, "any-hit", false)) return false;
        if (!g.intersection.empty() && !checkStage(g.intersection, "intersection", false)) return false;
        hasAnyHit |= !g.anyHit.empty();
    }

    // The functions to clone per level: tracing functions the recursive stages reach.
    std::set<std::string> clones;
    std::vector<std::string> work(recursive.begin(), recursive.end());
    while (!work.empty()) {
        const std::string f = work.back();
        work.pop_back();
        if (!clones.insert(f).second) continue;
        for (const auto& c : fns.at(f).calls) {
            if (tracing.count(c) && fns.count(c)) work.push_back(c);
        }
    }

    // The user's code as written traces at level 0 (from ray generation).
    static const std::regex traceCall(R"(\b(campello_traceRayPipeline_\w+?)\s*\()");
    out = std::regex_replace(rewritten, traceCall, "$1_L0(");

    // Level k clones (k = 1..depth): run from level k-1's dispatchers, trace at level k.
    auto levelName = [](const std::string& f, uint32_t k) { return f + "__campello_d" + std::to_string(k); };
    for (uint32_t k = 1; k <= depth && !clones.empty(); ++k) {
        std::map<std::string, std::string> renames;
        for (const auto& f : clones) renames[f] = levelName(f, k);
        for (const auto& f : clones) {
            const WgslFunction& fn = fns.at(f);
            std::string text = renameIdentifiers(clean.substr(fn.begin, fn.end - fn.begin), renames);
            text = std::regex_replace(text, traceCall, "$1_L" + std::to_string(k) + "(");
            out += "\n" + text + "\n";
        }
    }

    out += kPrelude;
    out += rootsDeclarations(vars);
    out += std::string("const CAMPELLO_INTERNAL_HAS_ANY_HIT: bool = ") + (hasAnyHit ? "true" : "false") + ";\n";

    // Dispatchers: a switch over the indices the descriptor fills. Their
    // parameters have reserved names, so they can't shadow a stage function.
    const std::string P = "campello_internal_payload", R = "campello_internal_ray", H = "campello_internal_hit",
                      PR = "campello_internal_prim";
    for (uint32_t level = 0; level < depth; ++level) {
        const std::string L = std::to_string(level);
        auto stageAt = [&](const std::string& f) { return clones.count(f) ? levelName(f, level + 1) : f; };
        out += "\nfn campello_internal_miss_L" + L + "(campello_internal_i: u32, " + P +
               ": ptr<function, CampelloPayload>, " + R + ": CampelloRay) {\n  switch campello_internal_i {\n";
        for (size_t i = 0; i < stages.miss.size(); ++i) {
            out += "    case " + std::to_string(i) + "u: { " + stageAt(stages.miss[i]) + "(" + P + ", " + R + "); }\n";
        }
        out += "    default: {}\n  }\n}\n";

        out += "\nfn campello_internal_closestHit_L" + L + "(campello_internal_i: u32, " + P +
               ": ptr<function, CampelloPayload>, " + R + ": CampelloRay, " + H +
               ": CampelloHit) {\n  switch campello_internal_i {\n";
        for (size_t i = 0; i < stages.hitGroups.size(); ++i) {
            if (stages.hitGroups[i].closestHit.empty()) continue;
            out += "    case " + std::to_string(i) + "u: { " + stageAt(stages.hitGroups[i].closestHit) + "(" + P +
                   ", " + R + ", " + H + "); }\n";
        }
        out += "    default: {}\n  }\n}\n";
    }

    out += "\nfn campello_internal_anyHit(campello_internal_i: u32, " + P + ": ptr<function, CampelloPayload>, " + R +
           ": CampelloRay, " + H + ": CampelloHit) -> u32 {\n  switch campello_internal_i {\n";
    for (size_t i = 0; i < stages.hitGroups.size(); ++i) {
        if (stages.hitGroups[i].anyHit.empty()) continue;
        out += "    case " + std::to_string(i) + "u: { return " + stages.hitGroups[i].anyHit + "(" + P + ", " + R +
               ", " + H + "); }\n";
    }
    out += "    default: { return CAMPELLO_ACCEPT; }\n  }\n}\n";

    // A procedural hit group without an intersection function hits its box
    // at the entry distance.
    out += "\nfn campello_internal_intersection(campello_internal_i: u32, " + R + ": CampelloRay, " + PR +
           ": CampelloProcedural) -> CampelloIntersection {\n  switch campello_internal_i {\n";
    for (size_t i = 0; i < stages.hitGroups.size(); ++i) {
        if (stages.hitGroups[i].intersection.empty()) continue;
        out += "    case " + std::to_string(i) + "u: { return " + stages.hitGroups[i].intersection + "(" + R + ", " +
               PR + "); }\n";
    }
    out += "    default: {\n"
           "      var r: CampelloIntersection;\n"
           "      r.t = campello_internal_slab(" + PR + ".boxMin, " + PR + ".boxMax, " + R + ".origin,\n"
           "                                   campello_internal_safeInverse(" + R + ".direction), " + R + ".tMin, " +
           R + ".tMax);\n"
           "      r.hit = r.t < CAMPELLO_INTERNAL_INF;\n"
           "      return r;\n"
           "    }\n  }\n}\n";

    for (const auto& v : vars) {
        out += traversal(v, false);
        out += traversal(v, true);
        for (uint32_t level = 0; level <= depth; ++level) {
            std::string api = level < depth ? kPipelineApi : kPipelineStub;
            replaceAll(api, "CAMPELLO_LEVEL", std::to_string(level));
            replaceAll(api, "CAMPELLO_AS", v.name);
            out += api;
        }
    }

    // Launch: the size comes from the backend's uniform, the index from the
    // invocation; ray generation runs once per index inside the size.
    const std::string g = std::to_string(launchGroup);
    out += "\nstruct CampelloInternalLaunch { size: vec3u, pad: u32 }\n"
           "@group(" + g + ") @binding(0) var<uniform> campello_internal_launch: CampelloInternalLaunch;\n"
           "var<private> campello_internal_launchId: vec3u;\n"
           "fn campello_launchId() -> vec3u { return campello_internal_launchId; }\n"
           "fn campello_launchSize() -> vec3u { return campello_internal_launch.size; }\n"
           "@compute @workgroup_size(8, 8, 1)\n"
           "fn campello_rt_main(@builtin(global_invocation_id) gid: vec3u) {\n"
           "  if (any(gid >= campello_internal_launch.size)) { return; }\n"
           "  campello_internal_launchId = gid;\n"
           "  " + stages.rayGeneration + "();\n"
           "}\n";
    return true;
}

std::string rewriteRayTracingWGSL(const std::string& source) {
    std::vector<AsVar> vars;
    std::string out = rewriteDeclarations(source, vars);
    if (vars.empty()) return source;
    out += kPrelude;
    out += rootsDeclarations(vars);
    for (const auto& v : vars) out += traversal(v, false);
    return out;
}

bool isRayTracingPipelineSource(const std::string& source) {
    return source.find("campello_traceRayPipeline") != std::string::npos ||
           source.find("CampelloPayload") != std::string::npos ||
           source.find("campello_launchId") != std::string::npos;
}

} // namespace systems::leal::campello_gpu
