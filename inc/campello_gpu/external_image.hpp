#pragma once

#include <cstdint>

namespace systems::leal::campello_gpu
{
    /**
     * @brief The kind of platform image an `ExternalImage` refers to.
     *
     * Each kind exists on one platform; `Device::supportsExternalImage()`
     * says which ones this device can import.
     */
    enum class ExternalImageType
    {
        /// `AHardwareBuffer*` (Android): camera frames (ARCore,
        /// `AImageReader`), `MediaCodec` output through an `AImageReader`.
        /// Must have been allocated with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`.
        androidHardwareBuffer,

        /// `CVPixelBufferRef` (macOS, iOS), IOSurface-backed: AVFoundation
        /// and VideoToolbox output created with
        /// `kCVPixelBufferMetalCompatibilityKey`. Pixel formats: `'420v'`,
        /// `'420f'` (NV12), `'y420'`, `'f420'` (3 planes) and `'BGRA'`.
        cvPixelBuffer,

        /// `ID3D12Resource*` on this device (Windows). Not implemented yet.
        d3d12Resource,

        /// NT `HANDLE` of a shared D3D11 texture (Windows, Media Foundation).
        /// Not implemented yet.
        d3dSharedHandle,

        /// `const DmaBufTextureDescriptor*` (Linux). Not implemented yet.
        dmaBuf,

        /// Web: a handle from `Module.campelloGpu.importExternalImage(source)`,
        /// where `source` is anything `GPUQueue.copyExternalImageToTexture()`
        /// accepts: a `VideoFrame` (WebCodecs), an `HTMLVideoElement`, an
        /// `ImageBitmap`, a canvas. A handle is used by one copy call, which
        /// releases it (`Module.campelloGpu.releaseExternalImage(handle)`
        /// releases one that is never copied). The caller still owns
        /// `source` (e.g. closes the `VideoFrame`) and may do so as soon as
        /// the copy call returns.
        webImageSource,
    };

    /// YCbCr to RGB matrix of a YUV source.
    enum class YCbCrMatrix
    {
        fromSource, ///< The image's metadata, else BT.709 from 720 lines up and BT.601 below.
        bt601,
        bt709,
    };

    /// Value range of a YUV source.
    enum class YCbCrRange
    {
        fromSource, ///< The image's metadata, else limited ("video") range.
        full,       ///< Y and CbCr use 0..255.
        limited,    ///< Y uses 16..235, CbCr 16..240.
    };

    /**
     * @brief A platform image (video or camera frame) to copy into a texture
     * with `copyExternalImageToTexture()`, without a CPU copy.
     *
     * The caller keeps ownership of `handle`: it may release the image as
     * soon as the copy call returns, since the backend holds its own
     * reference until the GPU is done with it.
     */
    struct ExternalImage
    {
        ExternalImageType type   = ExternalImageType::cvPixelBuffer;
        void             *handle = nullptr;

        /// Override the color settings when the image's metadata is missing
        /// or wrong. Ignored for RGB sources (and on the web, where the
        /// browser converts).
        YCbCrMatrix matrix = YCbCrMatrix::fromSource;
        YCbCrRange  range  = YCbCrRange::fromSource;

        /// Producer-side GPU sync, when the source comes with one.
        /// Android: a sync_file fd the copy waits for (e.g. from
        /// `AImageReader_acquireNextImageAsync()`); campello_gpu takes
        /// ownership of it. -1: the image is ready.
        int acquireFenceFd = -1;
    };

    /// Options of `copyExternalImageToTexture()`.
    struct ExternalImageCopyOptions
    {
        /**
         * Row-major 2×3 affine map from target uv to source uv (both in
         * [0, 1]², origin top-left): target pixel uv samples the source at
         * `(t[0]·u + t[1]·v + t[2], t[3]·u + t[4]·v + t[5])`. Covers crop,
         * scale, rotation and mirroring; points outside the source clamp to
         * its edge. nullptr: identity (the whole source, stretched to the
         * whole target).
         */
        const float *uvTransform = nullptr;
    };

}
