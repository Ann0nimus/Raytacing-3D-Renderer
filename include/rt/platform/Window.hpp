#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace rt::platform {

class Window {
public:
    Window(std::uint32_t width, std::uint32_t height, std::string_view title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool shouldClose() const noexcept;
    void pollEvents() const;

    [[nodiscard]] GLFWwindow* handle() const noexcept;
    [[nodiscard]] VkExtent2D framebufferExtent() const noexcept;
    [[nodiscard]] bool framebufferResized() const noexcept;
    void clearFramebufferResized() noexcept;

    [[nodiscard]] std::span<const char* const> requiredVulkanExtensions() const noexcept;
    [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    bool framebufferResized_ = false;
    std::vector<const char*> requiredExtensions_;
};

} // namespace rt::platform
