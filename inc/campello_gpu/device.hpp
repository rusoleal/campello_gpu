#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>
#include <campello_gpu/texture.hpp>
#include <campello_gpu/buffer.hpp>
#include <campello_gpu/render_pipeline.hpp>
#include <campello_gpu/compute_pipeline.hpp>
#include <campello_gpu/shader_module.hpp>
#include <campello_gpu/bind_group_layout.hpp>
#include <campello_gpu/pipeline_layout.hpp>
#include <campello_gpu/sampler.hpp>
#include <campello_gpu/query_set.hpp>
#include <campello_gpu/command_encoder.hpp>
#include <campello_gpu/descriptors/render_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/compute_pipeline_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/storage_mode.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/feature.hpp>

namespace systems::leal::campello_gpu
{

    class Adapter;

    class Device
    {
    private:
        void *native;

        Device(void *data);

    public:
        ~Device();

        //void *getNative();

        std::string getName();
        std::set<Feature> getFeatures();

        std::shared_ptr<Texture> createTexture(TextureType type, PixelFormat pixelFormat, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, uint32_t samples, TextureUsage usageMode);
        //std::shared_ptr<Texture> createTexture(TextureType type, PixelFormat pixelFormat, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevels, uint32_t samples, TextureUsage usageMode, void *data);

        std::shared_ptr<Buffer> createBuffer(uint64_t size, BufferUsage usage);
        std::shared_ptr<Buffer> createBuffer(uint64_t size, BufferUsage usage, void *data);
        std::shared_ptr<ShaderModule> createShaderModule(const uint8_t *buffer, uint64_t size);
        std::shared_ptr<RenderPipeline> createRenderPipeline(const RenderPipelineDescriptor &descriptor);
        std::shared_ptr<ComputePipeline> createComputePipeline(const ComputePipelineDescriptor &descriptor);
        std::shared_ptr<BindGroupLayout> createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor);
        std::shared_ptr<BindGroup> createBindGroup(const BindGroupDescriptor &descriptor);
        std::shared_ptr<PipelineLayout> createPipelineLayout(const PipelineLayoutDescriptor &descriptor);
        std::shared_ptr<Sampler> createSampler(const SamplerDescriptor &descriptor);
        std::shared_ptr<QuerySet> createQuerySet(const QuerySetDescriptor &descriptor);
        std::shared_ptr<CommandEncoder> createCommandEncoder();

        static std::shared_ptr<Device> createDefaultDevice(void *pd);
        static std::shared_ptr<Device> createDevice(std::shared_ptr<Adapter> adapter, void *pd);
        static std::vector<std::shared_ptr<Adapter>> getAdapters();

        static std::string getEngineVersion();
    };

    std::string getVersion();

}
