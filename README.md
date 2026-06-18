# Raytacing Renderer

This repository starts a modular Vulkan ray-tracing renderer. The current executable is a headless sandbox that validates the platform/device boundary, uploads a triangle mesh, and builds both BLAS and TLAS acceleration structures using standard Vulkan KHR ray-tracing extensions.

## Build

Install a Vulkan SDK with Vulkan 1.3 headers and a driver that exposes:

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_tracing_pipeline`
- `VK_KHR_ray_query`
- `VK_KHR_buffer_device_address`

Then configure and build. If the Vulkan SDK is not installed, CMake still builds the backend-independent render graph test and skips the Vulkan target.

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\raytacing_sandbox.exe
```

## Module Boundaries

- `rt::vulkan::Instance` owns Vulkan instance creation and optional validation.
- `rt::vulkan::Device` selects a ray-tracing capable physical device, enables required features/extensions, and loads KHR function pointers.
- `rt::vulkan::CommandContext` owns transient one-shot GPU submissions for uploads and acceleration-structure builds.
- `rt::vulkan::Buffer` owns Vulkan buffer memory and device-address access.
- `rt::vulkan::AccelerationStructureBuilder` builds and refits BLAS/TLAS objects.
- `rt::vulkan::RayTracingPipeline` owns the KHR ray tracing pipeline, pipeline layout, shader binding table, and trace dispatch boundary.
- `rt::scene` is CPU-side, data-oriented scene storage.
- `rt::render::RenderGraph` provides dependency-ordered pass execution without binding the graph to a specific backend.
