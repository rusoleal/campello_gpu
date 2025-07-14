#include <campello_gpu/device.hpp>

using namespace systems::leal::campello_gpu;

std::shared_ptr<Buffer> Device::createBuffer(uint64_t size, BufferUsage usage, void *data) {
    auto toReturn = createBuffer(size, usage);
    if (toReturn != nullptr) {
        toReturn->upload(0, size, data);
    }

    return toReturn;
}

uint32_t systems::leal::campello_gpu::getPixelFormatSize(PixelFormat format) {

    switch (format) {
        case PixelFormat::invalid: return 0;
        // 8-bit formats
        case PixelFormat::r8unorm:
        case PixelFormat::r8snorm:
        case PixelFormat::r8uint:
        case PixelFormat::r8sint:
            return 8;

        // 16-bit formats
        case PixelFormat::r16unorm:
        case PixelFormat::r16snorm:
        case PixelFormat::r16uint:
        case PixelFormat::r16sint:
        case PixelFormat::r16float:
        case PixelFormat::rg8unorm:
        case PixelFormat::rg8snorm:
        case PixelFormat::rg8uint:
        case PixelFormat::rg8sint:
            return 16;

        // 32-bit formats
        case PixelFormat::r32uint:
        case PixelFormat::r32sint:
        case PixelFormat::r32float:
        case PixelFormat::rg16unorm:
        case PixelFormat::rg16snorm:
        case PixelFormat::rg16uint:
        case PixelFormat::rg16sint:
        case PixelFormat::rg16float:
        case PixelFormat::rgba8unorm:
        case PixelFormat::rgba8unorm_srgb:
        case PixelFormat::rgba8snorm:
        case PixelFormat::rgba8uint:
        case PixelFormat::rgba8sint:
        case PixelFormat::bgra8unorm:
        case PixelFormat::bgra8unorm_srgb:
        // Packed 32-bit formats
        case PixelFormat::rgb9e5ufloat:
        case PixelFormat::rgb10a2uint:
        case PixelFormat::rgb10a2unorm:
        case PixelFormat::rg11b10ufloat:
            return 32;

        // 64-bit formats
        case PixelFormat::rg32uint:
        case PixelFormat::rg32sint:
        case PixelFormat::rg32float:
        case PixelFormat::rgba16unorm:
        case PixelFormat::rgba16snorm:
        case PixelFormat::rgba16uint:
        case PixelFormat::rgba16sint:
        case PixelFormat::rgba16float:
            return 64;

        // 128-bit formats
        case PixelFormat::rgba32uint:
        case PixelFormat::rgba32sint:
        case PixelFormat::rgba32float:
            return 128;

        // Depth/stencil formats
        case PixelFormat::stencil8:
            return 8;
        case PixelFormat::depth16unorm:
            return 16;
        // depth24plus, // no metal compatible
        case PixelFormat::depth24plus_stencil8:
        case PixelFormat::depth32float:
            return 32;

        // "depth32float-stencil8" feature
        case PixelFormat::depth32float_stencil8:
            return 40;

        // BC compressed formats usable if "texture-compression-bc" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::bc1_rgba_unorm:
        case PixelFormat::bc1_rgba_unorm_srgb:
            return 4;
        case PixelFormat::bc2_rgba_unorm:
        case PixelFormat::bc2_rgba_unorm_srgb:
        case PixelFormat::bc3_rgba_unorm:
        case PixelFormat::bc3_rgba_unorm_srgb:
            return 8;
        case PixelFormat::bc4_r_unorm:
        case PixelFormat::bc4_r_snorm:
            return 4;
        case PixelFormat::bc5_rg_unorm:
        case PixelFormat::bc5_rg_snorm:
        case PixelFormat::bc6h_rgb_ufloat:
        case PixelFormat::bc6h_rgb_float:
        case PixelFormat::bc7_rgba_unorm:
        case PixelFormat::bc7_rgba_unorm_srgb:
            return 8;

        // ETC2 compressed formats usable if "texture-compression-etc2" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::etc2_rgb8unorm:
        case PixelFormat::etc2_rgb8unorm_srgb:
            return 4;
        case PixelFormat::etc2_rgb8a1unorm:
        case PixelFormat::etc2_rgb8a1unorm_srgb:
            return 8;
        // etc2_rgba8unorm, // no metal compatible type
        // etc2_rgba8unorm_srgb, // no metal compatible type
        case PixelFormat::eac_r11unorm:
        case PixelFormat::eac_r11snorm:
        case PixelFormat::eac_rg11unorm:
        case PixelFormat::eac_rg11snorm:

        // ASTC compressed formats usable if "texture-compression-astc" is both
        // supported by the device/user agent and enabled in requestDevice.
        // astc_4x4_unorm,
        case PixelFormat::astc_4x4_unorm_srgb:
        // astc_5x4_unorm,
        case PixelFormat::astc_5x4_unorm_srgb:
        // astc_5x5_unorm,
        case PixelFormat::astc_5x5_unorm_srgb:
        // astc_6x5_unorm,
        case PixelFormat::astc_6x5_unorm_srgb:
        // astc_6x6_unorm,
        case PixelFormat::astc_6x6_unorm_srgb:
        // astc_8x5_unorm,
        case PixelFormat::astc_8x5_unorm_srgb:
        // astc_8x6_unorm,
        case PixelFormat::astc_8x6_unorm_srgb:
        // astc_8x8_unorm,
        case PixelFormat::astc_8x8_unorm_srgb:
        // astc_10x5_unorm,
        case PixelFormat::astc_10x5_unorm_srgb:
        // astc_10x6_unorm,
        case PixelFormat::astc_10x6_unorm_srgb:
        // astc_10x8_unorm,
        case PixelFormat::astc_10x8_unorm_srgb:
        // astc_10x10_unorm,
        case PixelFormat::astc_10x10_unorm_srgb:
        // astc_12x10_unorm,
        case PixelFormat::astc_12x10_unorm_srgb:
        // astc_12x12_unorm,
        case PixelFormat::astc_12x12_unorm_srgb:
            return 0;
    }
}

