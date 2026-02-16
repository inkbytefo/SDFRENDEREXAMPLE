#pragma once

#include <array>

namespace engine::core {

struct InputState {
    // Mouse
    double mouseX = 0.0, mouseY = 0.0;
    double mouseDeltaX = 0.0, mouseDeltaY = 0.0;
    double scrollDelta = 0.0;
    bool mouseButtons[3] = { false, false, false }; // Left, Right, Middle
    bool mouseCaptured = false; // true when controlling camera (right-click held)

    // Keyboard
    std::array<bool, 512> keys{};
    
    // Frame helpers
    bool isKeyDown(int key) const { return key >= 0 && key < 512 && keys[key]; }
    bool isMouseDown(int button) const { return button >= 0 && button < 3 && mouseButtons[button]; }
    
    void resetDeltas() {
        mouseDeltaX = 0.0;
        mouseDeltaY = 0.0;
        scrollDelta = 0.0;
    }
};

} // namespace engine::core
