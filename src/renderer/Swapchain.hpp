#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>

namespace engine::renderer {

class Swapchain {
public:
    struct SwapChainSupportDetails {
        vk::SurfaceCapabilitiesKHR capabilities;
        std::vector<vk::SurfaceFormatKHR> formats;
        std::vector<vk::PresentModeKHR> presentModes;
    };

    Swapchain(vk::Device device, vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, uint32_t width, uint32_t height);
    ~Swapchain();

    vk::SwapchainKHR getSwapchain() const { return swapChain.get(); }
    vk::Format getFormat() const { return swapChainImageFormat; }
    vk::Extent2D getExtent() const { return swapChainExtent; }
    const std::vector<vk::ImageView>& getImageViews() const { return swapChainImageViews; }
    const std::vector<vk::Image>& getImages() const { return swapChainImages; }

    static SwapChainSupportDetails querySwapChainSupport(vk::PhysicalDevice device, vk::SurfaceKHR surface);

private:
    vk::Device device;
    vk::UniqueSwapchainKHR swapChain;
    std::vector<vk::Image> swapChainImages;
    vk::Format swapChainImageFormat;
    vk::Extent2D swapChainExtent;
    std::vector<vk::ImageView> swapChainImageViews;

    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats);
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes);
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);
};

} // namespace engine::renderer
