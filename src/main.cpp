#include "rt/render/RenderGraph.hpp"
#include "rt/scene/Scene.hpp"
#include "rt/vulkan/AccelerationStructure.hpp"
#include "rt/vulkan/Buffer.hpp"
#include "rt/vulkan/Commands.hpp"
#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/Instance.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

rt::scene::Scene makeSingleTriangleScene()
{
    rt::scene::Scene scene;
    scene.materials.push_back({.baseColor = {0.9F, 0.72F, 0.45F}, .roughness = 0.35F, .metallic = 0.0F});
    scene.vertices = {
        {.position = {-0.7F, -0.5F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .texcoord = {0.0F, 0.0F}},
        {.position = {0.7F, -0.5F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .texcoord = {1.0F, 0.0F}},
        {.position = {0.0F, 0.75F, 0.0F}, .normal = {0.0F, 0.0F, 1.0F}, .texcoord = {0.5F, 1.0F}},
    };
    scene.indices = {0, 1, 2};
    scene.meshes.push_back({
        .name = "triangle",
        .vertexOffset = 0,
        .vertexCount = static_cast<std::uint32_t>(scene.vertices.size()),
        .indexOffset = 0,
        .indexCount = static_cast<std::uint32_t>(scene.indices.size()),
        .materialIndex = 0,
        .dynamicGeometry = false,
    });
    scene.instances.push_back({.meshIndex = 0, .instanceId = 0});
    return scene;
}

} // namespace

int main()
{
    try {
        rt::vulkan::Instance instance({.applicationName = "Raytacing Sandbox"});
        rt::vulkan::Device device(instance, {.requireRayTracing = true});
        rt::vulkan::CommandContext commands(device);

        const rt::scene::Scene scene = makeSingleTriangleScene();

        rt::vulkan::Buffer vertexBuffer(device, {
                                                    .size = sizeof(rt::scene::Vertex) * scene.vertices.size(),
                                                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                             VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                    .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                    .debugName = "triangle vertices",
                                                });

        rt::vulkan::Buffer indexBuffer(device, {
                                                   .size = sizeof(std::uint32_t) * scene.indices.size(),
                                                   .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                   .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                   .debugName = "triangle indices",
                                               });

        vertexBuffer.upload(commands, rt::vulkan::asBytes(std::span<const rt::scene::Vertex>(scene.vertices)));
        indexBuffer.upload(commands, rt::vulkan::asBytes(std::span<const std::uint32_t>(scene.indices)));

        rt::vulkan::AccelerationStructureBuilder asBuilder(device);
        const auto& mesh = scene.meshes.front();
        rt::vulkan::TriangleGeometry triangleGeometry{
            .vertexAddress = vertexBuffer.deviceAddress() + mesh.vertexOffset * sizeof(rt::scene::Vertex),
            .vertexStride = sizeof(rt::scene::Vertex),
            .vertexCount = mesh.vertexCount,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .indexAddress = indexBuffer.deviceAddress() + mesh.indexOffset * sizeof(std::uint32_t),
            .indexCount = mesh.indexCount,
            .indexType = VK_INDEX_TYPE_UINT32,
            .opaque = true,
        };

        auto blas = asBuilder.buildBottomLevelTriangles(commands, triangleGeometry, {.allowUpdate = mesh.dynamicGeometry});
        const std::array tlasInstances{
            rt::vulkan::InstanceGeometry{
                .bottomLevelAddress = blas.deviceAddress(),
                .transform = scene.instances.front().transform,
                .instanceId = scene.instances.front().instanceId,
                .mask = scene.instances.front().visibilityMask,
            },
        };
        auto tlas = asBuilder.buildTopLevel(commands, tlasInstances, {.allowUpdate = true});

        rt::render::RenderGraph graph;
        graph.addPass("build-acceleration-structures", {}, {{"scene-tlas"}}, [](const rt::render::PassContext& ctx) {
            std::cout << "executed pass: " << ctx.passName << '\n';
        });
        graph.compile();
        graph.execute();

        std::cout << "Vulkan device: " << device.properties().deviceName << '\n';
        std::cout << "BLAS address: 0x" << std::hex << blas.deviceAddress() << '\n';
        std::cout << "TLAS address: 0x" << std::hex << tlas.deviceAddress() << std::dec << '\n';
        std::cout << "Max ray recursion depth: " << device.rayTracingLimits().maxRayRecursionDepth << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "renderer bootstrap failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
