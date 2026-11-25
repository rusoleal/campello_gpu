// WGSL compute kernels of the software ray tracing BVH builder (LBVH, Karras
// 2012). See rt_internal.hpp for the acceleration-structure record layout and
// bvh_builder.cpp for the order the kernels run in.
//
// Two modules share one bind group layout (BuilderParams + heap + scratch +
// three read-only source buffers):
//   kBvhKernelsPlain   heap/scratch as plain u32 arrays: init, Morton codes,
//                      radix sort, hierarchy, the one-primitive tree
//   kBvhKernelsAtomic  heap/scratch as atomic u32 arrays: the gathers (scene
//                      bounds are reduced with atomicMin/Max) and the bottom-up
//                      bounds pass (nodes completed by other invocations of
//                      the same dispatch are read with atomicLoad)

#include "rt_internal.hpp"
#include <string>

namespace systems::leal::campello_gpu {

#define CAMPELLO_BVH_COMMON R"WGSL(
struct Params {
  n: u32, numTiles: u32, recBase: u32, nodeBase: u32,
  primBase: u32, leafParentBase: u32, nodeParentBase: u32, nodeCount: u32,
  sBounds: u32, sAabbs: u32, sKeysA: u32, sValsA: u32,
  sKeysB: u32, sValsB: u32, sHist: u32, sCounters: u32,
  shift: u32, srcIsB: u32, kind: u32, buildFlags: u32,
  geomIndex: u32, primStart: u32, primCount: u32, geomFlags: u32,
  srcBase: u32, srcStride: u32, xfBase: u32, sCost: u32,
  sCount: u32, treeletMin: u32, pad3: u32, pad4: u32,
}

const LEAF: u32 = 0x80000000u;
const INVALID: u32 = 0xFFFFFFFFu;
const BIG: f32 = 3.0e38;

const GEOM_OPAQUE: u32 = 1u;
const GEOM_AABB: u32 = 2u;
const GEOM_INDEXED: u32 = 4u;
const GEOM_INDEX16: u32 = 8u;
const GEOM_TRANSFORM: u32 = 16u;

@group(0) @binding(0) var<uniform> P: Params;
@group(0) @binding(3) var<storage, read> src0: array<u32>;
@group(0) @binding(4) var<storage, read> src1: array<u32>;
@group(0) @binding(5) var<storage, read> src2: array<u32>;

// Invocations are laid out in 2D when a dispatch needs more than 65535
// workgroups along x.
fn linearId(wid: vec3u, nwg: vec3u, lid: u32, wg: u32) -> u32 {
  return (wid.y * nwg.x + wid.x) * wg + lid;
}
)WGSL"

static const char kPlainModule[] = CAMPELLO_BVH_COMMON R"WGSL(
@group(0) @binding(1) var<storage, read_write> heap: array<u32>;
@group(0) @binding(2) var<storage, read_write> scratch: array<u32>;

fn decodeOrdered(u: u32) -> f32 {
  if ((u & 0x80000000u) != 0u) { return bitcast<f32>(u & 0x7fffffffu); }
  return bitcast<f32>(~u);
}

fn hf(i: u32, v: f32) { heap[i] = bitcast<u32>(v); }

// Header and scene-bound reset; one invocation.
@compute @workgroup_size(1)
fn init() {
  scratch[P.sBounds + 0u] = INVALID;
  scratch[P.sBounds + 1u] = INVALID;
  scratch[P.sBounds + 2u] = INVALID;
  scratch[P.sBounds + 3u] = 0u;
  scratch[P.sBounds + 4u] = 0u;
  scratch[P.sBounds + 5u] = 0u;
  let h = P.recBase;
  heap[h + 0u] = P.kind;
  heap[h + 1u] = 1u;
  heap[h + 2u] = P.buildFlags;
  heap[h + 3u] = P.n;
  heap[h + 4u] = P.nodeBase - P.recBase;
  heap[h + 5u] = P.primBase - P.recBase;
  heap[h + 6u] = P.nodeCount;
  heap[h + 7u] = P.leafParentBase - P.recBase;
}

@compute @workgroup_size(64)
fn clearCounters(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
                 @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.nodeCount) { return; }
  scratch[P.sCounters + i] = 0u;
}

fn expandBits(v: u32) -> u32 {
  var x = v & 0x3ffu;
  x = (x | (x << 16u)) & 0x030000FFu;
  x = (x | (x << 8u)) & 0x0300F00Fu;
  x = (x | (x << 4u)) & 0x030C30C3u;
  x = (x | (x << 2u)) & 0x09249249u;
  return x;
}

