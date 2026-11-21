#version 460
#extension GL_GOOGLE_include_directive : require

// ============================================================
// SDF Playground - raymarcher
//
// Marching uses the baked brick volume:
//   - empty sparse-map cells are skipped in one step (stored safe distance or cell exit)
//   - surface cells sample the 8^3 brick in the atlas with hardware trilinear filtering
// The edit list is evaluated analytically only at the hit point (refine, normal,
// material, picking), with per-edit bounding-sphere culling.
// ============================================================

layout(local_size_x = 8, local_size_y = 8) in;

#define EDIT_BINDING   1
#define VOLUME_BINDING 7
#include "include/sdf_common.glsl"

layout(binding = 0, rgba8) uniform writeonly image2D outImage;

layout(std430, binding = 2) buffer SelectionBuffer {
    int   hitIndex;    // -1 none, 0 terrain, 1+ edit
    float hitPosX, hitPosY, hitPosZ;
};

layout(binding = 3) uniform sampler2D terrainHeight;
layout(binding = 4) uniform sampler2D terrainSplat;
layout(binding = 5) uniform sampler3D brickAtlas;
layout(binding = 6, r32ui) uniform readonly uimage3D sparseMap;

// Must match engine::renderer::RayPushConstants (96 bytes).
layout(push_constant) uniform PushConstants {
    vec4  camPos;   // xyz, w = time
    vec4  camDir;   // xyz, w = unused
    vec4  brush;    // xyz = world pos, w = radius (0 = hidden)
    vec4  mouse;    // xy = pixel (-1 = none), z = edit count, w = unused
    uvec4 flags;    // x = render mode, y = show ground, z = show grid
    uvec4 size;     // xy = resolution
} pc;

const int   MAX_STEPS = 192;
const float MAX_DIST  = 200.0;
const float TERRAIN_WORLD_SIZE = 256.0;

const uint MODE_LIT = 0u, MODE_NORMALS = 1u, MODE_COMPLEXITY = 2u, MODE_BRICKS = 3u;

uint editCount() { return uint(pc.mouse.z); }

// ---------------- Terrain (heightfield, outside the brick volume) ----------------

float sdTerrain(vec3 p) {
    vec2 uv = (p.xz + TERRAIN_WORLD_SIZE * 0.5) / TERRAIN_WORLD_SIZE;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return p.y;
    float h = textureLod(terrainHeight, uv, 0.0).r;
    return (p.y - h) * 0.5; // heightfields are not true SDFs; halve the step to stay conservative
}

float groundDist(vec3 p) { return pc.flags.y == 1u ? sdTerrain(p) : BIG; }

// ---------------- Brick volume queries ----------------

vec3 volumeMin() { return vol.originVoxel.xyz; }
vec3 volumeMax() { return vol.originVoxel.xyz + vec3(vol.gridDim.xyz) * cellSize(); }

// Distance to the edit field at p.
//   isBrick = true : trilinear brick sample (accurate near the surface)
//   isBrick = false: conservative lower bound (empty cell or outside the volume)
float volumeQuery(vec3 p, out bool isBrick, out ivec3 cell, out bool inside) {
    isBrick = false;
    vec3 g = (p - volumeMin()) / cellSize();
    cell = ivec3(floor(g));
    inside = all(greaterThanEqual(cell, ivec3(0))) && all(lessThan(cell, ivec3(vol.gridDim.xyz)));

    if (!inside) {
        // Everything is clipped to the volume, so the distance to its box is safe.
        vec3 c = 0.5 * (volumeMin() + volumeMax());
        return max(sdBox(p - c, 0.5 * (volumeMax() - volumeMin())), 0.0);
    }

    uint v = imageLoad(sparseMap, cell).r;
    if ((v & BRICK_FLAG) == 0u) return uintBitsToFloat(v);

    isBrick = true;
    uvec3 b = brickAtlasCoord(v & BRICK_MASK);
    vec3 local = clamp((p - cellMin(uvec3(cell))) / voxelSize(), vec3(0.0), vec3(BRICK_SAMPLES - 1));
    vec3 atlasTexels = vec3(vol.atlasBricks.xyz * uint(BRICK_SAMPLES));
    vec3 uvw = (vec3(b * uint(BRICK_SAMPLES)) + local + 0.5) / atlasTexels;
    return textureLod(brickAtlas, uvw, 0.0).r;
}

float cellExitDist(vec3 p, vec3 rd, ivec3 cell) {
    vec3 safeRd = mix(rd, vec3(1e-8), lessThan(abs(rd), vec3(1e-8)));
    vec3 bmin = cellMin(uvec3(cell));
    vec3 t1 = (bmin - p) / safeRd;
    vec3 t2 = (bmin + cellSize() - p) / safeRd;
    vec3 tmax = max(t1, t2);
    return min(tmax.x, min(tmax.y, tmax.z));
}

