#pragma once

#include "SDFEdit.hpp"
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace engine::core {

// Deterministic random scene used by the editor's stress button and `Engine --bench`.
inline std::vector<SDFEdit> makeRandomEdits(size_t count, uint32_t& seed) {
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };

    std::vector<SDFEdit> out;
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        SDFEdit e{};
        e.primitiveType = static_cast<uint32_t>(rnd() * 5.0f) % 5;
        e.operation = rnd() < 0.8f ? static_cast<uint32_t>(SDFOp::SmoothUnion) : static_cast<uint32_t>(SDFOp::Subtraction);
        e.position = glm::vec3(rnd() * 24.0f - 12.0f, rnd() * 4.0f, rnd() * 24.0f - 4.0f);
        e.scale = glm::vec3(0.3f + rnd() * 1.2f, 0.2f + rnd() * 0.8f, 0.3f + rnd() * 1.2f);
        e.material.editorEulerDeg = glm::vec3(rnd() * 90.0f, rnd() * 360.0f, 0.0f);
        glm::quat q(glm::radians(e.material.editorEulerDeg));
        e.rotation = glm::vec4(q.x, q.y, q.z, q.w);
        e.blendFactor = 0.1f + rnd() * 0.4f;
        e.material.albedo = glm::vec3(rnd(), rnd(), rnd());
        e.material.roughness = 0.2f + rnd() * 0.7f;
        e.material.metallic = rnd() < 0.3f ? 0.9f : 0.0f;
        out.push_back(e);
    }
    return out;
}

} // namespace engine::core
