#pragma once

#include <webgpu/webgpu.h>
#include <emscripten.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <set>
#include <campello_gpu/metrics.hpp>
#include <campello_gpu/constants/feature.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>

namespace systems::leal::campello_gpu {

// ---------------------------------------------------------------------------
// Targets the upstream webgpu.h shipped by Dawn's `emdawnwebgpu` Emscripten
// port (`--use-port=emdawnwebgpu`), not the legacy `-sUSE_WEBGPU` binding.
// ---------------------------------------------------------------------------

/** Process-wide WebGPU instance, created on first use and never released. */
WGPUInstance getWGPUInstance();

/** Wraps a C string (nullptr allowed) as a null-terminated WGPUStringView. */
inline WGPUStringView toStringView(const char* s) {
    WGPUStringView v{};
    v.data = s;
    v.length = s ? WGPU_STRLEN : 0;
    return v;
}

/** Wraps a std::string; an empty string yields a null view (API default). */
inline WGPUStringView toStringView(const std::string& s) {
    WGPUStringView v{};
    if (!s.empty()) {
        v.data = s.data();
        v.length = s.size();
    }
    return v;
}

inline std::string fromStringView(WGPUStringView v) {
    if (!v.data) return {};
    if (v.length == WGPU_STRLEN) return std::string(v.data);
    return std::string(v.data, v.length);
}

/**
 * Result slot for one webgpu.h async operation (map, work-done, request
 * adapter/device). Filled by the callback; `handle` receives the WGPUAdapter /
 * WGPUDevice for request callbacks. If `onDone` is set it runs inside the
 * callback, and `heapOwned` results delete themselves afterwards.
 */
struct WGPUAsyncResult {
    bool done = false;
    int status = 0;
    void* handle = nullptr;
    // Second callback argument when it is an enum, e.g. the WGPUErrorType of
    // a pop-error-scope callback.
    int value = 0;
    std::string message;
    std::function<void(WGPUAsyncResult&)> onDone;
    bool heapOwned = false;
};

namespace detail {
template <typename T>
inline void captureMessage(WGPUAsyncResult& r, const T& arg) {
    if constexpr (std::is_same_v<std::decay_t<T>, WGPUStringView>) {
        r.message = fromStringView(arg);
    }
}

// Every webgpu.h callback has the shape (status, [payload...], userdata1,
// userdata2). Deriving the thunk from the callback type itself keeps this
// code independent of the exact parameter list, which has changed between
// webgpu.h revisions (e.g. a message parameter added to some callbacks).
template <typename Fn> struct CallbackThunk;
template <typename R, typename... Args>
struct CallbackThunk<R (*)(Args...)> {
    static R invoke(Args... args) {
        constexpr size_t N = sizeof...(Args);
        static_assert(N >= 3, "unexpected webgpu.h callback shape");
        auto tup = std::forward_as_tuple(args...);
        auto* r = static_cast<WGPUAsyncResult*>(std::get<N - 2>(tup));
        r->status = static_cast<int>(std::get<0>(tup));
        using T1 = std::decay_t<std::tuple_element_t<1, std::tuple<Args...>>>;
        if constexpr (std::is_pointer_v<T1>) {
            r->handle = static_cast<void*>(std::get<1>(tup));
        } else if constexpr (std::is_enum_v<T1> || std::is_integral_v<T1>) {
            r->value = static_cast<int>(std::get<1>(tup));
        }
        std::apply([r](auto&... a) { (captureMessage(*r, a), ...); }, tup);
        r->done = true;
        if (r->onDone) r->onDone(*r);
        if (r->heapOwned) delete r;
    }
};
} // namespace detail

/** Builds a `WGPU*CallbackInfo` that reports into `r` (AllowSpontaneous mode). */
template <typename Info>
inline Info makeCallbackInfo(WGPUAsyncResult* r) {
    Info info{};
    info.mode = WGPUCallbackMode_AllowSpontaneous;
    info.callback = &detail::CallbackThunk<decltype(info.callback)>::invoke;
    info.userdata1 = r;
    return info;
}

/**
 * Blocks (yielding to the browser event loop through ASYNCIFY) until `r`
 * completes. AllowSpontaneous callbacks fire from the event loop, so yielding
 * is all that is needed.
 */
inline void waitForAsync(const WGPUAsyncResult& r) {
    while (!r.done) {
        emscripten_sleep(1);
    }
}

/** Starts an async operation whose result is delivered to `onDone`. */
inline WGPUAsyncResult* newAsyncResult(std::function<void(WGPUAsyncResult&)> onDone) {
    auto* r = new WGPUAsyncResult();
    r->onDone = std::move(onDone);
    r->heapOwned = true;
    return r;
}

struct MipmapGenResources {
    WGPUShaderModule vsModule = nullptr;
    WGPUShaderModule fsModule = nullptr;
    WGPUBindGroupLayout bgl = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    WGPUSampler sampler = nullptr;
};

struct WebGPUDeviceData {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    bool surfaceConfigured = false;
    std::string canvasSelector;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_Undefined;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;
    bool timestampQueryEnabled = false;
    MipmapGenResources mipmapGen;