@compute @workgroup_size(64)
fn morton(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
          @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.n) { return; }
  let b = P.sAabbs + i * 8u;
  let mn = vec3f(bitcast<f32>(scratch[b]), bitcast<f32>(scratch[b + 1u]), bitcast<f32>(scratch[b + 2u]));
  let mx = vec3f(bitcast<f32>(scratch[b + 4u]), bitcast<f32>(scratch[b + 5u]), bitcast<f32>(scratch[b + 6u]));
  let c = 0.5 * (mn + mx);
  let lo = vec3f(decodeOrdered(scratch[P.sBounds]), decodeOrdered(scratch[P.sBounds + 1u]),
                 decodeOrdered(scratch[P.sBounds + 2u]));
  let hi = vec3f(decodeOrdered(scratch[P.sBounds + 3u]), decodeOrdered(scratch[P.sBounds + 4u]),
                 decodeOrdered(scratch[P.sBounds + 5u]));
  let ext = hi - lo;
  let safeExt = select(vec3f(1.0), ext, ext > vec3f(0.0));
  let t = clamp(select(vec3f(0.0), (c - lo) / safeExt, ext > vec3f(0.0)), vec3f(0.0), vec3f(1.0));
  let q = vec3u(min(t * 1024.0, vec3f(1023.0)));
  scratch[P.sKeysA + i] = (expandBits(q.x) << 2u) | (expandBits(q.y) << 1u) | expandBits(q.z);
  scratch[P.sValsA + i] = i;
}

// ---- Radix sort: 4-bit digits, tiles of 1024 keys (128 invocations x 8) ----

const SORT_WG: u32 = 128u;
const PER_THREAD: u32 = 8u;
const TILE: u32 = 1024u;

var<workgroup> wgHist: array<atomic<u32>, 16>;

@compute @workgroup_size(128)
fn sortCount(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
             @builtin(local_invocation_index) lid: u32) {
  let tile = wid.y * nwg.x + wid.x;
  if (lid < 16u) { atomicStore(&wgHist[lid], 0u); }
  workgroupBarrier();
  let kb = select(P.sKeysA, P.sKeysB, P.srcIsB != 0u);
  let start = tile * TILE + lid * PER_THREAD;
  for (var k = 0u; k < PER_THREAD; k++) {
    let i = start + k;
    if (i < P.n) {
      atomicAdd(&wgHist[(scratch[kb + i] >> P.shift) & 15u], 1u);
    }
  }
  workgroupBarrier();
  if (lid < 16u && tile < P.numTiles) {
    scratch[P.sHist + lid * P.numTiles + tile] = atomicLoad(&wgHist[lid]);
  }
}

var<workgroup> wgSums: array<u32, 256>;

// Exclusive prefix sum over the digit-major histogram (16 x numTiles); one
// workgroup.
@compute @workgroup_size(256)
fn sortScan(@builtin(local_invocation_index) lid: u32) {
  let count = 16u * P.numTiles;
  let per = (count + 255u) / 256u;
  let s = min(lid * per, count);
  let e = min(s + per, count);
  var sum = 0u;
  for (var i = s; i < e; i++) { sum += scratch[P.sHist + i]; }
  wgSums[lid] = sum;
  workgroupBarrier();
  for (var off = 1u; off < 256u; off <<= 1u) {
    var v = 0u;
    if (lid >= off) { v = wgSums[lid - off]; }
    workgroupBarrier();
    wgSums[lid] += v;
    workgroupBarrier();
  }
  var run = 0u;
  if (lid > 0u) { run = wgSums[lid - 1u]; }
  for (var i = s; i < e; i++) {
    let c = scratch[P.sHist + i];
    scratch[P.sHist + i] = run;
    run += c;
  }
}

var<workgroup> wgCounts: array<u32, 2048>;  // [invocation * 16 + digit]

