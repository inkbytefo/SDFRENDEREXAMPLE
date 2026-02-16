#pragma once

#include "core/VulkanContext.hpp"
#include "ComputePipeline.hpp"
#include <memory>

namespace engine::renderer {

class DescriptorManager;

class Terrain {
public:
    struct BrushParams {
        glm::vec2 pos; // UV coordinates (0-1)
        float radius;  // UV radius
        float strength;
        uint32_t mode; // 0=Raise, 1=Lower, 2=Flatten, 3=Smooth, 4=Paint
        uint32_t layer; // 0=Base, 1=R, 2=G, 3=B
        float targetHeight; // For Flatten
        float padding;
    };

    Terrain(core::VulkanContext& context, uint32_t size = 1024);
    ~Terrain();

    void queueBrush(const BrushParams& params);
    void executePending(vk::CommandBuffer cmd);

    ResourceManager::Image& getHeightmap() { return heightmap; }
    ResourceManager::Image& getSplatmap() { return splatmap; }
    
    // Material settings (could be a UBO, but for now simple getters/setters or just fixed)
    // We'll hardcode materials in shader for now or pass as push constants if needed,
    // but the SDF renderer already has material logic.

private:
    core::VulkanContext& context;
    uint32_t size;

    ResourceManager::Image heightmap;
    ResourceManager::Image splatmap; // RGBA8 (Base, Layer1, Layer2, Layer3 weights)

    std::unique_ptr<DescriptorManager> descriptorManager;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;
    std::unique_ptr<ComputePipeline> computePipeline;

    bool hasPending = false;
    BrushParams pendingParams{};

    void createResources();
    void createPipeline();
};

} // namespace engine::renderer
