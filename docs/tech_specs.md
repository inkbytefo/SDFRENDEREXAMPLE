# SDF-Dynamic Game Engine Architecture - Technical Documentation

This document specifies the architecture for a high-performance, dynamic game engine leveraging Signed Distance Fields (SDF) as its primary geometry representation.

## 1. Core Geometry Representation: SDF Edits

The world is not a static mesh but a set of dynamic operations applied to primitive shapes.

### Purpose
To enable infinite, non-destructive, and real-time world manipulation (deformation, destruction, construction) with perfect mathematical representation.

### Data Structure (SDF Edit)
Each "Edit" represents a single volumetric operation.

```cpp
enum class SDFOp : uint32_t {
    Union,          // Boolean A | B
    Subtraction,    // Boolean A - B
    Intersection,   // Boolean A & B
    SmoothUnion,    // Blended A | B
    SmoothSub,      // Blended A - B
};

struct SDFEdit {
    vec3 position;
    quat rotation;
    vec3 scale;
    uint32_t primitiveType; // Sphere, Box, Torus, Capsule, etc.
    SDFOp operation;
    float blendFactor;      // Smoothing radius for blending ops
    bool isDynamic;         // True if the edit moves frequently (physics vs static)
    
    struct Material {
        vec3 albedo;
        float roughness;
        float metallic;
    } material;
};
```

---

## 2. Voxel Storage: Sparse Brick-Map & Atlas

Evaluating thousands of SDF edits per pixel is computationally prohibitive. We use a hierarchical caching system.

### Purpose
To cache evaluated distance and material values in a GPU-friendly format.

### Architecture
- **Brick**: A small volumetric block, typically **8x8x8** texels.
- **Brick Atlas**: A large **3D Texture** on the GPU that stores the physical data for all active bricks.
- **Sparse Brick-Map**: A 3D spatial index (grid or octree) where each cell points to a location in the Brick Atlas or remains empty.

### Data Flow
1. **CPU/GPU**: Track "dirty" regions where edits have changed.
2. **Compute Shader**: For each dirty brick, re-evaluate all intersecting `SDFEdit` items.
3. **Trilinear Sampling**: During rendering, ray-marchers sample the 3D Brick Atlas for smooth transitions.

| Component | Content | Purpose |
| :--- | :--- | :--- |
| **Brick-Map** | Pointers/Indices | Spatial lookup & Occlusion |
| **Brick Atlas** | float (Dist) + uint (ID) | Raw SDF & Material data |
| **Edit Buffer** | List of `SDFEdit` | Source of truth for evaluation |

---

## 3. Geometry Clipmaps & LOD

To support massive worlds, we use nested levels of detail (LOD) around the camera.

### Purpose
Focus computational power and memory on the area immediately surrounding the player.

### Implementation
- **Nested Grids**: Multiple concentric Brick-Maps where each outer level has double the cell size of the inner level.
- **Memory Optimization**: Only the core level (Level 0) stores high-frequency details. Outer levels average the distance field of inner levels.
- **Seamless Transistions**: Distance values are interpolated between LOD levels in a "transition zone" to prevent popping.

---

## 4. Rendering Pipeline: Hybrid Architecture

The engine uses a Deferred PBR pipeline combined with Ray-marching for SDFs and Rasterization for standard meshes.

### Purpose
Achieve photorealistic lighting on dynamic volumes while maintaining the ability to render traditional GPU-optimized meshes (characters, items).

### Pipeline Stages
1. **HiZ Culling**: Use a low-res depth buffer to skip ray-marching occluded bricks.
2. **SDF Ray-marching**: 
   - Step along rays through the Brick-Map. 
   - Within a brick, use trilinear interpolation of the 3D Atlas.
   - Output: Normal, Depth, Albedo, Roughness, Metallic (GBuffer).
3. **Mesh Rasterization**: Standard forward/deferred pass for non-SDF objects.
4. **Lighting Pass**: Deferred shading using the combined GBuffer.

```glsl
// Simplified Hybrid Ray-march Loop
vec3 march(vec3 rayOrig, vec3 rayDir) {
    float t = 0.0;
    for(int i = 0; i < MAX_STEPS; i++) {
        vec3 p = rayOrig + rayDir * t;
        // 1. Get Brick from Sparse Map
        // 2. Sample Brick Atlas (3D Texture)
        float d = sampleBrickAtlas(p);
        if(d < EPSILON) return p;
        t += d;
    }
}
```

---

## 5. Physics: Jolt Integration & Chunking

Dynamic geometry requires a dynamic physical representation.

### Purpose
Provide collision and interaction between SDF volumes and standard rigid bodies.

### Strategies
- **Static World**: When a chunk is finalized or "settled," run **Marching Cubes** to bake a triangle mesh for the Jolt Physics static backend.
- **Dynamic Edits**: For moving SDF objects (like a rolling boulder), create a dedicated Jolt Body. Collision is handled via SDF sampling queries during the physics step.
- **Trigger/Query**: Efficient `sdf_query(point)` functions allow for instantaneous collision checks without meshes.

---

## 6. Advanced Extensions

| Feature | Implementation | Benefit |
| :--- | :--- | :--- |
| **Cloth Sim** | Position Based Dynamics (PBD) | Real-time fabric interacting with SDF shapes |
| **Fluid Sim** | SPH (Smoothed Particle Hydrodynamics) | Particles that "fill" SDF holes and flow over surfaces |
| **SDFGI** | Sparse Voxel Global Illumination | Dynamic occlusions and bounce lighting using the Brick Atlas |

---

## 7. Optimization Techniques

1. **Incremental Update**: Only update bricks touched by an edit's bounding box.
2. **Material Caching**: Hash material properties to avoid redundant GBuffer writes.
3. **Dirty Region Tracking**: Bitmask/Hierarchical Z for quickly identifying which Atlas voxels need compute.
4. **Sparse Allocation**: Only allocate Atlas space for bricks containing geometry (surface nearby).

---

## 8. Roadmap & Toolchain

### Recommended Toolchain
- **Language**: C++20 (System), GLSL/HLSL (Shaders).
- **API**: **WebGPU** for prototyping, **Vulkan 1.3** for production (Mesh Shaders / RT Cores).
- **Physics**: **Jolt Physics** (Multi-threaded performance).
- **Editor**: Custom ImGui-based volume editor.

### Roadmap
- **Phase 1**: Core Ray-marcher & Voxel Atlas (Static).
- **Phase 2**: Sparse Brick-Map & CPU/GPU synchronization (Dynamic Edits).
- **Phase 3**: Physics integration & Marching Cubes baking.
- **Phase 4**: Advanced VFX (Fluid, Cloth, SDFGI).
- **Phase 5**: Multi-backend DX12/Vulkan implementation.

---
*End of Documentation*
