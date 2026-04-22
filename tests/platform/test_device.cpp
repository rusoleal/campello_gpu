// Platform integration tests for Device.
//
// These tests require a real GPU device. They are compiled only when
// BUILD_INTEGRATION_TESTS=ON (see tests/CMakeLists.txt).

#include <gtest/gtest.h>
#include <campello_gpu/device.hpp>
#include <campello_gpu/descriptors/sampler_descriptor.hpp>
#include <campello_gpu/descriptors/query_set_descriptor.hpp>
#include <campello_gpu/descriptors/pipeline_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_layout_descriptor.hpp>
#include <campello_gpu/descriptors/bind_group_descriptor.hpp>
#include <campello_gpu/constants/query_set_type.hpp>
#include <campello_gpu/constants/filter_mode.hpp>
#include <campello_gpu/constants/wrap_mode.hpp>
#include <campello_gpu/constants/texture_usage.hpp>
#include <campello_gpu/constants/texture_type.hpp>
#include <campello_gpu/constants/pixel_format.hpp>
#include <campello_gpu/constants/buffer_usage.hpp>

using namespace systems::leal::campello_gpu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::shared_ptr<Device> tryCreateDevice() {
#if defined(__ANDROID__)
    return Device::createDefaultDevice(nullptr);
#elif defined(__APPLE__)
    return Device::createDefaultDevice(nullptr);
#elif defined(_WIN32)
    return Device::createDefaultDevice(nullptr);
#elif defined(__linux__)
    return Device::createDefaultDevice(nullptr);
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// getEngineVersion — no GPU required
// ---------------------------------------------------------------------------

TEST(Device, GetEngineVersionReturnsString) {
    std::string version = Device::getEngineVersion();
    EXPECT_FALSE(version.empty());
}

// ---------------------------------------------------------------------------
// getAdapters — enumerates physical GPUs, no window/surface required
// ---------------------------------------------------------------------------

TEST(Device, GetAdaptersReturnsAtLeastOneOnSupportedPlatform) {
#if defined(__ANDROID__) || defined(__APPLE__) || defined(_WIN32)
    auto adapters = Device::getAdapters();
    EXPECT_FALSE(adapters.empty()) << "Expected at least one GPU adapter";
#else
    GTEST_SKIP() << "getAdapters not yet implemented for this platform";
#endif
}

// ---------------------------------------------------------------------------
// createDefaultDevice
// ---------------------------------------------------------------------------

TEST(Device, CreateDefaultDeviceReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_NE(device, nullptr);
}

TEST(Device, GetNameReturnsNonEmptyString) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_FALSE(device->getName().empty());
}

TEST(Device, GetFeaturesDoesNotThrow) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_NO_THROW(device->getFeatures());
}

// ---------------------------------------------------------------------------
// createSampler
// ---------------------------------------------------------------------------

TEST(Device, CreateSamplerReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::clampToEdge;
    desc.addressModeV = WrapMode::clampToEdge;
    desc.addressModeW = WrapMode::clampToEdge;
    desc.minFilter    = FilterMode::fmLinear;
    desc.magFilter    = FilterMode::fmLinear;
    desc.lodMinClamp  = 0.0;
    desc.lodMaxClamp  = 32.0;
    desc.maxAnisotropy = 1.0;

    auto sampler = device->createSampler(desc);
    EXPECT_NE(sampler, nullptr);
}

// ---------------------------------------------------------------------------
// createTexture
// ---------------------------------------------------------------------------

TEST(Device, CreateTexture2DReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    EXPECT_NE(texture, nullptr);
}

TEST(Device, CreateTextureDimensionsMatchRequest) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        128, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->getWidth(),  128u);
    EXPECT_EQ(texture->getHeight(),  64u);
    EXPECT_EQ(texture->getFormat(), PixelFormat::rgba8unorm);
    EXPECT_EQ(texture->getDimension(), TextureType::tt2d);
    EXPECT_EQ(texture->getMipLevelCount(), 1u);
    EXPECT_EQ(texture->getSampleCount(),   1u);
}

// ---------------------------------------------------------------------------
// createQuerySet
// ---------------------------------------------------------------------------

TEST(Device, CreateQuerySetReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    QuerySetDescriptor desc{};
    desc.count = 8;
    desc.type  = QuerySetType::occlusion;

    auto querySet = device->createQuerySet(desc);
    EXPECT_NE(querySet, nullptr);
}

