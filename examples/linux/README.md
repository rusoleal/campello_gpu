# Linux Windowed Example

A minimal X11 + Vulkan example that opens a window and clears the swapchain to a rotating color.

## Requirements

- Linux desktop with X11
- Vulkan driver (Mesa, NVIDIA, AMD, or Intel)
- CMake 3.22+
- `libx11-dev` and `libvulkan-dev`

## Build

From the repository root:

```bash
cd examples/linux
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Or build together with the main project:

```bash
cd /path/to/campello_gpu
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target campello_linux_example
```

## Run

```bash
./campello_linux_example
```

Press any key or close the window to quit.

## Wayland

To run on Wayland, change the `LinuxSurfaceInfo` in `main.cpp`:

```cpp
surfaceInfo.display = (void*)wl_display;
surfaceInfo.window  = (void*)wl_surface;
surfaceInfo.api     = LinuxWindowApi::wayland;
```

The `campello_gpu` Vulkan backend handles both X11 and Wayland surfaces automatically.
