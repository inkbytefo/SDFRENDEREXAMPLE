# API, Optimization & Roadmap

This document covers the low-level system design and the future trajectory of the engine.

## 1. Graphics API Design

The engine is built with a modernize-first approach, targeting explicit APIs.

- **WebGPU (Prototype Phase)**: Used for rapid iteration and cross-platform accessibility.
- **DX12 / Vulkan (Production Phase)**:
  - **Bindless Rendering**: All textures and buffers are accessible globally to shaders.
  - **Mesh Shaders**: Future optimization for the Marching Cubes mesh generation path.
  - **Async Compute**: Brick updates happen on a separate compute queue to avoid stalling the graphics pipeline.

## 2. Memory Optimization Strategies

| Strategy | Implementation | Benefit |
| :--- | :--- | :--- |
| **Sparse Allocation** | Only surface bricks are allocated in the Atlas. | Saves 80-90% of VRAM in open worlds. |
| **Material Caching** | Material hashes are matched to avoid redundant state changes. | Reduces draw call overhead. |
| **Dirty Tracking** | Bitmasks track which 8x8x8 regions need updates. | Compute passes only touch modified voxels. |
| **Pool Management** | Bricks are recycled in a LRU (Least Recently Used) manner. | Stable memory footprint during camera movement. |

## 3. Performance Pitfalls

- **High-Frequency Edits**: Too many small edits in one brick can slow down the compute evaluator. Solution: "Collate" edits into a single baked distance field periodically.
- **Ray-marching Accuracy**: Extremely thin surfaces may be missed if the step size is too large. Solution: Use secondary "refining" steps when a surface hit is detected.

## 4. Development Roadmap

### Phase 1: Foundation (Current)
- [x] Basic Ray-marcher.
- [x] Primitive Edit List.
- [x] WebGPU integration.

### Phase 2: Scalability (In Progress)
- [ ] Sparse Brick-Map management.
- [ ] Brick Atlas dynamic allocation.
- [ ] Geometry Clipmaps/LOD.

### Phase 3: Interaction
- [ ] Jolt Physics integration.
- [ ] Marching Cubes surface extraction.
- [ ] Dynamic SDF-to-SDF collisions.

### Phase 4: Polish & Advanced FX
- [ ] SDF Global Illumination.
- [ ] PBD Cloth and SPH Fluid.
- [ ] Multi-Backend (Vulkan/DX12).
