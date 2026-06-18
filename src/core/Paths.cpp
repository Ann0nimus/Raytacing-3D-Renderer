#include "rt/core/Paths.hpp"

#include <fstream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace rt {

std::filesystem::path executableDirectory(std::string_view argv0)
{
#if defined(_WIN32)
    std::wstring buffer;
    buffer.resize(32768);
    const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) {
        buffer.resize(size);
        return std::filesystem::path(buffer).parent_path();
    }
#endif

    std::filesystem::path path(argv0);
    if (path.empty()) {
        return std::filesystem::current_path();
    }
    if (path.is_relative()) {
        path = std::filesystem::absolute(path);
    }
    return path.parent_path();
}

std::filesystem::path findDirectoryNearExecutable(std::string_view argv0,
                                                  std::string_view directoryName,
                                                  std::vector<std::filesystem::path> extraSearchRoots)
{
    std::vector<std::filesystem::path> roots;
    roots.push_back(executableDirectory(argv0));
    roots.push_back(std::filesystem::current_path());
    roots.insert(roots.end(), extraSearchRoots.begin(), extraSearchRoots.end());

    for (const auto& root : roots) {
        std::filesystem::path cursor = root;
        for (int i = 0; i < 6 && !cursor.empty(); ++i) {
            const auto candidate = cursor / directoryName;
            if (std::filesystem::is_directory(candidate)) {
                return candidate;
            }
            cursor = cursor.parent_path();
        }
    }

    throw std::runtime_error("could not locate runtime directory: " + std::string(directoryName));
}

std::vector<std::byte> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("failed to open binary file: " + path.string());
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        throw std::runtime_error("binary file is empty: " + path.string());
    }

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("failed to read binary file: " + path.string());
    }

    return data;
}

} // namespace rt
