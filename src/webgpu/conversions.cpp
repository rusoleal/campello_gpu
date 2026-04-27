#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/primitive_topology.hpp>
#include <campello_gpu/constants/front_face.hpp>
#include <campello_gpu/constants/cull_mode.hpp>
#include <campello_gpu/descriptors/fragment_descriptor.hpp>
#include <campello_gpu/constants/compare_op.hpp>
#include <campello_gpu/constants/stencil_op.hpp>
#include <campello_gpu/constants/index_format.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/shader_stage.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/vertex_descriptor.hpp>
#include <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#include "common.hpp"

using namespace systems::leal::campello_gpu;

WGPUTextureFormat systems::leal::campello_gpu::toWGPUTextureFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::r8unorm: return WGPUTextureFormat_R8Unorm;
        case PixelFormat::r8snorm: return WGPUTextureFormat_R8Snorm;
        case PixelFormat::r8uint: return WGPUTextureFormat_R8Uint;
        case PixelFormat::r8sint: return WGPUTextureFormat_R8Sint;
        case PixelFormat::r16unorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::r16snorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::r16uint: return WGPUTextureFormat_R16Uint;
        case PixelFormat::r16sint: return WGPUTextureFormat_R16Sint;
        case PixelFormat::r16float: return WGPUTextureFormat_R16Float;
        case PixelFormat::rg8unorm: return WGPUTextureFormat_RG8Unorm;
        case PixelFormat::rg8snorm: return WGPUTextureFormat_RG8Snorm;
        case PixelFormat::rg8uint: return WGPUTextureFormat_RG8Uint;
        case PixelFormat::rg8sint: return WGPUTextureFormat_RG8Sint;
        case PixelFormat::r32uint: return WGPUTextureFormat_R32Uint;
        case PixelFormat::r32sint: return WGPUTextureFormat_R32Sint;
        case PixelFormat::r32float: return WGPUTextureFormat_R32Float;
        case PixelFormat::rg16unorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::rg16snorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::rg16uint: return WGPUTextureFormat_RG16Uint;
        case PixelFormat::rg16sint: return WGPUTextureFormat_RG16Sint;
        case PixelFormat::rg16float: return WGPUTextureFormat_RG16Float;
        case PixelFormat::rgba8unorm: return WGPUTextureFormat_RGBA8Unorm;
        case PixelFormat::rgba8unorm_srgb: return WGPUTextureFormat_RGBA8UnormSrgb;
        case PixelFormat::rgba8snorm: return WGPUTextureFormat_RGBA8Snorm;
        case PixelFormat::rgba8uint: return WGPUTextureFormat_RGBA8Uint;
        case PixelFormat::rgba8sint: return WGPUTextureFormat_RGBA8Sint;
        case PixelFormat::bgra8unorm: return WGPUTextureFormat_BGRA8Unorm;
        case PixelFormat::bgra8unorm_srgb: return WGPUTextureFormat_BGRA8UnormSrgb;
        case PixelFormat::rgb9e5ufloat: return WGPUTextureFormat_RGB9E5Ufloat;
        case PixelFormat::rgb10a2uint: return WGPUTextureFormat_Undefined;
        case PixelFormat::rgb10a2unorm: return WGPUTextureFormat_RGB10A2Unorm;
        case PixelFormat::rg11b10ufloat: return WGPUTextureFormat_RG11B10Ufloat;
        case PixelFormat::rg32uint: return WGPUTextureFormat_RG32Uint;
        case PixelFormat::rg32sint: return WGPUTextureFormat_RG32Sint;
        case PixelFormat::rg32float: return WGPUTextureFormat_RG32Float;
        case PixelFormat::rgba16unorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::rgba16snorm: return WGPUTextureFormat_Undefined;
        case PixelFormat::rgba16uint: return WGPUTextureFormat_RGBA16Uint;
        case PixelFormat::rgba16sint: return WGPUTextureFormat_RGBA16Sint;
        case PixelFormat::rgba16float: return WGPUTextureFormat_RGBA16Float;
        case PixelFormat::rgba32uint: return WGPUTextureFormat_RGBA32Uint;
        case PixelFormat::rgba32sint: return WGPUTextureFormat_RGBA32Sint;
        case PixelFormat::rgba32float: return WGPUTextureFormat_RGBA32Float;
        case PixelFormat::stencil8: return WGPUTextureFormat_Stencil8;
        case PixelFormat::depth16unorm: return WGPUTextureFormat_Depth16Unorm;
        case PixelFormat::depth24plus_stencil8: return WGPUTextureFormat_Depth24PlusStencil8;
        case PixelFormat::depth32float: return WGPUTextureFormat_Depth32Float;
        case PixelFormat::depth32float_stencil8: return WGPUTextureFormat_Depth32FloatStencil8;
        case PixelFormat::bc1_rgba_unorm: return WGPUTextureFormat_BC1RGBAUnorm;
        case PixelFormat::bc1_rgba_unorm_srgb: return WGPUTextureFormat_BC1RGBAUnormSrgb;
        case PixelFormat::bc2_rgba_unorm: return WGPUTextureFormat_BC2RGBAUnorm;
        case PixelFormat::bc2_rgba_unorm_srgb: return WGPUTextureFormat_BC2RGBAUnormSrgb;
        case PixelFormat::bc3_rgba_unorm: return WGPUTextureFormat_BC3RGBAUnorm;
        case PixelFormat::bc3_rgba_unorm_srgb: return WGPUTextureFormat_BC3RGBAUnormSrgb;
        case PixelFormat::bc4_r_unorm: return WGPUTextureFormat_BC4RUnorm;
        case PixelFormat::bc4_r_snorm: return WGPUTextureFormat_BC4RSnorm;
        case PixelFormat::bc5_rg_unorm: return WGPUTextureFormat_BC5RGUnorm;
        case PixelFormat::bc5_rg_snorm: return WGPUTextureFormat_BC5RGSnorm;
        case PixelFormat::bc6h_rgb_ufloat: return WGPUTextureFormat_BC6HRGBUfloat;
        case PixelFormat::bc6h_rgb_float: return WGPUTextureFormat_BC6HRGBFloat;
        case PixelFormat::bc7_rgba_unorm: return WGPUTextureFormat_BC7RGBAUnorm;
        case PixelFormat::bc7_rgba_unorm_srgb: return WGPUTextureFormat_BC7RGBAUnormSrgb;
        case PixelFormat::etc2_rgb8unorm: return WGPUTextureFormat_ETC2RGB8Unorm;
        case PixelFormat::etc2_rgb8unorm_srgb: return WGPUTextureFormat_ETC2RGB8UnormSrgb;
        case PixelFormat::etc2_rgb8a1unorm: return WGPUTextureFormat_ETC2RGB8A1Unorm;
        case PixelFormat::etc2_rgb8a1unorm_srgb: return WGPUTextureFormat_ETC2RGB8A1UnormSrgb;
        case PixelFormat::eac_r11unorm: return WGPUTextureFormat_EACR11Unorm;
        case PixelFormat::eac_r11snorm: return WGPUTextureFormat_EACR11Snorm;
        case PixelFormat::eac_rg11unorm: return WGPUTextureFormat_EACRG11Unorm;
        case PixelFormat::eac_rg11snorm: return WGPUTextureFormat_EACRG11Snorm;
        case PixelFormat::astc_4x4_unorm_srgb: return WGPUTextureFormat_ASTC4x4UnormSrgb;
        case PixelFormat::astc_5x4_unorm_srgb: return WGPUTextureFormat_ASTC5x4UnormSrgb;
        case PixelFormat::astc_5x5_unorm_srgb: return WGPUTextureFormat_ASTC5x5UnormSrgb;
        case PixelFormat::astc_6x5_unorm_srgb: return WGPUTextureFormat_ASTC6x5UnormSrgb;
        case PixelFormat::astc_6x6_unorm_srgb: return WGPUTextureFormat_ASTC6x6UnormSrgb;
        case PixelFormat::astc_8x5_unorm_srgb: return WGPUTextureFormat_ASTC8x5UnormSrgb;
        case PixelFormat::astc_8x6_unorm_srgb: return WGPUTextureFormat_ASTC8x6UnormSrgb;
        case PixelFormat::astc_8x8_unorm_srgb: return WGPUTextureFormat_ASTC8x8UnormSrgb;
        case PixelFormat::astc_10x5_unorm_srgb: return WGPUTextureFormat_ASTC10x5UnormSrgb;
        case PixelFormat::astc_10x6_unorm_srgb: return WGPUTextureFormat_ASTC10x6UnormSrgb;
        case PixelFormat::astc_10x8_unorm_srgb: return WGPUTextureFormat_ASTC10x8UnormSrgb;
        case PixelFormat::astc_10x10_unorm_srgb: return WGPUTextureFormat_ASTC10x10UnormSrgb;
        case PixelFormat::astc_12x10_unorm_srgb: return WGPUTextureFormat_ASTC12x10UnormSrgb;
        case PixelFormat::astc_12x12_unorm_srgb: return WGPUTextureFormat_ASTC12x12UnormSrgb;
        default: return WGPUTextureFormat_Undefined;
    }
}

