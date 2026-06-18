#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace rt {

[[nodiscard]] std::filesystem::path executableDirectory(std::string_view argv0);

[[nodiscard]] std::filesystem::path findDirectoryNearExecutable(std::string_view argv0,
                                                                 std::string_view directoryName,
                                                                 std::vector<std::filesystem::path> extraSearchRoots = {});

[[nodiscard]] std::vector<std::byte> readBinaryFile(const std::filesystem::path& path);

} // namespace rt
