#include "rt/vulkan/Buffer.hpp"

#include "rt/vulkan/Commands.hpp"
#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <cstring>
#include <stdexcept>
#include <utility>

namespace rt::vulkan {

Buffer::Buffer(const Device& device, const BufferDesc& desc)
    : device_(&device),
      size_(desc.size),
      memoryProperties_(desc.memoryProperties)
{
    if (desc.size == 0) {
        throw std::invalid_argument("buffer size must be non-zero");
    }
    if (desc.usage == 0) {
        throw std::invalid_argument("buffer usage flags must be non-zero");
    }
    if (desc.memoryProperties == 0) {
        throw std::invalid_argument("buffer memory property flags must be non-zero");
    }

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = desc.size,
        .usage = desc.usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    check(vkCreateBuffer(device_->logicalDevice(), &bufferInfo, nullptr, &handle_), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_->logicalDevice(), handle_, &requirements);

    VkMemoryAllocateFlagsInfo allocateFlags{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
    };

    const bool needsDeviceAddress = (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
    VkMemoryAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = needsDeviceAddress ? &allocateFlags : nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = device_->findMemoryType(requirements.memoryTypeBits, desc.memoryProperties),
    };

    check(vkAllocateMemory(device_->logicalDevice(), &allocateInfo, nullptr, &memory_), "vkAllocateMemory(buffer)");
    check(vkBindBufferMemory(device_->logicalDevice(), handle_, memory_, 0), "vkBindBufferMemory");

    if (desc.debugName != nullptr) {
        device_->setObjectName(reinterpret_cast<std::uint64_t>(handle_), VK_OBJECT_TYPE_BUFFER, desc.debugName);
    }
}

Buffer::~Buffer()
{
    release();
}

Buffer::Buffer(Buffer&& other) noexcept
{
    *this = std::move(other);
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    if (this != &other) {
        release();
        device_ = other.device_;
        handle_ = other.handle_;
        memory_ = other.memory_;
        size_ = other.size_;
        memoryProperties_ = other.memoryProperties_;

        other.device_ = nullptr;
        other.handle_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
        other.memoryProperties_ = 0;
    }
    return *this;
}

VkBuffer Buffer::handle() const noexcept
{
    return handle_;
}

VkDeviceMemory Buffer::memory() const noexcept
{
    return memory_;
}

VkDeviceSize Buffer::size() const noexcept
{
    return size_;
}

VkDeviceAddress Buffer::deviceAddress() const
{
    if (handle_ == VK_NULL_HANDLE) {
        return 0;
    }

    const VkBufferDeviceAddressInfo addressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = handle_,
    };
    return device_->rt().getBufferDeviceAddress(device_->logicalDevice(), &addressInfo);
}

void Buffer::write(std::span<const std::byte> bytes, VkDeviceSize offset) const
{
    if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        throw std::logic_error("cannot directly write to non-host-visible buffer");
    }
    if (offset + bytes.size_bytes() > size_) {
        throw std::out_of_range("buffer write exceeds allocation size");
    }

    void* mapped = nullptr;
    check(vkMapMemory(device_->logicalDevice(), memory_, offset, bytes.size_bytes(), 0, &mapped), "vkMapMemory(buffer)");
    std::memcpy(mapped, bytes.data(), bytes.size_bytes());

    if ((memoryProperties_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory = memory_,
            .offset = offset,
            .size = bytes.size_bytes(),
        };
        check(vkFlushMappedMemoryRanges(device_->logicalDevice(), 1, &range), "vkFlushMappedMemoryRanges(buffer)");
    }

    vkUnmapMemory(device_->logicalDevice(), memory_);
}

void Buffer::upload(CommandContext& commands, std::span<const std::byte> bytes) const
{
    if (bytes.empty()) {
        throw std::invalid_argument("buffer upload requires non-empty data");
    }
    if (bytes.size_bytes() > size_) {
        throw std::out_of_range("buffer upload exceeds destination size");
    }

    Buffer staging(*device_,
                   {
                       .size = bytes.size_bytes(),
                       .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       .memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       .debugName = "upload staging buffer",
                   });
    staging.write(bytes);

    const VkCommandBuffer commandBuffer = commands.beginOneShot();
    const VkBufferCopy copyRegion{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = bytes.size_bytes(),
    };
    vkCmdCopyBuffer(commandBuffer, staging.handle(), handle_, 1, &copyRegion);
    commands.endSubmitAndWait();
}

void Buffer::release() noexcept
{
    if (device_ != nullptr) {
        if (handle_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_->logicalDevice(), handle_, nullptr);
        }
        if (memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_->logicalDevice(), memory_, nullptr);
        }
    }

    device_ = nullptr;
    handle_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
    size_ = 0;
    memoryProperties_ = 0;
}

} // namespace rt::vulkan