PixelFormat systems::leal::campello_gpu::wgpuFormatToPixelFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Unorm: return PixelFormat::r8unorm;
        case WGPUTextureFormat_R8Snorm: return PixelFormat::r8snorm;
        case WGPUTextureFormat_R8Uint: return PixelFormat::r8uint;
        case WGPUTextureFormat_R8Sint: return PixelFormat::r8sint;
        case WGPUTextureFormat_R16Uint: return PixelFormat::r16uint;
        case WGPUTextureFormat_R16Sint: return PixelFormat::r16sint;
        case WGPUTextureFormat_R16Float: return PixelFormat::r16float;
        case WGPUTextureFormat_RG8Unorm: return PixelFormat::rg8unorm;
        case WGPUTextureFormat_RG8Snorm: return PixelFormat::rg8snorm;
        case WGPUTextureFormat_RG8Uint: return PixelFormat::rg8uint;
        case WGPUTextureFormat_RG8Sint: return PixelFormat::rg8sint;
        case WGPUTextureFormat_R32Uint: return PixelFormat::r32uint;
        case WGPUTextureFormat_R32Sint: return PixelFormat::r32sint;
        case WGPUTextureFormat_R32Float: return PixelFormat::r32float;
        case WGPUTextureFormat_RG16Uint: return PixelFormat::rg16uint;
        case WGPUTextureFormat_RG16Sint: return PixelFormat::rg16sint;
        case WGPUTextureFormat_RG16Float: return PixelFormat::rg16float;
        case WGPUTextureFormat_RGBA8Unorm: return PixelFormat::rgba8unorm;
        case WGPUTextureFormat_RGBA8UnormSrgb: return PixelFormat::rgba8unorm_srgb;
        case WGPUTextureFormat_RGBA8Snorm: return PixelFormat::rgba8snorm;
        case WGPUTextureFormat_RGBA8Uint: return PixelFormat::rgba8uint;
        case WGPUTextureFormat_RGBA8Sint: return PixelFormat::rgba8sint;
        case WGPUTextureFormat_BGRA8Unorm: return PixelFormat::bgra8unorm;
        case WGPUTextureFormat_BGRA8UnormSrgb: return PixelFormat::bgra8unorm_srgb;
        case WGPUTextureFormat_RGB9E5Ufloat: return PixelFormat::rgb9e5ufloat;
        // RGB10A2Uint not supported in core WebGPU
        case WGPUTextureFormat_RGB10A2Unorm: return PixelFormat::rgb10a2unorm;
        case WGPUTextureFormat_RG11B10Ufloat: return PixelFormat::rg11b10ufloat;
        case WGPUTextureFormat_RG32Uint: return PixelFormat::rg32uint;
        case WGPUTextureFormat_RG32Sint: return PixelFormat::rg32sint;
        case WGPUTextureFormat_RG32Float: return PixelFormat::rg32float;
        case WGPUTextureFormat_RGBA16Uint: return PixelFormat::rgba16uint;
        case WGPUTextureFormat_RGBA16Sint: return PixelFormat::rgba16sint;
        case WGPUTextureFormat_RGBA16Float: return PixelFormat::rgba16float;
        case WGPUTextureFormat_RGBA32Uint: return PixelFormat::rgba32uint;
        case WGPUTextureFormat_RGBA32Sint: return PixelFormat::rgba32sint;
        case WGPUTextureFormat_RGBA32Float: return PixelFormat::rgba32float;
        case WGPUTextureFormat_Stencil8: return PixelFormat::stencil8;
        case WGPUTextureFormat_Depth16Unorm: return PixelFormat::depth16unorm;
        case WGPUTextureFormat_Depth24PlusStencil8: return PixelFormat::depth24plus_stencil8;
        case WGPUTextureFormat_Depth32Float: return PixelFormat::depth32float;
        case WGPUTextureFormat_Depth32FloatStencil8: return PixelFormat::depth32float_stencil8;
        default: return PixelFormat::invalid;
    }
}

