#include "rt/vulkan/Device.hpp"

#include "rt/vulkan/Instance.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace rt::vulkan {

namespace {

constexpr std::array requiredRayTracingExtensions{
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
};

template <typename T>
T* appendPNext(T& value, void*& head) noexcept
{
    value.pNext = head;
    head = &value;
    return &value;
}

} // namespace

Device::Device(const Instance& instance, const DeviceConfig& config)
{
    const Candidate candidate = pickPhysicalDevice(instance, config);
    physicalDevice_ = candidate.physicalDevice;
    graphicsQueueFamily_ = candidate.queueFamilies.graphicsCompute;
    properties_ = candidate.properties;
    memoryProperties_ = candidate.memoryProperties;
    rayTracingLimits_ = candidate.rayTracingLimits;
    createLogicalDevice(candidate, config);
    loadRayTracingFunctions();

    setDebugUtilsObjectName_ = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device_, "vkSetDebugUtilsObjectNameEXT"));
}

Device::~Device()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        vkDestroyDevice(device_, nullptr);
    }
}

VkPhysicalDevice Device::physicalDevice() const noexcept
{
    return physicalDevice_;
}

VkDevice Device::logicalDevice() const noexcept
{
    return device_;
}

VkQueue Device::graphicsQueue() const noexcept
{
    return graphicsQueue_;
}

std::uint32_t Device::graphicsQueueFamily() const noexcept
{
    return graphicsQueueFamily_;
}

const VkPhysicalDeviceProperties& Device::properties() const noexcept
{
    return properties_;
}

const RayTracingLimits& Device::rayTracingLimits() const noexcept
{
    return rayTracingLimits_;
}

const RayTracingFunctions& Device::rt() const noexcept
{
    return rt_;
}

std::uint32_t Device::findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags required) const
{
    for (std::uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
        const bool supported = (typeBits & (1U << i)) != 0;
        const bool hasFlags = (memoryProperties_.memoryTypes[i].propertyFlags & required) == required;
        if (supported && hasFlags) {
            return i;
        }
    }

    throw std::runtime_error("no compatible Vulkan memory type found");
}

void Device::setObjectName(std::uint64_t handle, VkObjectType type, const char* name) const
{
    if (setDebugUtilsObjectName_ == nullptr || name == nullptr) {
        return;
    }

    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };
    setDebugUtilsObjectName_(device_, &nameInfo);
}

Device::Candidate Device::pickPhysicalDevice(const Instance& instance, const DeviceConfig& config) const
{
    std::uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance.handle(), &deviceCount, nullptr), "vkEnumeratePhysicalDevices(count)");
    if (deviceCount == 0) {
        throw std::runtime_error("no Vulkan physical devices found");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    check(vkEnumeratePhysicalDevices(instance.handle(), &deviceCount, devices.data()), "vkEnumeratePhysicalDevices(list)");

    std::optional<Candidate> best;
    for (VkPhysicalDevice physicalDevice : devices) {
        VkPhysicalDeviceProperties properties{};
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

        auto queueFamilies = findQueueFamilies(physicalDevice);
        if (!queueFamilies.has_value()) {
            continue;
        }

        RayTracingLimits limits{};
        if (config.requireRayTracing) {
            if (!supportsExtensions(physicalDevice, std::span<const char* const>(requiredRayTracingExtensions)) ||
                !supportsRayTracingFeatures(physicalDevice, limits)) {
                continue;
            }
        }

        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        score += static_cast<int>(properties.limits.maxImageDimension2D / 1024);
        if (config.preferredDeviceName.has_value() &&
            config.preferredDeviceName.value() == properties.deviceName) {
            score += 10000;
        }

        Candidate candidate{
            .physicalDevice = physicalDevice,
            .queueFamilies = *queueFamilies,
            .properties = properties,
            .memoryProperties = memoryProperties,
            .rayTracingLimits = limits,
            .score = score,
        };

        if (!best.has_value() || candidate.score > best->score) {
            best = candidate;
        }
    }

    if (!best.has_value()) {
        throw std::runtime_error("no Vulkan device satisfies renderer requirements");
    }
    return *best;
}

std::optional<QueueFamilySelection> Device::findQueueFamilies(VkPhysicalDevice physicalDevice) const
{
    std::uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i) {
        const VkQueueFlags flags = queueFamilies[i].queueFlags;
        if ((flags & VK_QUEUE_GRAPHICS_BIT) != 0 && (flags & VK_QUEUE_COMPUTE_BIT) != 0) {
            return QueueFamilySelection{.graphicsCompute = i};
        }
    }

    return std::nullopt;
}

