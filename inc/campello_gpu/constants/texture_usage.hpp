#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief Bitmask flags describing how a `Texture` may be used.
     *
     * Declare all intended usages at texture creation time by OR-ing the relevant
     * flags together. The runtime rejects texture views used in ways not declared here.
     *
     * @code
     * // A texture that can receive GPU writes and then be sampled in a shader:
     * TextureDescriptor td{};
     * td.usage = TextureUsage::copyDst | TextureUsage::textureBinding;
     * @endcode
     */
    enum class TextureUsage
    {
        /**
         * @brief Texture can be the *source* of a copy operation.
         *
         * For example, as the source argument of a `copyTextureToBuffer()` call.
         */
        copySrc = 0x01,

        /**
         * @brief Texture can be the *destination* of a copy or write operation.
         *
         * For example, as the destination argument of a `copyBufferToTexture()` call.
         */
        copyDst = 0x02,

        /**
         * @brief Texture can be sampled by a shader.
         *
         * Required when binding the texture in a bind group entry whose layout declares
         * a texture binding.
         */
        textureBinding = 0x04,

        /**
         * @brief Texture can be used as a storage texture (read/write from shader).
         *
         * Required when binding the texture in a bind group entry whose layout declares
         * a storage texture binding.
         */
        storageBinding = 0x08,

        /**
         * @brief Texture can be used as a color or depth/stencil render attachment.
         *
         * Required when passing the texture's view as an attachment in
         * `BeginRenderPassDescriptor`.
         */
        renderTarget = 0x10,
    };

}
