#version 460
#extension GL_GOOGLE_include_directive : require

// ============================================================
// Brick baker: keeps the sparse brick map + atlas in sync with the edit list.
//
//   pass 0  INIT      fill the free-brick stack with every atlas slot
//   pass 1  CLASSIFY  for each dirty cell: does the surface pass through it?
//                     no  -> free its brick, store a conservative skip distance
//                     yes -> keep its brick (queue a refill) or request a new one
//   pass 2  ALLOCATE  pop bricks from the free stack for requested cells
//   pass 3  FILL      one workgroup per queued brick: evaluate 8^3 distance samples
//
// Frees only happen in pass 1 and pops only in pass 2, so the stack never sees
// concurrent push and pop.
// ============================================================

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

#define EDIT_BINDING   0
#define VOLUME_BINDING 6
#include "include/sdf_common.glsl"

layout(binding = 1, r32ui) uniform uimage3D sparseMap;
layout(binding = 2, r16f)  uniform writeonly image3D brickAtlas;

layout(std430, binding = 3) buffer BrickState {
    uint freeTop;
    uint overflow;
    uint allocCount;
    uint pad;
    uint freeStack[];
} state;

layout(std430, binding = 4) buffer AllocList {
    uint allocCells[];
};

// Header doubles as VkDispatchIndirectCommand for the FILL pass.
layout(std430, binding = 5) buffer WorkList {
    uint groupsX;
    uint groupsY;
    uint groupsZ;
    uint workPad;
    uvec2 work[]; // (cell linear index, brick id)
};

layout(push_constant) uniform BakeParams {
    uvec4 boxMin;   // xyz = first dirty cell, w = pass
    uvec4 boxSize;  // xyz = dirty cell count, w = edit count
} pc;

uint flatInvocation() {
    uvec3 n = gl_NumWorkGroups * gl_WorkGroupSize;
    uvec3 g = gl_GlobalInvocationID;
    return g.x + g.y * n.x + g.z * n.x * n.y;
}

void queueWork(uint cellIdx, uint brickId) {
    uint slot = atomicAdd(groupsX, 1u);
    work[slot] = uvec2(cellIdx, brickId);
}

void passInit() {
    uint i = flatInvocation();
    uint capacity = vol.atlasBricks.w;
    if (i < capacity) state.freeStack[i] = capacity - 1u - i;
    if (i == 0u) {
        state.freeTop = capacity;
        state.overflow = 0u;
        state.allocCount = 0u;
    }
}

void passClassify() {
    uvec3 local = gl_GlobalInvocationID;
    if (any(greaterThanEqual(local, pc.boxSize.xyz))) return;
    uvec3 cell = pc.boxMin.xyz + local;
    if (any(greaterThanEqual(cell, vol.gridDim.xyz))) return;

    float halfDiag = vol.metrics.y;
    float margin   = voxelSize() * 1.75;     // keeps trilinear/normal taps inside allocated bricks
    float maxSafe  = vol.metrics.w;
    float band     = halfDiag + margin + maxSafe;

    vec3 center = cellMin(cell) + 0.5 * cellSize();
    float d = evalEditsDist(center, pc.boxSize.w, band);
    bool needsBrick = abs(d) <= halfDiag + margin;

    uint old = imageLoad(sparseMap, ivec3(cell)).r;
    bool hasBrick = (old & BRICK_FLAG) != 0u;

    if (needsBrick) {
        if (hasBrick) {
            queueWork(cellLinear(cell), old & BRICK_MASK);
        } else {
            uint slot = atomicAdd(state.allocCount, 1u);
            if (slot < vol.atlasBricks.w) {
                allocCells[slot] = cellLinear(cell);
            } else {
                atomicAdd(state.overflow, 1u);
                imageStore(sparseMap, ivec3(cell), uvec4(floatBitsToUint(0.0)));
            }
        }
    } else {
        if (hasBrick) {
            uint top = atomicAdd(state.freeTop, 1u);
            state.freeStack[top] = old & BRICK_MASK;
        }
        // Any point in this cell is at least |d| - halfDiag from the surface.
        float safe = clamp(abs(d) - halfDiag, 0.0, maxSafe);
        imageStore(sparseMap, ivec3(cell), uvec4(floatBitsToUint(safe)));
    }
}

void passAllocate() {
    uint i = flatInvocation();
    uint requested = min(state.allocCount, vol.atlasBricks.w);
    if (i >= requested) return;

    uint cellIdx = allocCells[i];
    ivec3 cell = ivec3(cellFromLinear(cellIdx));

    // Lock-free pop.
    uint top = state.freeTop;
    bool got = false;
    uint brick = 0u;
    while (top > 0u) {
        uint prev = atomicCompSwap(state.freeTop, top, top - 1u);
        if (prev == top) {
            brick = state.freeStack[top - 1u];
            got = true;
            break;
        }
        top = prev;
    }

    if (got) {
        imageStore(sparseMap, cell, uvec4(BRICK_FLAG | brick));
        queueWork(cellIdx, brick);
    } else {
        atomicAdd(state.overflow, 1u);
        imageStore(sparseMap, cell, uvec4(floatBitsToUint(0.0)));
    }
}

shared uint activeEdit[256];

void passFill() {
    uvec2 item = work[gl_WorkGroupID.x];
    uvec3 cell = cellFromLinear(item.x);
    uint brick = item.y;
    uint count = min(pc.boxSize.w, 256u);
    float band = vol.metrics.z;

    vec3 base = cellMin(cell);
    float h = voxelSize();

    // Per-brick culling: each thread tests one edit against the brick's bounding sphere.
    uint li = gl_LocalInvocationIndex;
    if (li < count) {
        vec3 brickCenter = base + 0.5 * cellSize();
        activeEdit[li] = editCulled(edits[li], brickCenter, vol.metrics.y, band) ? 0u : 1u;
    }
    barrier();

    vec3 p = base + vec3(gl_LocalInvocationID) * h;
    float res = BIG;
    for (uint i = 0u; i < count; ++i) {
        if (activeEdit[i] == 0u) continue;
        SDFEditGPU e = edits[i];
        res = combineDist(res, evalPrimitive(p, e), e);
    }
    res = clamp(res, -band, band);

    ivec3 texel = ivec3(brickAtlasCoord(brick) * uint(BRICK_SAMPLES) + gl_LocalInvocationID);
    imageStore(brickAtlas, texel, vec4(res));
}

void main() {
    switch (pc.boxMin.w) {
        case 0u: passInit(); break;
        case 1u: passClassify(); break;
        case 2u: passAllocate(); break;
        case 3u: passFill(); break;
    }
}
