#include "rt/app/RendererApp.hpp"

#include "rt/core/Math.hpp"
#include "rt/core/Paths.hpp"
#include "rt/platform/Window.hpp"
#include "rt/vulkan/Commands.hpp"
#include "rt/vulkan/Device.hpp"
#include "rt/vulkan/VulkanError.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace rt::app {

namespace {

constexpr std::uint32_t initialWidth = 1280;
constexpr std::uint32_t initialHeight = 720;

void checkVk(VkResult result)
{
    vulkan::check(result, "ImGui Vulkan backend");
}

VkImageSubresourceRange colorRange()
{
    return {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
}

void imageBarrier(VkCommandBuffer commandBuffer,
                  VkImage image,
                  VkImageLayout oldLayout,
                  VkImageLayout newLayout,
                  VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage,
                  VkPipelineStageFlags dstStage)
{
    const VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = colorRange(),
    };

    vkCmdPipelineBarrier(commandBuffer,
                         srcStage,
                         dstStage,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
}

std::uint32_t clampSceneIndex(int index, std::size_t count)
{
    if (count == 0) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::clamp(index, 0, static_cast<int>(count - 1)));
}

} // namespace

RendererApp::RendererApp(std::string_view argv0)
    : argv0_(argv0)
{
    initialize(argv0);
}

RendererApp::~RendererApp()
{
    if (device_) {
        vkDeviceWaitIdle(device_->logicalDevice());
    }

    shutdownImGui();
    destroyFramebuffers();
    if (imguiRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_->logicalDevice(), imguiRenderPass_, nullptr);
    }
    if (sceneDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_->logicalDevice(), sceneDescriptorPool_, nullptr);
    }
    if (sceneDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_->logicalDevice(), sceneDescriptorSetLayout_, nullptr);
    }

    rayPipeline_.reset();
    tlas_ = {};
    blas_.clear();
    cameraBuffer_ = {};
    objectBuffer_ = {};
    materialBuffer_ = {};
    indexBuffer_ = {};
    vertexBuffer_ = {};
    rayOutput_ = {};
    swapchain_.reset();
    destroyFrameResources();

    if (surface_ != VK_NULL_HANDLE && instance_) {
        vkDestroySurfaceKHR(instance_->handle(), surface_, nullptr);
    }
}

int RendererApp::run()
{
    while (!window_->shouldClose()) {
        window_->pollEvents();
        drawFrame();
    }

    vkDeviceWaitIdle(device_->logicalDevice());
    return 0;
}

void RendererApp::initialize(std::string_view argv0)
{
    window_ = std::make_unique<platform::Window>(initialWidth, initialHeight, "Raytacing Renderer");

    std::vector<const char*> instanceExtensions(window_->requiredVulkanExtensions().begin(),
                                                window_->requiredVulkanExtensions().end());
    instance_ = std::make_unique<vulkan::Instance>(vulkan::InstanceConfig{
        .applicationName = "Raytacing Renderer",
        .enableValidation = true,
        .requiredExtensions = instanceExtensions,
    });

    surface_ = window_->createSurface(instance_->handle());
    device_ = std::make_unique<vulkan::Device>(*instance_, vulkan::DeviceConfig{
                                                               .requireRayTracing = true,
                                                               .requireSwapchain = true,
                                                               .presentSurface = surface_,
                                                           });

    assetRoot_ = findDirectoryNearExecutable(argv0, "assets");
    shaderRoot_ = findDirectoryNearExecutable(argv0, "shaders");
    sceneDescriptors_ = scene::SceneLoader::discover(assetRoot_);
    if (sceneDescriptors_.empty()) {
        throw std::runtime_error("no scenes available");
    }

    auto cornell = std::ranges::find_if(sceneDescriptors_, [](const scene::SceneDescriptor& descriptor) {
        const std::string lower = descriptor.name;
        return lower.find("cornell") != std::string::npos || lower.find("Cornell") != std::string::npos;
    });
    currentSceneIndex_ = cornell == sceneDescriptors_.end()
                             ? 0
                             : static_cast<std::size_t>(std::distance(sceneDescriptors_.begin(), cornell));

    swapchain_ = std::make_unique<vulkan::Swapchain>(*device_, surface_, window_->framebufferExtent());
    createDescriptorSetLayout();
    createDescriptorPoolAndSet();
    createFrameResources();
    createRayOutput();
    createImGuiRenderPass();
    createFramebuffers();
    initializeImGui();
    createRayTracingPipeline();
    loadScene(currentSceneIndex_);
}

