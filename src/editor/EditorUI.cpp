#include "EditorUI.hpp"
#include "renderer/SDFRenderer.hpp"
#include "core/SDFEdit.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <cmath>

namespace engine::editor {

// Primitive type names for UI
static const char* primitiveNames[] = { "Sphere", "Box", "Torus", "Capsule", "Cylinder" };
static const char* operationNames[] = { "Union", "Subtraction", "Intersection", "SmoothUnion", "SmoothSub" };
static const char* renderModeNames[] = { "Lit (Standard PBR)", "Normals", "Complexity (Steps)" };

EditorUI::EditorUI(engine::core::VulkanContext& ctx, GLFWwindow* window) : context(ctx) {
    initImGui(window);
}

EditorUI::~EditorUI() {
    shutdownImGui();
}

void EditorUI::initImGui(GLFWwindow* window) {
    // Create descriptor pool for ImGui
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        { vk::DescriptorType::eSampler, 100 },
        { vk::DescriptorType::eCombinedImageSampler, 100 },
        { vk::DescriptorType::eSampledImage, 100 },
        { vk::DescriptorType::eStorageImage, 100 },
        { vk::DescriptorType::eUniformTexelBuffer, 100 },
        { vk::DescriptorType::eStorageTexelBuffer, 100 },
        { vk::DescriptorType::eUniformBuffer, 100 },
        { vk::DescriptorType::eStorageBuffer, 100 },
        { vk::DescriptorType::eUniformBufferDynamic, 100 },
        { vk::DescriptorType::eStorageBufferDynamic, 100 },
        { vk::DescriptorType::eInputAttachment, 100 }
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 100;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    imguiPool = context.getDevice().createDescriptorPoolUnique(poolInfo);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark theme with custom vibes
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(12, 12);
    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);

    // Accent colors
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
    colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.28f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.30f, 0.40f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.50f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.40f, 0.70f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.52f, 0.85f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.35f, 0.65f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.18f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.26f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.32f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.06f, 0.06f, 0.08f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.20f, 1.0f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.55f, 0.90f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.65f, 1.0f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.30f, 0.70f, 1.0f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.18f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.52f, 0.85f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.40f, 0.70f, 1.0f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.20f, 0.28f, 1.0f);

    // Init GLFW backend
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // Init Vulkan backend with dynamic rendering
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(context.getInstance());
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(context.getPhysicalDevice());
    initInfo.Device = static_cast<VkDevice>(context.getDevice());
    initInfo.QueueFamily = context.getQueueFamily();
    initInfo.Queue = static_cast<VkQueue>(context.getGraphicsQueue());
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(imguiPool.get());

    uint32_t imageCount = static_cast<uint32_t>(context.getSwapchain()->getImages().size());
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.UseDynamicRendering = true;
    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != 0) {
            std::cerr << "ImGui Vulkan Error: " << (int)err << std::endl;
        }
    };

    // For dynamic rendering, set the color attachment format
    VkFormat colorFormat = static_cast<VkFormat>(context.getSwapchain()->getFormat());
    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

    // Load Vulkan functions for ImGui (using instance proc addr to handle both device and instance functions)
    ImGui_ImplVulkan_LoadFunctions([](const char* function_name, void* v_context) {
        auto* ctx = static_cast<engine::core::VulkanContext*>(v_context);
        return reinterpret_cast<PFN_vkVoidFunction>(glfwGetInstanceProcAddress(ctx->getInstance(), function_name));
    }, &context);

    ImGui_ImplVulkan_Init(&initInfo);

    // Upload fonts
    ImGui_ImplVulkan_CreateFontsTexture();

    // Load dynamic rendering function pointers from device
    pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(
        context.getDevice().getProcAddr("vkCmdBeginRendering"));
    pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(
        context.getDevice().getProcAddr("vkCmdEndRendering"));

    if (!pfnCmdBeginRendering || !pfnCmdEndRendering) {
        throw std::runtime_error("Failed to load vkCmdBeginRendering/vkCmdEndRendering");
    }

    std::cout << "ImGui initialized with Vulkan dynamic rendering." << std::endl;
}

