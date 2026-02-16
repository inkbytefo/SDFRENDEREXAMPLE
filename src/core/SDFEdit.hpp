#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace engine::core {

enum class SDFOp : uint32_t {
    Union = 0,
    Subtraction = 1,
    Intersection = 2,
    SmoothUnion = 3,
    SmoothSub = 4
};

struct SDFEdit {
    glm::vec3 position;
    float padding1;
    glm::vec4 rotation; // quaternion
    glm::vec3 scale;
    uint32_t primitiveType; // 0: Sphere, 1: Box, etc.
    uint32_t operation; // SDFOp
    float blendFactor;
    uint32_t isDynamic;
    float padding2;

    struct Material {
        glm::vec3 albedo;
        float roughness;
        float metallic;
        float padding3[3];
    } material;
};

} // namespace engine::core
