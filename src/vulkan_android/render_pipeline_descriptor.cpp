#include <campello_gpu/render_pipeline_descriptor.hpp>

using namespace systems::leal::campello_gpu;

RenderPipelineDescriptor::RenderPipelineDescriptor() {
    this->fragment = {};
    this->cullMode = CullMode::none;
    this->frontFace = FrontFace::ccw;
    this->topology = PrimitiveTopology::triangleStrip;
    this->vertex = VertexDescriptor();
    this->depthStencil = {};
}