void EditorUI::shutdownImGui() {
    context.getDevice().waitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUI::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::buildPanels(engine::renderer::SDFRenderer& renderer, int& selectedIndex) {
    auto& edits = renderer.getEdits();

    // --- Scene Hierarchy ---
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Objects", nullptr, ImGuiWindowFlags_NoCollapse);

    // Add button
    if (ImGui::Button("+ Add Object", ImVec2(-1, 30))) {
        engine::core::SDFEdit newEdit{};
        newEdit.position = glm::vec3(0.0f, 1.0f, 5.0f);
        newEdit.rotation = glm::vec4(0, 0, 0, 1);
        newEdit.scale = glm::vec3(1.0f);
        newEdit.primitiveType = 0; // Sphere
        newEdit.operation = 0; // Union
        newEdit.blendFactor = 0.3f;
        newEdit.isDynamic = 0;
        newEdit.material.albedo = glm::vec3(0.8f, 0.3f, 0.2f);
        newEdit.material.roughness = 0.5f;
        newEdit.material.metallic = 0.0f;
        edits.push_back(newEdit);
        selectedIndex = static_cast<int>(edits.size()) - 1;
        renderer.markEditsDirty();
    }

    ImGui::Separator();

    // Object list
    for (int i = 0; i < static_cast<int>(edits.size()); i++) {
        int primType = static_cast<int>(edits[i].primitiveType);
        if (primType < 0 || primType > 4) primType = 0;
        
        char label[64];
        snprintf(label, sizeof(label), "%s %s #%d", 
                 primitiveNames[primType],
                 operationNames[edits[i].operation],
                 i);

        bool selected = (selectedIndex == i);
        if (ImGui::Selectable(label, selected)) {
            selectedIndex = i;
        }
    }

    // Delete button
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(edits.size())) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Delete Selected", ImVec2(-1, 28))) {
            edits.erase(edits.begin() + selectedIndex);
            if (selectedIndex >= static_cast<int>(edits.size()))
                selectedIndex = static_cast<int>(edits.size()) - 1;
            renderer.markEditsDirty();
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::End();

    // --- Inspector ---
    ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(edits.size())) {
        auto& edit = edits[selectedIndex];

        // Primitive Type
        int primType = static_cast<int>(edit.primitiveType);
        if (ImGui::Combo("Primitive", &primType, primitiveNames, IM_ARRAYSIZE(primitiveNames))) {
            edit.primitiveType = static_cast<uint32_t>(primType);
            renderer.markEditsDirty();
        }

        // Operation
        int op = static_cast<int>(edit.operation);
        if (ImGui::Combo("Operation", &op, operationNames, IM_ARRAYSIZE(operationNames))) {
            edit.operation = static_cast<uint32_t>(op);
            renderer.markEditsDirty();
        }

        ImGui::Separator();
        ImGui::Text("Transform");

        // Position
        if (ImGui::DragFloat3("Position", &edit.position.x, 0.05f, -50.0f, 50.0f, "%.2f")) {
            renderer.markEditsDirty();
        }

        // Scale
        if (ImGui::DragFloat3("Scale", &edit.scale.x, 0.02f, 0.05f, 20.0f, "%.2f")) {
            renderer.markEditsDirty();
        }

        // Blend factor
        if (ImGui::SliderFloat("Blend", &edit.blendFactor, 0.0f, 2.0f, "%.2f")) {
            renderer.markEditsDirty();
        }

        ImGui::Separator();
        ImGui::Text("Material");

        // Color
        if (ImGui::ColorEdit3("Albedo", &edit.material.albedo.x)) {
            renderer.markEditsDirty();
        }
        if (ImGui::SliderFloat("Roughness", &edit.material.roughness, 0.01f, 1.0f, "%.2f")) {
            renderer.markEditsDirty();
        }
        if (ImGui::SliderFloat("Metallic", &edit.material.metallic, 0.0f, 1.0f, "%.2f")) {
            renderer.markEditsDirty();
        }
    } else {
        ImGui::TextWrapped("Select an object or add a new one.");
    }

    ImGui::End();

    // --- Display Settings ---
    ImGui::SetNextWindowPos(ImVec2(300, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 140), ImGuiCond_FirstUseEver);
    ImGui::Begin("Display Settings", nullptr, ImGuiWindowFlags_NoCollapse);
    
    int renderMode = static_cast<int>(renderer.getRenderMode());
    if (ImGui::Combo("Render Mode", &renderMode, renderModeNames, IM_ARRAYSIZE(renderModeNames))) {
        renderer.getRenderMode() = static_cast<uint32_t>(renderMode);
    }
    
    bool showGround = renderer.getShowGround();
    if (ImGui::Checkbox("Show Ground Plane", &showGround)) {
        renderer.getShowGround() = showGround;
    }

    ImGui::Separator();
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Objects: %d", (int)edits.size());

    ImGui::End();

    // --- Controls / Info ---
    ImGui::SetNextWindowPos(ImVec2(300, 160), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 80), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("RMB + WASD: Fly camera\nScroll: Speed");
    ImGui::End();
}

void EditorUI::endFrame(vk::CommandBuffer cmd, vk::ImageView swapchainImageView, vk::Extent2D extent) {
    ImGui::Render();

    // Begin dynamic rendering on the swapchain image
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = static_cast<VkImageView>(swapchainImageView);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.extent = {extent.width, extent.height};
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    pfnCmdBeginRendering(static_cast<VkCommandBuffer>(cmd), &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd));

    pfnCmdEndRendering(static_cast<VkCommandBuffer>(cmd));
}

bool EditorUI::wantsCaptureKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool EditorUI::wantsCaptureMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace engine::editor
