#include "rt/vulkan/Instance.hpp"

#include "rt/vulkan/VulkanError.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <unordered_set>

namespace rt::vulkan {

namespace {

constexpr std::array validationLayers{"VK_LAYER_KHRONOS_validation"};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                             void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "Vulkan validation: " << callbackData->pMessage << '\n';
    }
    return VK_FALSE;
}

} // namespace

Instance::Instance(const InstanceConfig& config)
{
    validationEnabled_ = config.enableValidation && validationLayerAvailable();

    enabledExtensions_ = config.requiredExtensions;
    if (validationEnabled_ && extensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        enabledExtensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    const VkApplicationInfo appInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = config.applicationName.c_str(),
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Raytacing",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };

    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validationEnabled_ ? &debugCreateInfo : nullptr,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = validationEnabled_ ? static_cast<std::uint32_t>(validationLayers.size()) : 0U,
        .ppEnabledLayerNames = validationEnabled_ ? validationLayers.data() : nullptr,
        .enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions_.size()),
        .ppEnabledExtensionNames = enabledExtensions_.empty() ? nullptr : enabledExtensions_.data(),
    };

    check(vkCreateInstance(&createInfo, nullptr, &instance_), "vkCreateInstance");
    setupDebugMessenger();
}

Instance::~Instance()
{
    if (debugMessenger_ != VK_NULL_HANDLE) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy != nullptr) {
            destroy(instance_, debugMessenger_, nullptr);
        }
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

VkInstance Instance::handle() const noexcept
{
    return instance_;
}

bool Instance::validationEnabled() const noexcept
{
    return validationEnabled_;
}

std::span<const char* const> Instance::enabledExtensions() const noexcept
{
    return enabledExtensions_;
}

void Instance::setupDebugMessenger()
{
    if (!validationEnabled_) {
        return;
    }

    const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (create == nullptr) {
        return;
    }

    const VkDebugUtilsMessengerCreateInfoEXT createInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback,
    };
    check(create(instance_, &createInfo, nullptr, &debugMessenger_), "vkCreateDebugUtilsMessengerEXT");
}

bool Instance::validationLayerAvailable() const
{
    std::uint32_t layerCount = 0;
    check(vkEnumerateInstanceLayerProperties(&layerCount, nullptr), "vkEnumerateInstanceLayerProperties(count)");
    std::vector<VkLayerProperties> layers(layerCount);
    check(vkEnumerateInstanceLayerProperties(&layerCount, layers.data()), "vkEnumerateInstanceLayerProperties(list)");

    return std::ranges::all_of(validationLayers, [&layers](const char* required) {
        return std::ranges::any_of(layers, [required](const VkLayerProperties& layer) {
            return std::strcmp(layer.layerName, required) == 0;
        });
    });
}

bool Instance::extensionAvailable(const char* extensionName) const
{
    std::uint32_t extensionCount = 0;
    check(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr),
          "vkEnumerateInstanceExtensionProperties(count)");
    std::vector<VkExtensionProperties> extensions(extensionCount);
    check(vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data()),
          "vkEnumerateInstanceExtensionProperties(list)");

    return std::ranges::any_of(extensions, [extensionName](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, extensionName) == 0;
    });
}

} // namespace rt::vulkan