WGPUPrimitiveTopology systems::leal::campello_gpu::toWGPUPrimitiveTopology(PrimitiveTopology topology) {
    switch (topology) {
        case PrimitiveTopology::pointList: return WGPUPrimitiveTopology_PointList;
        case PrimitiveTopology::lineList: return WGPUPrimitiveTopology_LineList;
        case PrimitiveTopology::lineStrip: return WGPUPrimitiveTopology_LineStrip;
        case PrimitiveTopology::triangleList: return WGPUPrimitiveTopology_TriangleList;
        case PrimitiveTopology::triangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
        default: return WGPUPrimitiveTopology_TriangleList;
    }
}

WGPUFrontFace systems::leal::campello_gpu::toWGPUFrontFace(FrontFace face) {
    switch (face) {
        case FrontFace::ccw: return WGPUFrontFace_CCW;
        case FrontFace::cw: return WGPUFrontFace_CW;
        default: return WGPUFrontFace_CCW;
    }
}

WGPUCullMode systems::leal::campello_gpu::toWGPUCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::none: return WGPUCullMode_None;
        case CullMode::front: return WGPUCullMode_Front;
        case CullMode::back: return WGPUCullMode_Back;
        default: return WGPUCullMode_None;
    }
}

WGPUBlendFactor systems::leal::campello_gpu::toWGPUBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::zero: return WGPUBlendFactor_Zero;
        case BlendFactor::one: return WGPUBlendFactor_One;
        case BlendFactor::srcColor: return WGPUBlendFactor_Src;
        case BlendFactor::oneMinusSrcColor: return WGPUBlendFactor_OneMinusSrc;
        case BlendFactor::srcAlpha: return WGPUBlendFactor_SrcAlpha;
        case BlendFactor::oneMinusSrcAlpha: return WGPUBlendFactor_OneMinusSrcAlpha;
        case BlendFactor::dstColor: return WGPUBlendFactor_Dst;
        case BlendFactor::oneMinusDstColor: return WGPUBlendFactor_OneMinusDst;
        case BlendFactor::dstAlpha: return WGPUBlendFactor_DstAlpha;
        case BlendFactor::oneMinusDstAlpha: return WGPUBlendFactor_OneMinusDstAlpha;
        default: return WGPUBlendFactor_One;
    }
}