// Stable scatter: each invocation owns 8 consecutive keys of the tile, and
// ranks among equal digits follow invocation order.
@compute @workgroup_size(128)
fn sortScatter(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
               @builtin(local_invocation_index) lid: u32) {
  let tile = wid.y * nwg.x + wid.x;
  let fromB = P.srcIsB != 0u;
  let ks = select(P.sKeysA, P.sKeysB, fromB);
  let vs = select(P.sValsA, P.sValsB, fromB);
  let kd = select(P.sKeysB, P.sKeysA, fromB);
  let vd = select(P.sValsB, P.sValsA, fromB);
  let start = tile * TILE + lid * PER_THREAD;

  var cnt: array<u32, 16>;
  var digits: array<u32, 8>;
  for (var k = 0u; k < PER_THREAD; k++) {
    let i = start + k;
    if (i < P.n) {
      let d = (scratch[ks + i] >> P.shift) & 15u;
      digits[k] = d;
      cnt[d] += 1u;
    }
  }
  for (var d = 0u; d < 16u; d++) { wgCounts[lid * 16u + d] = cnt[d]; }
  workgroupBarrier();
  if (lid < 16u) {
    var run = 0u;
    for (var t = 0u; t < SORT_WG; t++) {
      let c = wgCounts[t * 16u + lid];
      wgCounts[t * 16u + lid] = run;
      run += c;
    }
  }
  workgroupBarrier();
  if (tile < P.numTiles) {
    var seen: array<u32, 16>;
    for (var k = 0u; k < PER_THREAD; k++) {
      let i = start + k;
      if (i < P.n) {
        let d = digits[k];
        let pos = scratch[P.sHist + d * P.numTiles + tile] + wgCounts[lid * 16u + d] + seen[d];
        seen[d] += 1u;
        scratch[kd + pos] = scratch[ks + i];
        scratch[vd + pos] = scratch[vs + i];
      }
    }
  }
}

// ---- Hierarchy (Karras 2012) over the sorted keys in keys/vals A ----------

fn delta(i: i32, j: i32) -> i32 {
  if (j < 0 || j >= i32(P.n)) { return -1; }
  let ki = scratch[P.sKeysA + u32(i)];
  let kj = scratch[P.sKeysA + u32(j)];
  if (ki == kj) { return 32 + i32(countLeadingZeros(u32(i) ^ u32(j))); }
  return i32(countLeadingZeros(ki ^ kj));
}

fn linkChild(pos: i32, isLeaf: bool, parent: u32) -> u32 {
  if (isLeaf) {
    let prim = scratch[P.sValsA + u32(pos)];
    heap[P.leafParentBase + prim] = parent;
    return prim | LEAF;
  }
  heap[P.nodeParentBase + u32(pos)] = parent;
  return u32(pos);
}

@compute @workgroup_size(64)
fn hierarchy(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
             @builtin(local_invocation_index) lid: u32) {
  let iu = linearId(wid, nwg, lid, 64u);
  if (iu + 1u >= P.n) { return; }
  let i = i32(iu);
  let d = select(-1, 1, delta(i, i + 1) > delta(i, i - 1));
  let dmin = delta(i, i - d);
  var lmax = 2;
  while (delta(i, i + lmax * d) > dmin) { lmax *= 2; }
  var l = 0;
  var t = lmax / 2;
  while (t >= 1) {
    if (delta(i, i + (l + t) * d) > dmin) { l += t; }
    t /= 2;
  }
  let j = i + l * d;
  let dnode = delta(i, j);
  var s = 0;
  var tt = l;
  loop {
    tt = (tt + 1) / 2;
    if (delta(i, i + (s + tt) * d) > dnode) { s += tt; }
    if (tt <= 1) { break; }
  }
  let gamma = i + s * d + min(d, 0);
  let left = linkChild(gamma, min(i, j) == gamma, iu);
  let right = linkChild(gamma + 1, max(i, j) == gamma + 1, iu);
  let nb = P.nodeBase + iu * 16u;
  heap[nb + 3u] = left;
  heap[nb + 7u] = right;
  if (iu == 0u) { heap[P.nodeParentBase] = INVALID; }
}

// ---- Compaction: src0 holds the source record (a copy), the heap the destination ----
// P.recBase/nodeBase/primBase: destination; P.srcBase: source node words,
// P.srcStride: source primitive words, P.xfBase: primitive words to copy.

@compute @workgroup_size(1)
fn compactHeader() {
  let h = P.recBase;
  heap[h + 0u] = src0[0u];
  heap[h + 1u] = 2u;
  heap[h + 2u] = src0[2u];
  heap[h + 3u] = src0[3u];
  heap[h + 4u] = P.nodeBase - P.recBase;
  heap[h + 5u] = P.primBase - P.recBase;
  heap[h + 6u] = P.nodeCount;
  heap[h + 7u] = INVALID;
  for (var k = 8u; k < 16u; k++) { heap[h + k] = src0[k]; }
}

@compute @workgroup_size(64)
fn compactPrims(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
                @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.xfBase) { return; }
  heap[P.primBase + i] = src0[P.srcStride + i];
}

fn quantizeLo(lo: f32, origin: f32, scale: f32) -> u32 {
  let bound = lo - abs(lo) * 2e-7 - 1e-30;  // rounded outward, past any rounding in decoding
  var q = clamp(floor((bound - origin) / scale), 0.0, 255.0);
  while (q > 0.0 && origin + q * scale > bound) { q -= 1.0; }
  return u32(q);
}

