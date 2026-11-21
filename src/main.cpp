#include <cstdlib>
#include <cstring>
#include <iostream>
#include "core/Window.hpp"
#include "core/VulkanContext.hpp"
#include "core/PhysicsSystem.hpp"
#include "renderer/SDFRenderer.hpp"
#include "editor/EditorUI.hpp"
#include "core/SceneGen.hpp"

#include <imgui.h>

using engine::core::SDFEdit;
using engine::core::SDFOp;
using engine::core::SDFPrimitive;

static SDFEdit makeEdit(SDFPrimitive prim, glm::vec3 pos, glm::vec3 scale, glm::vec3 albedo,
                        float roughness, float metallic) {
    SDFEdit e{};
    e.position = pos;
    e.scale = scale;
    e.primitiveType = static_cast<uint32_t>(prim);
    e.operation = static_cast<uint32_t>(SDFOp::Union);
    e.material.albedo = albedo;
    e.material.roughness = roughness;
    e.material.metallic = metallic;
    return e;
}

// Usage: Engine [--bench [objectCount]] [--bench-res W H]
//   --bench      adds objectCount random objects (default 96), renders 300 frames without
//                input, prints the average frame time and exits.
//   --bench-res  renders at a fixed internal resolution (upscaled to the window).
int main(int argc, char** argv) {
    int benchObjects = -1;
    uint32_t benchW = 0, benchH = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--bench") == 0) {
            benchObjects = (i + 1 < argc) ? std::atoi(argv[i + 1]) : 96;
            if (benchObjects <= 0) benchObjects = 96;
        } else if (std::strcmp(argv[i], "--bench-res") == 0 && i + 2 < argc) {
            benchW = static_cast<uint32_t>(std::atoi(argv[i + 1]));
            benchH = static_cast<uint32_t>(std::atoi(argv[i + 2]));
        }
    }

    try {
        engine::core::Window window(1280, 720, "SDF Playground - Vulkan + Jolt");
        engine::core::VulkanContext context(window);
        engine::core::PhysicsSystem physics;
        engine::renderer::SDFRenderer renderer(context);
        engine::editor::EditorUI editor(context, window.getGLFWwindow());

        auto& edits = renderer.getEdits();
        edits.push_back(makeEdit(SDFPrimitive::Sphere, { 0.0f, 1.0f, 5.0f }, glm::vec3(1.0f), { 0.9f, 0.3f, 0.2f }, 0.3f, 0.0f));
        edits.push_back(makeEdit(SDFPrimitive::Box, { 3.0f, 0.8f, 5.0f }, glm::vec3(0.8f), { 0.3f, 0.7f, 0.9f }, 0.5f, 0.2f));
        edits.push_back(makeEdit(SDFPrimitive::Torus, { -2.5f, 0.7f, 6.0f }, { 0.8f, 0.25f, 1.0f }, { 0.9f, 0.8f, 0.2f }, 0.3f, 0.8f));

        if (benchObjects > 0) {
            uint32_t seed = 1234567u;
            for (auto& e : engine::core::makeRandomEdits(static_cast<size_t>(benchObjects), seed)) edits.push_back(e);
        }
        if (benchW > 0 && benchH > 0) renderer.resize({ benchW, benchH });
        int benchFrame = 0;
        double benchStart = 0.0;
        constexpr int kBenchWarmup = 30, kBenchFrames = 300;

        std::cout << "Playground ready! RMB+WASD to fly, scroll for speed, LMB to select." << std::endl;

        float lastFrameTime = static_cast<float>(glfwGetTime());
        int selectedEdit = 0;
        uint32_t swapchainGeneration = context.getSwapchainGeneration();

        while (!window.shouldClose()) {
            window.pollEvents();

            float now = static_cast<float>(glfwGetTime());
            float deltaTime = now - lastFrameTime;
            lastFrameTime = now;

            // Waits for this slot's fence; false means the swapchain was rebuilt.
            bool frameReady = context.beginFrame();
            if (context.getSwapchainGeneration() != swapchainGeneration) {
                swapchainGeneration = context.getSwapchainGeneration();
                renderer.resize(benchW > 0 && benchH > 0 ? vk::Extent2D{ benchW, benchH }
                                                         : context.getSwapchain()->getExtent());
                editor.onSwapchainRecreated();
            }
            if (!frameReady) continue;

            const uint32_t frame = context.getCurrentFrame();
            renderer.beginFrame(frame);

            // Click-to-select results arrive a couple of frames after the click.
            if (auto pick = renderer.takeClickResult(); pick && pick->hitIndex > 0) {
                selectedEdit = pick->hitIndex - 1;
            }

            editor.beginFrame();
            editor.buildPanels(renderer, selectedEdit);
            const bool imguiCapture = editor.wantsCaptureMouse();

            // Hover picking runs every frame; window coords -> output pixels.
            const auto& input = window.getInput();
            ImVec2 fbScale = ImGui::GetIO().DisplayFramebufferScale;
            if (!imguiCapture) {
                renderer.setMousePixel(static_cast<float>(input.mouseX) * fbScale.x,
                                       static_cast<float>(input.mouseY) * fbScale.y);
            } else {
                renderer.setMousePixel(-1.0f, -1.0f);
            }
            if (input.mouseClicked[0] && !imguiCapture && !editor.isPaintingTerrain()) {
                renderer.requestClickPick();
            }

            physics.update(deltaTime);
            renderer.update(deltaTime, input, imguiCapture);

            auto cmd = context.getCurrentCommandBuffer();
            renderer.render(cmd, frame);
            context.endFrameBlit(renderer.getOutputImage(), renderer.getOutputExtent());

            vk::ImageView swapView = context.getSwapchain()->getImageViews()[context.getImageIndex()];
            editor.endFrame(cmd, swapView, context.getSwapchain()->getExtent());

            context.endFramePresent();

            if (benchObjects > 0) {
                ++benchFrame;
                if (benchFrame == kBenchWarmup) {
                    context.getDevice().waitIdle();
                    benchStart = glfwGetTime();
                } else if (benchFrame == kBenchWarmup + kBenchFrames) {
                    context.getDevice().waitIdle();
                    double ms = (glfwGetTime() - benchStart) * 1000.0 / kBenchFrames;
                    auto ext = renderer.getOutputExtent();
                    std::cout << "BENCH objects=" << edits.size() << " res=" << ext.width << "x" << ext.height
                              << " avg_frame_ms=" << ms << " bricks=" << renderer.getVolume().getStats().used << std::endl;
                    break;
                }
            }
        }

        context.getDevice().waitIdle();
        std::cout << "Editor Shutdown Successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
