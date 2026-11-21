// Shared SDF code for the brick baker and the raymarcher.
// The including shader must #define EDIT_BINDING and VOLUME_BINDING before including this file.

#ifndef SDF_COMMON_GLSL
#define SDF_COMMON_GLSL

// Must match engine::core::SDFEdit (96 bytes, std430).
struct SDFEditGPU {
    vec3  position;      float pad0;
    vec4  rotation;      // quaternion xyzw, normalized on the CPU
    vec3  scale;         uint  primitiveType;
    uint  operation;     float blendFactor;
    uint  isDynamic;     float pad1;
    vec3  albedo;        float roughness;
    float metallic;      float editorEuler[3]; // editor-only, ignored here
};

layout(std430, binding = EDIT_BINDING) readonly buffer EditBuffer {
    SDFEditGPU edits[];
};

// Must match engine::renderer::VolumeParams (std140).
layout(std140, binding = VOLUME_BINDING) uniform VolumeParamsUBO {
    vec4  originVoxel;   // xyz = world-space volume min corner, w = voxel size
    uvec4 gridDim;       // xyz = sparse map cells, w = max edits
    uvec4 atlasBricks;   // xyz = atlas size in bricks, w = brick capacity
    vec4  metrics;       // x = cell size, y = half cell diagonal, z = brick clamp distance, w = max safe skip
} vol;

const uint  BRICK_FLAG    = 0x80000000u;
const uint  BRICK_MASK    = 0x7FFFFFFFu;
const int   BRICK_SAMPLES = 8;            // 8^3 samples per brick, 7 voxel intervals per cell
const float BIG           = 1e10;

const uint OP_UNION = 0u, OP_SUB = 1u, OP_INTERSECT = 2u, OP_SMOOTH_UNION = 3u, OP_SMOOTH_SUB = 4u;

// ---------------- Primitives (local space, centered on the edit) ----------------

float sdSphere(vec3 p, float r) { return length(p) - r; }

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

// Vertical capsule of total segment length h, centered on the origin.
float sdCapsule(vec3 p, float h, float r) {
    p.y += 0.5 * h;
    p.y -= clamp(p.y, 0.0, h);
    return length(p) - r;
}

float sdCylinder(vec3 p, float h, float r) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

vec3 quatRotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

float evalPrimitive(vec3 p, SDFEditGPU e) {
    // World -> local: inverse rotation (conjugate quaternion).
    vec3 lp = quatRotate(vec4(-e.rotation.xyz, e.rotation.w), p - e.position);
    switch (e.primitiveType) {
        case 0u: return sdSphere(lp, e.scale.x);
        case 1u: return sdBox(lp, e.scale);
        case 2u: return sdTorus(lp, e.scale.xy);
        case 3u: return sdCapsule(lp, e.scale.y, e.scale.x);
        case 4u: return sdCylinder(lp, e.scale.y, e.scale.x);
        default: return sdSphere(lp, e.scale.x);
    }
}

// Radius of a sphere around e.position that contains the primitive. Mirrors editBoundRadius() in C++.
float editBoundRadius(SDFEditGPU e) {
    switch (e.primitiveType) {
        case 0u: return e.scale.x;
        case 1u: return length(e.scale);
        case 2u: return e.scale.x + e.scale.y;
        case 3u: return 0.5 * e.scale.y + e.scale.x;
        case 4u: return length(e.scale.xy);
        default: return length(e.scale);
    }
}

// True if edit e cannot change the field anywhere inside the sphere (p, r) by more than `band`.
// Intersection is global (it clips everything), so it is never culled.
bool editCulled(SDFEditGPU e, vec3 p, float r, float band) {
    if (e.operation == OP_INTERSECT) return false;
    float gap = distance(p, e.position) - editBoundRadius(e) - max(e.blendFactor, 0.0) - r;
    return gap > band;
}

// ---------------- Boolean operations ----------------

float opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

// Smoothly subtracts d2 from d1.
float opSmoothSub(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d1 + d2) / k, 0.0, 1.0);
    return mix(d1, -d2, h) + k * h * (1.0 - h);
}

