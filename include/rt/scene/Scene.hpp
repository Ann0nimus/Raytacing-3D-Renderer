#pragma once

#include "rt/core/Math.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rt::scene {

struct Vertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};
    std::array<float, 2> texcoord{};
};

struct Material {
    std::array<float, 3> baseColor{1.0F, 1.0F, 1.0F};
    float roughness = 0.5F;
    float metallic = 0.0F;
};

struct Mesh {
    std::string name;
    std::uint32_t vertexOffset = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
    bool dynamicGeometry = false;
};

struct Instance {
    std::uint32_t meshIndex = 0;
    Mat3x4 transform = identityTransform3x4();
    std::uint32_t instanceId = 0;
    std::uint8_t visibilityMask = 0xFF;
};

struct Camera {
    std::array<float, 3> position{};
    std::array<float, 3> forward{0.0F, 0.0F, -1.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    float verticalFovRadians = 1.0471975512F;
    float nearPlane = 0.01F;
    float farPlane = 10000.0F;
};

struct Scene {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Instance> instances;
    Camera camera;
};

} // namespace rt::scene