void RendererApp::initializeImGui()
{
    const std::array<VkDescriptorPoolSize, 11> poolSizes{{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    }};

    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 1000,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vulkan::check(vkCreateDescriptorPool(device_->logicalDevice(), &poolInfo, nullptr, &imguiDescriptorPool_),
                  "vkCreateDescriptorPool(imgui)");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window_->handle(), true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = instance_->handle();
    initInfo.PhysicalDevice = device_->physicalDevice();
    initInfo.Device = device_->logicalDevice();
    initInfo.QueueFamily = device_->graphicsQueueFamily();
    initInfo.Queue = device_->graphicsQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiDescriptorPool_;
    initInfo.RenderPass = imguiRenderPass_;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = static_cast<std::uint32_t>(swapchain_->images().size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = checkVk;
    ImGui_ImplVulkan_Init(&initInfo);
}

void RendererApp::shutdownImGui() noexcept
{
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    if (imguiDescriptorPool_ != VK_NULL_HANDLE && device_) {
        vkDestroyDescriptorPool(device_->logicalDevice(), imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
}

void RendererApp::createDescriptorSetLayout()
{
    const std::array<VkDescriptorSetLayoutBinding, 7> bindings{{
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
        {.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 4, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 5, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
        {.binding = 6, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR},
    }};

    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    vulkan::check(vkCreateDescriptorSetLayout(device_->logicalDevice(), &layoutInfo, nullptr, &sceneDescriptorSetLayout_),
                  "vkCreateDescriptorSetLayout(scene)");
}

void RendererApp::createDescriptorPoolAndSet()
{
    const std::array<VkDescriptorPoolSize, 4> poolSizes{{
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
    }};
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    vulkan::check(vkCreateDescriptorPool(device_->logicalDevice(), &poolInfo, nullptr, &sceneDescriptorPool_),
                  "vkCreateDescriptorPool(scene)");

    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = sceneDescriptorPool_,
        .descriptorSetCount = 1,
        .pSetLayouts = &sceneDescriptorSetLayout_,
    };
    vulkan::check(vkAllocateDescriptorSets(device_->logicalDevice(), &allocateInfo, &sceneDescriptorSet_),
                  "vkAllocateDescriptorSets(scene)");
}

void RendererApp::updateDescriptorSet()
{
    if (tlas_.handle() == VK_NULL_HANDLE || rayOutput_.view() == VK_NULL_HANDLE ||
        vertexBuffer_.handle() == VK_NULL_HANDLE || indexBuffer_.handle() == VK_NULL_HANDLE ||
        materialBuffer_.handle() == VK_NULL_HANDLE || objectBuffer_.handle() == VK_NULL_HANDLE ||
        cameraBuffer_.handle() == VK_NULL_HANDLE) {
        return;
    }

    VkWriteDescriptorSetAccelerationStructureKHR tlasInfo{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructureCount = 1,
        .pAccelerationStructures = nullptr,
    };
    const VkAccelerationStructureKHR tlasHandle = tlas_.handle();
    tlasInfo.pAccelerationStructures = &tlasHandle;

    const VkDescriptorImageInfo imageInfo{
        .sampler = VK_NULL_HANDLE,
        .imageView = rayOutput_.view(),
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };

    const std::array<VkDescriptorBufferInfo, 5> bufferInfos{{
        {.buffer = vertexBuffer_.handle(), .offset = 0, .range = vertexBuffer_.size()},
        {.buffer = indexBuffer_.handle(), .offset = 0, .range = indexBuffer_.size()},
        {.buffer = objectBuffer_.handle(), .offset = 0, .range = objectBuffer_.size()},
        {.buffer = materialBuffer_.handle(), .offset = 0, .range = materialBuffer_.size()},
        {.buffer = cameraBuffer_.handle(), .offset = 0, .range = cameraBuffer_.size()},
    }};

    std::array<VkWriteDescriptorSet, 7> writes{{
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .pNext = &tlasInfo, .dstSet = sceneDescriptorSet_, .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &imageInfo},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 2, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bufferInfos[0]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 3, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bufferInfos[1]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 4, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bufferInfos[2]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 5, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bufferInfos[3]},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sceneDescriptorSet_, .dstBinding = 6, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &bufferInfos[4]},
    }};

    vkUpdateDescriptorSets(device_->logicalDevice(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void RendererApp::createRayTracingPipeline()
{
    std::vector<std::uint32_t> raygen = readSpirv(shaderRoot_ / "pathtrace.rgen.spv");
    std::vector<std::uint32_t> miss = readSpirv(shaderRoot_ / "pathtrace.rmiss.spv");
    std::vector<std::uint32_t> closestHit = readSpirv(shaderRoot_ / "pathtrace.rchit.spv");

    std::vector<vulkan::ShaderStageDesc> stages{
        {.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR, .spirv = raygen},
        {.stage = VK_SHADER_STAGE_MISS_BIT_KHR, .spirv = miss},
        {.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, .spirv = closestHit},
    };

    rayPipeline_ = std::make_unique<vulkan::RayTracingPipeline>(
        *device_,
        vulkan::RayTracingPipelineDesc{
            .stages = stages,
            .raygenStageIndices = {0},
            .missStageIndices = {1},
            .hitGroups = {{.closestHitStage = 2}},
            .descriptorSetLayouts = {sceneDescriptorSetLayout_},
            .maxRayRecursionDepth = 1,
        });
}

void RendererApp::createFrameResources()
{
    const VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device_->graphicsQueueFamily(),
    };
    vulkan::check(vkCreateCommandPool(device_->logicalDevice(), &poolInfo, nullptr, &frameCommandPool_),
                  "vkCreateCommandPool(frame)");

    const VkCommandBufferAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = frameCommandPool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    vulkan::check(vkAllocateCommandBuffers(device_->logicalDevice(), &allocateInfo, &frameCommandBuffer_),
                  "vkAllocateCommandBuffers(frame)");

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    vulkan::check(vkCreateSemaphore(device_->logicalDevice(), &semaphoreInfo, nullptr, &imageAvailable_), "vkCreateSemaphore(image)");
    vulkan::check(vkCreateSemaphore(device_->logicalDevice(), &semaphoreInfo, nullptr, &renderFinished_), "vkCreateSemaphore(render)");
    vulkan::check(vkCreateFence(device_->logicalDevice(), &fenceInfo, nullptr, &frameFence_), "vkCreateFence(frame)");
}

void RendererApp::destroyFrameResources() noexcept
{
    if (!device_) {
        return;
    }
    if (frameFence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_->logicalDevice(), frameFence_, nullptr);
    }
    if (renderFinished_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_->logicalDevice(), renderFinished_, nullptr);
    }
    if (imageAvailable_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_->logicalDevice(), imageAvailable_, nullptr);
    }
    if (frameCommandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_->logicalDevice(), frameCommandPool_, nullptr);
    }

    frameFence_ = VK_NULL_HANDLE;
    renderFinished_ = VK_NULL_HANDLE;
    imageAvailable_ = VK_NULL_HANDLE;
    frameCommandPool_ = VK_NULL_HANDLE;
    frameCommandBuffer_ = VK_NULL_HANDLE;
}