float combineDist(float res, float d, SDFEditGPU e) {
    float k = max(e.blendFactor, 0.01);
    switch (e.operation) {
        case OP_UNION:        return min(res, d);
        case OP_SUB:          return max(res, -d);
        case OP_INTERSECT:    return max(res, d);
        case OP_SMOOTH_UNION: return opSmoothUnion(res, d, k);
        case OP_SMOOTH_SUB:   return opSmoothSub(res, d, k);
    }
    return res;
}

// Distance from p to the edit list. With band >= 0, edits that cannot affect values inside
// [-band, band] are skipped and the result is clamped to that band: exact where |d| <= band,
// a conservative bound outside it.
float evalEditsDist(vec3 p, uint count, float band) {
    float res = BIG;
    for (uint i = 0u; i < count; ++i) {
        SDFEditGPU e = edits[i];
        if (band >= 0.0 && editCulled(e, p, 0.0, band)) continue;
        res = combineDist(res, evalPrimitive(p, e), e);
    }
    return band >= 0.0 ? clamp(res, -band, band) : res;
}

struct EditHit {
    float dist;
    vec3  albedo;
    float roughness;
    float metallic;
    int   index;      // 1-based edit index, -1 if none
};

// Full evaluation with material blending; used once per pixel at the hit point.
EditHit evalEditsMaterial(vec3 p, uint count, float band) {
    EditHit res;
    res.dist = BIG;
    res.albedo = vec3(0.5);
    res.roughness = 0.8;
    res.metallic = 0.0;
    res.index = -1;

    for (uint i = 0u; i < count; ++i) {
        SDFEditGPU e = edits[i];
        if (editCulled(e, p, 0.0, band)) continue;
        float d = evalPrimitive(p, e);
        float k = max(e.blendFactor, 0.01);

        switch (e.operation) {
            case OP_UNION:
                if (d < res.dist) {
                    res.dist = d;
                    res.albedo = e.albedo;
                    res.roughness = e.roughness;
                    res.metallic = e.metallic;
                    res.index = int(i) + 1;
                }
                break;
            case OP_SUB:
                res.dist = max(res.dist, -d);
                break;
            case OP_INTERSECT:
                if (d > res.dist) res.index = int(i) + 1;
                res.dist = max(res.dist, d);
                break;
            case OP_SMOOTH_UNION: {
                float h = clamp(0.5 + 0.5 * (d - res.dist) / k, 0.0, 1.0); // 1 = keep previous
                res.albedo = mix(e.albedo, res.albedo, h);
                res.roughness = mix(e.roughness, res.roughness, h);
                res.metallic = mix(e.metallic, res.metallic, h);
                if (h < 0.5) res.index = int(i) + 1;
                res.dist = opSmoothUnion(res.dist, d, k);
                break;
            }
            case OP_SMOOTH_SUB: {
                float h = clamp(0.5 - 0.5 * (res.dist + d) / k, 0.0, 1.0);
                res.albedo = mix(res.albedo, e.albedo, h * 0.5); // tint the cut
                res.dist = opSmoothSub(res.dist, d, k);
                break;
            }
        }
    }
    return res;
}

// ---------------- Volume addressing ----------------

float cellSize()  { return vol.metrics.x; }
float voxelSize() { return vol.originVoxel.w; }

uvec3 brickAtlasCoord(uint id) {
    uint ax = vol.atlasBricks.x, ay = vol.atlasBricks.y;
    return uvec3(id % ax, (id / ax) % ay, id / (ax * ay));
}

uint cellLinear(uvec3 c) {
    return c.x + c.y * vol.gridDim.x + c.z * vol.gridDim.x * vol.gridDim.y;
}

uvec3 cellFromLinear(uint i) {
    uint gx = vol.gridDim.x, gy = vol.gridDim.y;
    return uvec3(i % gx, (i / gx) % gy, i / (gx * gy));
}

vec3 cellMin(uvec3 c) { return vol.originVoxel.xyz + vec3(c) * cellSize(); }

#endif
