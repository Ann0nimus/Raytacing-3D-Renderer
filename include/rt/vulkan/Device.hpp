#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class Instance;

struct QueueFamilySelection {
    std::uint32_t graphicsCompute = 0;
};

struct DeviceConfig {
    bool requireRayTracing = true;
    std::optional<std::string> preferredDeviceName;
};

struct RayTracingLimits {
    std::uint32_t shaderGroupHandleSize = 0;
    std::uint32_t shaderGroupBaseAlignment = 0;
    std::uint32_t shaderGroupHandleAlignment = 0;
    std::uint32_t maxRayRecursionDepth = 0;
    std::uint64_t maxGeometryCount = 0;
    std::uint64_t maxInstanceCount = 0;
};

struct RayTracingFunctions {
    PFN_vkGetBufferDeviceAddressKHR getBufferDeviceAddress = nullptr;
    PFN_vkCreateAccelerationStructureKHR createAccelerationStructure = nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizes = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureDeviceAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAccelerationStructures = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR cmdWriteAccelerationStructuresProperties = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR createRayTracingPipelines = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR getRayTracingShaderGroupHandles = nullptr;
    PFN_vkCmdTraceRaysKHR cmdTraceRays = nullptr;
};

class Device {
public:
    Device(const Instance& instance, const DeviceConfig& config);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept;
    [[nodiscard]] VkDevice logicalDevice() const noexcept;
    [[nodiscard]] VkQueue graphicsQueue() const noexcept;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept;
    [[nodiscard]] const RayTracingLimits& rayTracingLimits() const noexcept;
    [[nodiscard]] const RayTracingFunctions& rt() const noexcept;

    [[nodiscard]] std::uint32_t findMemoryType(std::uint32_t typeBits,
                                               VkMemoryPropertyFlags required) const;

    void setObjectName(std::uint64_t handle, VkObjectType type, const char* name) const;

private:
    struct Candidate {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        QueueFamilySelection queueFamilies{};
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        RayTracingLimits rayTracingLimits{};
        int score = 0;
    };

    [[nodiscard]] Candidate pickPhysicalDevice(const Instance& instance, const DeviceConfig& config) const;
    [[nodiscard]] std::optional<QueueFamilySelection> findQueueFamilies(VkPhysicalDevice physicalDevice) const;
    [[nodiscard]] bool supportsExtensions(VkPhysicalDevice physicalDevice,
                                          std::span<const char* const> requiredExtensions) const;
    [[nodiscard]] bool supportsRayTracingFeatures(VkPhysicalDevice physicalDevice,
                                                  RayTracingLimits& limits) const;
    void createLogicalDevice(const Candidate& candidate, const DeviceConfig& config);
    void loadRayTracingFunctions();

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = 0;
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memoryProperties_{};
    RayTracingLimits rayTracingLimits_{};
    RayTracingFunctions rt_{};
    PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectName_ = nullptr;
};

} // namespace rt::vulkan
