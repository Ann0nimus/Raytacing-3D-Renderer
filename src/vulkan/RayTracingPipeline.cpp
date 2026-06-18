#include "rt/vulkan/RayTracingPipeline.hpp"

#include "rt/vulkan/Commands.hpp"
#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace rt::vulkan {

namespace {

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

class ShaderModule {
public:
    ShaderModule(const Device& device, std::span<const std::uint32_t> spirv)
        : device_(&device)
    {
        if (spirv.empty()) {
            throw std::invalid_argument("shader module requires non-empty SPIR-V");
        }

        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spirv.size_bytes(),
            .pCode = spirv.data(),
        };
        check(vkCreateShaderModule(device_->logicalDevice(), &createInfo, nullptr, &module_),
              "vkCreateShaderModule");
    }

    ~ShaderModule()
    {
        if (device_ != nullptr && module_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_->logicalDevice(), module_, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&& other) noexcept
    {
        *this = std::move(other);
    }
    ShaderModule& operator=(ShaderModule&& other) noexcept
    {
        if (this != &other) {
            if (device_ != nullptr && module_ != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device_->logicalDevice(), module_, nullptr);
            }
            device_ = other.device_;
            module_ = other.module_;
            other.device_ = nullptr;
            other.module_ = VK_NULL_HANDLE;
        }
        return *this;
    }

    [[nodiscard]] VkShaderModule handle() const noexcept
    {
        return module_;
    }

private:
    const Device* device_ = nullptr;
    VkShaderModule module_ = VK_NULL_HANDLE;
};

void validateStageIndex(std::span<const ShaderStageDesc> stages,
                        std::uint32_t index,
                        VkShaderStageFlagBits expectedStage,
                        const char* groupName)
{
    if (index >= stages.size()) {
        throw std::out_of_range(std::string(groupName) + " references an invalid shader stage index");
    }
    if (stages[index].stage != expectedStage) {
        throw std::invalid_argument(std::string(groupName) + " references a shader stage with the wrong type");
    }
}

} // namespace

RayTracingPipeline::RayTracingPipeline(const Device& device, const RayTracingPipelineDesc& desc)
    : device_(&device)
{
    if (desc.raygenStageIndices.size() != 1) {
        throw std::invalid_argument("ray tracing pipeline requires exactly one ray generation group");
    }
    if (desc.maxRayRecursionDepth == 0 ||
        desc.maxRayRecursionDepth > device.rayTracingLimits().maxRayRecursionDepth) {
        throw std::invalid_argument("ray tracing pipeline recursion depth is not supported by this device");
    }

    createLayout(desc);
    createPipeline(desc);
    createShaderBindingTable(static_cast<std::uint32_t>(desc.raygenStageIndices.size()),
                             static_cast<std::uint32_t>(desc.missStageIndices.size()),
                             static_cast<std::uint32_t>(desc.hitGroups.size()));
}

RayTracingPipeline::~RayTracingPipeline()
{
    release();
}

RayTracingPipeline::RayTracingPipeline(RayTracingPipeline&& other) noexcept
{
    *this = std::move(other);
}

RayTracingPipeline& RayTracingPipeline::operator=(RayTracingPipeline&& other) noexcept
{
    if (this != &other) {
        release();
        device_ = other.device_;
        layout_ = other.layout_;
        pipeline_ = other.pipeline_;
        shaderBindingTableBuffer_ = std::move(other.shaderBindingTableBuffer_);
        shaderBindingTable_ = other.shaderBindingTable_;

        other.device_ = nullptr;
        other.layout_ = VK_NULL_HANDLE;
        other.pipeline_ = VK_NULL_HANDLE;
        other.shaderBindingTable_ = {};
    }
    return *this;
}

VkPipeline RayTracingPipeline::handle() const noexcept
{
    return pipeline_;
}

VkPipelineLayout RayTracingPipeline::layout() const noexcept
{
    return layout_;
}

const ShaderBindingTableLayout& RayTracingPipeline::shaderBindingTable() const noexcept
{
    return shaderBindingTable_;
}

