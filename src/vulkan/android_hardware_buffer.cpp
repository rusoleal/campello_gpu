// Device::copyAHardwareBufferToTexture(): AHardwareBuffer import with sampler
// YCbCr conversion (Android). See the method's doc comment in device.hpp.
//
// Everything here uses its own command pool, descriptor pool and fence, never
// the frame ring, so it can run on any thread while another records a frame;
// only the queue submission takes gpu_mutex. One call at a time (converter
// mutex).

#include <campello_gpu/device.hpp>
#include <campello_gpu/texture.hpp>

#include "common.hpp"
#include "shaders/ahb_copy_spirv.hpp"
#include "texture_handle.hpp"

#include <android/hardware_buffer.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

VkFormat pixelFormatToNative(systems::leal::campello_gpu::PixelFormat format);

namespace systems::leal::campello_gpu {

    namespace {
        constexpr size_t kMaxImports = 8;  // camera pipelines cycle a few buffers
        constexpr uint32_t kMaxSets = 16;
    }

    // A YCbCr conversion and the pipeline that samples through it: one per
    // distinct (buffer format, conversion parameters, target format).
    struct AhbPipeline {
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint64_t externalFormat = 0;
        VkSamplerYcbcrModelConversion model{};
        VkSamplerYcbcrRange range{};
        VkComponentMapping components{};
        VkChromaLocation xOffset{}, yOffset{};
        VkFilter filter = VK_FILTER_NEAREST;
        VkFormat targetFormat = VK_FORMAT_UNDEFINED;

        VkSamplerYcbcrConversion conversion = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    struct AhbImport {
        AHardwareBuffer *buffer = nullptr;  // acquired while imported
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        AhbPipeline *pipeline = nullptr;
        VkFormat targetFormat = VK_FORMAT_UNDEFINED;
        uint32_t width = 0, height = 0;
        uint64_t lastUse = 0;
    };

    struct AhbConverter {
        std::mutex mutex;
        VkDevice device = VK_NULL_HANDLE;
        PFN_vkGetAndroidHardwareBufferPropertiesANDROID getProperties = nullptr;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkShaderModule vertex = VK_NULL_HANDLE;
        VkShaderModule fragment = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<AhbPipeline>> pipelines;
        std::vector<AhbImport> imports;
        uint64_t clock = 0;
    };

    namespace {

        void destroyImport(AhbConverter &c, AhbImport &import) {
            if (import.set) vkFreeDescriptorSets(c.device, c.descriptorPool, 1, &import.set);
            if (import.view) vkDestroyImageView(c.device, import.view, nullptr);
            if (import.image) vkDestroyImage(c.device, import.image, nullptr);
            if (import.memory) vkFreeMemory(c.device, import.memory, nullptr);
            if (import.buffer) AHardwareBuffer_release(import.buffer);
            import = {};
        }

        void destroyPipeline(VkDevice device, AhbPipeline &p) {
            if (p.pipeline) vkDestroyPipeline(device, p.pipeline, nullptr);
            if (p.renderPass) vkDestroyRenderPass(device, p.renderPass, nullptr);
            if (p.layout) vkDestroyPipelineLayout(device, p.layout, nullptr);
            if (p.setLayout) vkDestroyDescriptorSetLayout(device, p.setLayout, nullptr);
            if (p.sampler) vkDestroySampler(device, p.sampler, nullptr);
            if (p.conversion) vkDestroySamplerYcbcrConversion(device, p.conversion, nullptr);
        }

        VkShaderModule shaderModule(VkDevice device, const uint32_t *code, size_t bytes) {
            VkShaderModuleCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            info.codeSize = bytes;
            info.pCode = code;
            VkShaderModule module = VK_NULL_HANDLE;
            vkCreateShaderModule(device, &info, nullptr, &module);
            return module;
        }

