#pragma once

#include <stdint.h>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/aspect.hpp>
#include <campello_gpu/texture_view.hpp>

namespace systems::leal::campello_gpu
{
    class Device;

    /**
     * @brief A GPU texture resource (1D, 2D, or 3D image).
     *
     * Textures store image data on the GPU. Their format, dimensions, mip-level count,
     * sample count, and allowed usages are fixed at creation time.
     *
     * Textures are created exclusively by `Device::createTexture()`. To use a texture as
     * a render-pass attachment or shader input, obtain a `TextureView` via `createView()`.
     *
     * @code
     * auto tex = device->createTexture(
     *     TextureType::tt2d, PixelFormat::rgba8unorm,
     *     512, 512, 1, 1, 1,
     *     TextureUsage::textureBinding | TextureUsage::copyDst);
     *
     * tex->upload(0, dataBytes, pixelData);
     * auto view = tex->createView(PixelFormat::rgba8unorm);
     * @endcode
     */
    class Texture
    {
    private:
        friend class Device;
        friend class RenderPassEncoder;
        friend class ComputePassEncoder;
        friend class RayTracingPassEncoder;
        friend class CommandEncoder;
        void *native;

        Texture(void *pd);

    public:
        ~Texture();

        /**
         * @brief Creates a `TextureView` that exposes (part of) this texture to the pipeline.
         *
         * A view is required wherever a texture is bound — render-pass color/depth
         * attachments, shader `texture2d` bindings, etc.
         *
         * @param format         Pixel format of the view. Pass the texture's own format
         *                       unless you need a reinterpretation.
         * @param arrayLayerCount Number of array layers to include. Pass -1 (default) to
         *                       include all remaining layers starting from `baseArrayLayer`.
         * @param aspect         Which aspect(s) to include (color, depth, stencil, or all).
         * @param baseArrayLayer First array layer visible through this view.
         * @param baseMipLevel   Most-detailed mip level visible through this view.
         * @param dimension      Logical dimensionality of the view (default: `tt2d`).
         * @return A new `TextureView` on success, or `nullptr` on failure.
         */
        std::shared_ptr<TextureView> createView(
            PixelFormat format,
            uint32_t    arrayLayerCount = -1,
            Aspect      aspect          = Aspect::all,
            uint32_t    baseArrayLayer  = 0,
            uint32_t    baseMipLevel    = 0,
            TextureType dimension       = TextureType::tt2d);

        /**
         * @brief Uploads CPU pixel data into the texture.
         *
         * Copies `length` bytes from `data` into the texture starting at byte `offset`
         * within the texture's backing storage. For most use-cases `offset` is 0 and
         * `length` equals the full texture size.
         *
         * @param offset Byte offset within the texture storage.
         * @param length Number of bytes to write.
         * @param data   Pointer to the source pixel data.
         * @return `true` on success, `false` otherwise.
         */
        bool upload(uint64_t offset, uint64_t length, void *data);

        /**
         * @brief Downloads texture pixel data to CPU memory.
         *
         * This is a convenience method that copies texture data to a temporary buffer,
         * submits the command, waits for completion, and copies the data to CPU memory.
         * For more control over the copy process (e.g., async readback), use
         * `CommandEncoder::copyTextureToBuffer()` directly.
         *
         * **Note:** This method creates a temporary buffer, records a command, submits it,
         * and waits for GPU completion. It is synchronous and may stall the pipeline.
         *
         * @param mipLevel   Mip level to download.
         * @param arrayLayer Array layer (for array textures) or depth slice (for 3D) to download.
         * @param data       Pointer to the destination CPU memory.
         * @param length     Number of bytes to read (must match the texture subresource size).
         * @return `true` on success, `false` otherwise.
         */
        bool download(uint32_t mipLevel, uint32_t arrayLayer, void *data, uint64_t length);

        /** @brief Returns the number of depth slices or array layers. */
        uint32_t    getDepthOrarrayLayers();
        /** @brief Returns the dimensionality of the texture (1D, 2D, or 3D). */
        TextureType getDimension();
        /** @brief Returns the pixel format of the texture. */
        PixelFormat getFormat();
        /** @brief Returns the width of the texture in texels. */
        uint32_t    getWidth();
        /** @brief Returns the height of the texture in texels. */
        uint32_t    getHeight();
        /** @brief Returns the number of mip-map levels. */
        uint32_t    getMipLevelCount();
        /** @brief Returns the MSAA sample count (1 = no multisampling). */
        uint32_t    getSampleCount();
        /** @brief Returns the usage flags the texture was created with. */
        TextureUsage getUsage();
    };
}