WGPUBlendOperation systems::leal::campello_gpu::toWGPUBlendOperation(BlendOperation op) {
    switch (op) {
        case BlendOperation::add: return WGPUBlendOperation_Add;
        case BlendOperation::subtract: return WGPUBlendOperation_Subtract;
        case BlendOperation::reverseSubtract: return WGPUBlendOperation_ReverseSubtract;
        case BlendOperation::min: return WGPUBlendOperation_Min;
        case BlendOperation::max: return WGPUBlendOperation_Max;
        default: return WGPUBlendOperation_Add;
    }
}

WGPUCompareFunction systems::leal::campello_gpu::toWGPUCompareFunction(CompareOp op) {
    switch (op) {
        case CompareOp::never: return WGPUCompareFunction_Never;
        case CompareOp::less: return WGPUCompareFunction_Less;
        case CompareOp::equal: return WGPUCompareFunction_Equal;
        case CompareOp::lessEqual: return WGPUCompareFunction_LessEqual;
        case CompareOp::greater: return WGPUCompareFunction_Greater;
        case CompareOp::notEqual: return WGPUCompareFunction_NotEqual;
        case CompareOp::greaterEqual: return WGPUCompareFunction_GreaterEqual;
        case CompareOp::always: return WGPUCompareFunction_Always;
        default: return WGPUCompareFunction_Always;
    }
}