fn quantizeHi(hi: f32, origin: f32, scale: f32) -> u32 {
  let bound = hi + abs(hi) * 2e-7 + 1e-30;
  var q = clamp(ceil((bound - origin) / scale), 0.0, 255.0);
  while (q < 255.0 && origin + q * scale < bound) { q += 1.0; }
  return u32(q);
}

@compute @workgroup_size(64)
fn compactNodes(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
                @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.nodeCount) { return; }
  let s = P.srcBase + i * 16u;
  let lc = src0[s + 3u];
  let rc = src0[s + 7u];
  var cmn: array<vec3f, 2>;
  var cmx: array<vec3f, 2>;
  cmn[0] = vec3f(bitcast<f32>(src0[s]), bitcast<f32>(src0[s + 1u]), bitcast<f32>(src0[s + 2u]));
  cmx[0] = vec3f(bitcast<f32>(src0[s + 4u]), bitcast<f32>(src0[s + 5u]), bitcast<f32>(src0[s + 6u]));
  cmn[1] = vec3f(bitcast<f32>(src0[s + 8u]), bitcast<f32>(src0[s + 9u]), bitcast<f32>(src0[s + 10u]));
  cmx[1] = vec3f(bitcast<f32>(src0[s + 12u]), bitcast<f32>(src0[s + 13u]), bitcast<f32>(src0[s + 14u]));
  var mn = vec3f(BIG);
  var mx = vec3f(-BIG);
  if (lc != INVALID) { mn = min(mn, cmn[0]); mx = max(mx, cmx[0]); }
  if (rc != INVALID) { mn = min(mn, cmn[1]); mx = max(mx, cmx[1]); }
  if (lc == INVALID && rc == INVALID) { mn = vec3f(0.0); mx = vec3f(0.0); }
  // Origin rounded down a little, so the children's mins can't fall below it.
  let origin = mn - abs(mn) * 2e-7 - vec3f(1e-30);
  let ext = mx - origin;
  var scale = vec3f(1.0);
  var packedExp = 0u;
  for (var k = 0u; k < 3u; k++) {
    var e = -126;
    if (ext[k] > 0.0) { e = clamp(i32(ceil(log2(ext[k] / 255.0))), -126, 127); }
    // One step up when rounding left 255 steps short of the extent.
    if (exp2(f32(e)) * 255.0 < ext[k] && e < 127) { e += 1; }
    scale[k] = exp2(f32(e));
    packedExp |= u32(e + 128) << (8u * k);
  }
  var bytes: array<u32, 12>;
  for (var c = 0u; c < 2u; c++) {
    for (var k = 0u; k < 3u; k++) {
      bytes[c * 6u + k] = quantizeLo(cmn[c][k], origin[k], scale[k]);
      bytes[c * 6u + 3u + k] = quantizeHi(cmx[c][k], origin[k], scale[k]);
    }
  }
  let d = P.nodeBase + i * 12u;
  heap[d + 0u] = bitcast<u32>(origin.x);
  heap[d + 1u] = bitcast<u32>(origin.y);
  heap[d + 2u] = bitcast<u32>(origin.z);
  heap[d + 3u] = packedExp;
  heap[d + 4u] = lc;
  heap[d + 5u] = rc;
  for (var w = 0u; w < 3u; w++) {
    heap[d + 6u + w] = bytes[w * 4u] | (bytes[w * 4u + 1u] << 8u) | (bytes[w * 4u + 2u] << 16u) | (bytes[w * 4u + 3u] << 24u);
  }
  heap[d + 9u] = 0u;
  heap[d + 10u] = 0u;
  heap[d + 11u] = 0u;
}

// A tree of one primitive: node 0 holds it on the left and nothing on the
// right (an empty box that no ray hits).
@compute @workgroup_size(1)
fn single() {
  let b = P.sAabbs;
  let nb = P.nodeBase;
  for (var k = 0u; k < 3u; k++) {
    heap[nb + k] = scratch[b + k];
    heap[nb + 4u + k] = scratch[b + 4u + k];
    hf(nb + 8u + k, BIG);
    hf(nb + 12u + k, -BIG);
    heap[P.recBase + 8u + k] = scratch[b + k];
    heap[P.recBase + 12u + k] = scratch[b + 4u + k];
  }
  heap[nb + 3u] = 0u | LEAF;
  heap[nb + 7u] = INVALID;
  heap[P.leafParentBase] = 0u;
  heap[P.nodeParentBase] = INVALID;
}
)WGSL";

