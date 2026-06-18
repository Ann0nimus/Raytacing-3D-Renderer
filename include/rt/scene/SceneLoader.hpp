#pragma once

#include "rt/scene/Scene.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace rt::scene {

enum class SceneSource {
    ObjFile,
    BuiltinMaterialLab,
};

struct SceneDescriptor {
    std::string name;
    SceneSource source = SceneSource::ObjFile;
    std::filesystem::path path;
};

class SceneLoader {
public:
    [[nodiscard]] static std::vector<SceneDescriptor> discover(const std::filesystem::path& assetRoot);
    [[nodiscard]] static Scene load(const SceneDescriptor& descriptor);
    [[nodiscard]] static Scene loadObj(const std::filesystem::path& path);
    [[nodiscard]] static Scene materialLab();
};

} // namespace rt::scene