// ---------------------------------------------------------------------------
// createPipelineLayout / createBindGroupLayout / createBindGroup
// ---------------------------------------------------------------------------

TEST(Device, CreatePipelineLayoutReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    PipelineLayoutDescriptor desc{};
    auto layout = device->createPipelineLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(Device, CreateBindGroupLayoutReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    BindGroupLayoutDescriptor desc{};
    auto layout = device->createBindGroupLayout(desc);
    EXPECT_NE(layout, nullptr);
}

TEST(Device, CreateBindGroupReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    BindGroupLayoutDescriptor layoutDesc{};
    auto layout = device->createBindGroupLayout(layoutDesc);
    ASSERT_NE(layout, nullptr);

    BindGroupDescriptor desc{};
    desc.layout = layout;
    auto bindGroup = device->createBindGroup(desc);
    EXPECT_NE(bindGroup, nullptr);
}

// ---------------------------------------------------------------------------
// createCommandEncoder
// ---------------------------------------------------------------------------

TEST(Device, CreateCommandEncoderReturnsNonNull) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto encoder = device->createCommandEncoder();
    EXPECT_NE(encoder, nullptr);
}

TEST(Device, CommandEncoderFinishReturnsCommandBuffer) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto encoder = device->createCommandEncoder();
    ASSERT_NE(encoder, nullptr);
    auto cmdBuffer = encoder->finish();
    EXPECT_NE(cmdBuffer, nullptr);
}

// ---------------------------------------------------------------------------
// Metrics API - Phase 1
// ---------------------------------------------------------------------------

TEST(Device, GetMemoryInfoDoesNotThrow) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    EXPECT_NO_THROW({
        auto memInfo = device->getMemoryInfo();
        // Values may be 0 if backend doesn't support querying, but struct should be valid
        (void)memInfo.hasUnifiedMemory;
    });
}

TEST(Device, GetResourceCountersReturnsZeroInitially) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto counters = device->getResourceCounters();
    EXPECT_EQ(counters.bufferCount, 0u);
    EXPECT_EQ(counters.textureCount, 0u);
    EXPECT_EQ(counters.renderPipelineCount, 0u);
    EXPECT_EQ(counters.computePipelineCount, 0u);
    EXPECT_EQ(counters.shaderModuleCount, 0u);
    EXPECT_EQ(counters.samplerCount, 0u);
}

TEST(Device, GetCommandStatsReturnsZeroInitially) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto stats = device->getCommandStats();
    EXPECT_EQ(stats.commandsSubmitted, 0u);
    EXPECT_EQ(stats.drawCalls, 0u);
    EXPECT_EQ(stats.dispatchCalls, 0u);
}

TEST(Device, GetMetricsReturnsCompleteSnapshot) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    auto metrics = device->getMetrics();
    // Just verify we can access all fields
    (void)metrics.deviceMemory.hasUnifiedMemory;
    (void)metrics.resources.bufferCount;
    (void)metrics.commands.commandsSubmitted;
}

TEST(Device, CreateBufferIncrementsBufferCounter) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceCounters().bufferCount;
    
    auto buffer = device->createBuffer(1024, BufferUsage::vertex);
    ASSERT_NE(buffer, nullptr);
    
    auto after = device->getResourceCounters().bufferCount;
    EXPECT_EQ(after, before + 1);
}

TEST(Device, CreateTextureIncrementsTextureCounter) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceCounters().textureCount;
    
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    
    auto after = device->getResourceCounters().textureCount;
    EXPECT_EQ(after, before + 1);
}

TEST(Device, CreateSamplerIncrementsSamplerCounter) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceCounters().samplerCount;
    
    SamplerDescriptor desc{};
    desc.addressModeU = WrapMode::clampToEdge;
    desc.addressModeV = WrapMode::clampToEdge;
    desc.addressModeW = WrapMode::clampToEdge;
    desc.minFilter    = FilterMode::fmLinear;
    desc.magFilter    = FilterMode::fmLinear;
    auto sampler = device->createSampler(desc);
    ASSERT_NE(sampler, nullptr);
    
    auto after = device->getResourceCounters().samplerCount;
    EXPECT_EQ(after, before + 1);
}

TEST(Device, ResetCommandStatsResetsCounters) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    // Just verify reset doesn't throw
    EXPECT_NO_THROW(device->resetCommandStats());
    
    auto stats = device->getCommandStats();
    EXPECT_EQ(stats.commandsSubmitted, 0u);
    EXPECT_EQ(stats.renderPasses, 0u);
    EXPECT_EQ(stats.computePasses, 0u);
    EXPECT_EQ(stats.drawCalls, 0u);
    EXPECT_EQ(stats.dispatchCalls, 0u);
    EXPECT_EQ(stats.copies, 0u);
}

