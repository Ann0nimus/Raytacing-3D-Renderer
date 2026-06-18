#include "rt/vulkan/AccelerationStructure.hpp"

#include "rt/vulkan/Commands.hpp"
#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace rt::vulkan {

namespace {

constexpr VkDeviceSize scratchAlignment = 256;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

VkTransformMatrixKHR toVkTransform(const Mat3x4& transform) noexcept
{
    VkTransformMatrixKHR result{};
    std::memcpy(result.matrix, transform.data(), sizeof(result.matrix));
    return result;
}

} // namespace

AccelerationStructure::AccelerationStructure(const Device& device,
                                             VkAccelerationStructureTypeKHR type,
                                             VkDeviceSize size)
    : device_(&device),
      type_(type),
      size_(size),
      storage_(device,
               {
                   .size = size,
                   .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   .debugName = "acceleration structure storage",
               })
{
    VkAccelerationStructureCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = storage_.handle(),
        .offset = 0,
        .size = size_,
        .type = type_,
    };
    check(device_->rt().createAccelerationStructure(device_->logicalDevice(), &createInfo, nullptr, &handle_),
          "vkCreateAccelerationStructureKHR");
}

AccelerationStructure::~AccelerationStructure()
{
    release();
}

AccelerationStructure::AccelerationStructure(AccelerationStructure&& other) noexcept
{
    *this = std::move(other);
}

AccelerationStructure& AccelerationStructure::operator=(AccelerationStructure&& other) noexcept
{
    if (this != &other) {
        release();
        device_ = other.device_;
        handle_ = other.handle_;
        type_ = other.type_;
        size_ = other.size_;
        storage_ = std::move(other.storage_);

        other.device_ = nullptr;
        other.handle_ = VK_NULL_HANDLE;
        other.type_ = VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR;
        other.size_ = 0;
    }
    return *this;
}

VkAccelerationStructureKHR AccelerationStructure::handle() const noexcept
{
    return handle_;
}

VkAccelerationStructureTypeKHR AccelerationStructure::type() const noexcept
{
    return type_;
}

VkDeviceAddress AccelerationStructure::deviceAddress() const noexcept
{
    if (handle_ == VK_NULL_HANDLE) {
        return 0;
    }

    VkAccelerationStructureDeviceAddressInfoKHR addressInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .accelerationStructure = handle_,
    };
    return device_->rt().getAccelerationStructureDeviceAddress(device_->logicalDevice(), &addressInfo);
}

VkDeviceSize AccelerationStructure::size() const noexcept
{
    return size_;
}

void AccelerationStructure::release() noexcept
{
    if (device_ != nullptr && handle_ != VK_NULL_HANDLE) {
        device_->rt().destroyAccelerationStructure(device_->logicalDevice(), handle_, nullptr);
    }

    handle_ = VK_NULL_HANDLE;
    device_ = nullptr;
    size_ = 0;
}

AccelerationStructureBuilder::AccelerationStructureBuilder(const Device& device)
    : device_(device)
{
}

AccelerationStructure AccelerationStructureBuilder::buildBottomLevelTriangles(CommandContext& commands,
                                                                               const TriangleGeometry& geometry,
                                                                               const BuildOptions& options) const
{
    return buildOrUpdateBottomLevel(commands, nullptr, geometry, options, BuildMode::Rebuild);
}

void AccelerationStructureBuilder::updateBottomLevelTriangles(CommandContext& commands,
                                                              AccelerationStructure& target,
                                                              const TriangleGeometry& geometry) const
{
    if (target.type() != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
        throw std::invalid_argument("target acceleration structure is not a BLAS");
    }

    buildOrUpdateBottomLevel(commands, &target, geometry, {.allowUpdate = true}, BuildMode::Refit);
}

AccelerationStructure AccelerationStructureBuilder::buildTopLevel(CommandContext& commands,
                                                                  std::span<const InstanceGeometry> instances,
                                                                  const BuildOptions& options) const
{
    return buildOrUpdateTopLevel(commands, nullptr, instances, options, BuildMode::Rebuild);
}

void AccelerationStructureBuilder::updateTopLevel(CommandContext& commands,
                                                  AccelerationStructure& target,
                                                  std::span<const InstanceGeometry> instances) const
{
    if (target.type() != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) {
        throw std::invalid_argument("target acceleration structure is not a TLAS");
    }

    buildOrUpdateTopLevel(commands, &target, instances, {.allowUpdate = true}, BuildMode::Refit);
}

