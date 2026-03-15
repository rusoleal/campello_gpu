#pragma once

namespace systems::leal::campello_gpu
{

    /**
     * @brief A compiled, immutable render pipeline state object.
     *
     * A `RenderPipeline` bundles the vertex shader, fragment shader, vertex input
     * layout, color attachment formats, depth/stencil configuration, primitive topology,
     * and rasterization state into a single pre-compiled GPU object.
     *
     * Pipelines are created by `Device::createRenderPipeline()` and are bound during a
     * render pass with `RenderPassEncoder::setPipeline()`. Creating pipelines up front
     * (at load time) avoids GPU stalls during rendering.
     *
     * @code
     * RenderPipelineDescriptor desc;
     * desc.vertex   = { shaderModule, "vertexMain",   { layout } };
     * desc.fragment = { shaderModule, "fragmentMain", { { PixelFormat::bgra8unorm, ColorWrite::all } } };
     * desc.topology = PrimitiveTopology::triangleList;
     * desc.cullMode = CullMode::back;
     * auto pipeline = device->createRenderPipeline(desc);
     * @endcode
     */
    class RenderPipeline
    {
    private:
        friend class Device;
        friend class RenderPassEncoder;
        void *native;

        RenderPipeline(void *data);

    public:
        ~RenderPipeline();
    };

}
