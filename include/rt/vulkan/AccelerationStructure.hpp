#pragma once

#include "rt/core/Math.hpp"
#include "rt/vulkan/Buffer.hpp"

#include <cstdint>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class CommandContext;
class Device;

enum class BuildMode {
    Rebuild,
    Refit,
};

struct BuildOptions {
    bool allowUpdate = false;
    bool preferFastBuild = false;
};

struct TriangleGeometry {
    VkDeviceAddress vertexAddress = 0;
    VkDeviceSize vertexStride = 0;
    std::uint32_t vertexCount = 0;
    VkFormat vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    VkDeviceAddress indexAddress = 0;
    std::uint32_t indexCount = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    VkDeviceAddress transformAddress = 0;
    bool opaque = true;
};

struct InstanceGeometry {
    VkDeviceAddress bottomLevelAddress = 0;
    Mat3x4 transform = identityTransform3x4();
    std::uint32_t instanceId = 0;
    std::uint32_t shaderBindingTableRecordOffset = 0;
    std::uint8_t mask = 0xFF;
    VkGeometryInstanceFlagsKHR flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
};

class AccelerationStructure {
public:
    AccelerationStructure() = default;
    AccelerationStructure(const Device& device,
                          VkAccelerationStructureTypeKHR type,
                          VkDeviceSize size);
    ~AccelerationStructure();

    AccelerationStructure(const AccelerationStructure&) = delete;
    AccelerationStructure& operator=(const AccelerationStructure&) = delete;
    AccelerationStructure(AccelerationStructure&& other) noexcept;
    AccelerationStructure& operator=(AccelerationStructure&& other) noexcept;

    [[nodiscard]] VkAccelerationStructureKHR handle() const noexcept;
    [[nodiscard]] VkAccelerationStructureTypeKHR type() const noexcept;
    [[nodiscard]] VkDeviceAddress deviceAddress() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;

private:
    void release() noexcept;

    const Device* device_ = nullptr;
    VkAccelerationStructureKHR handle_ = VK_NULL_HANDLE;
    VkAccelerationStructureTypeKHR type_ = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
    VkDeviceSize size_ = 0;
    Buffer storage_;
};

class AccelerationStructureBuilder {
public:
    explicit AccelerationStructureBuilder(const Device& device);

    AccelerationStructure buildBottomLevelTriangles(CommandContext& commands,
                                                    const TriangleGeometry& geometry,
                                                    const BuildOptions& options) const;

    void updateBottomLevelTriangles(CommandContext& commands,
                                    AccelerationStructure& target,
                                    const TriangleGeometry& geometry) const;

    AccelerationStructure buildTopLevel(CommandContext& commands,
                                        std::span<const InstanceGeometry> instances,
                                        const BuildOptions& options) const;

    void updateTopLevel(CommandContext& commands,
                        AccelerationStructure& target,
                        std::span<const InstanceGeometry> instances) const;

private:
    AccelerationStructure buildOrUpdateBottomLevel(CommandContext& commands,
                                                   AccelerationStructure* target,
                                                   const TriangleGeometry& geometry,
                                                   const BuildOptions& options,
                                                   BuildMode mode) const;

    AccelerationStructure buildOrUpdateTopLevel(CommandContext& commands,
                                                AccelerationStructure* target,
                                                std::span<const InstanceGeometry> instances,
                                                const BuildOptions& options,
                                                BuildMode mode) const;

    [[nodiscard]] VkBuildAccelerationStructureFlagsKHR flagsFor(const BuildOptions& options) const noexcept;

    const Device& device_;
};

} // namespace rt::vulkan
