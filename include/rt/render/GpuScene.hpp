#pragma once

#include "rt/scene/Scene.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace rt::render {

struct alignas(16) GpuVertex {
    std::array<float, 4> position{};
    std::array<float, 4> normal{};
    std::array<float, 4> texcoord{};
};

struct alignas(16) GpuMaterial {
    std::array<float, 4> baseColorRoughness{};
    std::array<float, 4> emissionMetallic{};
};

struct alignas(16) GpuObject {
    std::uint32_t vertexOffset = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t materialIndex = 0;
    std::uint32_t flags = 0;
};

struct GpuScene {
    std::vector<GpuVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<GpuMaterial> materials;
    std::vector<GpuObject> objects;
};

[[nodiscard]] GpuScene buildGpuScene(const scene::Scene& scene);

} // namespace rt::render