void RayTracingPipeline::bind(VkCommandBuffer commandBuffer) const
{
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
}

void RayTracingPipeline::trace(VkCommandBuffer commandBuffer,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::uint32_t depth) const
{
    device_->rt().cmdTraceRays(commandBuffer,
                               &shaderBindingTable_.raygen,
                               &shaderBindingTable_.miss,
                               &shaderBindingTable_.hit,
                               &shaderBindingTable_.callable,
                               width,
                               height,
                               depth);
}

void RayTracingPipeline::release() noexcept
{
    if (device_ != nullptr) {
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_->logicalDevice(), pipeline_, nullptr);
        }
        if (layout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_->logicalDevice(), layout_, nullptr);
        }
    }

    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    device_ = nullptr;
    shaderBindingTable_ = {};
}

void RayTracingPipeline::createLayout(const RayTracingPipelineDesc& desc)
{
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(desc.descriptorSetLayouts.size()),
        .pSetLayouts = desc.descriptorSetLayouts.empty() ? nullptr : desc.descriptorSetLayouts.data(),
        .pushConstantRangeCount = static_cast<std::uint32_t>(desc.pushConstants.size()),
        .pPushConstantRanges = desc.pushConstants.empty() ? nullptr : desc.pushConstants.data(),
    };
    check(vkCreatePipelineLayout(device_->logicalDevice(), &layoutInfo, nullptr, &layout_),
          "vkCreatePipelineLayout(ray tracing)");
}

void RayTracingPipeline::createPipeline(const RayTracingPipelineDesc& desc)
{
    std::vector<ShaderModule> modules;
    modules.reserve(desc.stages.size());
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    stages.reserve(desc.stages.size());

    for (const ShaderStageDesc& stageDesc : desc.stages) {
        modules.emplace_back(*device_, stageDesc.spirv);
        stages.push_back({
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stageDesc.stage,
            .module = modules.back().handle(),
            .pName = stageDesc.entryPoint,
        });
    }

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
    groups.reserve(desc.raygenStageIndices.size() + desc.missStageIndices.size() + desc.hitGroups.size());

    for (std::uint32_t stageIndex : desc.raygenStageIndices) {
        validateStageIndex(desc.stages, stageIndex, VK_SHADER_STAGE_RAYGEN_BIT_KHR, "raygen group");
        groups.push_back({
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = stageIndex,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        });
    }

    for (std::uint32_t stageIndex : desc.missStageIndices) {
        validateStageIndex(desc.stages, stageIndex, VK_SHADER_STAGE_MISS_BIT_KHR, "miss group");
        groups.push_back({
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
            .generalShader = stageIndex,
            .closestHitShader = VK_SHADER_UNUSED_KHR,
            .anyHitShader = VK_SHADER_UNUSED_KHR,
            .intersectionShader = VK_SHADER_UNUSED_KHR,
        });
    }

    for (const HitGroupDesc& hitGroup : desc.hitGroups) {
        const bool procedural = hitGroup.intersectionStage.has_value();
        if (hitGroup.closestHitStage.has_value()) {
            validateStageIndex(desc.stages, *hitGroup.closestHitStage, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "hit group");
        }
        if (hitGroup.anyHitStage.has_value()) {
            validateStageIndex(desc.stages, *hitGroup.anyHitStage, VK_SHADER_STAGE_ANY_HIT_BIT_KHR, "hit group");
        }
        if (hitGroup.intersectionStage.has_value()) {
            validateStageIndex(desc.stages, *hitGroup.intersectionStage, VK_SHADER_STAGE_INTERSECTION_BIT_KHR, "hit group");
        }

        groups.push_back({
            .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
            .type = procedural ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
                               : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
            .generalShader = VK_SHADER_UNUSED_KHR,
            .closestHitShader = hitGroup.closestHitStage.value_or(VK_SHADER_UNUSED_KHR),
            .anyHitShader = hitGroup.anyHitStage.value_or(VK_SHADER_UNUSED_KHR),
            .intersectionShader = hitGroup.intersectionStage.value_or(VK_SHADER_UNUSED_KHR),
        });
    }

    const VkRayTracingPipelineCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .groupCount = static_cast<std::uint32_t>(groups.size()),
        .pGroups = groups.data(),
        .maxPipelineRayRecursionDepth = desc.maxRayRecursionDepth,
        .layout = layout_,
    };

    check(device_->rt().createRayTracingPipelines(device_->logicalDevice(),
                                                  VK_NULL_HANDLE,
                                                  VK_NULL_HANDLE,
                                                  1,
                                                  &createInfo,
                                                  nullptr,
                                                  &pipeline_),
          "vkCreateRayTracingPipelinesKHR");
}