WGPUStencilOperation systems::leal::campello_gpu::toWGPUStencilOperation(StencilOp op) {
    switch (op) {
        case StencilOp::keep: return WGPUStencilOperation_Keep;
        case StencilOp::zero: return WGPUStencilOperation_Zero;
        case StencilOp::replace: return WGPUStencilOperation_Replace;
        case StencilOp::invert: return WGPUStencilOperation_Invert;
        case StencilOp::incrementClamp: return WGPUStencilOperation_IncrementClamp;
        case StencilOp::decrementClamp: return WGPUStencilOperation_DecrementClamp;
        case StencilOp::incrementWrap: return WGPUStencilOperation_IncrementWrap;
        case StencilOp::decrementWrap: return WGPUStencilOperation_DecrementWrap;
        default: return WGPUStencilOperation_Keep;
    }
}

WGPUIndexFormat systems::leal::campello_gpu::toWGPUIndexFormat(IndexFormat format) {
    switch (format) {
        case IndexFormat::uint16: return WGPUIndexFormat_Uint16;
        case IndexFormat::uint32: return WGPUIndexFormat_Uint32;
        default: return WGPUIndexFormat_Undefined;
    }
}

WGPUAddressMode systems::leal::campello_gpu::toWGPUAddressMode(WrapMode mode) {
    switch (mode) {
        case WrapMode::clampToEdge: return WGPUAddressMode_ClampToEdge;
        case WrapMode::repeat: return WGPUAddressMode_Repeat;
        case WrapMode::mirrorRepeat: return WGPUAddressMode_MirrorRepeat;
        default: return WGPUAddressMode_ClampToEdge;
    }
}

WGPUFilterMode systems::leal::campello_gpu::toWGPUFilterMode(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmNearest:
        case FilterMode::fmNearestMipmapNearest:
        case FilterMode::fmNearestMipmapLinear:
            return WGPUFilterMode_Nearest;
        case FilterMode::fmLinear:
        case FilterMode::fmLinearMipmapNearest:
        case FilterMode::fmLinearMipmapLinear:
            return WGPUFilterMode_Linear;
        default: return WGPUFilterMode_Nearest;
    }
}

WGPUMipmapFilterMode systems::leal::campello_gpu::toWGPUMipmapFilterMode(FilterMode mode) {
    switch (mode) {
        case FilterMode::fmNearestMipmapNearest:
        case FilterMode::fmLinearMipmapNearest:
            return WGPUMipmapFilterMode_Nearest;
        case FilterMode::fmNearestMipmapLinear:
        case FilterMode::fmLinearMipmapLinear:
            return WGPUMipmapFilterMode_Linear;
        default: return WGPUMipmapFilterMode_Nearest;
    }
}

WGPULoadOp systems::leal::campello_gpu::toWGPULoadOp(LoadOp op) {
    switch (op) {
        case LoadOp::clear: return WGPULoadOp_Clear;
        case LoadOp::load: return WGPULoadOp_Load;
        default: return WGPULoadOp_Clear;
    }
}

WGPUStoreOp systems::leal::campello_gpu::toWGPUStoreOp(StoreOp op) {
    switch (op) {
        case StoreOp::discard: return WGPUStoreOp_Discard;
        case StoreOp::store: return WGPUStoreOp_Store;
        default: return WGPUStoreOp_Discard;
    }
}

WGPUTextureDimension systems::leal::campello_gpu::toWGPUTextureDimension(TextureType type) {
    switch (type) {
        case TextureType::tt1d: return WGPUTextureDimension_1D;
        case TextureType::tt2d:
        case TextureType::ttCube:
        case TextureType::ttCubeArray:
            return WGPUTextureDimension_2D;
        case TextureType::tt3d: return WGPUTextureDimension_3D;
        default: return WGPUTextureDimension_2D;
    }
}

WGPUTextureViewDimension systems::leal::campello_gpu::toWGPUTextureViewDimension(TextureType type) {
    switch (type) {
        case TextureType::tt1d: return WGPUTextureViewDimension_1D;
        case TextureType::tt2d: return WGPUTextureViewDimension_2D;
        case TextureType::tt3d: return WGPUTextureViewDimension_3D;
        case TextureType::ttCube: return WGPUTextureViewDimension_Cube;
        case TextureType::ttCubeArray: return WGPUTextureViewDimension_CubeArray;
        // Note: campello_gpu does not have tt2dArray, only tt2d, ttCube, ttCubeArray
        default: return WGPUTextureViewDimension_2D;
    }
}