// Safe step along rd for the edit field.
float editsStep(vec3 p, vec3 rd, out bool isBrick) {
    ivec3 cell; bool inside;
    float d = volumeQuery(p, isBrick, cell, inside);
    if (isBrick || !inside) return d;
    // Empty cell: no surface inside, so jump at least to its far side.
    return max(d, cellExitDist(p, rd, cell) + 1e-3 * cellSize());
}

float editsPoint(vec3 p) {
    bool isBrick, inside; ivec3 cell;
    return volumeQuery(p, isBrick, cell, inside);
}

// ---------------- Normals (tetrahedron technique, 4 taps) ----------------

vec3 editNormal(vec3 p) {
    const vec2 k = vec2(1.0, -1.0);
    float h = 0.5 * voxelSize();
    float band = 4.0 * voxelSize();
    uint n = editCount();
    return normalize(k.xyy * evalEditsDist(p + k.xyy * h, n, band) +
                     k.yyx * evalEditsDist(p + k.yyx * h, n, band) +
                     k.yxy * evalEditsDist(p + k.yxy * h, n, band) +
                     k.xxx * evalEditsDist(p + k.xxx * h, n, band));
}

vec3 terrainNormal(vec3 p) {
    const vec2 k = vec2(1.0, -1.0);
    const float h = 0.15;
    return normalize(k.xyy * sdTerrain(p + k.xyy * h) +
                     k.yyx * sdTerrain(p + k.yyx * h) +
                     k.yxy * sdTerrain(p + k.yxy * h) +
                     k.xxx * sdTerrain(p + k.xxx * h));
}

// ---------------- Lighting ----------------

float softShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 64 && t < maxt; i++) {
        vec3 p = ro + rd * t;
        bool isBrick;
        float dE = editsStep(p, rd, isBrick);
        float dG = groundDist(p);
        float hitDist = min(isBrick ? dE : BIG, dG);
        if (hitDist < 0.001) return 0.0;
        res = min(res, k * hitDist / t);
        t += max(min(dE, dG), 0.02);
    }
    return clamp(res, 0.0, 1.0);
}

float calcAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.02 + 0.12 * float(i);
        vec3 q = p + n * h;
        float d = min(editsPoint(q), groundDist(q));
        occ += (h - min(d, h)) * sca;
        sca *= 0.75;
    }
    return clamp(1.0 - 1.5 * occ, 0.0, 1.0);
}

vec3 shade(vec3 p, vec3 n, vec3 viewDir, vec3 albedo, float roughness, float metallic) {
    vec3 lightDir1 = normalize(vec3(0.6, 0.8, -0.4));
    vec3 lightDir2 = normalize(vec3(-0.4, 0.5, 0.7));
    vec3 lightCol1 = vec3(1.4, 1.3, 1.2);
    vec3 lightCol2 = vec3(0.3, 0.4, 0.6);

    float diff1 = max(dot(n, lightDir1), 0.0);
    float diff2 = max(dot(n, lightDir2), 0.0);
    float shadow1 = diff1 > 0.0 ? softShadow(p + n * 0.02, lightDir1, 0.02, 30.0, 12.0) : 0.0;

    vec3 h1 = normalize(lightDir1 + viewDir);
    float spec1 = pow(max(dot(n, h1), 0.0), mix(8.0, 128.0, 1.0 - roughness));
    float specIntensity = mix(0.04, 1.0, metallic);

    float ao = calcAO(p, n);

    vec3 color = vec3(0.08, 0.09, 0.12) * ao * albedo;
    color += albedo * lightCol1 * diff1 * shadow1;
    color += albedo * lightCol2 * diff2 * 0.3;
    color += vec3(specIntensity) * lightCol1 * spec1 * shadow1;
    return color;
}

vec3 ACESFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ---------------- Terrain material + overlays ----------------

void terrainMaterial(vec3 p, out vec3 albedo, out float roughness, out float metallic) {
    metallic = 0.0;
    vec2 uv = (p.xz + TERRAIN_WORLD_SIZE * 0.5) / TERRAIN_WORLD_SIZE;
    if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)))) {
        vec4 splat = textureLod(terrainSplat, uv, 0.0);
        const vec3 col[4] = vec3[4](vec3(0.1, 0.4, 0.1), vec3(0.4, 0.3, 0.2), vec3(0.5), vec3(0.9, 0.9, 0.95));
        const vec4 rough = vec4(0.9, 1.0, 0.7, 0.3);
        float sum = max(dot(splat, vec4(1.0)), 0.001);
        albedo = (col[0] * splat.r + col[1] * splat.g + col[2] * splat.b + col[3] * splat.a) / sum;
        roughness = dot(rough, splat) / sum;
    } else {
        float checker = mod(floor(p.x * 0.5) + floor(p.z * 0.5), 2.0);
        albedo = mix(vec3(0.35, 0.38, 0.42), vec3(0.55, 0.58, 0.62), checker);
        roughness = 0.9;
    }

    if (pc.flags.z == 1u) {
        vec2 g = abs(fract(p.xz - 0.5) - 0.5);
        if (min(g.x, g.y) < 0.02) albedo = mix(albedo, vec3(0.8), 0.5);
        vec2 gMajor = abs(fract(p.xz / 10.0) - 0.5);
        if (min(gMajor.x, gMajor.y) < 0.005) albedo = mix(albedo, vec3(1.0), 0.6);
    }

    if (pc.brush.w > 0.0) {
        float dist = distance(p.xz, pc.brush.xz);
        if (abs(dist - pc.brush.w) < 0.1) albedo = mix(albedo, vec3(1.0, 0.5, 0.0), 0.8);
        else if (dist < pc.brush.w)       albedo = mix(albedo, vec3(1.0, 0.5, 0.0), 0.1);
    }
}

