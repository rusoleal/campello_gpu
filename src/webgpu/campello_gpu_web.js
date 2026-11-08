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
  campello_gpu_web_init__deps: ['$WebGPU'],
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
    };
  },
});
