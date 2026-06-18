#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vulkan/vulkan.h>

namespace rt::vulkan {

class CommandContext;
class Device;

struct BufferDesc {
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags memoryProperties = 0;
    const char* debugName = nullptr;
};

class Buffer {
public:
    Buffer() = default;
    Buffer(const Device& device, const BufferDesc& desc);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    [[nodiscard]] VkBuffer handle() const noexcept;
    [[nodiscard]] VkDeviceMemory memory() const noexcept;
    [[nodiscard]] VkDeviceSize size() const noexcept;
    [[nodiscard]] VkDeviceAddress deviceAddress() const;

    void write(std::span<const std::byte> bytes, VkDeviceSize offset = 0) const;
    void upload(CommandContext& commands, std::span<const std::byte> bytes) const;

private:
    void release() noexcept;

    const Device* device_ = nullptr;
    VkBuffer handle_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    VkMemoryPropertyFlags memoryProperties_ = 0;
};

template <typename T>
std::span<const std::byte> asBytes(std::span<const T> values) noexcept
{
    return {reinterpret_cast<const std::byte*>(values.data()), values.size_bytes()};
}

} // namespace rt::vulkan