WGPUTextureUsageFlags systems::leal::campello_gpu::toWGPUTextureUsage(TextureUsage usage) {
    WGPUTextureUsageFlags flags = WGPUTextureUsage_None;
    uint32_t u = static_cast<uint32_t>(usage);
    if (u & static_cast<uint32_t>(TextureUsage::copySrc)) flags |= WGPUTextureUsage_CopySrc;
    if (u & static_cast<uint32_t>(TextureUsage::copyDst)) flags |= WGPUTextureUsage_CopyDst;
    if (u & static_cast<uint32_t>(TextureUsage::textureBinding)) flags |= WGPUTextureUsage_TextureBinding;
    if (u & static_cast<uint32_t>(TextureUsage::storageBinding)) flags |= WGPUTextureUsage_StorageBinding;
    if (u & static_cast<uint32_t>(TextureUsage::renderTarget)) flags |= WGPUTextureUsage_RenderAttachment;
    return flags;
}

WGPUBufferUsageFlags systems::leal::campello_gpu::toWGPUBufferUsage(BufferUsage usage) {
    WGPUBufferUsageFlags flags = WGPUBufferUsage_None;
    uint32_t u = static_cast<uint32_t>(usage);
    if (u & static_cast<uint32_t>(BufferUsage::mapRead)) flags |= WGPUBufferUsage_MapRead;
    if (u & static_cast<uint32_t>(BufferUsage::mapWrite)) flags |= WGPUBufferUsage_MapWrite;
    if (u & static_cast<uint32_t>(BufferUsage::copySrc)) flags |= WGPUBufferUsage_CopySrc;
    if (u & static_cast<uint32_t>(BufferUsage::copyDst)) flags |= WGPUBufferUsage_CopyDst;
    if (u & static_cast<uint32_t>(BufferUsage::index)) flags |= WGPUBufferUsage_Index;
    if (u & static_cast<uint32_t>(BufferUsage::vertex)) flags |= WGPUBufferUsage_Vertex;
    if (u & static_cast<uint32_t>(BufferUsage::uniform)) flags |= WGPUBufferUsage_Uniform;
    if (u & static_cast<uint32_t>(BufferUsage::storage)) flags |= WGPUBufferUsage_Storage;
    if (u & static_cast<uint32_t>(BufferUsage::indirect)) flags |= WGPUBufferUsage_Indirect;
    if (u & static_cast<uint32_t>(BufferUsage::queryResolve)) flags |= WGPUBufferUsage_QueryResolve;
    return flags;
}

WGPUShaderStageFlags systems::leal::campello_gpu::toWGPUShaderStage(ShaderStage stage) {
    WGPUShaderStageFlags flags = WGPUShaderStage_None;
    uint32_t s = static_cast<uint32_t>(stage);
    if (s & static_cast<uint32_t>(ShaderStage::vertex)) flags |= WGPUShaderStage_Vertex;
    if (s & static_cast<uint32_t>(ShaderStage::fragment)) flags |= WGPUShaderStage_Fragment;
    if (s & static_cast<uint32_t>(ShaderStage::compute)) flags |= WGPUShaderStage_Compute;
    return flags;
}

WGPUBufferBindingType systems::leal::campello_gpu::toWGPUBindingType(EntryObjectType type, EntryObjectBufferType bufferType) {
    if (type != EntryObjectType::buffer) return WGPUBufferBindingType_Undefined;
    switch (bufferType) {
        case EntryObjectBufferType::uniform: return WGPUBufferBindingType_Uniform;
        case EntryObjectBufferType::storage: return WGPUBufferBindingType_Storage;
        case EntryObjectBufferType::readOnlyStorage: return WGPUBufferBindingType_ReadOnlyStorage;
        default: return WGPUBufferBindingType_Uniform;
    }
}

WGPUSamplerBindingType systems::leal::campello_gpu::toWGPUSamplerBindingType(EntryObjectSamplerType type) {
    switch (type) {
        case EntryObjectSamplerType::filtering: return WGPUSamplerBindingType_Filtering;
        case EntryObjectSamplerType::nonFiltering: return WGPUSamplerBindingType_NonFiltering;
        case EntryObjectSamplerType::comparison: return WGPUSamplerBindingType_Comparison;
        default: return WGPUSamplerBindingType_Filtering;
    }
}

