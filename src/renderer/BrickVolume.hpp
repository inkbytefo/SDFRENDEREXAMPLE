#pragma once

#include "ResourceManager.hpp"
#include "ComputePipeline.hpp"
#include "DescriptorManager.hpp"
#include <glm/glm.hpp>
#include <array>
#include <memory>

namespace engine::core { class VulkanContext; }

namespace engine::renderer {

// std140 uniform block shared by BrickBake.glsl and SDFCompute.glsl (VolumeParamsUBO).
struct VolumeParams {
    glm::vec4  originVoxel;  // xyz = volume min corner, w = voxel size
    glm::uvec4 gridDim;      // xyz = sparse map cells, w = max edits
    glm::uvec4 atlasBricks;  // xyz = atlas size in bricks, w = brick capacity
    glm::vec4  metrics;      // x = cell size, y = half cell diagonal, z = brick clamp, w = max safe skip
};
static_assert(sizeof(VolumeParams) == 64, "VolumeParams must match the std140 block");

// Sparse brick volume holding a baked copy of the edit list's distance field.
//
//   sparse map : one R32_UINT per cell. Either BRICK_FLAG | brickId, or the float bits of a
//                conservative "safe skip" distance for an empty cell.
//   atlas      : R16_SFLOAT 3D texture of 8^3-sample bricks. A brick covers exactly one cell
//                (7 voxel intervals) and stores its own border samples, so hardware trilinear
//                filtering never reads across bricks.
//
// Edits mark world-space boxes dirty; record() re-bakes only the cells inside them on the GPU.
class BrickVolume {
public:
    struct Config {
        glm::vec3 origin{-28.0f, -6.0f, -28.0f};
        float voxelSize = 0.05f;
        glm::uvec3 gridCells{160, 160, 160};  // 160 cells * 0.35 m = 56 m per side
        glm::uvec3 atlasBricks{32, 32, 48};   // 49,152 bricks, 256x256x384 R16F = 48 MB
    };

    struct Stats {
        uint32_t capacity = 0;
        uint32_t used = 0;
        uint32_t overflow = 0;   // cells that needed a brick but the atlas was full (last bake)
        uint32_t lastDirtyCells = 0;
    };

    static constexpr uint32_t MAX_EDITS = 256;

    BrickVolume(core::VulkanContext& context, const Config& config,
                const std::array<vk::Buffer, 2>& editBuffers, vk::DeviceSize editBufferSize);
    ~BrickVolume();

    void markDirty(const glm::vec3& worldMin, const glm::vec3& worldMax);
    void markAllDirty();

    // Records the bake passes for the accumulated dirty region (no-op if clean).
    void record(vk::CommandBuffer cmd, uint32_t frame, uint32_t editCount);

    // Call after the frame slot's fence has been waited on.
    void collectStats(uint32_t frame);
    const Stats& getStats() const { return stats; }

    vk::ImageView getAtlasView() const { return atlas.view.get(); }
    vk::ImageView getMapView() const { return sparseMap.view.get(); }
    vk::Buffer getParamsBuffer() const { return paramsBuffer.buffer.get(); }

    glm::vec3 worldMin() const { return config.origin; }
    glm::vec3 worldMax() const { return config.origin + glm::vec3(config.gridCells) * cellSize(); }
    float cellSize() const { return config.voxelSize * 7.0f; }

    // How far an edit's influence on stored data can reach beyond its bounding sphere.
    float dirtyPadding() const;

private:
    core::VulkanContext& context;
    Config config;
    VolumeParams params{};
    uint32_t capacity = 0;

    ResourceManager::Image atlas;
    ResourceManager::Image sparseMap;
    ResourceManager::Buffer stateBuffer;   // freeTop, overflow, allocCount, pad, freeStack[capacity]
    ResourceManager::Buffer allocBuffer;   // cell indices awaiting a brick
    ResourceManager::Buffer workBuffer;    // dispatch-indirect header + (cell, brick) pairs
    ResourceManager::Buffer paramsBuffer;  // VolumeParams
    std::array<ResourceManager::Buffer, 2> statsBuffers; // host-visible copy of the state header
    std::array<bool, 2> statsPending{};

    std::unique_ptr<DescriptorManager> descriptors;
    vk::DescriptorSetLayout setLayout;
    std::array<vk::DescriptorSet, 2> sets{};
    std::unique_ptr<ComputePipeline> pipeline;

    bool dirty = false;
    glm::ivec3 dirtyMin{0};
    glm::ivec3 dirtyMax{0}; // exclusive
    Stats stats;

    struct BakePush {
        glm::uvec4 boxMin;  // w = pass
        glm::uvec4 boxSize; // w = edit count
    };

    void createResources(const std::array<vk::Buffer, 2>& editBuffers, vk::DeviceSize editBufferSize);
    void initialize();
};

} // namespace engine::renderer
