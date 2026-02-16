#pragma once

#include <vulkan/vulkan.hpp>

namespace engine::renderer {

class ResourceManager {
public:
    ResourceManager(vk::Device device, vk::PhysicalDevice physicalDevice);

    struct Buffer {
        vk::UniqueBuffer buffer;
        vk::UniqueDeviceMemory memory;
    };

    struct Image {
        vk::UniqueImage image;
        vk::UniqueDeviceMemory memory;
        vk::UniqueImageView view;
    };

    Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
    Image createImage(uint32_t width, uint32_t height, uint32_t depth, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::ImageViewType viewType = vk::ImageViewType::e2D);

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

private:
    vk::Device device;
    vk::PhysicalDevice physicalDevice;
};

} // namespace engine::renderer