AccelerationStructure AccelerationStructureBuilder::buildOrUpdateBottomLevel(CommandContext& commands,
                                                                             AccelerationStructure* target,
                                                                             const TriangleGeometry& geometry,
                                                                             const BuildOptions& options,
                                                                             BuildMode mode) const
{
    if (geometry.vertexAddress == 0 || geometry.vertexStride == 0 || geometry.vertexCount == 0) {
        throw std::invalid_argument("BLAS triangle geometry requires a valid vertex buffer");
    }
    if (geometry.indexAddress == 0 || geometry.indexCount == 0) {
        throw std::invalid_argument("BLAS triangle geometry requires a valid index buffer");
    }
    if (mode == BuildMode::Refit && target == nullptr) {
        throw std::invalid_argument("BLAS refit requires an existing acceleration structure");
    }

    const VkAccelerationStructureGeometryTrianglesDataKHR triangles{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .vertexFormat = geometry.vertexFormat,
        .vertexData = {.deviceAddress = geometry.vertexAddress},
        .vertexStride = geometry.vertexStride,
        .maxVertex = geometry.vertexCount - 1,
        .indexType = geometry.indexType,
        .indexData = {.deviceAddress = geometry.indexAddress},
        .transformData = {.deviceAddress = geometry.transformAddress},
    };

    const VkAccelerationStructureGeometryKHR asGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry = {.triangles = triangles},
        .flags = geometry.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0U,
    };

    const std::uint32_t primitiveCount = geometry.indexCount / 3;
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = flagsFor(options),
        .mode = mode == BuildMode::Refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                         : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &asGeometry,
    };

    VkAccelerationStructureBuildSizesInfoKHR sizes{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    device_.rt().getAccelerationStructureBuildSizes(device_.logicalDevice(),
                                                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                    &buildInfo,
                                                    &primitiveCount,
                                                    &sizes);

    AccelerationStructure built;
    if (mode == BuildMode::Rebuild) {
        built = AccelerationStructure(device_, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, sizes.accelerationStructureSize);
        target = &built;
    }

    const VkDeviceSize scratchSize = mode == BuildMode::Refit ? sizes.updateScratchSize : sizes.buildScratchSize;
    Buffer scratch(device_,
                   {
                       .size = alignUp(scratchSize, scratchAlignment),
                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       .debugName = "BLAS scratch",
                   });

    buildInfo.srcAccelerationStructure = mode == BuildMode::Refit ? target->handle() : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = target->handle();
    buildInfo.scratchData.deviceAddress = scratch.deviceAddress();

    const VkAccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = primitiveCount,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {&range};

    const VkCommandBuffer commandBuffer = commands.beginOneShot();
    device_.rt().cmdBuildAccelerationStructures(commandBuffer, 1, &buildInfo, ranges);
    commands.endSubmitAndWait();

    return built;
}

AccelerationStructure AccelerationStructureBuilder::buildOrUpdateTopLevel(CommandContext& commands,
                                                                          AccelerationStructure* target,
                                                                          std::span<const InstanceGeometry> instances,
                                                                          const BuildOptions& options,
                                                                          BuildMode mode) const
{
    if (instances.empty()) {
        throw std::invalid_argument("TLAS requires at least one instance");
    }
    if (mode == BuildMode::Refit && target == nullptr) {
        throw std::invalid_argument("TLAS refit requires an existing acceleration structure");
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    vkInstances.reserve(instances.size());

    for (const InstanceGeometry& instance : instances) {
        if (instance.bottomLevelAddress == 0) {
            throw std::invalid_argument("TLAS instance requires a valid BLAS device address");
        }
        vkInstances.push_back({
            .transform = toVkTransform(instance.transform),
            .instanceCustomIndex = instance.instanceId,
            .mask = instance.mask,
            .instanceShaderBindingTableRecordOffset = instance.shaderBindingTableRecordOffset,
            .flags = instance.flags,
            .accelerationStructureReference = instance.bottomLevelAddress,
        });
    }

    Buffer instanceBuffer(device_,
                          {
                              .size = sizeof(VkAccelerationStructureInstanceKHR) * vkInstances.size(),
                              .usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                       VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              .debugName = "TLAS instances",
                          });
    instanceBuffer.upload(commands, asBytes(std::span<const VkAccelerationStructureInstanceKHR>(vkInstances)));

    const VkAccelerationStructureGeometryInstancesDataKHR instanceData{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data = {.deviceAddress = instanceBuffer.deviceAddress()},
    };

    const VkAccelerationStructureGeometryKHR asGeometry{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = {.instances = instanceData},
    };

    const std::uint32_t primitiveCount = static_cast<std::uint32_t>(instances.size());
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = flagsFor(options),
        .mode = mode == BuildMode::Refit ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                         : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .geometryCount = 1,
        .pGeometries = &asGeometry,
    };

    VkAccelerationStructureBuildSizesInfoKHR sizes{
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    device_.rt().getAccelerationStructureBuildSizes(device_.logicalDevice(),
                                                    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                    &buildInfo,
                                                    &primitiveCount,
                                                    &sizes);

    AccelerationStructure built;
    if (mode == BuildMode::Rebuild) {
        built = AccelerationStructure(device_, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, sizes.accelerationStructureSize);
        target = &built;
    }

    const VkDeviceSize scratchSize = mode == BuildMode::Refit ? sizes.updateScratchSize : sizes.buildScratchSize;
    Buffer scratch(device_,
                   {
                       .size = alignUp(scratchSize, scratchAlignment),
                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       .debugName = "TLAS scratch",
                   });

    buildInfo.srcAccelerationStructure = mode == BuildMode::Refit ? target->handle() : VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = target->handle();
    buildInfo.scratchData.deviceAddress = scratch.deviceAddress();

    const VkAccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = primitiveCount,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges[] = {&range};

    const VkCommandBuffer commandBuffer = commands.beginOneShot();
    device_.rt().cmdBuildAccelerationStructures(commandBuffer, 1, &buildInfo, ranges);
    commands.endSubmitAndWait();

    return built;
}

VkBuildAccelerationStructureFlagsKHR AccelerationStructureBuilder::flagsFor(const BuildOptions& options) const noexcept
{
    VkBuildAccelerationStructureFlagsKHR flags = options.preferFastBuild
                                                    ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                                                    : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    if (options.allowUpdate) {
        flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }
    return flags;
}

} // namespace rt::vulkan
