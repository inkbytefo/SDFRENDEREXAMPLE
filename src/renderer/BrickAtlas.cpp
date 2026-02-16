#include "BrickAtlas.hpp"
#include <stdexcept>

namespace engine::renderer {

BrickAtlas::BrickAtlas(ResourceManager& resourceManager, uint32_t atlasSizeInBricksX, uint32_t atlasSizeInBricksY, uint32_t atlasSizeInBricksZ)
    : resourceManager(resourceManager), 
      atlasSizeX(atlasSizeInBricksX), atlasSizeY(atlasSizeInBricksY), atlasSizeZ(atlasSizeInBricksZ) {
    
    maxBricks = atlasSizeX * atlasSizeY * atlasSizeZ;
    brickOccupancy.resize(maxBricks, false);

    // Create a 3D texture for the atlas
    // Format: R16_SFLOAT for distance values as per tech specs recommendation
    atlasImage = resourceManager.createImage(
        atlasSizeX * BRICK_SIZE, 
        atlasSizeY * BRICK_SIZE, 
        atlasSizeZ * BRICK_SIZE,
        vk::Format::eR16Sfloat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageViewType::e3D
    );
}

BrickAtlas::BrickId BrickAtlas::allocateBrick() {
    for (uint32_t i = 0; i < maxBricks; i++) {
        if (!brickOccupancy[i]) {
            brickOccupancy[i] = true;
            
            uint32_t z = i / (atlasSizeX * atlasSizeY);
            uint32_t y = (i % (atlasSizeX * atlasSizeY)) / atlasSizeX;
            uint32_t x = i % atlasSizeX;
            
            return { i, glm::uvec3(x, y, z) };
        }
    }
    throw std::runtime_error("Brick Atlas is full!");
}

void BrickAtlas::freeBrick(uint32_t id) {
    if (id < maxBricks) {
        brickOccupancy[id] = false;
    }
}

} // namespace engine::renderer
