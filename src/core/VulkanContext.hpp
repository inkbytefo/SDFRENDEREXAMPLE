#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>
#include <functional>
#include "Window.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/ResourceManager.hpp"

namespace engine::core {

class VulkanContext {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

    explicit VulkanContext(Window& window);
    ~VulkanContext();

    vk::Instance getInstance() const { return instance.get(); }
    vk::Device getDevice() const { return device.get(); }
    vk::PhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    vk::SurfaceKHR getSurface() const { return surface.get(); }
    renderer::Swapchain* getSwapchain() const { return swapChain.get(); }
    renderer::ResourceManager& getResourceManager() { return *resourceManager; }
    vk::Queue getGraphicsQueue() const { return graphicsQueue; }
    uint32_t getQueueFamily() const { return queueFamilyIndex; }
    vk::CommandPool getCommandPool() const { return commandPool.get(); }
    uint32_t getImageIndex() const { return imageIndex; }
    uint32_t getCurrentFrame() const { return currentFrame; }

    void immediateSubmit(const std::function<void(vk::CommandBuffer)>& func);

    struct QueueFamilyIndices {
        uint32_t graphicsFamily = 0;
        bool hasGraphicsFamily = false;
        bool isComplete() const { return hasGraphicsFamily; }
    };

    // Waits for this frame slot's fence and acquires a swapchain image.
    // Returns false if the frame must be skipped (swapchain was recreated or window minimized).
    // When it returns false, call getSwapchainGeneration() to detect that size-dependent resources need rebuilding.
    bool beginFrame();
    void endFrameBlit(vk::Image sourceImage, vk::Extent2D sourceExtent);
    void endFramePresent();
    vk::CommandBuffer getCurrentCommandBuffer() const { return commandBuffers[currentFrame].get(); }

    // Incremented every time the swapchain is recreated.
    uint32_t getSwapchainGeneration() const { return swapchainGeneration; }

    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device);

private:
    Window& window;
    vk::UniqueInstance instance;
    vk::DebugUtilsMessengerEXT debugMessenger;
    vk::PhysicalDevice physicalDevice;
    vk::UniqueDevice device;
    vk::Queue graphicsQueue;
    vk::UniqueSurfaceKHR surface;
    std::unique_ptr<renderer::Swapchain> swapChain;

    std::vector<vk::UniqueSemaphore> imageAvailableSemaphores;   // per frame in flight
    std::vector<vk::UniqueSemaphore> renderFinishedSemaphores;   // per swapchain image
    std::vector<vk::UniqueFence> inFlightFences;                 // per frame in flight

    std::unique_ptr<renderer::ResourceManager> resourceManager;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
    uint32_t currentFrame = 0;
    uint32_t imageIndex = 0;
    uint32_t queueFamilyIndex = 0;
    uint32_t swapchainGeneration = 0;
    bool swapchainDirty = false;
    bool validationEnabled = false;

    void createInstance();
    void createCommandPool();
    void createCommandBuffers();
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSyncObjects();
    void createRenderFinishedSemaphores();
    void createSurface();
    void createSwapchain();
    void recreateSwapchain();

    bool isDeviceSuitable(vk::PhysicalDevice device);
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
};

} // namespace engine::core
