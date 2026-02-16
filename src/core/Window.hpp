#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>
#include "InputState.hpp"

namespace engine::core {

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return glfwWindowShouldClose(window); }
    void pollEvents();
    
    GLFWwindow* getGLFWwindow() const { return window; }
    
    void getFramebufferSize(int& width, int& height) {
        glfwGetFramebufferSize(window, &width, &height);
    }

    InputState& getInput() { return input; }
    const InputState& getInput() const { return input; }

    static std::vector<const char*> getRequiredExtensions();

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string title;
    InputState input;
    double lastMouseX = 0.0, lastMouseY = 0.0;
    bool firstMouse = true;

    void initWindow();
    void setupCallbacks();

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};

} // namespace engine::core
