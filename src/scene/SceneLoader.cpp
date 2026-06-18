#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include "rt/scene/SceneLoader.hpp"

#include "rt/core/Math.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace rt::scene {

namespace {

struct PendingMesh {
    std::string name;
    std::uint32_t materialIndex = 0;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

std::string lowerCopy(std::string value)
{
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::uint32_t appendMaterial(Scene& scene, const tinyobj::material_t& material)
{
    Material out;
    out.name = material.name.empty() ? "material" : material.name;
    out.baseColor = {material.diffuse[0], material.diffuse[1], material.diffuse[2]};
    out.emission = {material.emission[0], material.emission[1], material.emission[2]};
    out.roughness = 1.0F - std::clamp(material.shininess / 512.0F, 0.0F, 0.95F);
    out.metallic = 0.0F;

    const std::string lowerName = lowerCopy(out.name);
    if (lowerName.find("light") != std::string::npos &&
        out.emission[0] + out.emission[1] + out.emission[2] <= 0.0001F) {
        out.emission = {14.0F, 11.0F, 7.0F};
    }

    scene.materials.push_back(out);
    return static_cast<std::uint32_t>(scene.materials.size() - 1);
}

std::array<float, 3> readPosition(const tinyobj::attrib_t& attrib, int index)
{
    return {
        attrib.vertices[3 * index + 0],
        attrib.vertices[3 * index + 1],
        attrib.vertices[3 * index + 2],
    };
}

std::array<float, 3> readNormal(const tinyobj::attrib_t& attrib, int index)
{
    if (index < 0) {
        return {0.0F, 0.0F, 0.0F};
    }
    return {
        attrib.normals[3 * index + 0],
        attrib.normals[3 * index + 1],
        attrib.normals[3 * index + 2],
    };
}

std::array<float, 2> readTexcoord(const tinyobj::attrib_t& attrib, int index)
{
    if (index < 0) {
        return {0.0F, 0.0F};
    }
    return {
        attrib.texcoords[2 * index + 0],
        attrib.texcoords[2 * index + 1],
    };
}

std::array<float, 3> triangleNormal(const std::array<float, 3>& a,
                                    const std::array<float, 3>& b,
                                    const std::array<float, 3>& c)
{
    const Vec3 normal = normalize(cross(sub(b, a), sub(c, a)));
    return {normal[0], normal[1], normal[2]};
}

void appendQuad(Scene& scene,
                std::string name,
                std::uint32_t materialIndex,
                std::array<float, 3> a,
                std::array<float, 3> b,
                std::array<float, 3> c,
                std::array<float, 3> d)
{
    const auto normal = triangleNormal(a, b, c);
    const std::uint32_t vertexOffset = static_cast<std::uint32_t>(scene.vertices.size());
    const std::uint32_t indexOffset = static_cast<std::uint32_t>(scene.indices.size());

    scene.vertices.push_back({.position = a, .normal = normal, .texcoord = {0.0F, 0.0F}});
    scene.vertices.push_back({.position = b, .normal = normal, .texcoord = {1.0F, 0.0F}});
    scene.vertices.push_back({.position = c, .normal = normal, .texcoord = {1.0F, 1.0F}});
    scene.vertices.push_back({.position = d, .normal = normal, .texcoord = {0.0F, 1.0F}});

    scene.indices.insert(scene.indices.end(), {0, 1, 2, 0, 2, 3});

    scene.meshes.push_back({
        .name = std::move(name),
        .vertexOffset = vertexOffset,
        .vertexCount = 4,
        .indexOffset = indexOffset,
        .indexCount = 6,
        .materialIndex = materialIndex,
    });
}

void appendBox(Scene& scene,
               std::string name,
               std::uint32_t materialIndex,
               std::array<float, 3> minCorner,
               std::array<float, 3> maxCorner)
{
    const auto [x0, y0, z0] = minCorner;
    const auto [x1, y1, z1] = maxCorner;
    appendQuad(scene, name + "/front", materialIndex, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});
    appendQuad(scene, name + "/back", materialIndex, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0});
    appendQuad(scene, name + "/left", materialIndex, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0});
    appendQuad(scene, name + "/right", materialIndex, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1});
    appendQuad(scene, name + "/top", materialIndex, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0});
    appendQuad(scene, name + "/bottom", materialIndex, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});
}