vec3 hashColor(uint x) {
    x = (x ^ 61u) ^ (x >> 16u); x *= 9u; x ^= x >> 4u; x *= 0x27d4eb2du; x ^= x >> 15u;
    return vec3(x & 255u, (x >> 8u) & 255u, (x >> 16u) & 255u) / 255.0;
}

// ---------------- Main ----------------

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(pc.size.xy);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size) * 2.0 - 1.0;
    uv.y = -uv.y;
    uv.x *= float(size.x) / float(size.y);

    vec3 ro = pc.camPos.xyz;
    vec3 forward = normalize(pc.camDir.xyz);
    vec3 right = normalize(cross(forward, vec3(0, 1, 0)));
    vec3 up = cross(right, forward);
    vec3 rd = normalize(forward + right * uv.x + up * uv.y);

    // ---- March ----
    float t = 0.0;
    int steps = 0;
    int hitKind = -1; // 0 terrain, 1 edits
    for (int i = 0; i < MAX_STEPS; i++) {
        steps++;
        vec3 p = ro + rd * t;
        bool isBrick;
        float dE = editsStep(p, rd, isBrick);
        float dG = groundDist(p);
        float eps = 0.0005 + 0.0005 * t;
        if (isBrick && dE < eps) { hitKind = 1; break; }
        if (dG < eps)            { hitKind = 0; break; }
        t += min(dE, dG);
        if (t > MAX_DIST) break;
    }

    // ---- Resolve hit ----
    vec3 p = ro + rd * t;
    vec3 n = vec3(0, 1, 0);
    vec3 albedo = vec3(0.5);
    float roughness = 0.8, metallic = 0.0;
    int index = -1;
    uint brickId = 0u;

    if (hitKind == 1) {
        // Snap from the quantized brick surface to the exact analytic surface.
        float band = 4.0 * voxelSize();
        EditHit h = evalEditsMaterial(p, editCount(), band);
        for (int i = 0; i < 2; ++i) {
            t += clamp(h.dist, -voxelSize(), voxelSize());
            p = ro + rd * t;
            h = evalEditsMaterial(p, editCount(), band);
        }
        n = editNormal(p);
        albedo = h.albedo; roughness = h.roughness; metallic = h.metallic;
        index = h.index;

        ivec3 cell = ivec3(floor((p - volumeMin()) / cellSize()));
        uint v = imageLoad(sparseMap, clamp(cell, ivec3(0), ivec3(vol.gridDim.xyz) - 1)).r;
        brickId = (v & BRICK_FLAG) != 0u ? (v & BRICK_MASK) + 1u : 0u;
    } else if (hitKind == 0) {
        n = terrainNormal(p);
        terrainMaterial(p, albedo, roughness, metallic);
        index = 0;
    }

    // ---- Picking: one pixel writes the result for the CPU ----
    if (pixel == ivec2(pc.mouse.xy)) {
        hitIndex = index;
        hitPosX = p.x; hitPosY = p.y; hitPosZ = p.z;
    }

    // ---- Shade ----
    vec3 color;
    uint mode = pc.flags.x;
    if (mode == MODE_COMPLEXITY) {
        float c = float(steps) / float(MAX_STEPS);
        color = vec3(c * c, c, 0.5 * c);
    } else if (hitKind < 0) {
        float skyT = 0.5 * (rd.y + 1.0);
        color = mix(vec3(0.45, 0.50, 0.60), vec3(0.20, 0.30, 0.55), skyT);
    } else if (mode == MODE_NORMALS) {
        color = n * 0.5 + 0.5;
    } else if (mode == MODE_BRICKS) {
        color = hitKind == 1 ? hashColor(brickId) * (0.4 + 0.6 * max(dot(n, normalize(vec3(0.6, 0.8, -0.4))), 0.0))
                             : vec3(0.15);
    } else {
        color = shade(p, n, -rd, albedo, roughness, metallic);
        float fog = 1.0 - exp(-0.003 * t * t);
        color = mix(color, vec3(0.45, 0.50, 0.60), fog);
    }

    if (mode == MODE_LIT) color = ACESFilm(color);
    if (mode == MODE_LIT || mode == MODE_NORMALS) color = pow(color, vec3(1.0 / 2.2));

    imageStore(outImage, pixel, vec4(color, 1.0));
}
