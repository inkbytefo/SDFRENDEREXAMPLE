#pragma once

#include "ResourceManager.hpp"
#include <glm/glm.hpp>
#include <vector>

namespace engine::renderer {

class SparseMap {
public:
    SparseMap(ResourceManager& resourceManager, uint32_t gridSizeX, uint32_t gridSizeY, uint32_t gridSizeZ);

    vk::ImageView getMapView() const { return mapImage.view.get(); }

private:
    ResourceManager& resourceManager;
    ResourceManager::Image mapImage;
    uint32_t sizeX, sizeY, sizeZ;
};

} // namespace engine::renderer