static const char kAtomicModule[] = CAMPELLO_BVH_COMMON R"WGSL(
@group(0) @binding(1) var<storage, read_write> heap: array<atomic<u32>>;
@group(0) @binding(2) var<storage, read_write> scratch: array<atomic<u32>>;

fn encodeOrdered(f: f32) -> u32 {
  let b = bitcast<u32>(f);
  if ((b & 0x80000000u) != 0u) { return ~b; }
  return b | 0x80000000u;
}

fn hst(i: u32, v: u32) { atomicStore(&heap[i], v); }
fn hstf(i: u32, v: f32) { atomicStore(&heap[i], bitcast<u32>(v)); }
fn hld(i: u32) -> u32 { return atomicLoad(&heap[i]); }
fn hldf(i: u32) -> f32 { return bitcast<f32>(atomicLoad(&heap[i])); }

fn storeLeafBox(i: u32, mn: vec3f, mx: vec3f) {
  let b = P.sAabbs + i * 8u;
  for (var k = 0u; k < 3u; k++) {
    atomicStore(&scratch[b + k], bitcast<u32>(mn[k]));
    atomicStore(&scratch[b + 4u + k], bitcast<u32>(mx[k]));
  }
  let c = 0.5 * (mn + mx);
  for (var k = 0u; k < 3u; k++) {
    let e = encodeOrdered(c[k]);
    atomicMin(&scratch[P.sBounds + k], e);
    atomicMax(&scratch[P.sBounds + 3u + k], e);
  }
}

fn indexAt(k: u32) -> u32 {
  if ((P.geomFlags & GEOM_INDEXED) == 0u) { return k; }
  if ((P.geomFlags & GEOM_INDEX16) != 0u) {
    let w = src1[k >> 1u];
    return select(w & 0xffffu, w >> 16u, (k & 1u) == 1u);
  }
  return src1[k];
}

fn vertexAt(v: u32) -> vec3f {
  let w = P.srcBase + v * P.srcStride;
  let p = vec3f(bitcast<f32>(src0[w]), bitcast<f32>(src0[w + 1u]), bitcast<f32>(src0[w + 2u]));
  if ((P.geomFlags & GEOM_TRANSFORM) == 0u) { return p; }
  let x = P.xfBase;
  let r0 = vec4f(bitcast<f32>(src2[x]), bitcast<f32>(src2[x + 1u]), bitcast<f32>(src2[x + 2u]), bitcast<f32>(src2[x + 3u]));
  let r1 = vec4f(bitcast<f32>(src2[x + 4u]), bitcast<f32>(src2[x + 5u]), bitcast<f32>(src2[x + 6u]), bitcast<f32>(src2[x + 7u]));
  let r2 = vec4f(bitcast<f32>(src2[x + 8u]), bitcast<f32>(src2[x + 9u]), bitcast<f32>(src2[x + 10u]), bitcast<f32>(src2[x + 11u]));
  let h = vec4f(p, 1.0);
  return vec3f(dot(r0, h), dot(r1, h), dot(r2, h));
}

fn primFlags() -> u32 {
  return (P.geomFlags & GEOM_OPAQUE) | (P.geomFlags & GEOM_AABB);
}

// Triangle record: v0.xyz + primitive index, v1.xyz + geometry index,
// v2.xyz + flags.
@compute @workgroup_size(64)
fn gatherTriangles(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
                   @builtin(local_invocation_index) lid: u32) {
  let j = linearId(wid, nwg, lid, 64u);
  if (j >= P.primCount) { return; }
  let i = P.primStart + j;
  let a = vertexAt(indexAt(3u * j));
  let b = vertexAt(indexAt(3u * j + 1u));
  let c = vertexAt(indexAt(3u * j + 2u));
  let pb = P.primBase + i * 12u;
  for (var k = 0u; k < 3u; k++) {
    hstf(pb + k, a[k]);
    hstf(pb + 4u + k, b[k]);
    hstf(pb + 8u + k, c[k]);
  }
  hst(pb + 3u, j);
  hst(pb + 7u, P.geomIndex);
  hst(pb + 11u, primFlags());
  storeLeafBox(i, min(a, min(b, c)), max(a, max(b, c)));
}

