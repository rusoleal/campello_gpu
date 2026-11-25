// JavaScript interop helpers for campello_gpu's WebGPU backend, exposed to
// page JavaScript as `Module.campelloGpu` once a device has been created.
//
// Typical use: draw a video frame, image or canvas into a texture from
// JavaScript and use it from C++.
//
//   // JavaScript
//   const device = Module.campelloGpu.getJsObject(devicePtr);    // GPUDevice
//   const texture = device.createTexture({ size, format: 'rgba8unorm',
//       usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST |
//              GPUTextureUsage.RENDER_ATTACHMENT });
//   device.queue.copyExternalImageToTexture({ source: video }, { texture }, size);
//   const handle = Module.campelloGpu.importTexture(texture);  // WGPUTexture
//
//   // C++: devicePtr comes from Device::getNativeWGPUDevice()
//   auto tex = device->createTextureFromWGPUTexture(handle);
//   wgpuTextureRelease(handle);  // campello_gpu holds its own reference
//
// Linked into every target that links campello_gpu (see wasm.cmake).

addToLibrary({
  campello_gpu_web_init__deps: ['$WebGPU', '$campelloGpuExternalImages'],
  campello_gpu_web_init: () => {
    if (Module['campelloGpu']) return;
    Module['campelloGpu'] = {
      // GPUTexture -> WGPUTexture handle, for Device::createTextureFromWGPUTexture().
      // The caller owns the returned reference.
      'importTexture': (texture) => WebGPU.importJsTexture(texture),
      // GPUTextureView -> WGPUTextureView handle, for TextureView::fromNative().
      // The caller owns the returned reference and must keep it alive.
      'importTextureView': (view) => WebGPU.importJsTextureView(view),
      // Any WGPU* handle -> its JavaScript object, e.g. the GPUDevice behind
      // Device::getNativeWGPUDevice().
      'getJsObject': (handle) => WebGPU.getJsObject(handle),
      // Image source (VideoFrame, HTMLVideoElement, ImageBitmap, canvas...)
      // -> handle for ExternalImage{ExternalImageType::webImageSource}. One
      // copyExternalImageToTexture() call uses and releases it; the caller
      // still owns (and closes) the source.
      'importExternalImage': (source) => {
        const handle = campelloGpuExternalImages.next++;
        campelloGpuExternalImages.sources.set(handle, source);
        return handle;
      },
      // Releases a handle that was never copied.
      'releaseExternalImage': (handle) => { campelloGpuExternalImages.sources.delete(handle); },
    };
  },

  $campelloGpuExternalImages: { next: 1, sources: new Map() },

  // Size of an imported image source (0 x 0 when unknown); see external_image.cpp.
  campello_gpu_external_image_size__deps: ['$campelloGpuExternalImages'],
  campello_gpu_external_image_size: (handle, width, height) => {
    const s = campelloGpuExternalImages.sources.get(handle);
    let w = 0, h = 0;
    if (s) {
      if ('displayWidth' in s) { w = s.displayWidth; h = s.displayHeight; }             // VideoFrame
      else if ('videoWidth' in s) { w = s.videoWidth; h = s.videoHeight; }              // <video>
      else if ('naturalWidth' in s) { w = s.naturalWidth; h = s.naturalHeight; }        // <img>
      else { w = s.width; h = s.height; }                                               // ImageBitmap, canvas
    }
    HEAP32[width >> 2] = w;
    HEAP32[height >> 2] = h;
  },

  // queue.copyExternalImageToTexture() of an imported source into `texture`;
  // releases the handle. Returns 1 on success.
  campello_gpu_copy_external_image__deps: ['$WebGPU', '$campelloGpuExternalImages'],
  campello_gpu_copy_external_image: (queue, texture, handle, width, height) => {
    const source = campelloGpuExternalImages.sources.get(handle);
    campelloGpuExternalImages.sources.delete(handle);
    if (!source) return 0;
    try {
      WebGPU.getJsObject(queue).copyExternalImageToTexture(
          { source }, { texture: WebGPU.getJsObject(texture) }, [width, height]);
      return 1;
    } catch (e) {
      console.error('campello_gpu: copyExternalImageToTexture failed:', e);
      return 0;
    }
  },

  // Drops a handle without copying (a rejected call).
  campello_gpu_release_external_image__deps: ['$campelloGpuExternalImages'],
  campello_gpu_release_external_image: (handle) => { campelloGpuExternalImages.sources.delete(handle); },
});