        AhbConverter *converterFor(DeviceData *d) {
            if (d->ahbConverter) return d->ahbConverter;
            auto c = std::make_unique<AhbConverter>();
            c->device = d->device;
            c->getProperties = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(
                d->device, "vkGetAndroidHardwareBufferPropertiesANDROID");

            VkCommandPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = d->queueFamilyIndex;
            VkCommandBufferAllocateInfo cbInfo{};
            cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbInfo.commandBufferCount = 1;
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            // Combined image samplers with YCbCr conversion may take several
            // descriptors each (multi-planar formats): be generous.
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxSets * 4};
            VkDescriptorPoolCreateInfo dpInfo{};
            dpInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            dpInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            dpInfo.maxSets = kMaxSets;
            dpInfo.poolSizeCount = 1;
            dpInfo.pPoolSizes = &size;

            bool ok = c->getProperties != nullptr &&
                      vkCreateCommandPool(d->device, &poolInfo, nullptr, &c->commandPool) == VK_SUCCESS;
            if (ok) {
                cbInfo.commandPool = c->commandPool;
                ok = vkAllocateCommandBuffers(d->device, &cbInfo, &c->commandBuffer) == VK_SUCCESS &&
                     vkCreateFence(d->device, &fenceInfo, nullptr, &c->fence) == VK_SUCCESS &&
                     vkCreateDescriptorPool(d->device, &dpInfo, nullptr, &c->descriptorPool) == VK_SUCCESS;
            }
            if (ok) {
                c->vertex = shaderModule(d->device, shaders::kAhbCopyVert, sizeof(shaders::kAhbCopyVert));
                c->fragment = shaderModule(d->device, shaders::kAhbCopyFrag, sizeof(shaders::kAhbCopyFrag));
                ok = c->vertex && c->fragment;
            }
            d->ahbConverter = c.release();
            if (!ok) LOG_ERROR("AHardwareBuffer import: converter setup failed");
            return d->ahbConverter;
        }

        AhbPipeline *pipelineFor(AhbConverter &c, const VkAndroidHardwareBufferFormatPropertiesANDROID &f,
                                 VkFormat targetFormat) {
            const VkFilter filter =
                (f.formatFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT)
                    ? VK_FILTER_LINEAR
                    : VK_FILTER_NEAREST;
            const uint64_t external = f.format == VK_FORMAT_UNDEFINED ? f.externalFormat : 0;
            for (auto &p : c.pipelines) {
                if (p->format == f.format && p->externalFormat == external && p->model == f.suggestedYcbcrModel &&
                    p->range == f.suggestedYcbcrRange &&
                    std::memcmp(&p->components, &f.samplerYcbcrConversionComponents, sizeof(VkComponentMapping)) ==
                        0 &&
                    p->xOffset == f.suggestedXChromaOffset && p->yOffset == f.suggestedYChromaOffset &&
                    p->filter == filter && p->targetFormat == targetFormat) {
                    return p.get();
                }
            }
            auto p = std::make_unique<AhbPipeline>();
            p->format = f.format;
            p->externalFormat = external;
            p->model = f.suggestedYcbcrModel;
            p->range = f.suggestedYcbcrRange;
            p->components = f.samplerYcbcrConversionComponents;
            p->xOffset = f.suggestedXChromaOffset;
            p->yOffset = f.suggestedYChromaOffset;
            p->filter = filter;
            p->targetFormat = targetFormat;

            VkExternalFormatANDROID externalInfo{};
            externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
            externalInfo.externalFormat = external;
            VkSamplerYcbcrConversionCreateInfo conv{};
            conv.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO;
            conv.pNext = external ? &externalInfo : nullptr;
            conv.format = f.format;
            conv.ycbcrModel = f.suggestedYcbcrModel;
            conv.ycbcrRange = f.suggestedYcbcrRange;
            conv.components = f.samplerYcbcrConversionComponents;
            conv.xChromaOffset = f.suggestedXChromaOffset;
            conv.yChromaOffset = f.suggestedYChromaOffset;
            conv.chromaFilter = filter;
            conv.forceExplicitReconstruction = VK_FALSE;
            bool ok = vkCreateSamplerYcbcrConversion(c.device, &conv, nullptr, &p->conversion) == VK_SUCCESS;

            VkSamplerYcbcrConversionInfo convInfo{};
            convInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
            convInfo.conversion = p->conversion;
            VkSamplerCreateInfo sampler{};
            sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.pNext = &convInfo;
            sampler.magFilter = filter;  // must match the conversion's chroma filter
            sampler.minFilter = filter;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sampler.maxLod = 0.0f;
            sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
            ok = ok && vkCreateSampler(c.device, &sampler, nullptr, &p->sampler) == VK_SUCCESS;

            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            binding.pImmutableSamplers = &p->sampler;  // required with YCbCr conversion
            VkDescriptorSetLayoutCreateInfo setInfo{};
            setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            setInfo.bindingCount = 1;
            setInfo.pBindings = &binding;
            ok = ok && vkCreateDescriptorSetLayout(c.device, &setInfo, nullptr, &p->setLayout) == VK_SUCCESS;

            VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8 * sizeof(float)};
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &p->setLayout;
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &push;
            ok = ok && vkCreatePipelineLayout(c.device, &layoutInfo, nullptr, &p->layout) == VK_SUCCESS;