// Procedural record: min.xyz + primitive index, max.xyz + geometry index,
// unused + flags.
@compute @workgroup_size(64)
fn gatherAabbs(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
               @builtin(local_invocation_index) lid: u32) {
  let j = linearId(wid, nwg, lid, 64u);
  if (j >= P.primCount) { return; }
  let i = P.primStart + j;
  let w = P.srcBase + j * P.srcStride;
  let mn = vec3f(bitcast<f32>(src0[w]), bitcast<f32>(src0[w + 1u]), bitcast<f32>(src0[w + 2u]));
  let mx = vec3f(bitcast<f32>(src0[w + 3u]), bitcast<f32>(src0[w + 4u]), bitcast<f32>(src0[w + 5u]));
  let pb = P.primBase + i * 12u;
  for (var k = 0u; k < 3u; k++) {
    hstf(pb + k, mn[k]);
    hstf(pb + 4u + k, mx[k]);
    hst(pb + 8u + k, 0u);
  }
  hst(pb + 3u, j);
  hst(pb + 7u, P.geomIndex);
  hst(pb + 11u, primFlags());
  storeLeafBox(i, mn, mx);
}

// Instance record (32 words), prepared on the CPU in src0: objectToWorld
// rows (0..11), worldToObject rows (12..23), instanceId | mask << 24 (24),
// hitGroupOffset | flags << 24 (25), BLAS record word offset in the page (26).
@compute @workgroup_size(64)
fn gatherInstances(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
                   @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.n) { return; }
  let s = i * 32u;
  let pb = P.primBase + i * 32u;
  for (var k = 0u; k < 32u; k++) { hst(pb + k, src0[s + k]); }
  let blas = src0[s + 26u];
  var mn = vec3f(BIG);
  var mx = vec3f(-BIG);
  if (blas != INVALID && hld(blas + 3u) > 0u) {
    let bmn = vec3f(hldf(blas + 8u), hldf(blas + 9u), hldf(blas + 10u));
    let bmx = vec3f(hldf(blas + 12u), hldf(blas + 13u), hldf(blas + 14u));
    let r0 = vec4f(bitcast<f32>(src0[s]), bitcast<f32>(src0[s + 1u]), bitcast<f32>(src0[s + 2u]), bitcast<f32>(src0[s + 3u]));
    let r1 = vec4f(bitcast<f32>(src0[s + 4u]), bitcast<f32>(src0[s + 5u]), bitcast<f32>(src0[s + 6u]), bitcast<f32>(src0[s + 7u]));
    let r2 = vec4f(bitcast<f32>(src0[s + 8u]), bitcast<f32>(src0[s + 9u]), bitcast<f32>(src0[s + 10u]), bitcast<f32>(src0[s + 11u]));
    for (var c = 0u; c < 8u; c++) {
      let p = vec4f(select(bmn.x, bmx.x, (c & 1u) != 0u), select(bmn.y, bmx.y, (c & 2u) != 0u),
                    select(bmn.z, bmx.z, (c & 4u) != 0u), 1.0);
      let w = vec3f(dot(r0, p), dot(r1, p), dot(r2, p));
      mn = min(mn, w);
      mx = max(mx, w);
    }
  } else {
    // No geometry: a point at the instance origin keeps the tree well formed;
    // traversal finds nothing below it.
    let o = vec3f(bitcast<f32>(src0[s + 3u]), bitcast<f32>(src0[s + 7u]), bitcast<f32>(src0[s + 11u]));
    mn = o;
    mx = o;
  }
  storeLeafBox(i, mn, mx);
}

// ---- Bottom-up bounds: the second invocation to reach a node completes it --

struct Box { mn: vec3f, mx: vec3f }

fn leafBox(prim: u32) -> Box {
  let b = P.sAabbs + prim * 8u;
  var r: Box;
  r.mn = vec3f(bitcast<f32>(atomicLoad(&scratch[b])), bitcast<f32>(atomicLoad(&scratch[b + 1u])),
               bitcast<f32>(atomicLoad(&scratch[b + 2u])));
  r.mx = vec3f(bitcast<f32>(atomicLoad(&scratch[b + 4u])), bitcast<f32>(atomicLoad(&scratch[b + 5u])),
               bitcast<f32>(atomicLoad(&scratch[b + 6u])));
  return r;
}

fn nodeBox(n: u32) -> Box {
  let nb = P.nodeBase + n * 16u;
  var r: Box;
  r.mn = min(vec3f(hldf(nb), hldf(nb + 1u), hldf(nb + 2u)), vec3f(hldf(nb + 8u), hldf(nb + 9u), hldf(nb + 10u)));
  r.mx = max(vec3f(hldf(nb + 4u), hldf(nb + 5u), hldf(nb + 6u)), vec3f(hldf(nb + 12u), hldf(nb + 13u), hldf(nb + 14u)));
  return r;
}

fn childBox(c: u32) -> Box {
  if ((c & LEAF) != 0u) { return leafBox(c & ~LEAF); }
  return nodeBox(c);
}