void addDefaultInstances(Scene& scene)
{
    scene.instances.clear();
    scene.instances.reserve(scene.meshes.size());
    for (std::uint32_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        scene.instances.push_back({
            .meshIndex = meshIndex,
            .instanceId = meshIndex,
            .visibilityMask = 0xFF,
        });
    }
}

} // namespace

std::vector<SceneDescriptor> SceneLoader::discover(const std::filesystem::path& assetRoot)
{
    std::vector<SceneDescriptor> descriptors;
    descriptors.push_back({
        .name = "Material Lab",
        .source = SceneSource::BuiltinMaterialLab,
    });

    const auto sceneRoot = assetRoot / "scenes";
    if (std::filesystem::is_directory(sceneRoot)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sceneRoot)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".obj") {
                continue;
            }
            descriptors.push_back({
                .name = entry.path().stem().string(),
                .source = SceneSource::ObjFile,
                .path = entry.path(),
            });
        }
    }

    std::ranges::sort(descriptors, [](const SceneDescriptor& a, const SceneDescriptor& b) {
        return a.name < b.name;
    });
    return descriptors;
}

Scene SceneLoader::load(const SceneDescriptor& descriptor)
{
    switch (descriptor.source) {
    case SceneSource::BuiltinMaterialLab:
        return materialLab();
    case SceneSource::ObjFile:
        return loadObj(descriptor.path);
    }

    throw std::invalid_argument("unknown scene source");
}

Scene SceneLoader::loadObj(const std::filesystem::path& path)
{
    tinyobj::ObjReaderConfig config;
    config.mtl_search_path = path.parent_path().string();
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path.string(), config)) {
        throw std::runtime_error(reader.Error().empty() ? "failed to load OBJ: " + path.string() : reader.Error());
    }

    Scene scene;
    scene.name = path.stem().string();

    const auto& attrib = reader.GetAttrib();
    const auto& materials = reader.GetMaterials();
    const auto& shapes = reader.GetShapes();

    if (materials.empty()) {
        scene.materials.push_back({.name = "default", .roughness = 0.65F});
    } else {
        for (const auto& material : materials) {
            appendMaterial(scene, material);
        }
    }

    for (const auto& shape : shapes) {
        std::map<int, PendingMesh> pendingByMaterial;
        std::size_t indexCursor = 0;

        for (std::size_t face = 0; face < shape.mesh.num_face_vertices.size(); ++face) {
            const int faceVertexCount = shape.mesh.num_face_vertices[face];
            if (faceVertexCount != 3) {
                indexCursor += static_cast<std::size_t>(faceVertexCount);
                continue;
            }

            const int rawMaterialId = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[face];
            const int materialId = rawMaterialId >= 0 ? rawMaterialId : 0;
            auto [it, inserted] = pendingByMaterial.try_emplace(materialId);
            if (inserted) {
                it->second.name = shape.name.empty() ? "mesh" : shape.name;
                it->second.name += "/" + scene.materials[static_cast<std::size_t>(materialId)].name;
                it->second.materialIndex = static_cast<std::uint32_t>(materialId);
            }

            PendingMesh& pending = it->second;
            std::array<Vertex, 3> tri{};
            for (int v = 0; v < 3; ++v) {
                const tinyobj::index_t index = shape.mesh.indices[indexCursor + static_cast<std::size_t>(v)];
                tri[static_cast<std::size_t>(v)] = {
                    .position = readPosition(attrib, index.vertex_index),
                    .normal = readNormal(attrib, index.normal_index),
                    .texcoord = readTexcoord(attrib, index.texcoord_index),
                };
            }

            if (tri[0].normal == std::array<float, 3>{0.0F, 0.0F, 0.0F}) {
                const auto normal = triangleNormal(tri[0].position, tri[1].position, tri[2].position);
                tri[0].normal = normal;
                tri[1].normal = normal;
                tri[2].normal = normal;
            }

            const std::uint32_t base = static_cast<std::uint32_t>(pending.vertices.size());
            pending.vertices.insert(pending.vertices.end(), tri.begin(), tri.end());
            pending.indices.insert(pending.indices.end(), {base, base + 1, base + 2});
            indexCursor += 3;
        }

        for (auto& [_, pending] : pendingByMaterial) {
            if (pending.vertices.empty()) {
                continue;
            }

            const std::uint32_t vertexOffset = static_cast<std::uint32_t>(scene.vertices.size());
            const std::uint32_t indexOffset = static_cast<std::uint32_t>(scene.indices.size());
            scene.vertices.insert(scene.vertices.end(), pending.vertices.begin(), pending.vertices.end());
            scene.indices.insert(scene.indices.end(), pending.indices.begin(), pending.indices.end());

            scene.meshes.push_back({
                .name = std::move(pending.name),
                .vertexOffset = vertexOffset,
                .vertexCount = static_cast<std::uint32_t>(pending.vertices.size()),
                .indexOffset = indexOffset,
                .indexCount = static_cast<std::uint32_t>(pending.indices.size()),
                .materialIndex = pending.materialIndex,
            });
        }
    }

    addDefaultInstances(scene);
    scene.camera = {
        .position = {0.0F, 1.0F, 3.15F},
        .forward = {0.0F, -0.08F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .verticalFovRadians = 0.75F,
    };
    return scene;
}

