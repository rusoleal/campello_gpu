//
//  RaytracingDemo.h
//  campello_test Shared
//
//  Minimal ray tracing demo using campello_gpu.
//  Call -setup to allocate GPU resources and build the acceleration structures.
//  Call -renderIntoTexture: each frame to write a ray-traced image via
//  campello_gpu's RayTracingPassEncoder.
//

#pragma once

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

NS_ASSUME_NONNULL_BEGIN

@interface RaytracingDemo : NSObject

/// Returns YES when the device exposes Feature::raytracing.
@property (nonatomic, readonly) BOOL isSupported;

/// Initialise GPU resources.  Call once before rendering.
- (void)setup;

/// Trace rays and write the result into @a outputTexture (must be rgba32float,
/// storage access).  Call each frame after -setup.
- (void)renderIntoTexture:(id<MTLTexture>)outputTexture
            commandBuffer:(id<MTLCommandBuffer>)commandBuffer;

@end

NS_ASSUME_NONNULL_END