            // The whole target is overwritten: no load; left ready for sampling.
            VkAttachmentDescription attachment{};
            attachment.format = targetFormat;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &colorRef;
            VkSubpassDependency deps[2]{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;  // earlier sampling of the target
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].srcSubpass = 0;  // later sampling of the target
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkRenderPassCreateInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &attachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 2;
            rpInfo.pDependencies = deps;
            ok = ok && vkCreateRenderPass(c.device, &rpInfo, nullptr, &p->renderPass) == VK_SUCCESS;

            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = c.vertex;
            stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = c.fragment;
            stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo vertexInput{};
            vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = 1;
            viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.cullMode = VK_CULL_MODE_NONE;
            raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            raster.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo multisample{};
            multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;
            const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = dynamicStates;
            VkGraphicsPipelineCreateInfo pipeInfo{};
            pipeInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeInfo.stageCount = 2;
            pipeInfo.pStages = stages;
            pipeInfo.pVertexInputState = &vertexInput;
            pipeInfo.pInputAssemblyState = &assembly;
            pipeInfo.pViewportState = &viewport;
            pipeInfo.pRasterizationState = &raster;
            pipeInfo.pMultisampleState = &multisample;
            pipeInfo.pColorBlendState = &blend;
            pipeInfo.pDynamicState = &dynamic;
            pipeInfo.layout = p->layout;
            pipeInfo.renderPass = p->renderPass;
            ok = ok && vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &p->pipeline) ==
                           VK_SUCCESS;
            if (!ok) {
                destroyPipeline(c.device, *p);
                LOG_ERROR("AHardwareBuffer import: pipeline creation failed");
                return nullptr;
            }
            c.pipelines.push_back(std::move(p));
            return c.pipelines.back().get();
        }

        AhbImport *importFor(AhbConverter &c, DeviceData *d, AHardwareBuffer *buffer, VkFormat targetFormat) {
            for (auto &i : c.imports) {
                if (i.buffer == buffer && i.targetFormat == targetFormat) {
                    i.lastUse = ++c.clock;
                    return &i;
                }
            }
            if (c.imports.size() >= kMaxImports) {  // least recently used
                auto oldest = std::min_element(c.imports.begin(), c.imports.end(),
                                               [](const AhbImport &a, const AhbImport &b) { return a.lastUse < b.lastUse; });
                destroyImport(c, *oldest);
                c.imports.erase(oldest);
            }

            AHardwareBuffer_Desc desc{};
            AHardwareBuffer_describe(buffer, &desc);
            VkAndroidHardwareBufferFormatPropertiesANDROID format{};
            format.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID;
            VkAndroidHardwareBufferPropertiesANDROID props{};
            props.sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID;
            props.pNext = &format;
            if (c.getProperties(c.device, buffer, &props) != VK_SUCCESS) {
                LOG_ERROR("AHardwareBuffer import: vkGetAndroidHardwareBufferPropertiesANDROID failed");
                return nullptr;
            }
            AhbPipeline *pipeline = pipelineFor(c, format, targetFormat);
            if (!pipeline) return nullptr;

            AhbImport i;
            i.pipeline = pipeline;
            i.targetFormat = targetFormat;
            i.width = desc.width;
            i.height = desc.height;

            VkExternalFormatANDROID externalInfo{};
            externalInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID;
            externalInfo.externalFormat = pipeline->externalFormat;
            VkExternalMemoryImageCreateInfo externalImage{};
            externalImage.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            externalImage.pNext = &externalInfo;
            externalImage.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.pNext = &externalImage;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format.format;  // VK_FORMAT_UNDEFINED for external formats
            imageInfo.extent = {desc.width, desc.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            bool ok = vkCreateImage(c.device, &imageInfo, nullptr, &i.image) == VK_SUCCESS;

            VkImportAndroidHardwareBufferInfoANDROID importInfo{};
            importInfo.sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID;
            importInfo.buffer = buffer;
            VkMemoryDedicatedAllocateInfo dedicated{};
            dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicated.pNext = &importInfo;
            dedicated.image = i.image;
            VkMemoryAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.pNext = &dedicated;
            alloc.allocationSize = props.allocationSize;
            alloc.memoryTypeIndex = 0;
            while (alloc.memoryTypeIndex < 32 && !(props.memoryTypeBits & (1u << alloc.memoryTypeIndex))) {
                ++alloc.memoryTypeIndex;
            }
            ok = ok && alloc.memoryTypeIndex < 32 &&
                 vkAllocateMemory(c.device, &alloc, nullptr, &i.memory) == VK_SUCCESS &&
                 vkBindImageMemory(c.device, i.image, i.memory, 0) == VK_SUCCESS;

            VkSamplerYcbcrConversionInfo convInfo{};
            convInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO;
            convInfo.conversion = pipeline->conversion;
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.pNext = &convInfo;
            viewInfo.image = i.image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format.format;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            ok = ok && vkCreateImageView(c.device, &viewInfo, nullptr, &i.view) == VK_SUCCESS;

            VkDescriptorSetAllocateInfo setAlloc{};
            setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            setAlloc.descriptorPool = c.descriptorPool;
            setAlloc.descriptorSetCount = 1;
            setAlloc.pSetLayouts = &pipeline->setLayout;
            ok = ok && vkAllocateDescriptorSets(c.device, &setAlloc, &i.set) == VK_SUCCESS;
            if (ok) {
                VkDescriptorImageInfo imageDesc{pipeline->sampler, i.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = i.set;
                write.dstBinding = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imageDesc;
                vkUpdateDescriptorSets(c.device, 1, &write, 0, nullptr);
            }
            if (!ok) {
                destroyImport(c, i);
                LOG_ERROR("AHardwareBuffer import failed (%ux%u, format %u)", desc.width, desc.height, desc.format);
                return nullptr;
            }
            AHardwareBuffer_acquire(buffer);  // kept alive while imported
            i.buffer = buffer;
            i.lastUse = ++c.clock;
            c.imports.push_back(i);
            (void)d;
            return &c.imports.back();
        }

    }

    void destroyAhbConverter(DeviceData *d) {
        AhbConverter *c = d->ahbConverter;
        if (!c) return;
        vkDeviceWaitIdle(d->device);
        for (auto &i : c->imports) destroyImport(*c, i);
        for (auto &p : c->pipelines) destroyPipeline(c->device, *p);
        if (c->vertex) vkDestroyShaderModule(c->device, c->vertex, nullptr);
        if (c->fragment) vkDestroyShaderModule(c->device, c->fragment, nullptr);
        if (c->descriptorPool) vkDestroyDescriptorPool(c->device, c->descriptorPool, nullptr);
        if (c->fence) vkDestroyFence(c->device, c->fence, nullptr);
        if (c->commandPool) vkDestroyCommandPool(c->device, c->commandPool, nullptr);
        delete c;
        d->ahbConverter = nullptr;
    }

    bool Device::supportsAHardwareBufferImport() const {
        return ((DeviceData *)native)->ahbImportEnabled;
    }

    bool Device::copyAHardwareBufferToTexture(AHardwareBuffer *source, std::shared_ptr<Texture> target,
                                              const float uvTransform[6]) {
        auto *d = (DeviceData *)native;
        if (!d->ahbImportEnabled || !source || !target) return false;
        auto *t = (TextureHandle *)target->native;
        if (!t || !t->image || !t->defaultView || t->textureType != TextureType::tt2d ||
            !((int)t->usage & (int)TextureUsage::renderTarget)) {
            LOG_ERROR("copyAHardwareBufferToTexture: target must be a 2D render-target texture");
            return false;
        }
        const VkFormat targetFormat = pixelFormatToNative(t->pixelFormat);

        // The converter is created once; its mutex serializes calls.
        AhbConverter *c;
        {
            std::lock_guard<std::mutex> lock(d->gpu_mutex);
            c = converterFor(d);
        }
        if (!c || !c->commandBuffer || !c->vertex) return false;
        std::lock_guard<std::mutex> lock(c->mutex);

        AhbImport *import = importFor(*c, d, source, targetFormat);
        if (!import) return false;

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = import->pipeline->renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &t->defaultView;
        fbInfo.width = t->width;
        fbInfo.height = t->height;
        fbInfo.layers = 1;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        if (vkCreateFramebuffer(c->device, &fbInfo, nullptr, &framebuffer) != VK_SUCCESS) return false;

        VkCommandBuffer cmd = c->commandBuffer;
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        // The buffer's producer (camera) wrote it outside Vulkan: acquire it
        // from the foreign queue family (contents preserved).
        VkImageMemoryBarrier acquire{};
        acquire.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        acquire.srcAccessMask = 0;
        acquire.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        acquire.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        acquire.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        acquire.srcQueueFamilyIndex = d->queueFamilyForeignEnabled ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_IGNORED;
        acquire.dstQueueFamilyIndex = d->queueFamilyForeignEnabled ? d->queueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
        acquire.image = import->image;
        acquire.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &acquire);

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = import->pipeline->renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea = {{0, 0}, {t->width, t->height}};
        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, import->pipeline->pipeline);
        VkViewport viewport{0.0f, 0.0f, float(t->width), float(t->height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {t->width, t->height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, import->pipeline->layout, 0, 1, &import->set,
                                0, nullptr);
        static const float kIdentity[6] = {1, 0, 0, 0, 1, 0};
        const float *m = uvTransform ? uvTransform : kIdentity;
        const float push[8] = {m[0], m[1], m[2], 0.0f, m[3], m[4], m[5], 0.0f};
        vkCmdPushConstants(cmd, import->pipeline->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkResetFences(c->device, 1, &c->fence);
        VkResult result;
        {
            std::lock_guard<std::mutex> queueLock(d->gpu_mutex);  // the queue is shared
            result = vkQueueSubmit(d->graphicsQueue, 1, &submit, c->fence);
        }
        if (result == VK_SUCCESS) result = vkWaitForFences(c->device, 1, &c->fence, VK_TRUE, UINT64_MAX);
        vkDestroyFramebuffer(c->device, framebuffer, nullptr);
        if (result != VK_SUCCESS) {
            LOG_ERROR("copyAHardwareBufferToTexture: submit failed (%d)", int(result));
            return false;
        }
        t->currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return true;
    }

}