    // Cleared in ~Device(); async callbacks that may outlive the device
    // (timestamp readback) check it before touching this struct.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    // Resource counters
    std::atomic<uint32_t> bufferCount{0};
    std::atomic<uint32_t> textureCount{0};
    std::atomic<uint32_t> renderPipelineCount{0};
    std::atomic<uint32_t> computePipelineCount{0};
    std::atomic<uint32_t> rayTracingPipelineCount{0};
    std::atomic<uint32_t> accelerationStructureCount{0};
    std::atomic<uint32_t> shaderModuleCount{0};
    std::atomic<uint32_t> samplerCount{0};
    std::atomic<uint32_t> bindGroupCount{0};
    std::atomic<uint32_t> bindGroupLayoutCount{0};
    std::atomic<uint32_t> pipelineLayoutCount{0};
    std::atomic<uint32_t> querySetCount{0};

    // Command stats
    std::atomic<uint64_t> commandsSubmitted{0};
    std::atomic<uint64_t> renderPasses{0};
    std::atomic<uint64_t> computePasses{0};
    std::atomic<uint64_t> rayTracingPasses{0};
    std::atomic<uint64_t> drawCalls{0};
    std::atomic<uint64_t> dispatchCalls{0};
    std::atomic<uint64_t> traceRaysCalls{0};
    std::atomic<uint64_t> copies{0};

    // Phase 2: Resource memory tracking (bytes)
    std::atomic<uint64_t> bufferBytes{0};
    std::atomic<uint64_t> textureBytes{0};
    std::atomic<uint64_t> accelerationStructureBytes{0};
    std::atomic<uint64_t> shaderModuleBytes{0};
    std::atomic<uint64_t> querySetBytes{0};

    // Phase 2: Peak memory tracking
    std::atomic<uint64_t> peakBufferBytes{0};
    std::atomic<uint64_t> peakTextureBytes{0};
    std::atomic<uint64_t> peakAccelerationStructureBytes{0};
    std::atomic<uint64_t> peakTotalBytes{0};

    // Phase 3: GPU pass timing (nanoseconds)
    std::atomic<uint64_t> renderPassTimeNs{0};
    std::atomic<uint64_t> computePassTimeNs{0};
    std::atomic<uint64_t> rayTracingPassTimeNs{0};
    std::atomic<uint32_t> renderPassSampleCount{0};
    std::atomic<uint32_t> computePassSampleCount{0};
    std::atomic<uint32_t> rayTracingPassSampleCount{0};