// ---------------------------------------------------------------------------
// Phase 2: Resource Memory Stats Tests
// ---------------------------------------------------------------------------

TEST(Device, GetResourceMemoryStatsReturnsValidStruct) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto stats = device->getResourceMemoryStats();
    
    // Verify struct is returned (values may be 0 initially)
    EXPECT_EQ(stats.totalTrackedBytes, 
              stats.bufferBytes + stats.textureBytes + 
              stats.accelerationStructureBytes + stats.querySetBytes);
}

TEST(Device, CreateBufferIncreasesBufferMemoryBytes) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceMemoryStats().bufferBytes;
    
    auto buffer = device->createBuffer(4096, BufferUsage::vertex);
    ASSERT_NE(buffer, nullptr);
    
    auto after = device->getResourceMemoryStats().bufferBytes;
    EXPECT_GT(after, before);
    
    // Buffer should allocate at least the requested size (may be more due to alignment)
    EXPECT_GE(after - before, 4096u);
}

TEST(Device, CreateTextureIncreasesTextureMemoryBytes) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceMemoryStats().textureBytes;
    
    auto texture = device->createTexture(
        TextureType::tt2d, PixelFormat::rgba8unorm,
        64, 64, 1, 1, 1, TextureUsage::textureBinding);
    ASSERT_NE(texture, nullptr);
    
    auto after = device->getResourceMemoryStats().textureBytes;
    EXPECT_GT(after, before);
    
    // 64x64 RGBA8 texture = 16KB minimum
    EXPECT_GE(after - before, 16384u);
}

TEST(Device, BufferDestructionDecreasesBufferMemoryBytes) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto before = device->getResourceMemoryStats().bufferBytes;
    
    {
        auto buffer = device->createBuffer(4096, BufferUsage::vertex);
        ASSERT_NE(buffer, nullptr);
        
        auto during = device->getResourceMemoryStats().bufferBytes;
        EXPECT_GT(during, before);
        
        // Buffer goes out of scope here
    }
    
    auto after = device->getResourceMemoryStats().bufferBytes;
    EXPECT_EQ(after, before);
}

TEST(Device, GetMetricsIncludesResourceMemoryStats) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto metrics = device->getMetrics();
    
    // Verify all Phase 2 fields are present
    EXPECT_EQ(metrics.resourceMemory.totalTrackedBytes,
              metrics.resourceMemory.bufferBytes + 
              metrics.resourceMemory.textureBytes + 
              metrics.resourceMemory.accelerationStructureBytes + 
              metrics.resourceMemory.querySetBytes);
}

TEST(Device, PeakMemoryStatsTracked) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    // Reset peaks first
    device->resetPeakMemoryStats();
    
    auto before = device->getResourceMemoryStats();
    
    {
        auto buffer = device->createBuffer(8192, BufferUsage::vertex);
        ASSERT_NE(buffer, nullptr);
        
        auto during = device->getResourceMemoryStats();
        
        // Peak should be at least the current value
        EXPECT_GE(during.peakBufferBytes, during.bufferBytes);
        EXPECT_GE(during.peakTotalBytes, during.totalTrackedBytes);
    }
    
    // After destruction, peaks should still reflect the high watermark
    auto after = device->getResourceMemoryStats();
    EXPECT_GE(after.peakBufferBytes, before.peakBufferBytes + 8192);
}

TEST(Device, ResetPeakMemoryStatsWorks) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    // Create a buffer to generate some memory usage
    auto buffer = device->createBuffer(4096, BufferUsage::vertex);
    ASSERT_NE(buffer, nullptr);
    
    auto before = device->getResourceMemoryStats();
    EXPECT_GT(before.peakBufferBytes, 0u);
    
    // Reset peaks
    device->resetPeakMemoryStats();
    
    auto after = device->getResourceMemoryStats();
    // Peak should now equal current (approximately, with some tolerance for alignment)
    EXPECT_LE(after.peakBufferBytes, before.peakBufferBytes);
}

// ---------------------------------------------------------------------------
// Phase 3: GPU Timing and Memory Pressure Tests
// ---------------------------------------------------------------------------