@compute @workgroup_size(64)
fn bounds(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
          @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.n) { return; }
  var p = hld(P.leafParentBase + i);
  loop {
    if (atomicAdd(&scratch[P.sCounters + p], 1u) == 0u) { return; }
    let nb = P.nodeBase + p * 16u;
    let lb = childBox(hld(nb + 3u));
    let rb = childBox(hld(nb + 7u));
    for (var k = 0u; k < 3u; k++) {
      hstf(nb + k, lb.mn[k]);
      hstf(nb + 4u + k, lb.mx[k]);
      hstf(nb + 8u + k, rb.mn[k]);
      hstf(nb + 12u + k, rb.mx[k]);
    }
    if (p == 0u) {
      for (var k = 0u; k < 3u; k++) {
        hstf(P.recBase + 8u + k, min(lb.mn[k], rb.mn[k]));
        hstf(P.recBase + 12u + k, max(lb.mx[k], rb.mx[k]));
      }
      return;
    }
    p = hld(P.nodeParentBase + p);
  }
}
)WGSL";

#undef CAMPELLO_BVH_COMMON

// Treelet restructuring (Karras & Aila 2013, "Fast Parallel Construction of
// High-Quality Bounding Volume Hierarchies"), for preferFastTrace. Appended to
// the atomic module: a bottom-up pass over a built tree (boxes valid); at each
// node it grows a treelet of up to 7 leaves by expanding the largest-area
// internal node, finds the topology of least surface area cost over all
// subsets of those leaves, and rewrites the treelet's internal nodes. A
// node's own box never changes, so its ancestors' child boxes stay valid.
static const char kTreeletKernel[] = R"WGSL(
const COST_NODE: f32 = 1.2;
const COST_PRIM: f32 = 1.0;

fn halfArea(mn: vec3f, mx: vec3f) -> f32 {
  let e = max(mx - mn, vec3f(0.0));
  return e.x * e.y + e.y * e.z + e.z * e.x;
}

fn slotMin(node: u32, side: u32) -> vec3f {
  let b = P.nodeBase + node * 16u + side * 8u;
  return vec3f(hldf(b), hldf(b + 1u), hldf(b + 2u));
}
fn slotMax(node: u32, side: u32) -> vec3f {
  let b = P.nodeBase + node * 16u + side * 8u;
  return vec3f(hldf(b + 4u), hldf(b + 5u), hldf(b + 6u));
}
fn slotChild(node: u32, side: u32) -> u32 { return hld(P.nodeBase + node * 16u + 3u + side * 4u); }

fn writeSlot(node: u32, side: u32, child: u32, mn: vec3f, mx: vec3f) {
  let b = P.nodeBase + node * 16u + side * 8u;
  for (var k = 0u; k < 3u; k++) {
    hstf(b + k, mn[k]);
    hstf(b + 4u + k, mx[k]);
  }
  hst(P.nodeBase + node * 16u + 3u + side * 4u, child);
}

fn childCost(c: u32, mn: vec3f, mx: vec3f) -> f32 {
  if ((c & LEAF) != 0u) { return COST_PRIM * halfArea(mn, mx); }
  return bitcast<f32>(atomicLoad(&scratch[P.sCost + c]));
}
fn childCount(c: u32) -> u32 {
  if ((c & LEAF) != 0u) { return 1u; }
  return atomicLoad(&scratch[P.sCount + c]);
}
fn setParent(c: u32, parent: u32) {
  if ((c & LEAF) != 0u) { hst(P.leafParentBase + (c & ~LEAF), parent); } else { hst(P.nodeParentBase + c, parent); }
}

