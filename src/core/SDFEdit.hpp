#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>

namespace engine::core {

enum class SDFOp : uint32_t {
    Union = 0,
    Subtraction = 1,
    Intersection = 2,
    SmoothUnion = 3,
    SmoothSub = 4
};

enum class SDFPrimitive : uint32_t {
    Sphere = 0,   // scale.x = radius
    Box = 1,      // scale = half extents
    Torus = 2,    // scale.x = major radius, scale.y = minor radius
    Capsule = 3,  // scale.x = radius, scale.y = segment length (centered)
    Cylinder = 4  // scale.x = radius, scale.y = half height
};

// One CSG operation. Uploaded verbatim to the GPU: layout must match SDFEditGPU in
// shaders/include/sdf_common.glsl (std430, 96 bytes).
struct SDFEdit {
    glm::vec3 position{0.0f};
    float padding1 = 0.0f;
    glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion (x, y, z, w)
    glm::vec3 scale{1.0f};
    uint32_t primitiveType = 0; // SDFPrimitive
    uint32_t operation = 0;     // SDFOp
    float blendFactor = 0.3f;
    uint32_t isDynamic = 0;
    float padding2 = 0.0f;

    struct Material {
        glm::vec3 albedo{0.8f};
        float roughness = 0.5f;
        float metallic = 0.0f;
        glm::vec3 editorEulerDeg{0.0f}; // editor-only rotation in degrees; the GPU ignores it
    } material;
};

static_assert(sizeof(SDFEdit) == 96, "SDFEdit must match SDFEditGPU (96 bytes)");
static_assert(sizeof(SDFEdit::Material) == 32, "SDFEdit::Material layout changed");

// Radius of a sphere around position that contains the primitive (rotation-independent).
// Mirrors editBoundRadius() in sdf_common.glsl.
inline float editBoundRadius(const SDFEdit& e) {
    switch (static_cast<SDFPrimitive>(e.primitiveType)) {
        case SDFPrimitive::Sphere:   return e.scale.x;
        case SDFPrimitive::Box:      return glm::length(e.scale);
        case SDFPrimitive::Torus:    return e.scale.x + e.scale.y;
        case SDFPrimitive::Capsule:  return 0.5f * e.scale.y + e.scale.x;
        case SDFPrimitive::Cylinder: return std::sqrt(e.scale.x * e.scale.x + e.scale.y * e.scale.y);
    }
    return glm::length(e.scale);
}

} // namespace engine::core
