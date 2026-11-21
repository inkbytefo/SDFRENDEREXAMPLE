#pragma once

#include "core/VulkanContext.hpp"
#include "core/SDFEdit.hpp"
#include <vector>
#include <string>

namespace engine::renderer { class SDFRenderer; }
namespace engine::core { struct SDFEdit; }

namespace engine::editor {

class EditorUI {
public:
    EditorUI(engine::core::VulkanContext& context, GLFWwindow* window);
    ~EditorUI();

    void beginFrame();
    void buildPanels(engine::renderer::SDFRenderer& renderer, int& selectedIndex);
    void endFrame(vk::CommandBuffer cmd, vk::ImageView swapchainImageView, vk::Extent2D extent);

    bool wantsCaptureKeyboard() const;
    bool wantsCaptureMouse() const;
    bool isPaintingTerrain() const;
    void onSwapchainRecreated();

private:
    engine::core::VulkanContext& context;
    vk::UniqueDescriptorPool imguiPool;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;

    // Device-loaded Vulkan 1.3 dynamic rendering functions
    PFN_vkCmdBeginRendering pfnCmdBeginRendering = nullptr;
    PFN_vkCmdEndRendering pfnCmdEndRendering = nullptr;

    void initImGui(GLFWwindow* window);
    void shutdownImGui();
};

} // namespace engine::editor