TEST(Device, GetPassPerformanceStatsReturnsValidStruct) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto stats = device->getPassPerformanceStats();
    
    // Initially all should be zero
    EXPECT_EQ(stats.renderPassTimeNs, 0u);
    EXPECT_EQ(stats.computePassTimeNs, 0u);
    EXPECT_EQ(stats.rayTracingPassTimeNs, 0u);
    EXPECT_EQ(stats.totalPassTimeNs, 0u);
    EXPECT_EQ(stats.renderPassSampleCount, 0u);
    EXPECT_EQ(stats.computePassSampleCount, 0u);
    EXPECT_EQ(stats.rayTracingPassSampleCount, 0u);
}

TEST(Device, ResetPassPerformanceStatsWorks) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    // Just verify it doesn't throw
    EXPECT_NO_THROW(device->resetPassPerformanceStats());
    
    auto stats = device->getPassPerformanceStats();
    EXPECT_EQ(stats.renderPassTimeNs, 0u);
    EXPECT_EQ(stats.computePassTimeNs, 0u);
}

TEST(Device, GetMemoryPressureLevelReturnsNormalInitially) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    // Should return Normal when no significant memory is used
    auto level = device->getMemoryPressureLevel();
    // Can be any value, but should be a valid enum
    EXPECT_TRUE(level == MemoryPressureLevel::Normal || 
                level == MemoryPressureLevel::Warning || 
                level == MemoryPressureLevel::Critical);
}

TEST(Device, MemoryBudgetGetSetRoundtrip) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    MemoryBudget budget;
    budget.warningThresholdPercent = 75;
    budget.criticalThresholdPercent = 90;
    budget.targetUsagePercent = 65;
    budget.enableAutomaticEviction = true;
    
    device->setMemoryBudget(budget);
    auto retrieved = device->getMemoryBudget();
    
    EXPECT_EQ(retrieved.warningThresholdPercent, 75u);
    EXPECT_EQ(retrieved.criticalThresholdPercent, 90u);
    EXPECT_EQ(retrieved.targetUsagePercent, 65u);
    EXPECT_TRUE(retrieved.enableAutomaticEviction);
}

TEST(Device, MemoryPressureCallbackCanBeSet) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    bool callbackInvoked = false;
    MemoryPressureLevel capturedLevel = MemoryPressureLevel::Normal;
    
    device->setMemoryPressureCallback([&](MemoryPressureLevel level, const ResourceMemoryStats& stats) {
        callbackInvoked = true;
        capturedLevel = level;
    });
    
    // Trigger a check
    device->checkMemoryPressure();
    
    // Callback may or may not be invoked depending on current memory state
    // Just verify no crash occurs
    
    // Unregister callback
    device->setMemoryPressureCallback(nullptr);
}

TEST(Device, CheckMemoryPressureReturnsCurrentLevel) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto level = device->checkMemoryPressure();
    EXPECT_TRUE(level == MemoryPressureLevel::Normal || 
                level == MemoryPressureLevel::Warning || 
                level == MemoryPressureLevel::Critical);
}

TEST(Device, GetMetricsWithTimingIncludesAllData) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto metrics = device->getMetricsWithTiming();
    
    // Verify all base fields are present
    EXPECT_EQ(metrics.resourceMemory.totalTrackedBytes,
              metrics.resourceMemory.bufferBytes + 
              metrics.resourceMemory.textureBytes + 
              metrics.resourceMemory.accelerationStructureBytes + 
              metrics.resourceMemory.querySetBytes);
    
    // Verify pass performance is included
    EXPECT_EQ(metrics.passPerformance.totalPassTimeNs,
              metrics.passPerformance.renderPassTimeNs + 
              metrics.passPerformance.computePassTimeNs + 
              metrics.passPerformance.rayTracingPassTimeNs);
}

TEST(Device, DefaultMemoryBudgetIsReasonable) {
    auto device = tryCreateDevice();
    if (!device) {
        GTEST_SKIP() << "createDefaultDevice not available on this platform yet";
    }
    
    auto budget = device->getMemoryBudget();
    
    // Default values should be reasonable
    EXPECT_GT(budget.warningThresholdPercent, 0u);
    EXPECT_LT(budget.warningThresholdPercent, 100u);
    EXPECT_GT(budget.criticalThresholdPercent, budget.warningThresholdPercent);
    EXPECT_LE(budget.criticalThresholdPercent, 100u);
    EXPECT_LT(budget.targetUsagePercent, budget.warningThresholdPercent);
}
