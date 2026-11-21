#pragma once

#include "core/VulkanContext.hpp"
#include "ComputePipeline.hpp"
#include <glm/glm.hpp>
#include <memory>

namespace engine::renderer {

class DescriptorManager;

// 2D heightfield terrain (256 x 256 m, centered on the origin) edited with a compute brush.
class Terrain {
public:
    static constexpr float WORLD_SIZE = 256.0f;

    enum class BrushMode : uint32_t { Raise = 0, Lower = 1, Flatten = 2, Smooth = 3, Paint = 4 };

    // Must match the push constant block in TerrainBrush.glsl.
    struct BrushParams {
        glm::vec2 pos;        // UV center
        float radius;         // UV radius
        float strength;
        uint32_t mode;        // BrushMode
        uint32_t layer;       // splat channel 0..3
        float targetHeight;   // Flatten
        float pad0 = 0.0f;
        glm::ivec2 origin{0}; // filled in by Terrain
        glm::ivec2 pad1{0};
    };
    static_assert(sizeof(BrushParams) == 48, "BrushParams must match TerrainBrush.glsl");

    Terrain(core::VulkanContext& context, uint32_t size = 1024);
    ~Terrain();

    // Brush is applied in world units; converted to UV here.
    void queueBrush(glm::vec3 worldPos, float worldRadius, float strength, BrushMode mode,
                    uint32_t layer, float targetHeight);
    void executePending(vk::CommandBuffer cmd);

    ResourceManager::Image& getHeightmap() { return heightmap; }
    ResourceManager::Image& getSplatmap() { return splatmap; }

private:
    core::VulkanContext& context;
    uint32_t size;

    ResourceManager::Image heightmap;
    ResourceManager::Image heightScratch; // snapshot for the Smooth brush
    ResourceManager::Image splatmap;      // RGBA8 layer weights

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
