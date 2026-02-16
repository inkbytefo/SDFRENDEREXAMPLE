#pragma once

#include "ResourceManager.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace engine::renderer {

class BrickAtlas {
public:
    // Brick size is 8x8x8 as per tech specs
    static const int BRICK_SIZE = 8;
    
    BrickAtlas(ResourceManager& resourceManager, uint32_t atlasSizeInBricksX, uint32_t atlasSizeInBricksY, uint32_t atlasSizeInBricksZ);

    vk::ImageView getAtlasView() const { return atlasImage.view.get(); }
    
    struct BrickId {
        uint32_t id;
        glm::uvec3 atlasCoord;
    };

    BrickId allocateBrick();
    void freeBrick(uint32_t id);

private:
    ResourceManager& resourceManager;
    ResourceManager::Image atlasImage;
    uint32_t atlasSizeX, atlasSizeY, atlasSizeZ;
    
    std::vector<bool> brickOccupancy;
    uint32_t maxBricks;
};

} // namespace engine::renderer
