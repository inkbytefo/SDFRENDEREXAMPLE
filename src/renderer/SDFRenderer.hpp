#pragma once

#include "core/VulkanContext.hpp"
#include "core/SDFEdit.hpp"
#include "core/InputState.hpp"
#include "ComputePipeline.hpp"
#include "DescriptorManager.hpp"
#include "BrickVolume.hpp"
#include "Terrain.hpp"

#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace engine::renderer {

// Must match the push constant block in SDFCompute.glsl.
struct RayPushConstants {
    glm::vec4 camPos;   // xyz, w = time
    glm::vec4 camDir;   // xyz
    glm::vec4 brush;    // xyz = world pos, w = radius (0 = hidden)
    glm::vec4 mouse;    // xy = pixel (-1 = none), z = edit count
    glm::uvec4 flags;   // x = render mode, y = show ground, z = show grid
    glm::uvec4 size;    // xy = output resolution
};
static_assert(sizeof(RayPushConstants) == 96, "RayPushConstants must match SDFCompute.glsl");

class SDFRenderer {
public:
    static constexpr uint32_t FRAMES = core::VulkanContext::MAX_FRAMES_IN_FLIGHT;
    static constexpr uint32_t MAX_EDITS = BrickVolume::MAX_EDITS;

    enum RenderMode : uint32_t { Lit = 0, Normals = 1, Complexity = 2, Bricks = 3 };

    struct SelectionData {
        int32_t hitIndex = -1; // -1 none, 0 terrain, 1+ edit (1-based)
        float posX = 0, posY = 0, posZ = 0;
        glm::vec3 position() const { return { posX, posY, posZ }; }
    };

    explicit SDFRenderer(core::VulkanContext& context);
    ~SDFRenderer();

    // Call right after VulkanContext::beginFrame() (the slot's fence has been waited on).
    // Reads back picking and brick statistics written by this slot's previous use.
    void beginFrame(uint32_t frame);
    void update(float deltaTime, const core::InputState& input, bool imguiCapture);
    // Bakes dirty bricks, applies queued terrain brushes, and raymarches into the output image.
    void render(vk::CommandBuffer cmd, uint32_t frame);
    void resize(vk::Extent2D extent);

    vk::Image getOutputImage() const { return outputImage.image.get(); }
    vk::Extent2D getOutputExtent() const { return { outputWidth, outputHeight }; }

    // Mouse position in output-image pixels; negative disables hover picking.
    void setMousePixel(float x, float y) { mouseX = x; mouseY = y; }
    // The click result arrives FRAMES frames later through takeClickResult().
    void requestClickPick() { clickRequested = true; }
    const SelectionData& getHover() const { return hover; }
    std::optional<SelectionData> takeClickResult();

    std::vector<core::SDFEdit>& getEdits() { return edits; }
    uint32_t& getRenderMode() { return renderMode; }
    bool& getShowGround() { return showGround; }
    bool& getShowGrid() { return showGrid; }
    void setBrush(const glm::vec3& pos, float radius) { brushPos = pos; brushRadius = radius; }

    Terrain& getTerrain() { return *terrain; }
    BrickVolume& getVolume() { return *volume; }

private:
    core::VulkanContext& context;

    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<BrickVolume> volume;
    std::unique_ptr<DescriptorManager> descriptors;
    vk::DescriptorSetLayout setLayout;
    std::array<vk::DescriptorSet, FRAMES> sets{};
    std::unique_ptr<ComputePipeline> pipeline;
    vk::UniqueSampler linearSampler;

    // Per frame in flight: the CPU writes slot N while the GPU may still read slot N-1.
    std::array<ResourceManager::Buffer, FRAMES> editBuffers;
    std::array<void*, FRAMES> editMapped{};
    std::array<ResourceManager::Buffer, FRAMES> selectionBuffers;
    std::array<SelectionData*, FRAMES> selectionMapped{};
    std::array<bool, FRAMES> clickInFlight{};

    ResourceManager::Image outputImage;
    uint32_t outputWidth = 0, outputHeight = 0;

    std::vector<core::SDFEdit> edits;      // authored by the editor
    std::vector<core::SDFEdit> bakedEdits; // what the brick volume currently reflects

    SelectionData hover;
    std::optional<SelectionData> clickResult;
    bool clickRequested = false;
    float mouseX = -1.0f, mouseY = -1.0f;

    // Camera
    float camYaw = 0.0f;
    float camPitch = -0.15f;
    float camSpeed = 5.0f;
    glm::vec3 camPos{0.0f, 2.5f, -5.0f};
    glm::vec3 camDir{0.0f, 0.0f, 1.0f};
    float totalTime = 0.0f;

    uint32_t renderMode = Lit;
    bool showGround = true;
    bool showGrid = false;
    glm::vec3 brushPos{0.0f};
    float brushRadius = 0.0f;

    void createOutputImage(vk::Extent2D extent);
    void writeDescriptorSets();
    uint32_t uploadEdits(uint32_t frame);
};

} // namespace engine::renderer
