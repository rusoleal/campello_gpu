#pragma once

#include <cstdint>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>

namespace systems::leal::campello_gpu
{
    /// Maximum planes supported by DmaBufTextureDescriptor. Matches the
    /// practical ceiling used by the Wayland linux-dmabuf protocol and the
    /// DRM_FORMAT_MOD_* modifier family.
    constexpr uint32_t kMaxDmaBufPlanes = 4;

    /**
     * @brief One memory plane of a dma-buf-backed image.
     *
     * A plane is a contiguous byte range within `fd`, described by its
     * offset and row pitch. Most buffers (plain RGBA8, linear or
     * single-plane tiled) have exactly one plane; multi-planar formats
     * (e.g. YUV) or some compressed tiling layouts use more.
     */
    struct DmaBufPlane
    {
        int      fd     = -1; ///< dma-buf file descriptor for this plane.
        uint32_t offset = 0;  ///< Byte offset of this plane's data within `fd`.
        uint32_t stride = 0;  ///< Row pitch in bytes.
    };

    /**
     * @brief Describes an externally-allocated dma-buf to import as a
     * read-only `Texture`, without copying.
     *
     * Intended for compositing a Wayland client's buffer (as handed to a
     * compositor by e.g. wlroots) directly, with no GPU-side copy.
     *
     * `campello_gpu` never takes ownership of any plane's file descriptor:
     * the caller retains ownership and may close it immediately after
     * `Device::createTextureFromDmaBuf()` returns, whether it succeeds or
     * fails — the Vulkan driver dups its own reference to the underlying
     * dma_buf during import.
     *
     * Only single-plane, non-disjoint formats are currently supported —
     * `planeCount` must be 1. Multi-planar formats (e.g. YUV) are not yet
     * implemented; `Device::createTextureFromDmaBuf()` returns `nullptr`
     * for any other `planeCount`.
     */
    struct DmaBufTextureDescriptor
    {
        uint32_t    width  = 0;
        uint32_t    height = 0;
        PixelFormat format = PixelFormat::bgra8unorm;

        /// DRM format modifier describing the buffer's tiling layout
        /// (`DRM_FORMAT_MOD_LINEAR` == 0, or a vendor-specific tiled/
        /// compressed modifier, as reported alongside the buffer by
        /// linux-dmabuf/wlroots).
        uint64_t drmFormatModifier = 0;

        uint32_t    planeCount = 1;
        DmaBufPlane planes[kMaxDmaBufPlanes];

        /// How the resulting Texture will be used. Compositing only ever
        /// needs to sample it, so this defaults to `textureBinding` —
        /// override only if a specific consumer genuinely needs more
        /// (e.g. storage access), since not every usage combination is
        /// guaranteed importable for every modifier.
        TextureUsage usage = TextureUsage::textureBinding;
    };
}
