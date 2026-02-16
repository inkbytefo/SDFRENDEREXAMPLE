#pragma once

#include "core/VulkanContext.hpp"
#include "ComputePipeline.hpp"
#include "DescriptorManager.hpp"
#include "core/SDFEdit.hpp"
#include "core/InputState.hpp"
#include <vector>
#include <cmath>
#include "Terrain.hpp"

namespace engine::renderer {

struct PushConstants {
    float camPosX, camPosY, camPosZ, pad0;
    float camDirX, camDirY, camDirZ, pad1;
    float resX, resY, time, editCount;
    uint32_t renderMode; // 0=Lit, 1=Normals, 2=Complexity
    uint32_t showGround; // 1=On, 0=Off
    float mouseX, mouseY; // -1 if not picking
    float brushX, brushY, brushZ, brushRadius; // World space brush
    uint32_t showGrid; // 1=On, 0=Off
    float pad2, pad3;
};

class SDFRenderer {
public:
    SDFRenderer(core::VulkanContext& context);
    ~SDFRenderer();

    void update(float deltaTime, const core::InputState& input, bool imguiCapture);
    void render(vk::CommandBuffer commandBuffer);
    vk::Image getOutputImage() const { return outputImage.image.get(); }

    struct SelectionData {
        int32_t hitIndex; // -1 none, 0 ground, 1+ edit
        float posX, posY, posZ;
    };
    SelectionData getSelection();
    void triggerPicking(float x, float y);

    // Public access for editor
    std::vector<core::SDFEdit>& getEdits() { return edits; }
    void markEditsDirty() { editsDirty = true; }

    uint32_t& getRenderMode() { return renderMode; }
    bool& getShowGround() { return showGround; }
    
    void setBrush(float x, float y, float z, float r) {
        brushX = x; brushY = y; brushZ = z; brushRadius = r;
    }
    bool& getShowGrid() { return showGrid; }

    Terrain& getTerrain() { return *terrain; }

private:
    core::VulkanContext& context;
    std::unique_ptr<DescriptorManager> descriptorManager;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;
    
    std::unique_ptr<ComputePipeline> computePipeline;
    
    ResourceManager::Image outputImage;
    ResourceManager::Buffer editBuffer;
    ResourceManager::Buffer selectionBuffer;
    std::vector<core::SDFEdit> edits;
    bool editsDirty = true;
    bool pickingRequested = false;

    PushConstants pushConstants{};
    float totalTime = 0.0f;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;

    // Camera state
    float camYaw = -1.57f;
    float camPitch = -0.15f;
    float camSpeed = 5.0f;
    float camPosX = 0.0f, camPosY = 2.5f, camPosZ = -5.0f;
    
    uint32_t renderMode = 0;
    bool showGround = true;
    bool showGrid = false;
    float brushX = 0, brushY = 0, brushZ = 0, brushRadius = 0;

    void createDescriptorSets();
    void updateEditBuffer();

    std::unique_ptr<Terrain> terrain;
    vk::Sampler terrainSampler;
};

} // namespace engine::renderer
