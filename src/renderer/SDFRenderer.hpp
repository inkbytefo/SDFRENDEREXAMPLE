#pragma once

#include "core/VulkanContext.hpp"
#include "ComputePipeline.hpp"
#include "DescriptorManager.hpp"
#include "core/SDFEdit.hpp"
#include "core/InputState.hpp"
#include <vector>
#include <cmath>

namespace engine::renderer {

struct PushConstants {
    float camPosX, camPosY, camPosZ, pad0;
    float camDirX, camDirY, camDirZ, pad1;
    float resX, resY, time, editCount;
    uint32_t renderMode; // 0=Lit, 1=Normals, 2=Complexity
    uint32_t showGround; // 1=On, 0=Off
    float pad3, pad4;
};

class SDFRenderer {
public:
    SDFRenderer(core::VulkanContext& context);
    ~SDFRenderer();

    void update(float deltaTime, const core::InputState& input, bool imguiCapture);
    void render(vk::CommandBuffer commandBuffer);

    vk::Image getOutputImage() const { return outputImage.image.get(); }

    // Public access for editor
    std::vector<core::SDFEdit>& getEdits() { return edits; }
    void markEditsDirty() { editsDirty = true; }

    uint32_t& getRenderMode() { return renderMode; }
    bool& getShowGround() { return showGround; }

private:
    core::VulkanContext& context;
    std::unique_ptr<DescriptorManager> descriptorManager;
    vk::DescriptorSetLayout descriptorSetLayout;
    vk::DescriptorSet descriptorSet;
    
    std::unique_ptr<ComputePipeline> computePipeline;
    
    ResourceManager::Image outputImage;
    ResourceManager::Buffer editBuffer;
    std::vector<core::SDFEdit> edits;
    bool editsDirty = true;

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

    void createDescriptorSets();
    void updateEditBuffer();
};

} // namespace engine::renderer
