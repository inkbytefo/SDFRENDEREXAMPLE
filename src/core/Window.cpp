#include "Window.hpp"
#include <stdexcept>

namespace engine::core {

Window::Window(int width, int height, const std::string& title)
    : width(width), height(height), title(title) {
    initWindow();
    setupCallbacks();
}

Window::~Window() {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Window::initWindow() {
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window) {
        throw std::runtime_error("Failed to create GLFW window");
    }
}

void Window::setupCallbacks() {
    glfwSetWindowUserPointer(window, this);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
}

void Window::pollEvents() {
    input.resetDeltas();
    glfwPollEvents();
}

void Window::keyCallback(GLFWwindow* win, int key, int /*scancode*/, int action, int /*mods*/) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (key >= 0 && key < 512) {
        if (action == GLFW_PRESS)
            self->input.keys[key] = true;
        else if (action == GLFW_RELEASE)
            self->input.keys[key] = false;
    }
}

void Window::mouseButtonCallback(GLFWwindow* win, int button, int action, int /*mods*/) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (button >= 0 && button < 3) {
        if (action == GLFW_PRESS) self->input.mouseClicked[button] = true;
        self->input.mouseButtons[button] = (action == GLFW_PRESS);
    }
    // Right-click toggles camera capture
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        self->input.mouseCaptured = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            self->firstMouse = true;
        } else {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }
}

void Window::cursorPosCallback(GLFWwindow* win, double xpos, double ypos) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (self->firstMouse) {
        self->lastMouseX = xpos;
        self->lastMouseY = ypos;
        self->firstMouse = false;
    }
    self->input.mouseDeltaX += xpos - self->lastMouseX;
    self->input.mouseDeltaY += ypos - self->lastMouseY;
    self->lastMouseX = xpos;
    self->lastMouseY = ypos;
    self->input.mouseX = xpos;
    self->input.mouseY = ypos;
}

void Window::scrollCallback(GLFWwindow* win, double /*xoffset*/, double yoffset) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    self->input.scrollDelta += yoffset;
}

std::vector<const char*> Window::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    return extensions;
}

} // namespace engine::core
