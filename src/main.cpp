#include <iostream>
#include "core/Window.hpp"
#include "core/VulkanContext.hpp"
#include "core/PhysicsSystem.hpp"
#include "renderer/SDFRenderer.hpp"
#include "editor/EditorUI.hpp"

int main() {
    try {
        // 1. Window
        engine::core::Window window(1280, 720, "SDF Playground - Vulkan 1.4 + Jolt");

        // 2. Vulkan Context
        engine::core::VulkanContext context(window);

        // 3. Physics
        engine::core::PhysicsSystem physics;

        // 4. SDF Renderer
        engine::renderer::SDFRenderer renderer(context);
 
        // 5. Editor UI (ImGui)
        engine::editor::EditorUI editor(context, window.getGLFWwindow());

        // 6. Add default scene objects
        {
            auto& edits = renderer.getEdits();
            
            // Sphere
            engine::core::SDFEdit sphere{};
            sphere.position = glm::vec3(0.0f, 1.0f, 5.0f);
            sphere.rotation = glm::vec4(0, 0, 0, 1);
            sphere.scale = glm::vec3(1.0f);
            sphere.primitiveType = 0;
            sphere.operation = 0;
            sphere.blendFactor = 0.3f;
            sphere.material.albedo = glm::vec3(0.9f, 0.3f, 0.2f);
            sphere.material.roughness = 0.3f;
            sphere.material.metallic = 0.0f;
            edits.push_back(sphere);

            // Box
            engine::core::SDFEdit box{};
            box.position = glm::vec3(3.0f, 0.8f, 5.0f);
            box.rotation = glm::vec4(0, 0, 0, 1);
            box.scale = glm::vec3(0.8f);
            box.primitiveType = 1;
            box.operation = 0;
            box.blendFactor = 0.3f;
            box.material.albedo = glm::vec3(0.3f, 0.7f, 0.9f);
            box.material.roughness = 0.5f;
            box.material.metallic = 0.2f;
            edits.push_back(box);

            // Torus
            engine::core::SDFEdit torus{};
            torus.position = glm::vec3(-2.5f, 0.7f, 6.0f);
            torus.rotation = glm::vec4(0, 0, 0, 1);
            torus.scale = glm::vec3(0.8f, 0.25f, 1.0f);
            torus.primitiveType = 2;
            torus.operation = 0;
            torus.blendFactor = 0.3f;
            torus.material.albedo = glm::vec3(0.9f, 0.8f, 0.2f);
            torus.material.roughness = 0.3f;
            torus.material.metallic = 0.8f;
            edits.push_back(torus);

            renderer.markEditsDirty();
        }

        std::cout << "Playground ready! RMB+WASD to fly, scroll for speed." << std::endl;

        // 7. Main Loop
        float lastFrameTime = static_cast<float>(glfwGetTime());
        int selectedEdit = 0;

        while (!window.shouldClose()) {
            window.pollEvents();

            float currentTime = static_cast<float>(glfwGetTime());
            float deltaTime = currentTime - lastFrameTime;
            lastFrameTime = currentTime;

            // Check if ImGui wants input
            bool imguiCapture = editor.wantsCaptureMouse();

            // Update
            physics.update(deltaTime);
            renderer.update(deltaTime, window.getInput(), imguiCapture);

            // Begin frame
            context.beginFrame();
            auto cmd = context.getCurrentCommandBuffer();

            // Compute SDF render
            renderer.render(cmd);

            // Blit compute result to swapchain
            context.endFrameBlit(renderer.getOutputImage());

            // ImGui overlay
            editor.beginFrame();
            editor.buildPanels(renderer, selectedEdit);
            renderer.markEditsDirty(); // Always upload (edits may change via UI)

            auto swapExtent = context.getSwapchain()->getExtent();
            auto imageViews = context.getSwapchain()->getImageViews();
            vk::ImageView currentView = imageViews[context.getImageIndex()];
            editor.endFrame(cmd, currentView, swapExtent);

            // Present
            context.endFramePresent();
        }

        context.getDevice().waitIdle();
        std::cout << "Editor Shutdown Successfully!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
