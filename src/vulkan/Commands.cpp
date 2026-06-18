#include "rt/vulkan/Commands.hpp"

#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <stdexcept>

namespace rt::vulkan {

CommandContext::CommandContext(const Device& device)
    : device_(device)
{
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                 VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device_.graphicsQueueFamily(),
    };
    check(vkCreateCommandPool(device_.logicalDevice(), &poolInfo, nullptr, &pool_), "vkCreateCommandPool");

    const VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    check(vkAllocateCommandBuffers(device_.logicalDevice(), &allocateInfo, &current_), "vkAllocateCommandBuffers");
}

CommandContext::~CommandContext()
{
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_.logicalDevice(), pool_, nullptr);
    }
}

VkCommandBuffer CommandContext::beginOneShot()
{
    if (recording_) {
        throw std::logic_error("command context is already recording");
    }

    check(vkResetCommandBuffer(current_, 0), "vkResetCommandBuffer");
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    check(vkBeginCommandBuffer(current_, &beginInfo), "vkBeginCommandBuffer");
    recording_ = true;
    return current_;
}

void CommandContext::endSubmitAndWait()
{
    if (!recording_) {
        throw std::logic_error("command context is not recording");
    }

    check(vkEndCommandBuffer(current_), "vkEndCommandBuffer");

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &current_,
    };

    VkFence fence = VK_NULL_HANDLE;
    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    check(vkCreateFence(device_.logicalDevice(), &fenceInfo, nullptr, &fence), "vkCreateFence");
    check(vkQueueSubmit(device_.graphicsQueue(), 1, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(device_.logicalDevice(), 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkDestroyFence(device_.logicalDevice(), fence, nullptr);

    recording_ = false;
}

VkCommandBuffer CommandContext::current() const noexcept
{
    return current_;
}

} // namespace rt::vulkan
