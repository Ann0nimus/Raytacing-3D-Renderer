#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class Device;

class Swapchain {
public:
    Swapchain() = default;
    Swapchain(const Device& device, VkSurfaceKHR surface, VkExtent2D requestedExtent);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;

    [[nodiscard]] VkSwapchainKHR handle() const noexcept;
    [[nodiscard]] VkFormat format() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;
    [[nodiscard]] const std::vector<VkImage>& images() const noexcept;
    [[nodiscard]] const std::vector<VkImageView>& imageViews() const noexcept;

    [[nodiscard]] VkResult acquireNextImage(VkSemaphore signalSemaphore, std::uint32_t& imageIndex) const;
    [[nodiscard]] VkResult present(std::uint32_t imageIndex, VkSemaphore waitSemaphore) const;

private:
    struct SupportDetails {
        VkSurfaceCapabilitiesKHR capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    [[nodiscard]] SupportDetails querySupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
    [[nodiscard]] VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    [[nodiscard]] VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    [[nodiscard]] VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D requestedExtent) const;
    void createImageViews();
    void release() noexcept;

    const Device* device_ = nullptr;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
};

} // namespace rt::vulkan
