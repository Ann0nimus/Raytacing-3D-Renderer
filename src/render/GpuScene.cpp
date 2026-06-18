#include "rt/render/GpuScene.hpp"

#include <algorithm>

namespace rt::render {

GpuScene buildGpuScene(const scene::Scene& scene)
{
    GpuScene gpuScene;
    gpuScene.vertices.reserve(scene.vertices.size());
    gpuScene.indices = scene.indices;
    gpuScene.materials.reserve(std::max<std::size_t>(scene.materials.size(), 1));
    gpuScene.objects.reserve(scene.meshes.size());

    for (const scene::Vertex& vertex : scene.vertices) {
        gpuScene.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2], 1.0F},
            .normal = {vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0F},
            .texcoord = {vertex.texcoord[0], vertex.texcoord[1], 0.0F, 0.0F},
        });
    }

    if (scene.materials.empty()) {
        gpuScene.materials.push_back({
            .baseColorRoughness = {1.0F, 1.0F, 1.0F, 0.55F},
            .emissionMetallic = {0.0F, 0.0F, 0.0F, 0.0F},
        });
    } else {
        for (const scene::Material& material : scene.materials) {
            gpuScene.materials.push_back({
                .baseColorRoughness = {
                    material.baseColor[0],
                    material.baseColor[1],
                    material.baseColor[2],
                    material.roughness,
                },
                .emissionMetallic = {
                    material.emission[0],
                    material.emission[1],
                    material.emission[2],
                    material.metallic,
                },
            });
        }
    }

    for (const scene::Mesh& mesh : scene.meshes) {
        gpuScene.objects.push_back({
            .vertexOffset = mesh.vertexOffset,
            .indexOffset = mesh.indexOffset,
            .materialIndex = std::min<std::uint32_t>(
                mesh.materialIndex,
                static_cast<std::uint32_t>(gpuScene.materials.size() - 1)),
            .flags = mesh.dynamicGeometry ? 1U : 0U,
        });
    }

    return gpuScene;
}

} // namespace rt::render