void RendererApp::createRayOutput()
{
    rayOutput_ = vulkan::Image(*device_,
                               {
                                   .extent = swapchain_->extent(),
                                   .format = VK_FORMAT_R8G8B8A8_UNORM,
                                   .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                   .debugName = "ray traced output",
                               });
    rayOutputLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
}

void RendererApp::createImGuiRenderPass()
{
    const VkAttachmentDescription colorAttachment{
        .format = swapchain_->format(),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    const VkAttachmentReference colorRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
    };
    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
    };
    vulkan::check(vkCreateRenderPass(device_->logicalDevice(), &renderPassInfo, nullptr, &imguiRenderPass_),
                  "vkCreateRenderPass(imgui)");
}

void RendererApp::createFramebuffers()
{
    framebuffers_.reserve(swapchain_->imageViews().size());
    for (VkImageView imageView : swapchain_->imageViews()) {
        const VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = imguiRenderPass_,
            .attachmentCount = 1,
            .pAttachments = &imageView,
            .width = swapchain_->extent().width,
            .height = swapchain_->extent().height,
            .layers = 1,
        };
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        vulkan::check(vkCreateFramebuffer(device_->logicalDevice(), &framebufferInfo, nullptr, &framebuffer),
                      "vkCreateFramebuffer(imgui)");
        framebuffers_.push_back(framebuffer);
    }
}

void RendererApp::destroyFramebuffers() noexcept
{
    if (!device_) {
        return;
    }
    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_->logicalDevice(), framebuffer, nullptr);
    }
    framebuffers_.clear();
}

