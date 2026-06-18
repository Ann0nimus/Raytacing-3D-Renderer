#include "rt/vulkan/Swapchain.hpp"

#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rt::vulkan {

Swapchain::Swapchain(const Device& device, VkSurfaceKHR surface, VkExtent2D requestedExtent)
    : device_(&device),
      surface_(surface)
{
    const SupportDetails support = querySupport(device.physicalDevice(), surface);
    if (support.formats.empty() || support.presentModes.empty()) {
        throw std::runtime_error("surface does not support swapchain presentation");
    }

    const VkSurfaceFormatKHR surfaceFormat = chooseFormat(support.formats);
    const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
    extent_ = chooseExtent(support.capabilities, requestedExtent);

    std::uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }

    const std::uint32_t queueFamilies[] = {device.graphicsQueueFamily(), device.presentQueueFamily()};
    const bool sharedQueues = device.graphicsQueueFamily() != device.presentQueueFamily();

    const VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent_,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode = sharedQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = sharedQueues ? 2U : 0U,
        .pQueueFamilyIndices = sharedQueues ? queueFamilies : nullptr,
        .preTransform = support.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
    };

    check(vkCreateSwapchainKHR(device_->logicalDevice(), &createInfo, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    format_ = surfaceFormat.format;

    std::uint32_t swapImageCount = 0;
    check(vkGetSwapchainImagesKHR(device_->logicalDevice(), swapchain_, &swapImageCount, nullptr),
          "vkGetSwapchainImagesKHR(count)");
    images_.resize(swapImageCount);
    check(vkGetSwapchainImagesKHR(device_->logicalDevice(), swapchain_, &swapImageCount, images_.data()),
          "vkGetSwapchainImagesKHR(list)");
    createImageViews();
}

Swapchain::~Swapchain()
{
    release();
}

Swapchain::Swapchain(Swapchain&& other) noexcept
{
    *this = std::move(other);
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept
{
    if (this != &other) {
        release();
        device_ = other.device_;
        surface_ = other.surface_;
        swapchain_ = other.swapchain_;
        format_ = other.format_;
        extent_ = other.extent_;
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);

        other.device_ = nullptr;
        other.surface_ = VK_NULL_HANDLE;
        other.swapchain_ = VK_NULL_HANDLE;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {};
    }
    return *this;
}

VkSwapchainKHR Swapchain::handle() const noexcept
{
    return swapchain_;
}

VkFormat Swapchain::format() const noexcept
{
    return format_;
}

VkExtent2D Swapchain::extent() const noexcept
{
    return extent_;
}

const std::vector<VkImage>& Swapchain::images() const noexcept
{
    return images_;
}

const std::vector<VkImageView>& Swapchain::imageViews() const noexcept
{
    return imageViews_;
}

VkResult Swapchain::acquireNextImage(VkSemaphore signalSemaphore, std::uint32_t& imageIndex) const
{
    return vkAcquireNextImageKHR(device_->logicalDevice(),
                                 swapchain_,
                                 std::numeric_limits<std::uint64_t>::max(),
                                 signalSemaphore,
                                 VK_NULL_HANDLE,
                                 &imageIndex);
}

VkResult Swapchain::present(std::uint32_t imageIndex, VkSemaphore waitSemaphore) const
{
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &imageIndex,
    };
    return vkQueuePresentKHR(device_->presentQueue(), &presentInfo);
}

Swapchain::SupportDetails Swapchain::querySupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const
{
    SupportDetails details;
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t formatCount = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    details.formats.resize(formatCount);
    if (formatCount > 0) {
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.formats.data()),
              "vkGetPhysicalDeviceSurfaceFormatsKHR(list)");
    }

    std::uint32_t presentModeCount = 0;
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr),
          "vkGetPhysicalDeviceSurfacePresentModesKHR(count)");
    details.presentModes.resize(presentModeCount);
    if (presentModeCount > 0) {
        check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.presentModes.data()),
              "vkGetPhysicalDeviceSurfacePresentModesKHR(list)");
    }

    return details;
}

VkSurfaceFormatKHR Swapchain::chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const auto& format : formats) {
        if ((format.format == VK_FORMAT_R8G8B8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkPresentModeKHR Swapchain::choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
{
    if (std::ranges::find(presentModes, VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end()) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D requestedExtent) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    return {
        .width = std::clamp(requestedExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        .height = std::clamp(requestedExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

void Swapchain::createImageViews()
{
    imageViews_.reserve(images_.size());
    for (VkImage image : images_) {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format_,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        };
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_->logicalDevice(), &viewInfo, nullptr, &view), "vkCreateImageView(swapchain)");
        imageViews_.push_back(view);
    }
}

void Swapchain::release() noexcept
{
    if (device_ != nullptr) {
        for (VkImageView view : imageViews_) {
            vkDestroyImageView(device_->logicalDevice(), view, nullptr);
        }
        if (swapchain_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device_->logicalDevice(), swapchain_, nullptr);
        }
    }

    device_ = nullptr;
    surface_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
    images_.clear();
    imageViews_.clear();
}

} // namespace rt::vulkan
