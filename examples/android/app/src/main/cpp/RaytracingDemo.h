#pragma once

//
//  RaytracingDemo.h
//
//  Minimal campello_gpu ray tracing demo for Android (Vulkan backend).
//
//  Usage:
//    RaytracingDemo demo;
//    if (demo.setup(device)) {
//        // Each frame:
//        demo.render(encoder, outputTexture);
//    }
//

#include <memory>
#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/acceleration_structure.hpp>
#include <campello_gpu/ray_tracing_pipeline.hpp>
#include <campello_gpu/descriptors/bottom_level_acceleration_structure_descriptor.hpp>
#include <campello_gpu/descriptors/top_level_acceleration_structure_descriptor.hpp>

namespace systems::leal::campello_gpu { class CommandEncoder; }

class RaytracingDemo {
public:
    RaytracingDemo() = default;
    ~RaytracingDemo() = default;

    /// Allocate GPU resources and build acceleration structures.
    /// Returns false if Feature::raytracing is not available.
    bool setup(std::shared_ptr<systems::leal::campello_gpu::Device> device);

    /// Trace rays into @p outputTexture (rgba32float, StorageBinding usage).
    /// Must be called after a successful setup().
    void render(std::shared_ptr<systems::leal::campello_gpu::CommandEncoder> encoder,
                std::shared_ptr<systems::leal::campello_gpu::Texture> outputTexture);

    bool isReady() const { return _ready; }

private:
    std::shared_ptr<systems::leal::campello_gpu::Device>               _device;
    std::shared_ptr<systems::leal::campello_gpu::AccelerationStructure> _tlas;
    std::shared_ptr<systems::leal::campello_gpu::RayTracingPipeline>   _pipeline;
    std::shared_ptr<systems::leal::campello_gpu::BindGroupLayout>      _bindGroupLayout;
    std::shared_ptr<systems::leal::campello_gpu::PipelineLayout>       _pipelineLayout;
    bool _ready = false;
};
