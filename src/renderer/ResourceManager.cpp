#include "ResourceManager.hpp"

namespace engine::renderer {

ResourceManager::ResourceManager(vk::Device device, vk::PhysicalDevice physicalDevice)
    : device(device), physicalDevice(physicalDevice) {}

ResourceManager::Buffer ResourceManager::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties) {
    Buffer buffer;

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer.buffer = device.createBufferUnique(bufferInfo);

    vk::MemoryRequirements memRequirements = device.getBufferMemoryRequirements(buffer.buffer.get());

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    buffer.memory = device.allocateMemoryUnique(allocInfo);
    device.bindBufferMemory(buffer.buffer.get(), buffer.memory.get(), 0);

    return buffer;
}

ResourceManager::Image ResourceManager::createImage(uint32_t width, uint32_t height, uint32_t depth, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::ImageViewType viewType) {
    Image image;

    vk::ImageCreateInfo imageInfo{};
    imageInfo.imageType = depth > 1 ? vk::ImageType::e3D : vk::ImageType::e2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    image.image = device.createImageUnique(imageInfo);

    vk::MemoryRequirements memRequirements = device.getImageMemoryRequirements(image.image.get());

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    image.memory = device.allocateMemoryUnique(allocInfo);
    device.bindImageMemory(image.image.get(), image.memory.get(), 0);

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image.image.get();
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    image.view = device.createImageViewUnique(viewInfo);

    return image;
}

uint32_t ResourceManager::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

} // namespace engine::renderer