void RendererApp::loadScene(std::size_t sceneIndex)
{
    vkDeviceWaitIdle(device_->logicalDevice());
    currentSceneIndex_ = sceneIndex;
    scene_ = scene::SceneLoader::load(sceneDescriptors_[sceneIndex]);
    createSceneGpuResources();
    materialDirty_ = false;
    cameraDirty_ = true;
    frameIndex_ = 0;
}

void RendererApp::createSceneGpuResources()
{
    gpuScene_ = render::buildGpuScene(scene_);
    if (gpuScene_.vertices.empty() || gpuScene_.indices.empty() || gpuScene_.objects.empty()) {
        throw std::runtime_error("scene has no renderable triangle geometry");
    }

    tlas_ = {};
    blas_.clear();
    cameraBuffer_ = {};
    objectBuffer_ = {};
    materialBuffer_ = {};
    indexBuffer_ = {};
    vertexBuffer_ = {};

    vulkan::CommandContext uploadCommands(*device_);
    vertexBuffer_ = vulkan::Buffer(*device_,
                                   {
                                       .size = sizeof(render::GpuVertex) * gpuScene_.vertices.size(),
                                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       .debugName = "scene vertices",
                                   });
    indexBuffer_ = vulkan::Buffer(*device_,
                                  {
                                      .size = sizeof(std::uint32_t) * gpuScene_.indices.size(),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                      .debugName = "scene indices",
                                  });
    objectBuffer_ = vulkan::Buffer(*device_,
                                   {
                                       .size = sizeof(render::GpuObject) * gpuScene_.objects.size(),
                                       .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                       .debugName = "scene object records",
                                   });
    materialBuffer_ = vulkan::Buffer(*device_,
                                     {
                                         .size = sizeof(render::GpuMaterial) * gpuScene_.materials.size(),
                                         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         .memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                         .debugName = "scene materials",
                                     });
    cameraBuffer_ = vulkan::Buffer(*device_,
                                   {
                                       .size = sizeof(CameraGpu),
                                       .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                       .memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                       .debugName = "camera uniform",
                                   });

    vertexBuffer_.upload(uploadCommands, vulkan::asBytes(std::span<const render::GpuVertex>(gpuScene_.vertices)));
    indexBuffer_.upload(uploadCommands, vulkan::asBytes(std::span<const std::uint32_t>(gpuScene_.indices)));
    objectBuffer_.upload(uploadCommands, vulkan::asBytes(std::span<const render::GpuObject>(gpuScene_.objects)));
    materialBuffer_.upload(uploadCommands, vulkan::asBytes(std::span<const render::GpuMaterial>(gpuScene_.materials)));
    uploadCamera();

    vulkan::AccelerationStructureBuilder builder(*device_);
    blas_.reserve(scene_.meshes.size());
    for (const scene::Mesh& mesh : scene_.meshes) {
        blas_.push_back(builder.buildBottomLevelTriangles(
            uploadCommands,
            vulkan::TriangleGeometry{
                .vertexAddress = vertexBuffer_.deviceAddress() + mesh.vertexOffset * sizeof(render::GpuVertex),
                .vertexStride = sizeof(render::GpuVertex),
                .vertexCount = mesh.vertexCount,
                .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                .indexAddress = indexBuffer_.deviceAddress() + mesh.indexOffset * sizeof(std::uint32_t),
                .indexCount = mesh.indexCount,
                .indexType = VK_INDEX_TYPE_UINT32,
                .opaque = true,
            },
            {.allowUpdate = mesh.dynamicGeometry}));
    }

    std::vector<vulkan::InstanceGeometry> instances;
    instances.reserve(scene_.instances.size());
    for (const scene::Instance& instance : scene_.instances) {
        instances.push_back({
            .bottomLevelAddress = blas_[instance.meshIndex].deviceAddress(),
            .transform = instance.transform,
            .instanceId = instance.meshIndex,
            .mask = instance.visibilityMask,
        });
    }
    tlas_ = builder.buildTopLevel(uploadCommands, instances, {.allowUpdate = true});
    updateDescriptorSet();
}

void RendererApp::uploadCamera()
{
    const CameraGpu camera = buildCameraGpu();
    cameraBuffer_.write(vulkan::asBytes(std::span<const CameraGpu>(&camera, 1)));
    cameraDirty_ = false;
}

