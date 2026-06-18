#pragma once

#include <vulkan/vulkan.h>

namespace rt::vulkan {

class Device;

class CommandContext {
public:
    explicit CommandContext(const Device& device);
    ~CommandContext();

    CommandContext(const CommandContext&) = delete;
    CommandContext& operator=(const CommandContext&) = delete;

    [[nodiscard]] VkCommandBuffer beginOneShot();
    void endSubmitAndWait();
    [[nodiscard]] VkCommandBuffer current() const noexcept;

private:
    const Device& device_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer current_ = VK_NULL_HANDLE;
    bool recording_ = false;
};

} // namespace rt::vulkan
