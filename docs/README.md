# SDF-Dynamic Engine Documentation

Welcome to the technical documentation for the SDF-Dynamic Game Engine. This engine is built on the principle of **Signed Distance Fields (SDF)** for all world geometry, enabling unprecedented levels of destructibility and dynamic manipulation.

## Core Philosophical Principles
- **Voxel-Free Storage**: While we use voxels for caching, the source of truth is a mathematical "Edit List".
- **Real-Time Everything**: No baked lighting, no static meshes. Everything can change at any frame.
- **Hybrid Performance**: Utilizing the best of both GPU ray-marching and traditional rasterization.

## Documentation Index

1. [**Technical Specifications**](tech_specs.md)  
   *The high-level technical overview of the engine's capabilities.*

2. [**Architecture & Spatial Indexing**](architecture.md)  
   *Deep dive into Sparse Brick-Maps, Brick Atlas, and the Edit Paradigm.*

3. [**Rendering & Pipeline**](rendering.md)  
   *Details on GPU Ray-marching, Hybrid PBR, and Optimization techniques.*

4. [**Physics & Simulations**](physics_and_simulation.md)  
   *Jolt Physics integration, Marching Cubes, Cloth, and Fluids.*

5. [**Development & Optimization**](api_and_optimization.md)  
   *API design (WebGPU/Vulkan), memory management, and development roadmap.*

---
*Maintained by the Engine Technical Lead*
