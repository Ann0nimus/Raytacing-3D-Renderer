#include "rt/platform/Window.hpp"

#include "rt/vulkan/VulkanError.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace rt::platform {

Window::Window(std::uint32_t width, std::uint32_t height, std::string_view title)
{
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(static_cast<int>(width),
                               static_cast<int>(height),
                               std::string(title).c_str(),
                               nullptr,
                               nullptr);
    if (window_ == nullptr) {
        glfwTerminate();
        throw std::runtime_error("failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);

    std::uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0) {
        throw std::runtime_error("GLFW did not report required Vulkan extensions");
    }
    requiredExtensions_.assign(extensions, extensions + extensionCount);
}

Window::~Window()
{
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::shouldClose() const noexcept
{
    return glfwWindowShouldClose(window_) == GLFW_TRUE;
}

void Window::pollEvents() const
{
    glfwPollEvents();
}

GLFWwindow* Window::handle() const noexcept
{
    return window_;
}

VkExtent2D Window::framebufferExtent() const noexcept
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    return {
        .width = static_cast<std::uint32_t>(std::max(width, 1)),
        .height = static_cast<std::uint32_t>(std::max(height, 1)),
    };
}

bool Window::framebufferResized() const noexcept
{
    return framebufferResized_;
}

void Window::clearFramebufferResized() noexcept
{
    framebufferResized_ = false;
}

std::span<const char* const> Window::requiredVulkanExtensions() const noexcept
{
    return requiredExtensions_;
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    rt::vulkan::check(glfwCreateWindowSurface(instance, window_, nullptr, &surface), "glfwCreateWindowSurface");
    return surface;
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int)
{
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr) {
        self->framebufferResized_ = true;
    }
}

} // namespace rt::platform
