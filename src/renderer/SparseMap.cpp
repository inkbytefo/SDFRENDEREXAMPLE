#include "SparseMap.hpp"

namespace engine::renderer {

SparseMap::SparseMap(ResourceManager& resourceManager, uint32_t gridSizeX, uint32_t gridSizeY, uint32_t gridSizeZ)
    : resourceManager(resourceManager), sizeX(gridSizeX), sizeY(gridSizeY), sizeZ(gridSizeZ) {
    
    // Map stores indices/pointers to the Brick Atlas
    // Format: R32_UINT for indices
    mapImage = resourceManager.createImage(
        sizeX, sizeY, sizeZ,
        vk::Format::eR32Uint,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageViewType::e3D
    );
}

} // namespace engine::renderer