WGPUTextureSampleType systems::leal::campello_gpu::toWGPUTextureSampleType(EntryObjectTextureType type) {
    switch (type) {
        case EntryObjectTextureType::ttFloat: return WGPUTextureSampleType_Float;
        case EntryObjectTextureType::ttUnfilterableFloat: return WGPUTextureSampleType_UnfilterableFloat;
        case EntryObjectTextureType::ttDepth: return WGPUTextureSampleType_Depth;
        case EntryObjectTextureType::ttSint: return WGPUTextureSampleType_Sint;
        case EntryObjectTextureType::ttUint: return WGPUTextureSampleType_Uint;
        default: return WGPUTextureSampleType_Float;
    }
}

WGPUVertexFormat systems::leal::campello_gpu::toWGPUVertexFormat(ComponentType comp, AccessorType acc, bool normalized) {
    switch (acc) {
        case AccessorType::acScalar:
            switch (comp) {
                case ComponentType::ctFloat: return WGPUVertexFormat_Float32;
                case ComponentType::ctUnsignedInt: return WGPUVertexFormat_Uint32;
                case ComponentType::ctByte: return normalized ? WGPUVertexFormat_Snorm8x2 : WGPUVertexFormat_Sint8x2;
                case ComponentType::ctUnsignedByte: return normalized ? WGPUVertexFormat_Unorm8x2 : WGPUVertexFormat_Uint8x2;
                case ComponentType::ctShort: return normalized ? WGPUVertexFormat_Snorm16x2 : WGPUVertexFormat_Sint16x2;
                case ComponentType::ctUnsignedShort: return normalized ? WGPUVertexFormat_Unorm16x2 : WGPUVertexFormat_Uint16x2;
                default: return WGPUVertexFormat_Float32;
            }
        case AccessorType::acVec2:
            switch (comp) {
                case ComponentType::ctFloat: return WGPUVertexFormat_Float32x2;
                case ComponentType::ctUnsignedInt: return WGPUVertexFormat_Uint32x2;
                case ComponentType::ctByte: return normalized ? WGPUVertexFormat_Snorm8x2 : WGPUVertexFormat_Sint8x2;
                case ComponentType::ctUnsignedByte: return normalized ? WGPUVertexFormat_Unorm8x2 : WGPUVertexFormat_Uint8x2;
                case ComponentType::ctShort: return normalized ? WGPUVertexFormat_Snorm16x2 : WGPUVertexFormat_Sint16x2;
                case ComponentType::ctUnsignedShort: return normalized ? WGPUVertexFormat_Unorm16x2 : WGPUVertexFormat_Uint16x2;
                default: return WGPUVertexFormat_Float32x2;
            }
        case AccessorType::acVec3:
            switch (comp) {
                case ComponentType::ctFloat: return WGPUVertexFormat_Float32x3;
                case ComponentType::ctUnsignedInt: return WGPUVertexFormat_Uint32x3;
                case ComponentType::ctByte: return normalized ? WGPUVertexFormat_Snorm8x4 : WGPUVertexFormat_Sint8x4;
                case ComponentType::ctUnsignedByte: return normalized ? WGPUVertexFormat_Unorm8x4 : WGPUVertexFormat_Uint8x4;
                case ComponentType::ctShort: return normalized ? WGPUVertexFormat_Snorm16x4 : WGPUVertexFormat_Sint16x4;
                case ComponentType::ctUnsignedShort: return normalized ? WGPUVertexFormat_Unorm16x4 : WGPUVertexFormat_Uint16x4;
                default: return WGPUVertexFormat_Float32x3;
            }
        case AccessorType::acVec4:
            switch (comp) {
                case ComponentType::ctFloat: return WGPUVertexFormat_Float32x4;
                case ComponentType::ctUnsignedInt: return WGPUVertexFormat_Uint32x4;
                case ComponentType::ctByte: return normalized ? WGPUVertexFormat_Snorm8x4 : WGPUVertexFormat_Sint8x4;
                case ComponentType::ctUnsignedByte: return normalized ? WGPUVertexFormat_Unorm8x4 : WGPUVertexFormat_Uint8x4;
                case ComponentType::ctShort: return normalized ? WGPUVertexFormat_Snorm16x4 : WGPUVertexFormat_Sint16x4;
                case ComponentType::ctUnsignedShort: return normalized ? WGPUVertexFormat_Unorm16x4 : WGPUVertexFormat_Uint16x4;
                default: return WGPUVertexFormat_Float32x4;
            }
        default:
            return WGPUVertexFormat_Float32;
    }
}