    // Phase 3: Memory budget and pressure management
    MemoryBudget memoryBudget;
    MemoryPressureCallback memoryPressureCallback;
    std::atomic<MemoryPressureLevel> lastPressureLevel{MemoryPressureLevel::Normal};
};

struct AdapterHandle {
    WGPUAdapter adapter = nullptr;
    // A WebGPU adapter can create only one device ("consumed" afterwards);
    // Device::createDevice() requests a fresh adapter once this is set.
    bool consumed = false;
};

struct BufferHandle {
    WGPUBuffer buffer = nullptr;
    WGPUBufferUsage usage = WGPUBufferUsage_None;  // actual usage, incl. implicit copy usages
    uint64_t size = 0;
    uint64_t allocatedSize = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct TextureHandle {
    WGPUTexture texture = nullptr;
    WGPUTextureView defaultView = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t arrayLayers = 0;
    uint32_t mipLevels = 0;
    uint32_t samples = 0;
    PixelFormat pixelFormat = PixelFormat::invalid;
    TextureUsage usage = static_cast<TextureUsage>(0);
    TextureType textureType = TextureType::tt2d;
    uint64_t allocatedSize = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct TextureViewHandle {
    WGPUTextureView view = nullptr;
    bool ownsView = true;
    // Undefined when unknown (views wrapped with TextureView::fromNative()).
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
};

struct ShaderModuleHandle {
    WGPUShaderModule shaderModule = nullptr;
    uint64_t size = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct RenderPipelineHandle {
    WGPURenderPipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct ComputePipelineHandle {
    WGPUComputePipeline pipeline = nullptr;
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct BindGroupLayoutHandle {
    WGPUBindGroupLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct PipelineLayoutHandle {
    WGPUPipelineLayout layout = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct SamplerHandle {
    WGPUSampler sampler = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct QuerySetHandle {
    WGPUQuerySet querySet = nullptr;
    WGPUQueryType queryType = WGPUQueryType_Occlusion;
    uint32_t count = 0;
    WebGPUDeviceData* deviceData = nullptr;
};

struct BindGroupHandle {
    WGPUBindGroup bindGroup = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

/** One pass timed by the backend: indices into the encoder's timestamp query set. */
struct PassTimestampSlot {
    bool isCompute = false;
    uint32_t beginIndex = 0;
    uint32_t endIndex = 0;
};

/**
 * GPU timing of one command buffer. WebGPU has no encoder-level timestamp
 * (writeTimestamp was removed from the spec), so every pass the backend
 * encodes gets its own begin/end `timestampWrites`; the command buffer's GPU
 * time spans the first pass's begin to the last pass's end. Shared between
 * the CommandBuffer and the in-flight map callback started by submit().
 */
struct GpuTimingState {
    WGPUBuffer readbackBuffer = nullptr;
    uint64_t readbackSize = 0;
    std::vector<PassTimestampSlot> passes;
    WebGPUDeviceData* deviceData = nullptr;
    std::shared_ptr<std::atomic<bool>> deviceAlive;
    bool submitted = false;
    bool done = false;
    uint64_t totalNs = 0;
    std::vector<std::function<void(uint64_t)>> waiters;

    ~GpuTimingState() {
        if (readbackBuffer) wgpuBufferRelease(readbackBuffer);
    }
};

/** Max passes per command encoder that get backend timestamps (2 queries each). */
constexpr uint32_t kMaxTimedPassesPerEncoder = 64;

struct CommandEncoderHandle {
    WGPUCommandEncoder encoder = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
    // Backend-owned timestamp queries, created lazily on the first timed pass
    // and resolved into GpuTimingState::readbackBuffer in finish().
    WGPUQuerySet timestampQuerySet = nullptr;
    uint32_t nextTimestampIndex = 0;
    std::vector<PassTimestampSlot> timedPasses;
};

struct CommandBufferHandle {
    WGPUCommandBuffer commandBuffer = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
    std::shared_ptr<GpuTimingState> timing;  // null when timing is unavailable
};

struct RenderPassEncoderHandle {
    WGPURenderPassEncoder encoder = nullptr;
    WGPURenderPipeline currentPipeline = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct ComputePassEncoderHandle {
    WGPUComputePassEncoder encoder = nullptr;
    WGPUComputePipeline currentPipeline = nullptr;
    WebGPUDeviceData* deviceData = nullptr;
};

struct FenceHandle {
    std::atomic<bool> signaled{false};
};

struct AccelerationStructureHandle {
    void* placeholder = nullptr;
};

struct RayTracingPipelineHandle {
    void* placeholder = nullptr;
};

struct RayTracingPassEncoderHandle {
    void* placeholder = nullptr;
};

// Format conversion helpers
WGPUTextureFormat toWGPUTextureFormat(PixelFormat format);
PixelFormat wgpuFormatToPixelFormat(WGPUTextureFormat format);
WGPUVertexFormat toWGPUVertexFormat(ComponentType comp, AccessorType acc, bool normalized);
WGPUPrimitiveTopology toWGPUPrimitiveTopology(PrimitiveTopology topology);
WGPUFrontFace toWGPUFrontFace(FrontFace face);
WGPUCullMode toWGPUCullMode(CullMode mode);
WGPUBlendFactor toWGPUBlendFactor(BlendFactor factor);
WGPUBlendOperation toWGPUBlendOperation(BlendOperation op);
WGPUCompareFunction toWGPUCompareFunction(CompareOp op);
WGPUStencilOperation toWGPUStencilOperation(StencilOp op);
WGPUIndexFormat toWGPUIndexFormat(IndexFormat format);
WGPUAddressMode toWGPUAddressMode(WrapMode mode);
WGPUFilterMode toWGPUFilterMode(FilterMode mode);
WGPUMipmapFilterMode toWGPUMipmapFilterMode(FilterMode mode);
WGPULoadOp toWGPULoadOp(LoadOp op);
WGPUStoreOp toWGPUStoreOp(StoreOp op);
WGPUTextureDimension toWGPUTextureDimension(TextureType type);
WGPUTextureViewDimension toWGPUTextureViewDimension(TextureType type);
WGPUTextureViewDimension toWGPUTextureViewDimension(TextureType type, uint32_t arrayLayerCount);
WGPUTextureUsage toWGPUTextureUsage(TextureUsage usage);
TextureUsage fromWGPUTextureUsage(WGPUTextureUsage usage);
WGPUBufferUsage toWGPUBufferUsage(BufferUsage usage);
WGPUShaderStage toWGPUShaderStage(ShaderStage stage);
WGPUBufferBindingType toWGPUBindingType(EntryObjectType type, EntryObjectBufferType bufferType);
WGPUSamplerBindingType toWGPUSamplerBindingType(EntryObjectSamplerType type);
WGPUTextureSampleType toWGPUTextureSampleType(EntryObjectTextureType type);

/**
 * Optional WebGPU features the backend requests at device creation whenever
 * the adapter supports them (see adapter.cpp).
 */
std::vector<WGPUFeatureName> desiredOptionalFeatures();

/** Maps the WebGPU features reported by `has` to campello_gpu Features. */
std::set<Feature> featuresFromWGPU(const std::function<bool(WGPUFeatureName)>& has);

bool formatHasDepth(WGPUTextureFormat format);
bool formatHasStencil(WGPUTextureFormat format);

/** Creates a WGSL shader module (nullptr code yields nullptr). */
WGPUShaderModule createWGSLShaderModule(WGPUDevice device, const char* code);

} // namespace systems::leal::campello_gpu