bool Device::supportsExtensions(VkPhysicalDevice physicalDevice, std::span<const char* const> requiredExtensions) const
{
    std::uint32_t extensionCount = 0;
    check(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr),
          "vkEnumerateDeviceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    check(vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data()),
          "vkEnumerateDeviceExtensionProperties(list)");

    std::unordered_set<std::string> available;
    for (const auto& extension : extensions) {
        available.emplace(extension.extensionName);
    }

    return std::ranges::all_of(requiredExtensions, [&available](const char* required) {
        return available.contains(required);
    });
}

bool Device::supportsRayTracingFeatures(VkPhysicalDevice physicalDevice, RayTracingLimits& limits) const
{
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &bufferDeviceAddress,
    };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = &accelerationStructure,
    };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .pNext = &rayTracingPipeline,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &rayQuery,
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

    if (bufferDeviceAddress.bufferDeviceAddress != VK_TRUE ||
        accelerationStructure.accelerationStructure != VK_TRUE ||
        rayTracingPipeline.rayTracingPipeline != VK_TRUE ||
        rayQuery.rayQuery != VK_TRUE) {
        return false;
    }

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationStructureProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
    };
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
        .pNext = &accelerationStructureProperties,
    };
    VkPhysicalDeviceProperties2 properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rayTracingProperties,
    };
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    limits = {
        .shaderGroupHandleSize = rayTracingProperties.shaderGroupHandleSize,
        .shaderGroupBaseAlignment = rayTracingProperties.shaderGroupBaseAlignment,
        .shaderGroupHandleAlignment = rayTracingProperties.shaderGroupHandleAlignment,
        .maxRayRecursionDepth = rayTracingProperties.maxRayRecursionDepth,
        .maxGeometryCount = accelerationStructureProperties.maxGeometryCount,
        .maxInstanceCount = accelerationStructureProperties.maxInstanceCount,
    };
    return true;
}

void Device::createLogicalDevice(const Candidate& candidate, const DeviceConfig& config)
{
    const float queuePriority = 1.0F;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = candidate.queueFamilies.graphicsCompute,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    std::vector<const char*> extensions;
    if (config.requireRayTracing) {
        extensions.assign(requiredRayTracingExtensions.begin(), requiredRayTracingExtensions.end());
    }

    void* featureChain = nullptr;
    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddress{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .accelerationStructure = VK_TRUE,
    };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .rayTracingPipeline = VK_TRUE,
    };
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .rayQuery = VK_TRUE,
    };

    if (config.requireRayTracing) {
        appendPNext(bufferDeviceAddress, featureChain);
        appendPNext(accelerationStructure, featureChain);
        appendPNext(rayTracingPipeline, featureChain);
        appendPNext(rayQuery, featureChain);
    }

    const VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = featureChain,
    };

    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
    };

    check(vkCreateDevice(candidate.physicalDevice, &createInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, candidate.queueFamilies.graphicsCompute, 0, &graphicsQueue_);
}

void Device::loadRayTracingFunctions()
{
    auto load = [this](const char* name) {
        PFN_vkVoidFunction fn = vkGetDeviceProcAddr(device_, name);
        if (fn == nullptr) {
            throw std::runtime_error(std::string("missing Vulkan device function: ") + name);
        }
        return fn;
    };

    rt_.getBufferDeviceAddress = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(load("vkGetBufferDeviceAddressKHR"));
    rt_.createAccelerationStructure =
        reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(load("vkCreateAccelerationStructureKHR"));
    rt_.destroyAccelerationStructure =
        reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(load("vkDestroyAccelerationStructureKHR"));
    rt_.getAccelerationStructureBuildSizes =
        reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(load("vkGetAccelerationStructureBuildSizesKHR"));
    rt_.getAccelerationStructureDeviceAddress =
        reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(load("vkGetAccelerationStructureDeviceAddressKHR"));
    rt_.cmdBuildAccelerationStructures =
        reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(load("vkCmdBuildAccelerationStructuresKHR"));
    rt_.cmdWriteAccelerationStructuresProperties =
        reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(load("vkCmdWriteAccelerationStructuresPropertiesKHR"));
    rt_.createRayTracingPipelines =
        reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(load("vkCreateRayTracingPipelinesKHR"));
    rt_.getRayTracingShaderGroupHandles =
        reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(load("vkGetRayTracingShaderGroupHandlesKHR"));
    rt_.cmdTraceRays = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(load("vkCmdTraceRaysKHR"));
}

} // namespace rt::vulkan
