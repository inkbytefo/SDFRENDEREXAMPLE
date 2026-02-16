#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>
#include <functional>
#include "Window.hpp"
#include "renderer/Swapchain.hpp"
#include "renderer/ResourceManager.hpp"
#include "renderer/BrickAtlas.hpp"
#include "renderer/SparseMap.hpp"

namespace engine::core {

class VulkanContext {
public:
    VulkanContext(Window& window);
    ~VulkanContext();

    vk::Instance getInstance() const { return instance.get(); }
    vk::Device getDevice() const { return device.get(); }
    vk::PhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    vk::SurfaceKHR getSurface() const { return surface.get(); }
    renderer::Swapchain* getSwapchain() const { return swapChain.get(); }
    renderer::ResourceManager& getResourceManager() { return *resourceManager; }
    renderer::BrickAtlas& getBrickAtlas() { return *brickAtlas; }
    renderer::SparseMap& getSparseMap() { return *sparseMap; }
    vk::Queue getGraphicsQueue() const { return graphicsQueue; }
    uint32_t getQueueFamily() const { return queueFamilyIndex; }
    vk::CommandPool getCommandPool() const { return commandPool.get(); }
    uint32_t getImageIndex() const { return imageIndex; }

    void immediateSubmit(std::function<void(vk::CommandBuffer)> func);
    
    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        bool hasGraphicsFamily = false;

        bool isComplete() const { return hasGraphicsFamily; }
    };

    void beginFrame();
    void endFrameBlit(vk::Image sourceImage);
    void endFramePresent();
    vk::CommandBuffer getCurrentCommandBuffer() const { return commandBuffers[currentFrame].get(); }

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

    std::vector<vk::UniqueSemaphore> imageAvailableSemaphores;
    std::vector<vk::UniqueSemaphore> renderFinishedSemaphores;
    std::vector<vk::UniqueFence> inFlightFences;

    std::unique_ptr<renderer::ResourceManager> resourceManager;
    std::unique_ptr<renderer::BrickAtlas> brickAtlas;
    std::unique_ptr<renderer::SparseMap> sparseMap;

    vk::UniqueCommandPool commandPool;
    std::vector<vk::UniqueCommandBuffer> commandBuffers;
    uint32_t currentFrame = 0;
    uint32_t imageIndex = 0;
    uint32_t queueFamilyIndex = 0;

    void createInstance();
    void createCommandPool();
    void createCommandBuffers();
    void setupDebugMessenger();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSyncObjects();
    void createSurface();

    static const int MAX_FRAMES_IN_FLIGHT = 2;

    bool isDeviceSuitable(vk::PhysicalDevice device);
    
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();
};

} // namespace engine::core
