#pragma once

#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

struct InstanceConfig {
    std::string applicationName = "Raytacing";
    bool enableValidation = true;
    std::vector<const char*> requiredExtensions;
};

class Instance {
public:
    explicit Instance(const InstanceConfig& config);
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] VkInstance handle() const noexcept;
    [[nodiscard]] bool validationEnabled() const noexcept;
    [[nodiscard]] std::span<const char* const> enabledExtensions() const noexcept;

private:
    void setupDebugMessenger();
    [[nodiscard]] bool validationLayerAvailable() const;
    [[nodiscard]] bool extensionAvailable(const char* extensionName) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    bool validationEnabled_ = false;
    std::vector<const char*> enabledExtensions_;
};

} // namespace rt::vulkan