void RendererApp::uploadMaterials()
{
    vkDeviceWaitIdle(device_->logicalDevice());
    gpuScene_ = render::buildGpuScene(scene_);
    vulkan::CommandContext commands(*device_);
    materialBuffer_.upload(commands, vulkan::asBytes(std::span<const render::GpuMaterial>(gpuScene_.materials)));
    materialDirty_ = false;
    frameIndex_ = 0;
}

void RendererApp::drawUi()
{
    ImGui::Begin("Renderer");
    ImGui::Text("Device: %s", device_->properties().deviceName);
    ImGui::Text("Scene: %s", scene_.name.c_str());
    ImGui::Text("Meshes: %zu  Triangles: %zu", scene_.meshes.size(), scene_.indices.size() / 3);
    ImGui::Text("Frame: %u", frameIndex_);
    cameraDirty_ |= ImGui::SliderFloat("Exposure", &exposure_, 0.1F, 4.0F);
    ImGui::Checkbox("ImGui demo", &showDemoWindow_);

    int sceneIndex = static_cast<int>(currentSceneIndex_);
    if (ImGui::BeginCombo("Scene", sceneDescriptors_[currentSceneIndex_].name.c_str())) {
        for (std::size_t i = 0; i < sceneDescriptors_.size(); ++i) {
            const bool selected = i == currentSceneIndex_;
            if (ImGui::Selectable(sceneDescriptors_[i].name.c_str(), selected)) {
                sceneIndex = static_cast<int>(i);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (clampSceneIndex(sceneIndex, sceneDescriptors_.size()) != currentSceneIndex_) {
        loadScene(static_cast<std::size_t>(sceneIndex));
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        cameraDirty_ |= ImGui::DragFloat3("Position", scene_.camera.position.data(), 0.025F);
        cameraDirty_ |= ImGui::DragFloat3("Forward", scene_.camera.forward.data(), 0.01F);
        cameraDirty_ |= ImGui::SliderFloat("Vertical FOV", &scene_.camera.verticalFovRadians, 0.25F, 1.6F);
    }

    if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (std::size_t i = 0; i < scene_.materials.size(); ++i) {
            scene::Material& material = scene_.materials[i];
            if (ImGui::TreeNode(material.name.c_str())) {
                materialDirty_ |= ImGui::ColorEdit3("Base color", material.baseColor.data());
                materialDirty_ |= ImGui::DragFloat3("Emission", material.emission.data(), 0.1F, 0.0F, 40.0F);
                materialDirty_ |= ImGui::SliderFloat("Roughness", &material.roughness, 0.02F, 1.0F);
                materialDirty_ |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0F, 1.0F);
                ImGui::TreePop();
            }
        }
    }

    ImGui::End();

    if (showDemoWindow_) {
        ImGui::ShowDemoWindow(&showDemoWindow_);
    }
}

void RendererApp::drawFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    drawUi();
    ImGui::Render();

    vulkan::check(vkWaitForFences(device_->logicalDevice(), 1, &frameFence_, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences(frame)");
    uploadCamera();
    if (materialDirty_) {
        uploadMaterials();
    }

    std::uint32_t imageIndex = 0;
    VkResult acquire = swapchain_->acquireNextImage(imageAvailable_, imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR || window_->framebufferResized()) {
        window_->clearFramebufferResized();
        recreateSwapchain();
        return;
    }
    vulkan::check(acquire, "vkAcquireNextImageKHR");

    vulkan::check(vkResetFences(device_->logicalDevice(), 1, &frameFence_), "vkResetFences(frame)");
    vulkan::check(vkResetCommandBuffer(frameCommandBuffer_, 0), "vkResetCommandBuffer(frame)");

    const VkCommandBufferBeginInfo beginInfo{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vulkan::check(vkBeginCommandBuffer(frameCommandBuffer_, &beginInfo), "vkBeginCommandBuffer(frame)");

    imageBarrier(frameCommandBuffer_,
                 rayOutput_.handle(),
                 rayOutputLayout_,
                 VK_IMAGE_LAYOUT_GENERAL,
                 rayOutputLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_READ_BIT,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 rayOutputLayout_ == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

    rayPipeline_->bind(frameCommandBuffer_);
    vkCmdBindDescriptorSets(frameCommandBuffer_,
                            VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                            rayPipeline_->layout(),
                            0,
                            1,
                            &sceneDescriptorSet_,
                            0,
                            nullptr);
    rayPipeline_->trace(frameCommandBuffer_, swapchain_->extent().width, swapchain_->extent().height);

    imageBarrier(frameCommandBuffer_,
                 rayOutput_.handle(),
                 VK_IMAGE_LAYOUT_GENERAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT,
                 VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);

    imageBarrier(frameCommandBuffer_,
                 swapchain_->images()[imageIndex],
                 VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 0,
                 VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {
        static_cast<int>(rayOutput_.extent().width),
        static_cast<int>(rayOutput_.extent().height),
        1,
    };
    blit.dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {
        static_cast<int>(swapchain_->extent().width),
        static_cast<int>(swapchain_->extent().height),
        1,
    };
    vkCmdBlitImage(frameCommandBuffer_,
                   rayOutput_.handle(),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   swapchain_->images()[imageIndex],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1,
                   &blit,
                   VK_FILTER_NEAREST);

    imageBarrier(frameCommandBuffer_,
                 swapchain_->images()[imageIndex],
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_ACCESS_TRANSFER_WRITE_BIT,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    const VkClearValue clearValue{.color = {{0.0F, 0.0F, 0.0F, 1.0F}}};
    const VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = imguiRenderPass_,
        .framebuffer = framebuffers_[imageIndex],
        .renderArea = {.offset = {0, 0}, .extent = swapchain_->extent()},
        .clearValueCount = 1,
        .pClearValues = &clearValue,
    };
    vkCmdBeginRenderPass(frameCommandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frameCommandBuffer_);
    vkCmdEndRenderPass(frameCommandBuffer_);

    vulkan::check(vkEndCommandBuffer(frameCommandBuffer_), "vkEndCommandBuffer(frame)");
    rayOutputLayout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailable_,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frameCommandBuffer_,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinished_,
    };
    vulkan::check(vkQueueSubmit(device_->graphicsQueue(), 1, &submitInfo, frameFence_), "vkQueueSubmit(frame)");

    const VkResult present = swapchain_->present(imageIndex, renderFinished_);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR || window_->framebufferResized()) {
        window_->clearFramebufferResized();
        recreateSwapchain();
    } else {
        vulkan::check(present, "vkQueuePresentKHR");
    }

    ++frameIndex_;
}

void RendererApp::recreateSwapchain()
{
    vkDeviceWaitIdle(device_->logicalDevice());
    destroyFramebuffers();
    rayOutput_ = {};
    swapchain_.reset();

    swapchain_ = std::make_unique<vulkan::Swapchain>(*device_, surface_, window_->framebufferExtent());
    createRayOutput();
    createFramebuffers();
    updateDescriptorSet();
}

RendererApp::CameraGpu RendererApp::buildCameraGpu() const
{
    const float aspect = static_cast<float>(std::max(swapchain_->extent().width, 1U)) /
                         static_cast<float>(std::max(swapchain_->extent().height, 1U));
    const float viewportHeight = 2.0F * std::tan(scene_.camera.verticalFovRadians * 0.5F);
    const float viewportWidth = viewportHeight * aspect;

    const Vec3 origin = scene_.camera.position;
    const Vec3 forward = normalize(scene_.camera.forward);
    const Vec3 right = normalize(cross(forward, scene_.camera.up));
    const Vec3 up = normalize(cross(right, forward));
    const Vec3 horizontal = mul(right, viewportWidth);
    const Vec3 vertical = mul(up, viewportHeight);
    const Vec3 lowerLeft = sub(sub(add(origin, forward), mul(horizontal, 0.5F)), mul(vertical, 0.5F));

    return {
        .origin = {origin[0], origin[1], origin[2], 1.0F},
        .lowerLeft = {lowerLeft[0], lowerLeft[1], lowerLeft[2], 0.0F},
        .horizontal = {horizontal[0], horizontal[1], horizontal[2], 0.0F},
        .vertical = {vertical[0], vertical[1], vertical[2], 0.0F},
        .params = {exposure_, static_cast<float>(frameIndex_), 0.0F, 0.0F},
    };
}

std::vector<std::uint32_t> RendererApp::readSpirv(const std::filesystem::path& path) const
{
    const std::vector<std::byte> bytes = readBinaryFile(path);
    if ((bytes.size() % sizeof(std::uint32_t)) != 0) {
        throw std::runtime_error("SPIR-V file size is not 32-bit aligned: " + path.string());
    }

    std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

} // namespace rt::app
