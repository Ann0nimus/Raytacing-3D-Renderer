#pragma once

#include "rt/vulkan/Buffer.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class Device;

struct ShaderStageDesc {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
    std::span<const std::uint32_t> spirv;
    const char* entryPoint = "main";
};

struct HitGroupDesc {
    std::optional<std::uint32_t> closestHitStage;
    std::optional<std::uint32_t> anyHitStage;
    std::optional<std::uint32_t> intersectionStage;
};

struct RayTracingPipelineDesc {
    std::vector<ShaderStageDesc> stages;
    std::vector<std::uint32_t> raygenStageIndices;
    std::vector<std::uint32_t> missStageIndices;
    std::vector<HitGroupDesc> hitGroups;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    std::vector<VkPushConstantRange> pushConstants;
    std::uint32_t maxRayRecursionDepth = 1;
};

struct ShaderBindingTableLayout {
    VkStridedDeviceAddressRegionKHR raygen{};
    VkStridedDeviceAddressRegionKHR miss{};
    VkStridedDeviceAddressRegionKHR hit{};
    VkStridedDeviceAddressRegionKHR callable{};
};

class RayTracingPipeline {
public:
    RayTracingPipeline() = default;
    RayTracingPipeline(const Device& device, const RayTracingPipelineDesc& desc);
    ~RayTracingPipeline();

    RayTracingPipeline(const RayTracingPipeline&) = delete;
    RayTracingPipeline& operator=(const RayTracingPipeline&) = delete;
    RayTracingPipeline(RayTracingPipeline&& other) noexcept;
    RayTracingPipeline& operator=(RayTracingPipeline&& other) noexcept;

    [[nodiscard]] VkPipeline handle() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] const ShaderBindingTableLayout& shaderBindingTable() const noexcept;

    void bind(VkCommandBuffer commandBuffer) const;
    void trace(VkCommandBuffer commandBuffer,
               std::uint32_t width,
               std::uint32_t height,
               std::uint32_t depth = 1) const;

private:
    void release() noexcept;
    void createLayout(const RayTracingPipelineDesc& desc);
    void createPipeline(const RayTracingPipelineDesc& desc);
    void createShaderBindingTable(std::uint32_t raygenGroupCount,
                                  std::uint32_t missGroupCount,
                                  std::uint32_t hitGroupCount);

    const Device* device_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    Buffer shaderBindingTableBuffer_;
    ShaderBindingTableLayout shaderBindingTable_{};
};

} // namespace rt::vulkan
