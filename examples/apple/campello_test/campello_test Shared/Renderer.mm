//
//  Renderer.mm
//  campello_test Shared
//
//  Triangle demo: all GPU work goes through campello_gpu.
//  The only native Metal calls are MTKView drawable access and present.
//

#import "Renderer.h"

#import <campello_gpu/device.hpp>
#import <campello_gpu/command_encoder.hpp>
#import <campello_gpu/render_pass_encoder.hpp>
#import <campello_gpu/texture_view.hpp>
#import <campello_gpu/descriptors/begin_render_pass_descriptor.hpp>
#import <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#import <campello_gpu/descriptors/vertex_descriptor.hpp>
#import <campello_gpu/descriptors/fragment_descriptor.hpp>
#import <campello_gpu/constants/pixel_format.hpp>
#import <campello_gpu/constants/cull_mode.hpp>
#import <campello_gpu/constants/front_face.hpp>
#import <campello_gpu/constants/primitive_topology.hpp>

using namespace systems::leal::campello_gpu;

@implementation Renderer
{
    std::shared_ptr<Device>         _device;
    std::shared_ptr<RenderPipeline> _pipeline;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)view
{
    self = [super init];
    if (!self) return self;

    // Use non-sRGB so the pipeline format matches without gamma correction.
    view.colorPixelFormat        = MTLPixelFormatBGRA8Unorm;
    view.depthStencilPixelFormat = MTLPixelFormatInvalid; // no depth for a 2-D triangle
    view.clearColor              = MTLClearColorMake(0.05, 0.05, 0.05, 1.0);

    _device = Device::createDefaultDevice(nullptr);
    if (!_device) {
        NSLog(@"campello_gpu: failed to create device");
        return self;
    }
    NSLog(@"campello_gpu device: %s  engine: %s",
          _device->getName().c_str(),
          Device::getEngineVersion().c_str());

    // -----------------------------------------------------------------------
    // Shader module — load the compiled default.metallib from the app bundle.
    // Shaders.metal is compiled by Xcode into this file automatically.
    // -----------------------------------------------------------------------
    NSString *libPath = [[NSBundle mainBundle] pathForResource:@"default" ofType:@"metallib"];
    NSData   *libData = [NSData dataWithContentsOfFile:libPath];
    if (!libData) {
        NSLog(@"campello_gpu: could not load default.metallib");
        return self;
    }
    auto shaderModule = _device->createShaderModule(
        reinterpret_cast<const uint8_t *>(libData.bytes),
        static_cast<uint64_t>(libData.length));
    if (!shaderModule) {
        NSLog(@"campello_gpu: createShaderModule failed");
        return self;
    }

    // -----------------------------------------------------------------------
    // Render pipeline
    // Positions and colours are hard-coded in the shader — no vertex buffer.
    // -----------------------------------------------------------------------
    VertexDescriptor vertDesc;
    vertDesc.module     = shaderModule;
    vertDesc.entryPoint = "vertexMain";
    // vertDesc.buffers intentionally left empty (no vertex input)

    FragmentDescriptor fragDesc;
    fragDesc.module     = shaderModule;
    fragDesc.entryPoint = "fragmentMain";
    fragDesc.targets    = {{ PixelFormat::bgra8unorm, ColorWrite::all }};

    RenderPipelineDescriptor pipeDesc;
    pipeDesc.vertex   = vertDesc;
    pipeDesc.fragment = fragDesc;
    pipeDesc.topology = PrimitiveTopology::triangleList;
    pipeDesc.cullMode = CullMode::none;
    pipeDesc.frontFace = FrontFace::ccw;

    _pipeline = _device->createRenderPipeline(pipeDesc);
    if (!_pipeline) {
        NSLog(@"campello_gpu: createRenderPipeline failed");
    }

    return self;
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

- (void)drawInMTKView:(nonnull MTKView *)view
{
    if (!_device || !_pipeline) return;

    // The drawable and its texture come from MTKView (unavoidable native call).
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!drawable) return;

    // Bridge the drawable's MTLTexture into a campello_gpu TextureView.
    // fromNative() retains the texture; the TextureView releases it on destruction.
    auto colorView = TextureView::fromNative((__bridge void *)drawable.texture);

    // Build the render pass descriptor.
    ColorAttachment colorAttach;
    colorAttach.clearValue[0] = 0.05f;
    colorAttach.clearValue[1] = 0.05f;
    colorAttach.clearValue[2] = 0.05f;
    colorAttach.clearValue[3] = 1.00f;
    colorAttach.loadOp        = LoadOp::clear;
    colorAttach.storeOp       = StoreOp::store;
    colorAttach.view          = colorView;

    BeginRenderPassDescriptor rpDesc;
    rpDesc.colorAttachments = { colorAttach };

    // Record commands via campello_gpu.
    auto encoder   = _device->createCommandEncoder();
    auto renderEnc = encoder->beginRenderPass(rpDesc);
    renderEnc->setPipeline(_pipeline);
    renderEnc->draw(3, 1, 0, 0);   // 3 vertices, 1 instance
    renderEnc->end();

    auto cmdBuffer = encoder->finish();

    // Submit (commits the underlying MTL command buffer).
    _device->submit(cmdBuffer);

    // Present the drawable (safe to call after commit for display scheduling).
    [drawable present];
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    // No projection matrix needed for a 2-D triangle.
    (void)view; (void)size;
}

@end
