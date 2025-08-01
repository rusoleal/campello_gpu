#pragma once

namespace systems::leal::campello_gpu
{

    enum class TextureUsage
    {

        /**
         * (WebGPU)
         * The texture can be used as the source of a copy operation, for example the source
         * argument of a copyTextureToBuffer() call.
         */
        copySrc = 0x01,

        /**
         * (WebGPU)
         * The texture can be used as the destination of a copy/write operation, for example the
         * destination argument of a copyBufferToTexture() call.
         */
        copyDst = 0x02,

        /**
         * The texture can be bound for use as a sampled texture in a shader, for example as a 
         * resource in a bind group entry when creating a GPUBindGroup (via createBindGroup()), 
         * which adheres to a GPUBindGroupLayout entry with a specified texture binding layout.
         */
        textureBinding = 0x04,

        /**
         * The texture can be bound for use as a storage texture in a shader, for example as a 
         * resource in a bind group entry when creating a GPUBindGroup (via createBindGroup()), 
         * which adheres to a GPUBindGroupLayout entry with a specified storage texture binding 
         * layout.
         */
        storageBinding = 0x08,

        /**
         * (WebGPU)
         * The texture can be used as a color or depth/stencil attachment in a render pass,
         * for example as the view property of the descriptor object in a beginRenderPass() call.
         */
        renderTarget = 0x10,
    };

}