std::string systems::leal::campello_gpu::pixelFormatToString(PixelFormat format) {

    switch (format) {
        case PixelFormat::invalid: return "invalid";
        // 8-bit formats
        case PixelFormat::r8unorm: return "r8unorm";
        case PixelFormat::r8snorm: return "r8snorm";
        case PixelFormat::r8uint: return "r8uint";
        case PixelFormat::r8sint: return "r8sint";

        // 16-bit formats
        case PixelFormat::r16unorm: return "r16unorm";
        case PixelFormat::r16snorm: return "r16snorm";
        case PixelFormat::r16uint: return "r16uint";
        case PixelFormat::r16sint: return "r16sint";
        case PixelFormat::r16float: return "r16float";
        case PixelFormat::rg8unorm: return "rg8unorm";
        case PixelFormat::rg8snorm: return "rg8snorm";
        case PixelFormat::rg8uint: return "rg8uint";
        case PixelFormat::rg8sint: return "rg8sint";

        // 32-bit formats
        case PixelFormat::r32uint: return "r32uint";
        case PixelFormat::r32sint: return "r32sint";
        case PixelFormat::r32float: return "r32float";
        case PixelFormat::rg16unorm: return "rg16unorm";
        case PixelFormat::rg16snorm: return "rg16snorm";
        case PixelFormat::rg16uint: return "rg16uint";
        case PixelFormat::rg16sint: return "rg16sint";
        case PixelFormat::rg16float: return "rg16float";
        case PixelFormat::rgba8unorm: return "rgba8unorm";
        case PixelFormat::rgba8unorm_srgb: return "rgba8unorm_srgb";
        case PixelFormat::rgba8snorm: return "rgba8snorm";
        case PixelFormat::rgba8uint: return "rgba8uint";
        case PixelFormat::rgba8sint: return "rgba8sint";
        case PixelFormat::bgra8unorm: return "bgra8unorm";
        case PixelFormat::bgra8unorm_srgb: return "bgra8unorm_srgb";
        // Packed 32-bit formats
        case PixelFormat::rgb9e5ufloat: return "rgb9e5ufloat";
        case PixelFormat::rgb10a2uint: return "rgb10a2uint";
        case PixelFormat::rgb10a2unorm: return "rgb10a2unorm";
        case PixelFormat::rg11b10ufloat: return "rg11b10ufloat";

        // 64-bit formats
        case PixelFormat::rg32uint: return "rg32uint";
        case PixelFormat::rg32sint: return "rg32sint";
        case PixelFormat::rg32float: return "rg32float";
        case PixelFormat::rgba16unorm: return "rgba16unorm";
        case PixelFormat::rgba16snorm: return "rgba16snorm";
        case PixelFormat::rgba16uint: return "rgba16uint";
        case PixelFormat::rgba16sint: return "rgba16sint";
        case PixelFormat::rgba16float: return "rgba16float";

        // 128-bit formats
        case PixelFormat::rgba32uint: return "rgba32uint";
        case PixelFormat::rgba32sint: return "rgba32sint";
        case PixelFormat::rgba32float: return "rgba32float";

        // Depth/stencil formats
        case PixelFormat::stencil8: return "stencil8";
        case PixelFormat::depth16unorm: return "depth16unorm";
        // depth24plus, // no metal compatible
        case PixelFormat::depth24plus_stencil8: return "depth24plus_stencil8";
        case PixelFormat::depth32float: return "depth32float";

        // "depth32float-stencil8" feature
        case PixelFormat::depth32float_stencil8: return "depth32float_stencil8";

        // BC compressed formats usable if "texture-compression-bc" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::bc1_rgba_unorm: return "bc1_rgba_unorm";
        case PixelFormat::bc1_rgba_unorm_srgb: return "bc1_rgba_unorm_srgb";
        case PixelFormat::bc2_rgba_unorm: return "bc2_rgba_unorm";
        case PixelFormat::bc2_rgba_unorm_srgb: return "bc2_rgba_unorm_srgb";
        case PixelFormat::bc3_rgba_unorm: return "bc3_rgba_unorm";
        case PixelFormat::bc3_rgba_unorm_srgb: return "bc3_rgba_unorm_srgb";
        case PixelFormat::bc4_r_unorm: return "bc4_r_unorm";
        case PixelFormat::bc4_r_snorm: return "bc4_r_snorm";
        case PixelFormat::bc5_rg_unorm: return "bc5_rg_unorm";
        case PixelFormat::bc5_rg_snorm: return "bc5_rg_snorm";
        case PixelFormat::bc6h_rgb_ufloat: return "bc6h_rgb_ufloat";
        case PixelFormat::bc6h_rgb_float: return "bc6h_rgb_float";
        case PixelFormat::bc7_rgba_unorm: return "bc7_rgba_unorm";
        case PixelFormat::bc7_rgba_unorm_srgb: return "bc7_rgba_unorm_srgb";

        // ETC2 compressed formats usable if "texture-compression-etc2" is both
        // supported by the device/user agent and enabled in requestDevice.
        case PixelFormat::etc2_rgb8unorm: return "etc2_rgb8unorm";
        case PixelFormat::etc2_rgb8unorm_srgb: return "etc2_rgb8unorm_srgb";
        case PixelFormat::etc2_rgb8a1unorm: return "etc2_rgb8a1unorm";
        case PixelFormat::etc2_rgb8a1unorm_srgb: return "etc2_rgb8a1unorm_srgb";
        // etc2_rgba8unorm, // no metal compatible type
        // etc2_rgba8unorm_srgb, // no metal compatible type
        case PixelFormat::eac_r11unorm: return "eac_r11unorm";
        case PixelFormat::eac_r11snorm: return "eac_r11snorm";
        case PixelFormat::eac_rg11unorm: return "eac_rg11unorm";
        case PixelFormat::eac_rg11snorm: return "eac_rg11snorm";

        // ASTC compressed formats usable if "texture-compression-astc" is both
        // supported by the device/user agent and enabled in requestDevice.
        // astc_4x4_unorm,
        case PixelFormat::astc_4x4_unorm_srgb: return "astc_4x4_unorm_srgb";
        // astc_5x4_unorm,
        case PixelFormat::astc_5x4_unorm_srgb: return "astc_5x4_unorm_srgb";
        // astc_5x5_unorm,
        case PixelFormat::astc_5x5_unorm_srgb: return "astc_5x5_unorm_srgb";
        // astc_6x5_unorm,
        case PixelFormat::astc_6x5_unorm_srgb: return "astc_6x5_unorm_srgb";
        // astc_6x6_unorm,
        case PixelFormat::astc_6x6_unorm_srgb: return "astc_6x6_unorm_srgb";
        // astc_8x5_unorm,
        case PixelFormat::astc_8x5_unorm_srgb: return "astc_8x5_unorm_srgb";
        // astc_8x6_unorm,
        case PixelFormat::astc_8x6_unorm_srgb: return "astc_8x6_unorm_srgb";
        // astc_8x8_unorm,
        case PixelFormat::astc_8x8_unorm_srgb: return "astc_8x8_unorm_srgb";
        // astc_10x5_unorm,
        case PixelFormat::astc_10x5_unorm_srgb: return "astc_10x5_unorm_srgb";
        // astc_10x6_unorm,
        case PixelFormat::astc_10x6_unorm_srgb: return "astc_10x6_unorm_srgb";
        // astc_10x8_unorm,
        case PixelFormat::astc_10x8_unorm_srgb: return "astc_10x8_unorm_srgb";
        // astc_10x10_unorm,
        case PixelFormat::astc_10x10_unorm_srgb: return "astc_10x10_unorm_srgb";
        // astc_12x10_unorm,
        case PixelFormat::astc_12x10_unorm_srgb: return "astc_12x10_unorm_srgb";
        // astc_12x12_unorm,
        case PixelFormat::astc_12x12_unorm_srgb: return "astc_12x12_unorm_srgb";
    }

}

