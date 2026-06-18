#pragma once

#include <stdexcept>
#include <string_view>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class VulkanError final : public std::runtime_error {
public:
    VulkanError(VkResult result, std::string_view operation);

    [[nodiscard]] VkResult result() const noexcept;

private:
    VkResult result_;
};

void check(VkResult result, std::string_view operation);
[[nodiscard]] const char* resultName(VkResult result) noexcept;

} // namespace rt::vulkan