void RayTracingPipeline::createShaderBindingTable(std::uint32_t raygenGroupCount,
                                                  std::uint32_t missGroupCount,
                                                  std::uint32_t hitGroupCount)
{
    const RayTracingLimits& limits = device_->rayTracingLimits();
    const std::uint32_t groupCount = raygenGroupCount + missGroupCount + hitGroupCount;
    const VkDeviceSize handleSize = limits.shaderGroupHandleSize;
    const VkDeviceSize recordStride = alignUp(handleSize, limits.shaderGroupBaseAlignment);
    const VkDeviceSize raygenSize = raygenGroupCount * recordStride;
    const VkDeviceSize missSize = missGroupCount * recordStride;
    const VkDeviceSize hitSize = hitGroupCount * recordStride;
    const VkDeviceSize raygenOffset = 0;
    const VkDeviceSize missOffset = alignUp(raygenOffset + raygenSize, limits.shaderGroupBaseAlignment);
    const VkDeviceSize hitOffset = alignUp(missOffset + missSize, limits.shaderGroupBaseAlignment);
    const VkDeviceSize totalSize = hitOffset + hitSize;

    std::vector<std::byte> shaderHandles(groupCount * handleSize);
    check(device_->rt().getRayTracingShaderGroupHandles(device_->logicalDevice(),
                                                        pipeline_,
                                                        0,
                                                        groupCount,
                                                        shaderHandles.size(),
                                                        shaderHandles.data()),
          "vkGetRayTracingShaderGroupHandlesKHR");

    std::vector<std::byte> table(totalSize);
    for (std::uint32_t groupIndex = 0; groupIndex < groupCount; ++groupIndex) {
        VkDeviceSize sectionOffset = 0;
        std::uint32_t sectionIndex = groupIndex;

        if (groupIndex < raygenGroupCount) {
            sectionOffset = raygenOffset;
        } else if (groupIndex < raygenGroupCount + missGroupCount) {
            sectionOffset = missOffset;
            sectionIndex = groupIndex - raygenGroupCount;
        } else {
            sectionOffset = hitOffset;
            sectionIndex = groupIndex - raygenGroupCount - missGroupCount;
        }

        std::memcpy(table.data() + sectionOffset + sectionIndex * recordStride,
                    shaderHandles.data() + groupIndex * handleSize,
                    handleSize);
    }

    shaderBindingTableBuffer_ = Buffer(*device_,
                                       {
                                           .size = totalSize,
                                           .usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                           .debugName = "ray tracing shader binding table",
                                       });

    CommandContext uploadCommands(*device_);
    shaderBindingTableBuffer_.upload(uploadCommands, std::span<const std::byte>(table));

    const VkDeviceAddress baseAddress = shaderBindingTableBuffer_.deviceAddress();
    shaderBindingTable_.raygen = {
        .deviceAddress = baseAddress + raygenOffset,
        .stride = recordStride,
        .size = raygenSize,
    };
    shaderBindingTable_.miss = {
        .deviceAddress = baseAddress + missOffset,
        .stride = recordStride,
        .size = missSize,
    };
    shaderBindingTable_.hit = {
        .deviceAddress = baseAddress + hitOffset,
        .stride = recordStride,
        .size = hitSize,
    };
    shaderBindingTable_.callable = {};
}

} // namespace rt::vulkan
