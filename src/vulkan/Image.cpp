#include "rt/vulkan/Image.hpp"

#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <stdexcept>
#include <utility>

namespace rt::vulkan {

Image::Image(const Device& device, const ImageDesc& desc)
    : device_(&device),
      format_(desc.format),
      extent_(desc.extent)
{
    if (desc.extent.width == 0 || desc.extent.height == 0) {
        throw std::invalid_argument("image extent must be non-zero");
    }
    if (desc.format == VK_FORMAT_UNDEFINED) {
        throw std::invalid_argument("image format must be defined");
    }
    if (desc.usage == 0) {
        throw std::invalid_argument("image usage must be non-zero");
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = desc.format,
        .extent = {.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = desc.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    check(vkCreateImage(device_->logicalDevice(), &imageInfo, nullptr, &image_), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_->logicalDevice(), image_, &requirements);

    const VkMemoryAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size,
        .memoryTypeIndex = device_->findMemoryType(requirements.memoryTypeBits, desc.memoryProperties),
    };
    check(vkAllocateMemory(device_->logicalDevice(), &allocateInfo, nullptr, &memory_), "vkAllocateMemory(image)");
    check(vkBindImageMemory(device_->logicalDevice(), image_, memory_, 0), "vkBindImageMemory");

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image_,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = desc.format,
        .subresourceRange = {
            .aspectMask = desc.aspect,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    check(vkCreateImageView(device_->logicalDevice(), &viewInfo, nullptr, &view_), "vkCreateImageView(image)");

    if (desc.debugName != nullptr) {
        device_->setObjectName(reinterpret_cast<std::uint64_t>(image_), VK_OBJECT_TYPE_IMAGE, desc.debugName);
    }
}

Image::~Image()
{
    release();
}

Image::Image(Image&& other) noexcept
{
    *this = std::move(other);
}

Image& Image::operator=(Image&& other) noexcept
{
    if (this != &other) {
        release();
        device_ = other.device_;
        image_ = other.image_;
        memory_ = other.memory_;
        view_ = other.view_;
        format_ = other.format_;
        extent_ = other.extent_;

        other.device_ = nullptr;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {};
    }
    return *this;
}

VkImage Image::handle() const noexcept
{
    return image_;
}

VkImageView Image::view() const noexcept
{
    return view_;
}

VkFormat Image::format() const noexcept
{
    return format_;
}

VkExtent2D Image::extent() const noexcept
{
    return extent_;
}

void Image::release() noexcept
{
    if (device_ != nullptr) {
        if (view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_->logicalDevice(), view_, nullptr);
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_->logicalDevice(), image_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_->logicalDevice(), memory_, nullptr);
        }
    }

    device_ = nullptr;
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    view_ = VK_NULL_HANDLE;
    format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
}

} // namespace rt::vulkan