fn restructure(root: u32) {
  var lid: array<u32, 7>;
  var lmn: array<vec3f, 7>;
  var lmx: array<vec3f, 7>;
  var lcost: array<f32, 7>;
  var lcount: array<u32, 7>;
  var intern: array<u32, 6>;
  intern[0] = root;
  var ni = 1u;
  var nl = 2u;
  for (var s = 0u; s < 2u; s++) {
    lid[s] = slotChild(root, s);
    lmn[s] = slotMin(root, s);
    lmx[s] = slotMax(root, s);
    lcost[s] = childCost(lid[s], lmn[s], lmx[s]);
    lcount[s] = childCount(lid[s]);
  }
  // Grow: expand the internal leaf of largest area until there are 7 leaves.
  while (nl < 7u) {
    var best = 7u;
    var bestArea = -1.0;
    for (var j = 0u; j < nl; j++) {
      if ((lid[j] & LEAF) == 0u) {
        let a = halfArea(lmn[j], lmx[j]);
        if (a > bestArea) { bestArea = a; best = j; }
      }
    }
    if (best == 7u) { break; }
    let n = lid[best];
    intern[ni] = n;
    ni += 1u;
    for (var s = 0u; s < 2u; s++) {
      let j = select(nl, best, s == 0u);
      lid[j] = slotChild(n, s);
      lmn[j] = slotMin(n, s);
      lmx[j] = slotMax(n, s);
      lcost[j] = childCost(lid[j], lmn[j], lmx[j]);
      lcount[j] = childCount(lid[j]);
    }
    nl += 1u;
  }

  // Optimal partitions of every subset of the leaves.
  let full = (1u << nl) - 1u;
  var area: array<f32, 128>;
  var cost: array<f32, 128>;
  var part: array<u32, 128>;
  for (var s = 1u; s <= full; s++) {
    var mn = vec3f(BIG);
    var mx = vec3f(-BIG);
    for (var j = 0u; j < nl; j++) {
      if ((s & (1u << j)) != 0u) { mn = min(mn, lmn[j]); mx = max(mx, lmx[j]); }
    }
    area[s] = halfArea(mn, mx);
  }
  for (var j = 0u; j < nl; j++) { cost[1u << j] = lcost[j]; }
  for (var s = 1u; s <= full; s++) {
    if (countOneBits(s) < 2u) { continue; }
    let low = s & (~s + 1u);
    var bestCost = BIG;
    var bestPart = low;
    // Submasks holding the lowest leaf: each split is seen once.
    var p = (s - 1u) & s;
    loop {
      if (p == 0u) { break; }
      if ((p & low) != 0u) {
        let c = cost[p] + cost[s ^ p];
        if (c < bestCost) { bestCost = c; bestPart = p; }
      }
      p = (p - 1u) & s;
    }
    cost[s] = COST_NODE * area[s] + bestCost;
    part[s] = bestPart;
  }

  // Rebuild the treelet's internal nodes from the partitions, root first.
  var stackMask: array<u32, 6>;
  var stackNode: array<u32, 6>;
  var sp = 1u;
  stackMask[0] = full;
  stackNode[0] = root;
  var next = 1u;
  while (sp > 0u) {
    sp -= 1u;
    let mask = stackMask[sp];
    let node = stackNode[sp];
    var count = 0u;
    for (var s = 0u; s < 2u; s++) {
      let m = select(mask ^ part[mask], part[mask], s == 0u);
      var mn = vec3f(BIG);
      var mx = vec3f(-BIG);
      for (var j = 0u; j < nl; j++) {
        if ((m & (1u << j)) != 0u) { mn = min(mn, lmn[j]); mx = max(mx, lmx[j]); count += lcount[j]; }
      }
      var child: u32;
      if (countOneBits(m) == 1u) {
        child = lid[firstTrailingBit(m)];
      } else {
        child = intern[next];
        next += 1u;
        stackMask[sp] = m;
        stackNode[sp] = child;
        sp += 1u;
      }
      writeSlot(node, s, child, mn, mx);
      setParent(child, node);
    }
    atomicStore(&scratch[P.sCost + node], bitcast<u32>(cost[mask]));
    atomicStore(&scratch[P.sCount + node], count);
  }
}

@compute @workgroup_size(64)
fn treelets(@builtin(workgroup_id) wid: vec3u, @builtin(num_workgroups) nwg: vec3u,
            @builtin(local_invocation_index) lid: u32) {
  let i = linearId(wid, nwg, lid, 64u);
  if (i >= P.n) { return; }
  var p = hld(P.leafParentBase + i);
  loop {
    if (atomicAdd(&scratch[P.sCounters + p], 1u) == 0u) { return; }
    let l = slotChild(p, 0u);
    let r = slotChild(p, 1u);
    if (childCount(l) + childCount(r) >= max(P.treeletMin, 3u)) {
      restructure(p);
    } else {
      let c = COST_NODE * halfArea(min(slotMin(p, 0u), slotMin(p, 1u)), max(slotMax(p, 0u), slotMax(p, 1u))) +
              childCost(l, slotMin(p, 0u), slotMax(p, 0u)) + childCost(r, slotMin(p, 1u), slotMax(p, 1u));
      atomicStore(&scratch[P.sCost + p], bitcast<u32>(c));
      atomicStore(&scratch[P.sCount + p], childCount(l) + childCount(r));
    }
    if (p == 0u) { return; }
    p = hld(P.nodeParentBase + p);
  }
}
)WGSL";

const char* bvhKernelsPlainSource() { return kPlainModule; }
const char* bvhKernelsAtomicSource() {
    static const std::string source = std::string(kAtomicModule) + kTreeletKernel;
    return source.c_str();
}

} // namespace systems::leal::campello_gpu