Scene SceneLoader::materialLab()
{
    Scene scene;
    scene.name = "Material Lab";
    scene.materials = {
        {.name = "matte white", .baseColor = {0.76F, 0.74F, 0.70F}, .roughness = 0.9F},
        {.name = "red wall", .baseColor = {0.63F, 0.07F, 0.05F}, .roughness = 0.85F},
        {.name = "green wall", .baseColor = {0.14F, 0.45F, 0.09F}, .roughness = 0.85F},
        {.name = "warm light", .baseColor = {1.0F, 0.92F, 0.78F}, .emission = {12.0F, 9.0F, 5.5F}},
        {.name = "blue rough", .baseColor = {0.12F, 0.32F, 0.95F}, .roughness = 0.35F},
        {.name = "gold metal", .baseColor = {1.0F, 0.72F, 0.28F}, .roughness = 0.2F, .metallic = 1.0F},
    };

    appendQuad(scene, "floor", 0, {-2.0F, 0.0F, 2.0F}, {2.0F, 0.0F, 2.0F}, {2.0F, 0.0F, -2.0F}, {-2.0F, 0.0F, -2.0F});
    appendQuad(scene, "ceiling", 0, {-2.0F, 2.4F, -2.0F}, {2.0F, 2.4F, -2.0F}, {2.0F, 2.4F, 2.0F}, {-2.0F, 2.4F, 2.0F});
    appendQuad(scene, "back", 0, {-2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, -2.0F}, {2.0F, 2.4F, -2.0F}, {-2.0F, 2.4F, -2.0F});
    appendQuad(scene, "left", 1, {-2.0F, 0.0F, 2.0F}, {-2.0F, 0.0F, -2.0F}, {-2.0F, 2.4F, -2.0F}, {-2.0F, 2.4F, 2.0F});
    appendQuad(scene, "right", 2, {2.0F, 0.0F, -2.0F}, {2.0F, 0.0F, 2.0F}, {2.0F, 2.4F, 2.0F}, {2.0F, 2.4F, -2.0F});
    appendQuad(scene, "area light", 3, {-0.45F, 2.38F, -0.45F}, {0.45F, 2.38F, -0.45F}, {0.45F, 2.38F, 0.35F}, {-0.45F, 2.38F, 0.35F});
    appendBox(scene, "blue cube", 4, {-1.1F, 0.0F, -0.65F}, {-0.35F, 0.75F, 0.1F});
    appendBox(scene, "gold cube", 5, {0.35F, 0.0F, -1.15F}, {1.2F, 1.35F, -0.3F});

    addDefaultInstances(scene);
    scene.camera = {
        .position = {0.0F, 1.15F, 4.2F},
        .forward = {0.0F, -0.04F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .verticalFovRadians = 0.72F,
    };
    return scene;
}

} // namespace rt::scene
