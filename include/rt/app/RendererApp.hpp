#pragma once

#include "rt/render/GpuScene.hpp"
#include "rt/scene/SceneLoader.hpp"
#include "rt/vulkan/AccelerationStructure.hpp"
#include "rt/vulkan/Buffer.hpp"
#include "rt/vulkan/Image.hpp"
#include "rt/vulkan/Instance.hpp"
#include "rt/vulkan/RayTracingPipeline.hpp"
#include "rt/vulkan/Swapchain.hpp"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace rt::platform {
class Window;
}

namespace rt::app {

class RendererApp {
public:
    explicit RendererApp(std::string_view argv0);
    ~RendererApp();

    RendererApp(const RendererApp&) = delete;
    RendererApp& operator=(const RendererApp&) = delete;

    int run();

private:
    struct CameraGpu {
        alignas(16) float origin[4]{};
        alignas(16) float lowerLeft[4]{};
        alignas(16) float horizontal[4]{};
        alignas(16) float vertical[4]{};
        alignas(16) float params[4]{};
    };

    void initialize(std::string_view argv0);
    void initializeImGui();
    void shutdownImGui() noexcept;

    void createDescriptorSetLayout();
    void createDescriptorPoolAndSet();
    void updateDescriptorSet();
    void createRayTracingPipeline();
    void createFrameResources();
    void destroyFrameResources() noexcept;
    void createRayOutput();
    void createImGuiRenderPass();
    void createFramebuffers();
    void destroyFramebuffers() noexcept;

    void loadScene(std::size_t sceneIndex);
    void createSceneGpuResources();
    void uploadCamera();
    void uploadMaterials();

    void drawUi();
    void drawFrame();
    void recreateSwapchain();

    [[nodiscard]] CameraGpu buildCameraGpu() const;
    [[nodiscard]] std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path) const;

    std::string argv0_;
    std::filesystem::path assetRoot_;
    std::filesystem::path shaderRoot_;

    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<vulkan::Instance> instance_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    std::unique_ptr<vulkan::Device> device_;
    std::unique_ptr<vulkan::Swapchain> swapchain_;
    vulkan::Image rayOutput_;
    VkImageLayout rayOutputLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDescriptorSetLayout sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet sceneDescriptorSet_ = VK_NULL_HANDLE;

    VkRenderPass imguiRenderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;

    VkCommandPool frameCommandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer frameCommandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;

    std::unique_ptr<vulkan::RayTracingPipeline> rayPipeline_;

    std::vector<scene::SceneDescriptor> sceneDescriptors_;
    std::size_t currentSceneIndex_ = 0;
    scene::Scene scene_;
    render::GpuScene gpuScene_;

    vulkan::Buffer vertexBuffer_;
    vulkan::Buffer indexBuffer_;
    vulkan::Buffer materialBuffer_;
    vulkan::Buffer objectBuffer_;
    vulkan::Buffer cameraBuffer_;
    std::vector<vulkan::AccelerationStructure> blas_;
    vulkan::AccelerationStructure tlas_;

    bool materialDirty_ = false;
    bool cameraDirty_ = true;
    bool showDemoWindow_ = false;
    float exposure_ = 1.0F;
    std::uint32_t frameIndex_ = 0;
};

} // namespace rt::app
